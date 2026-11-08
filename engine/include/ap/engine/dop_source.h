#pragma once

// DoP (DSD over PCM, v1.1): DSD carried in 24-bit PCM so that PCM-only paths
// (WASAPI exclusive, ASIO) can deliver it to a DoP-capable DAC unchanged.
//
// Each 24-bit sample = marker in the top byte (0x05 and 0xFA alternating per
// frame, the same in every channel of a frame) + 16 DSD bits of that channel
// below it, earliest bits highest. The PCM rate is the DSD rate / 16
// (DSD64 -> 176.4 kHz, DSD128 -> 352.8 kHz, DSD256 -> 705.6 kHz).
//
// Each track is made an even number of frames long (padding with DSD
// silence, 0x69), so the marker sequence continues correctly into the next
// track when tracks are joined gaplessly. A short track end is padded the
// same way. Nothing else is added or changed; DoP must never be processed.

#include <ap/engine/dsd_source.h>
#include <ap/engine/pcm_source.h>

#include <memory>
#include <vector>

namespace ap::engine {

inline constexpr std::uint8_t kDopMarkerEven = 0x05;
inline constexpr std::uint8_t kDopMarkerOdd = 0xFA;
inline constexpr std::uint8_t kDsdSilence = 0x69;

class DopSource final : public PcmSource
{
public:
    explicit DopSource(std::unique_ptr<DsdSource> dsd);

    const PcmFormat& format() const noexcept override { return format_; }
    std::uint64_t totalFrames() const noexcept override { return totalFrames_; }
    std::size_t read(std::span<std::byte> dst) override;
    bool seek(std::uint64_t frame) override;
    std::uint64_t position() const noexcept override { return position_; }
    std::optional<DopInfo> dop() const override { return info_; }
    std::string containerName() const override { return info_.container; }

private:
    std::unique_ptr<DsdSource> dsd_;
    PcmFormat format_;
    DopInfo info_;
    std::uint64_t totalFrames_ = 0;
    std::uint64_t position_ = 0;
    std::vector<std::byte> scratch_;
};

} // namespace ap::engine
