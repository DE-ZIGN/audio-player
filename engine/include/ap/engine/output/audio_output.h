#pragma once

#include <ap/engine/pcm_format.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ap::engine {

class DeliveryCheck;

// Why an output cannot take a format. The output layer never converts: a
// format is either accepted exactly or rejected with one of these reasons.
enum class FormatRejection
{
    None,
    SampleRate, // the device does not run at this rate at all
    BitDepth,   // the rate works, but not with this sample layout
    Channels,
    SampleType, // e.g. float not accepted
    Other,
};

struct FormatCheck
{
    bool supported = false;
    FormatRejection reason = FormatRejection::None;
    long nativeError = 0; // HRESULT or similar, for logs
    // When rejected: sample rates the device accepts right now with the
    // requested channel count (for "set your device to N kHz" guidance).
    std::vector<std::uint32_t> acceptedRates;

    static FormatCheck ok() { return {true, FormatRejection::None, 0, {}}; }
    static FormatCheck rejected(FormatRejection r, long native = 0) { return {false, r, native, {}}; }
};

enum class OutputError
{
    DeviceNotFound,
    DeviceInUse,             // another application holds the device in exclusive mode
    ExclusiveModeNotAllowed, // "Allow applications to take exclusive control" is off
    FormatNotSupported,
    DeviceInvalidated,       // unplugged or reconfigured
    Timeout,                 // the device stopped requesting data
    DeviceError,
};

class OutputException : public std::runtime_error
{
public:
    OutputException(OutputError code, const std::string& detail, long nativeError = 0)
        : std::runtime_error(detail), code_(code), nativeError_(nativeError)
    {
    }

    OutputError code() const noexcept { return code_; }
    long nativeError() const noexcept { return nativeError_; }

private:
    OutputError code_;
    long nativeError_;
};

// Fills dst with up to dst.size() / bytesPerFrame frames in the opened format
// and returns the number of frames written. Returning fewer frames than
// requested signals end of stream.
using RenderCallback = std::function<std::size_t(std::span<std::byte> dst)>;

struct RunStats
{
    std::uint64_t framesRendered = 0; // frames supplied by the callback
    std::uint32_t periodFrames = 0;   // frames requested per callback (last value)
    double periodMilliseconds = 0.0;
    std::uint32_t lateWakeups = 0;    // waits for the device that timed out
    bool stoppedByRequest = false;
};

// Device-side dropout counters and thread priority, readable while running.
struct OutputHealth
{
    std::uint32_t lateBuffers = 0; // buffers not ready when the device needed them (likely audible dropouts)
    std::uint32_t timeouts = 0;    // waits for the device that timed out
    bool realtimePriority = false; // the playback thread runs with MMCSS "Pro Audio"
};

enum class OutputApi
{
    Dummy,           // writes the bytes to a file (tests, --dummy)
    WasapiExclusive, // WASAPI exclusive mode, event-driven
    Asio,
};

// How the opened format's samples are placed in the device's buffers. None of
// these changes a sample value.
enum class SampleLayout
{
    Interleaved,            // the opened format's bytes as they are (WASAPI, dummy)
    PerChannel,             // ASIO: split per channel, each sample copied at the same width
    PerChannelRightAligned, // ASIO Int32LSB16/24: the sample in the low bytes of a 32-bit slot, sign-extended
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

struct AsioPathInfo
{
    std::string driverName; // as reported by the driver
    long driverVersion = 0;
    long sampleType = -1;   // asio::SampleTypeCode of the output channels
    std::string sampleTypeName;
    bool rateChanged = false; // open() switched the driver's sample rate
    double rateBefore = 0.0;  // the driver's rate before open()
};

// What the output layer does with the data, as reported by the output after
// open(). Only facts the output itself knows or the OS/driver reported.
struct OutputPathInfo
{
    OutputApi api = OutputApi::Dummy;
    std::string deviceName;
    PcmFormat format; // as opened (what the output receives)
    std::uint32_t periodFrames = 0; // frames handed over per device period / buffer switch
    double periodMilliseconds = 0.0;
    bool eventDriven = false; // WASAPI: the device signals when it needs data
    SampleLayout layout = SampleLayout::Interleaved;
    std::optional<AsioPathInfo> asio;                 // ASIO only
    std::optional<EndpointVolumeInfo> endpointVolume; // WASAPI only, when the endpoint has a volume control
};

class AudioOutput
{
public:
    virtual ~AudioOutput() = default;

    // Set between open() and close(); empty when not open or not reported.
    virtual std::optional<OutputPathInfo> pathInfo() const { return std::nullopt; }

    // Cumulative over the life of this object. Thread-safe.
    virtual OutputHealth health() const noexcept { return {}; }

    virtual std::string deviceName() const = 0;

    // Exact check: true only if the format can be sent unchanged.
    // May throw OutputException for device-level failures (device in use,
    // exclusive mode not allowed, device removed), which are not about the format.
    virtual FormatCheck checkFormat(const PcmFormat& format) = 0;

    // Throws OutputException.
    virtual void open(const PcmFormat& format) = 0;

    // Pulls data until the callback signals end of stream and the data has
    // been delivered, or until requestStop(). Blocks the calling thread.
    // Throws OutputException.
    virtual RunStats run(const RenderCallback& render) = 0;

    // Thread-safe.
    virtual void requestStop() noexcept = 0;

    virtual void close() noexcept = 0;

    // The bit-perfect self-check (task 4-3): while set, run() reports what it
    // wrote into the driver's buffers for every period with data. Set before
    // run(), cleared after it.
    void setDeliveryCheck(DeliveryCheck* check) noexcept { deliveryCheck_ = check; }

protected:
    DeliveryCheck* deliveryCheck_ = nullptr;
};

} // namespace ap::engine
