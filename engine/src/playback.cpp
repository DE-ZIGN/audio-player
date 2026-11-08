#include <ap/engine/playback.h>

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

PlaybackReport play(PcmSource& source, AudioOutput& output, const PlaybackOptions& options)
{
    const auto negotiation = negotiate(source.format(), output, options.allowRepack);
    if (!negotiation.plan)
        throw FormatNotSupportedError(source.format(), negotiation.exactCheck);

    const PlaybackPlan plan = *negotiation.plan;
    output.open(plan.output);

    RenderCallback render;
    std::vector<std::byte> scratch;
    if (plan.repack == Repack::None)
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

    PlaybackReport report;
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
