#include <ap/engine/output/dummy_output.h>

#include <ap/engine/output/format_probe.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <thread>

namespace ap::engine {

DummyOutput::DummyOutput(Options options) : options_(std::move(options))
{
    if (options_.periodFrames.empty())
        options_.periodFrames = {4096};
}

std::string DummyOutput::deviceName() const
{
    return "ダミー出力（ファイル）: " + options_.path.string();
}

FormatCheck DummyOutput::checkFormat(const PcmFormat& format)
{
    if (!format.isValid())
        return FormatCheck::rejected(FormatRejection::Other);
    if (options_.accepts)
        return checkFormatWith(format, options_.accepts);
    return options_.check ? options_.check(format) : FormatCheck::ok();
}

void DummyOutput::open(const PcmFormat& format)
{
    const auto check = checkFormat(format);
    if (!check.supported)
        throw OutputException(OutputError::FormatNotSupported, "dummy output rejects " + describe(format));
    format_ = format;
    opened_ = true;
    stop_ = false;
}

RunStats DummyOutput::run(const RenderCallback& render)
{
    if (!opened_)
        throw OutputException(OutputError::DeviceError, "dummy output is not open");

    std::ofstream out(options_.path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw OutputException(OutputError::DeviceError, "cannot create " + options_.path.string());

    const std::size_t bpf = format_.bytesPerFrame();
    std::size_t maxPeriod = 0;
    for (auto p : options_.periodFrames)
        maxPeriod = std::max(maxPeriod, p);
    std::vector<std::byte> buffer(maxPeriod * bpf);

    RunStats stats;
    for (std::size_t i = 0;; ++i)
    {
        if (stop_)
        {
            stats.stoppedByRequest = true;
            break;
        }
        const std::size_t period = options_.periodFrames[i % options_.periodFrames.size()];
        stats.periodFrames = static_cast<std::uint32_t>(period);
        stats.periodMilliseconds = 1000.0 * static_cast<double>(period) / format_.sampleRate;

        const std::size_t frames = render(std::span(buffer).first(period * bpf));
        out.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(frames * bpf));
        if (!out)
            throw OutputException(OutputError::DeviceError, "write failed: " + options_.path.string());
        stats.framesRendered += frames;
        if (frames < period)
            break;
        if (options_.paceInRealTime)
            std::this_thread::sleep_for(std::chrono::duration<double>(static_cast<double>(period) / format_.sampleRate));
    }
    return stats;
}

} // namespace ap::engine
