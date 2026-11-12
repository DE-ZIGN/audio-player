#pragma once

// A region of a track played over and over (task 6-6, the ABX). When it
// reaches `fade` frames before the end of the region, it mixes those last
// frames with the region's first `fade` frames (a raised-cosine crossfade),
// then goes on from start + fade: the jump back makes no click.
//
// Away from the join the bytes are the file's exactly. At the join the values
// are mixed (converted to 64-bit floating point and back, rounded): fine for
// the ABX, which always takes the processing path. Never for DoP (refused).
//
// position() is within the whole track and totalFrames() is the track's, so
// the seek bar shows where the region is. read() never reaches an end.

#include <ap/engine/pcm_source.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace ap::engine {

class LoopingSource final : public PcmSource
{
public:
    static constexpr double kFadeSeconds = 0.005;

    // `length` is cut to the end of the track; `fade` to a quarter of the
    // region. Throws std::invalid_argument for DoP or an empty region.
    LoopingSource(std::unique_ptr<PcmSource> source, std::uint64_t start, std::uint64_t length, std::uint32_t fade);

    const PcmFormat& format() const noexcept override { return source_->format(); }
    std::uint64_t totalFrames() const noexcept override { return source_->totalFrames(); }
    std::size_t read(std::span<std::byte> dst) override;
    // Clamped into the region (before its join).
    bool seek(std::uint64_t frame) override;
    std::uint64_t position() const noexcept override { return position_; }
    std::string containerName() const override { return source_->containerName(); }

    std::uint64_t loopStart() const noexcept { return start_; }
    std::uint64_t loopLength() const noexcept { return length_; }
    std::uint32_t fadeFrames() const noexcept { return fade_; }

private:
    void makeJoin(); // reads the tail and the head and mixes them into join_

    std::unique_ptr<PcmSource> source_;
    std::uint64_t start_;
    std::uint64_t length_;
    std::uint32_t fade_;
    std::uint64_t position_; // of the next frame, within the track
    std::vector<std::byte> join_; // the mixed frames of the join
    std::size_t joinPos_ = 0;     // frames of join_ already given out
    bool inJoin_ = false;
};

} // namespace ap::engine
