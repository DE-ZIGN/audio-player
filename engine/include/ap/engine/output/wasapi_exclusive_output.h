#pragma once

#include <ap/engine/output/audio_output.h>
#include <ap/engine/output/device_selection.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ap::engine {

// Initializes COM (multithreaded apartment) for the current thread.
class ScopedComInit
{
public:
    ScopedComInit();
    ~ScopedComInit();
    ScopedComInit(const ScopedComInit&) = delete;
    ScopedComInit& operator=(const ScopedComInit&) = delete;

private:
    bool initialized_ = false;
};

// Windows endpoint volume. In exclusive mode the Windows audio engine is
// bypassed, so the volume only has an effect when the device implements it
// in hardware (it is then applied inside the device).
struct EndpointVolumeInfo
{
    bool available = false;
    float scalar = 1.0f; // 0..1
    bool muted = false;
    bool hardwareVolume = false;
};

// Exclusive-mode, event-driven WASAPI render output. The device receives the
// opened format unchanged; formats the device does not accept are rejected.
// The thread calling run() is registered with MMCSS ("Pro Audio") for the
// duration of the call. COM must be initialized on the calling thread
// (ScopedComInit).
class WasapiExclusiveOutput final : public AudioOutput
{
public:
    // Active render endpoints. Throws OutputException.
    static std::vector<OutputDeviceInfo> listDevices();

    // Empty id = default console render endpoint. Throws OutputException.
    explicit WasapiExclusiveOutput(const std::string& deviceId = {});
    ~WasapiExclusiveOutput() override;

    std::string deviceName() const override;
    FormatCheck checkFormat(const PcmFormat& format) override;
    void open(const PcmFormat& format) override;
    RunStats run(const RenderCallback& render) override;
    void requestStop() noexcept override;
    void close() noexcept override;

    EndpointVolumeInfo endpointVolume() const;

    // The format set in Windows sound settings ("Default Format", used by the
    // shared-mode engine). For diagnostics only.
    std::optional<PcmFormat> windowsDefaultFormat() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ap::engine
