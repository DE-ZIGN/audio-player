#pragma once

#include <ap/engine/pcm_format.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>

namespace ap::engine {

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

    static FormatCheck ok() { return {true, FormatRejection::None, 0}; }
    static FormatCheck rejected(FormatRejection r, long native = 0) { return {false, r, native}; }
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

class AudioOutput
{
public:
    virtual ~AudioOutput() = default;

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
};

} // namespace ap::engine
