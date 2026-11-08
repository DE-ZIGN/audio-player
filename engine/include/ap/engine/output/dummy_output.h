#pragma once

#include <ap/engine/output/audio_output.h>

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <vector>

namespace ap::engine {

// Output that writes exactly the bytes it would hand to a device into a file
// (raw interleaved PCM, no header). Used by the acceptance tests to verify
// engine output byte for byte. Unlike a device, it does not append silence at
// end of stream.
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
    };

    explicit DummyOutput(Options options);

    std::string deviceName() const override;
    FormatCheck checkFormat(const PcmFormat& format) override;
    void open(const PcmFormat& format) override;
    RunStats run(const RenderCallback& render) override;
    void requestStop() noexcept override { stop_ = true; }
    void close() noexcept override { opened_ = false; }

private:
    Options options_;
    PcmFormat format_;
    bool opened_ = false;
    std::atomic<bool> stop_{false};
};

} // namespace ap::engine
