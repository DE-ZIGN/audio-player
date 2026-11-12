#pragma once

// Requantization to fewer bits with TPDF dither (task 6-6): for the ABX
// "原音 と 16bit にしたもの". The samples stay in 64-bit floating point but
// land on the grid of a `bits`-bit integer (TPDF dither of +-1 LSB before
// rounding, as the processing path does when it writes an integer format,
// task 3-2), clamped to that format's range. Off (0 bits): identity.
//
// Only a listening test: it is never part of the saved settings.

#include <ap/engine/dsp/dither.h>
#include <ap/engine/dsp/processing.h>

#include <atomic>

namespace ap::engine {

class RequantizeStage final : public ProcessingStage
{
public:
    // 0: off; otherwise 8..24. Thread-safe.
    void setBits(int bits) noexcept;
    int bits() const noexcept { return bits_; }

    std::string name() const override { return "requantize"; }
    void prepare(std::uint32_t, std::uint16_t) override {}
    bool isIdentity() const noexcept override { return bits_ == 0; }
    std::optional<double> displayGainDb() const noexcept override { return std::nullopt; }
    void process(std::span<double> samples, std::uint16_t channels) noexcept override;

private:
    std::atomic<int> bits_{0};
    dsp::TpdfDither dither_;
};

} // namespace ap::engine
