#include <ap/engine/playback.h>

#include <ap/engine/dsp/dither.h>
#include <ap/engine/dsp/sample_convert.h>

#include <vector>

namespace ap::engine {

NegotiationResult negotiate(const PcmFormat& source, AudioOutput& output, bool allowRepack)
{
    NegotiationResult result;
    result.exactCheck = output.checkFormat(source);
    if (result.exactCheck.supported)
    {
        result.plan = PlaybackPlan{source, source, Repack::None};
        return result;
    }

    // A rate or channel problem cannot be solved by repacking.
    const auto reason = result.exactCheck.reason;
    if (!allowRepack || reason == FormatRejection::SampleRate || reason == FormatRejection::Channels)
        return result;

    for (const auto& candidate : repackCandidates(source))
    {
        if (output.checkFormat(candidate).supported)
        {
            result.plan = PlaybackPlan{source, candidate, classifyRepack(source, candidate)};
            return result;
        }
    }
    return result;
}

NegotiationResult negotiateProcessing(const PcmFormat& source, AudioOutput& output)
{
    NegotiationResult result;
    result.exactCheck = output.checkFormat(source);
    // Without the right rate or channel count no sample format helps; the
    // rate is never converted (the resampler is phase 2).
    if (!result.exactCheck.supported
        && (result.exactCheck.reason == FormatRejection::SampleRate
            || result.exactCheck.reason == FormatRejection::Channels))
        return result;

    auto with = [&](std::uint16_t container, std::uint16_t valid, SampleType type) {
        PcmFormat f = source;
        f.containerBits = container;
        f.validBits = valid;
        f.type = type;
        return f;
    };
    const PcmFormat candidates[] = {
        with(32, 32, SampleType::Int), with(32, 24, SampleType::Int), with(24, 24, SampleType::Int),
        with(16, 16, SampleType::Int), with(32, 32, SampleType::Float),
    };
    for (const auto& c : candidates)
    {
        if (output.checkFormat(c).supported)
        {
            PlaybackPlan plan{source, c, Repack::None};
            plan.processing = true;
            result.plan = plan;
            return result;
        }
    }
    return result;
}

NegotiationResult planPlayback(const PcmSource& source, AudioOutput& output, const PlaybackOptions& options)
{
    // Full bypass: no chain, every stage identity, or DoP (never processed).
    const bool dop = source.dop().has_value();
    const ProcessingChain* chain = options.processing.get();
    const bool active = !dop && chain && !chain->bypassed();

    auto n = active ? negotiateProcessing(source.format(), output)
                    : negotiate(source.format(), output, options.allowRepack);
    // A float source has no integer original: when the output takes no float,
    // it is quantized (with dither) on the processing path. An integer source
    // the output cannot take is refused, never converted.
    const bool floatRefused = !dop && source.format().type == SampleType::Float && !n.exactCheck.supported
                           && n.exactCheck.reason == FormatRejection::SampleType;
    if (!active && !n.plan && floatRefused)
        n = negotiateProcessing(source.format(), output);
    if (n.plan && n.plan->processing)
    {
        n.plan->processingStages = active ? chain->activeStageNames() : std::vector<std::string>{};
        n.plan->processingRequired = floatRefused;
        n.plan->dither = n.plan->output.type == SampleType::Int;
    }
    return n;
}

PlaybackReport play(PcmSource& source, AudioOutput& output, const PlaybackOptions& options)
{
    const auto negotiation = planPlayback(source, output, options);
    if (!negotiation.plan)
        throw FormatNotSupportedError(source.format(), negotiation.exactCheck);

    const PlaybackPlan plan = *negotiation.plan;
    ProcessingChain none;
    ProcessingChain* chain = nullptr;
    if (plan.processing)
    {
        chain = options.processing ? options.processing.get() : &none;
        chain->prepare(plan.output.sampleRate, plan.output.channels);
    }
    output.open(plan.output);
    if (options.onStart)
        options.onStart(plan);

    PlaybackReport report;
    RenderCallback render;
    std::vector<std::byte> scratch;
    std::vector<double> samples;
    dsp::TpdfDither dither; // a fixed seed per session: the same input gives the same output
    if (chain)
    {
        const PcmFormat in = plan.source;
        const PcmFormat out = plan.output;
        dsp::TpdfDither* ditherFor = plan.dither ? &dither : nullptr;
        render = [&, chain, in, out, ditherFor](std::span<std::byte> dst) {
            const std::size_t frames = dst.size() / out.bytesPerFrame();
            if (scratch.size() < frames * in.bytesPerFrame())
                scratch.resize(frames * in.bytesPerFrame()); // first callback only, period size is fixed
            if (samples.size() < frames * in.channels)
                samples.resize(frames * in.channels);

            const std::size_t got = source.read(std::span(scratch).first(frames * in.bytesPerFrame()));
            const std::size_t n = got * in.channels;
            const auto block = std::span(samples).first(n);
            ScopedFlushDenormals ftz; // may run on a driver's thread: set and restore every time
            dsp::toDouble(std::span(scratch).first(got * in.bytesPerFrame()), in, block);
            // Process up to each track start, tell the stages, go on: settings
            // that depend on the track (ReplayGain) switch at the exact sample.
            std::size_t from = 0;
            for (const auto& seg : source.lastReadTracks())
            {
                const std::size_t at = std::min(seg.frameOffset, got);
                if (at > from)
                    chain->process(block.subspan(from * in.channels, (at - from) * in.channels), in.channels);
                chain->trackStarted(seg.track);
                from = at;
            }
            if (got > from)
                chain->process(block.subspan(from * in.channels), in.channels);
            report.overs += dsp::fromDouble(block, out, dst.first(got * out.bytesPerFrame()), ditherFor);
            return got;
        };
    }
    else if (plan.repack == Repack::None)
    {
        render = [&source](std::span<std::byte> dst) { return source.read(dst); };
    }
    else
    {
        const std::uint32_t srcBps = plan.source.bytesPerSample();
        const std::uint32_t dstBps = plan.output.bytesPerSample();
        const std::uint32_t srcBpf = plan.source.bytesPerFrame();
        const std::uint32_t dstBpf = plan.output.bytesPerFrame();
        const std::uint32_t channels = plan.source.channels;
        render = [&, srcBps, dstBps, srcBpf, dstBpf, channels](std::span<std::byte> dst) {
            const std::size_t frames = dst.size() / dstBpf;
            if (scratch.size() < frames * srcBpf)
                scratch.resize(frames * srcBpf); // first callback only, period size is fixed
            const std::size_t got = source.read(std::span(scratch).first(frames * srcBpf));
            repackSamples(scratch, srcBps, dst, dstBps, got * channels);
            return got;
        };
    }

    report.plan = plan;
    try
    {
        report.stats = output.run(render);
    }
    catch (...)
    {
        output.close();
        throw;
    }
    output.close();
    return report;
}

} // namespace ap::engine
