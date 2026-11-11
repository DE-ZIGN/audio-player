#include <ap/engine/playback.h>

#include <ap/engine/dsp/dither.h>
#include <ap/engine/dsp/sample_convert.h>

#include <algorithm>
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
        n.plan->comparison = active && chain->comparing();
    }
    return n;
}

PlaybackReport play(PcmSource& source, AudioOutput& output, const PlaybackOptions& options)
{
    const auto negotiation = planPlayback(source, output, options);
    if (!negotiation.plan)
        throw FormatNotSupportedError(source.format(), negotiation.exactCheck);

    const PlaybackPlan plan = *negotiation.plan;
    DeliveryCheck* check = plan.processing ? nullptr : options.deliveryCheck.get();
    if (check)
        check->reset(); // before onStart: a status read then shows this session's counts
    ProcessingChain none;
    ProcessingChain* chain = nullptr;
    if (plan.processing)
    {
        chain = options.processing ? options.processing.get() : &none;
        chain->prepare(plan.output.sampleRate, plan.output.channels);
    }
    output.open(plan.output);
    OutputMeter* meter = options.meter.get();
    if (meter)
        meter->beginSession(plan.output, !source.dop().has_value(), source.position());
    if (options.onStart)
        options.onStart(plan);

    // Where tracks start, for the meter: the output's frame k is the source's
    // frame k of this session on every path (the processing path compensates
    // its latency), so read positions mark output positions.
    std::uint64_t readFrames = 0;
    auto noteTracks = [&source, meter, &readFrames](std::size_t got) noexcept {
        if (meter)
            for (const auto& seg : source.lastReadTracks())
                meter->trackStarts(readFrames + seg.frameOffset, seg.track.queueIndex);
        readFrames += got;
    };

    PlaybackReport report;
    RenderCallback render;
    std::vector<std::byte> scratch;
    std::vector<double> samples;
    std::vector<double> pending; // processed samples not yet handed to the output
    dsp::TpdfDither dither; // a fixed seed per session: the same input gives the same output
    if (chain)
    {
        const PcmFormat in = plan.source;
        const PcmFormat out = plan.output;
        dsp::TpdfDither* ditherFor = plan.dither ? &dither : nullptr;
        // Stages with look-ahead (the limiter) delay the audio by `latency`
        // frames. The first `latency` processed frames are dropped and, once
        // the source ends, `latency` frames of silence push the rest out, so
        // the output has exactly the source's frames at the source's timing.
        const std::size_t latency = chain->latencyFrames();
        render = [&, chain, in, out, ditherFor, latency, toDrop = latency, ended = false,
                  flushed = false](std::span<std::byte> dst) mutable {
            const std::size_t frames = dst.size() / out.bytesPerFrame();
            const std::size_t ch = in.channels;
            if (scratch.size() < frames * in.bytesPerFrame())
                scratch.resize(frames * in.bytesPerFrame()); // first callback only, period size is fixed
            if (samples.size() < (frames + latency) * ch)
                samples.resize((frames + latency) * ch);
            if (pending.capacity() < (2 * frames + 2 * latency) * ch)
                pending.reserve((2 * frames + 2 * latency) * ch);

            ScopedFlushDenormals ftz; // may run on a driver's thread: set and restore every time
            auto keep = [&](std::span<const double> block) {
                std::size_t skip = std::min(toDrop, block.size() / ch);
                toDrop -= skip;
                pending.insert(pending.end(), block.begin() + static_cast<std::ptrdiff_t>(skip * ch), block.end());
            };
            while (pending.size() < frames * ch && !flushed)
            {
                if (!ended)
                {
                    const std::size_t got = source.read(std::span(scratch).first(frames * in.bytesPerFrame()));
                    noteTracks(got);
                    if (got < frames)
                        ended = true;
                    const auto block = std::span(samples).first(got * ch);
                    dsp::toDouble(std::span(scratch).first(got * in.bytesPerFrame()), in, block);
                    // Process up to each track start, tell the stages, go on: settings
                    // that depend on the track (ReplayGain) switch at the exact sample.
                    std::size_t from = 0;
                    for (const auto& seg : source.lastReadTracks())
                    {
                        const std::size_t at = std::min(seg.frameOffset, got);
                        if (at > from)
                            chain->process(block.subspan(from * ch, (at - from) * ch), in.channels);
                        chain->trackStarted(seg.track);
                        from = at;
                    }
                    if (got > from)
                        chain->process(block.subspan(from * ch), in.channels);
                    keep(block);
                }
                else
                {
                    flushed = true;
                    if (latency == 0)
                        break;
                    const auto block = std::span(samples).first(latency * ch);
                    std::fill(block.begin(), block.end(), 0.0);
                    chain->process(block, in.channels);
                    keep(block);
                }
            }
            const std::size_t n = std::min(frames, pending.size() / ch);
            report.overs += dsp::fromDouble(std::span(pending).first(n * ch), out, dst.first(n * out.bytesPerFrame()),
                                            ditherFor);
            pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(n * ch));
            return n;
        };
    }
    else if (plan.repack == Repack::None)
    {
        const PcmFormat in = plan.source;
        render = [&source, check, in, &noteTracks](std::span<std::byte> dst) {
            const std::size_t got = source.read(dst);
            noteTracks(got);
            if (check)
                check->read(dst.first(got * in.bytesPerFrame()), in, got); // before the output touches it
            return got;
        };
    }
    else
    {
        const std::uint32_t srcBps = plan.source.bytesPerSample();
        const std::uint32_t dstBps = plan.output.bytesPerSample();
        const std::uint32_t srcBpf = plan.source.bytesPerFrame();
        const std::uint32_t dstBpf = plan.output.bytesPerFrame();
        const std::uint32_t channels = plan.source.channels;
        const PcmFormat in = plan.source;
        render = [&, srcBps, dstBps, srcBpf, dstBpf, channels, check, in](std::span<std::byte> dst) {
            const std::size_t frames = dst.size() / dstBpf;
            if (scratch.size() < frames * srcBpf)
                scratch.resize(frames * srcBpf); // first callback only, period size is fixed
            const std::size_t got = source.read(std::span(scratch).first(frames * srcBpf));
            noteTracks(got);
            if (check)
                check->read(std::span(scratch).first(got * srcBpf), in, got); // the values as read, before the repack
            repackSamples(scratch, srcBps, dst, dstBps, got * channels);
            return got;
        };
    }

    if (meter)
    {
        // What the output is handed, after the render has filled it.
        render = [inner = std::move(render), meter, bpf = plan.output.bytesPerFrame()](std::span<std::byte> dst) {
            const std::size_t n = inner(dst);
            meter->delivered(dst.first(std::min(dst.size(), n * bpf)), n);
            return n;
        };
    }

    report.plan = plan;
    output.setDeliveryCheck(check);
    try
    {
        report.stats = output.run(render);
    }
    catch (...)
    {
        output.setDeliveryCheck(nullptr);
        output.close();
        if (meter)
            meter->endSession();
        throw;
    }
    output.setDeliveryCheck(nullptr);
    output.close();
    if (meter)
        meter->endSession();
    return report;
}

} // namespace ap::engine
