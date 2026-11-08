#pragma once

#include <ap/engine/output/audio_output.h>

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace ap::engine {

// Output that writes exactly the bytes it would hand to a device into a file
// (raw interleaved PCM, no header). Used by the acceptance tests to verify
// engine output byte for byte. Unlike a device, it does not append silence at
// end of stream. One instance writes one file: the first run() truncates it,
// later runs (after reopening, e.g. for a new sample rate) append.
class DummyOutput final : public AudioOutput
{
public:
    struct Options
    {
        std::filesystem::path path;
        // Frames requested per callback; cycled, so odd patterns exercise chunk boundaries.
        std::vector<std::size_t> periodFrames{4096};
        // Which formats this "device" accepts. Default: every valid format.
        std::function<FormatCheck(const PcmFormat&)> check;
        // Run on a real-time schedule like a device: one period per period
        // (for stop/seek tests). Buffers handed over later than the device
        // would have needed them are counted in health().lateBuffers.
        bool paceInRealTime = false;
        // Alternative to `check`: which exact formats this "device" accepts. The
        // reason and accepted rates are then worked out by checkFormatWith(),
        // the same way as for WASAPI.
        std::function<bool(const PcmFormat&)> accepts;
        // Receives the format of every run() (one entry per time the output is opened and started).
        std::shared_ptr<std::vector<PcmFormat>> sessionLog;
    };

    explicit DummyOutput(Options options);

    std::string deviceName() const override;
    FormatCheck checkFormat(const PcmFormat& format) override;
    void open(const PcmFormat& format) override;
    RunStats run(const RenderCallback& render) override;
    void requestStop() noexcept override { stop_ = true; }
    void close() noexcept override { opened_ = false; }
    OutputHealth health() const noexcept override { return {lateBuffers_.load(), 0, false}; }

private:
    Options options_;
    PcmFormat format_;
    bool opened_ = false;
    bool firstRun_ = true;
    std::atomic<bool> stop_{false};
    std::atomic<std::uint32_t> lateBuffers_{0};
};

} // namespace ap::engine
