#pragma once

// Bit-perfect self-check (task 4-3): on the direct path, compares the sample
// values read from the source with what the output actually wrote into the
// driver's buffers (the WASAPI buffer it got from the device, the ASIO
// driver's per-channel buffers, the dummy output's file), period by period.
//
// Both sides are put into one form before comparing (integers left-justified
// in 32 bits, floats as their bit patterns), so a lossless container repack
// or an ASIO right-aligned type counts as equal when the values are.
//
// What it shows: the application handed the driver exactly the values it
// read. That the decoder read the file correctly is the FLAC MD5 check's
// part (IntegrityCheck). What the driver, the USB transfer and the DAC do
// after that cannot be seen from the application.

#include <ap/engine/pcm_format.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ap::engine {

struct DeliveryResult
{
    std::uint64_t matchedFrames = 0;
    std::uint64_t mismatchedFrames = 0; // includes frames that could not be compared
};

class DeliveryCheck
{
public:
    // The values read for the next period (interleaved, in `format`).
    // Playback thread; allocates on the first period only (the period size is fixed).
    void read(std::span<const std::byte> bytes, const PcmFormat& format, std::size_t frames);

    // What the output wrote into the driver's buffers for that period, called
    // by the output once the data is in place (only when frames > 0):
    // interleaved (WASAPI, dummy) or per channel in an ASIO sample type.
    void deliveredInterleaved(std::span<const std::byte> bytes, const PcmFormat& format, std::size_t frames) noexcept;
    void deliveredAsio(long sampleType, std::span<void* const> channelBuffers, std::size_t frames) noexcept;

    // From the start of an output session. Not while the output runs.
    void reset() noexcept;

    // Thread-safe.
    DeliveryResult result() const noexcept;

private:
    void compare(std::size_t frames, std::size_t deliveredFrames) noexcept;

    std::vector<std::int64_t> expected_; // canonical values of the last read, interleaved
    std::vector<std::int64_t> delivered_;
    std::size_t expectedFrames_ = 0;
    std::uint16_t channels_ = 0;
    std::atomic<std::uint64_t> matched_{0};
    std::atomic<std::uint64_t> mismatched_{0};
};

} // namespace ap::engine
