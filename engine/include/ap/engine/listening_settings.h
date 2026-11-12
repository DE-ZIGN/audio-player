#pragma once

// The user's playback settings (task 6-3): the output, 原音モード or 音作りモード,
// and the processing settings, with the processing chain they drive.
//
//  - 原音モード (Original): nothing but the digital volume. ReplayGain and the
//    EQ are not applied; their settings are kept for 音作りモード. The
//    ISP-safe gain has its own setting here, off by default (CLAUDE.md
//    chapter 4), so the user can switch it on for a track that warns.
//  - 音作りモード (Processed): ReplayGain, the EQ and the ISP-safe gain (on,
//    measured, by default) as set.
// In either mode, with the volume at 100% and nothing else active, the chain
// is bypassed and playback takes the direct, bit-perfect path.
//
// The settings are saved as a small text file of key=value lines (no JSON
// library in the engine), read back leniently: unknown keys and bad values
// keep the defaults, so an older or damaged file never stops the app.

#include <ap/engine/dsp/eq.h>
#include <ap/engine/dsp/isp_safe.h>
#include <ap/engine/dsp/limiter.h>
#include <ap/engine/dsp/processing.h>
#include <ap/engine/dsp/replay_gain.h>
#include <ap/engine/dsp/requantize.h>
#include <ap/engine/dsp/volume.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ap::engine {

enum class ListeningMode
{
    Original,  // 原音モード
    Processed, // 音作りモード
};

struct ListeningSettings
{
    ListeningMode mode = ListeningMode::Original;
    ReplayGainMode replayGain = ReplayGainMode::Off;
    IspSafeMode ispSafeOriginal = IspSafeMode::Off;       // in 原音モード
    IspSafeMode ispSafeProcessed = IspSafeMode::Measured; // in 音作りモード
    bool eqEnabled = true;
    std::vector<EqBand> eqBands; // at most EqStage::kMaxBands
    // The EQ's preamp: automatic (the largest boost), or this value (<= 0 dB;
    // what goes over is left to the safety limiter). Task 6-3.
    bool eqAutoPreamp = true;
    double eqManualPreampDb = 0.0;

    bool operator==(const ListeningSettings&) const = default;
};

enum class OutputKind
{
    Wasapi, // exclusive WASAPI
    Asio,
};

struct OutputChoice
{
    OutputKind kind = OutputKind::Wasapi;
    // WASAPI: the endpoint ID (empty: whatever device is the Windows default).
    // ASIO: the driver's CLSID.
    std::string id;
    // Shown to the user, and used to find the device again when its ID
    // changed (see resolveOutputDevice).
    std::string name;
    // Send DSD as DoP. Only for a DAC that decodes DoP (others play it as loud
    // noise, and it cannot be detected), so it is off for a newly chosen output.
    bool allowDop = false;

    bool operator==(const OutputChoice&) const = default;
};

struct AppSettings
{
    OutputChoice output;
    ListeningSettings listening;

    bool operator==(const AppSettings&) const = default;
};

std::string settingsToText(const AppSettings& settings);
AppSettings settingsFromText(std::string_view text);

// The stages in their fixed order (CLAUDE.md chapter 4): ReplayGain, EQ,
// volume, ISP-safe gain, (the ABX's requantization, task 6-6), safety
// limiter (then dither, in play()).
class ListeningChain
{
public:
    ListeningChain();

    // Sets the stages from the settings. Thread-safe like the stages' own
    // setters; the player notices when the chain leaves or returns to bypass.
    // Call from one thread (the settings are remembered for setEqSolo).
    void apply(const ListeningSettings& settings);

    // Solo (task 6-3): the EQ plays only a band-pass at that band's frequency
    // and Q, to hear what the band works on. Not part of the settings; kept
    // across apply() while the band exists. nullopt: off. In 原音モード, or
    // with the EQ off, there is no EQ, solo or not.
    void setEqSolo(std::optional<std::size_t> band);
    std::optional<std::size_t> eqSolo() const noexcept { return solo_; }

    const std::shared_ptr<ProcessingChain>& chain() const noexcept { return chain_; }

    const std::shared_ptr<ReplayGainStage> replayGain = std::make_shared<ReplayGainStage>();
    const std::shared_ptr<EqStage> eq = std::make_shared<EqStage>();
    const std::shared_ptr<VolumeStage> volume = std::make_shared<VolumeStage>();
    const std::shared_ptr<IspSafeStage> ispSafe = std::make_shared<IspSafeStage>();
    const std::shared_ptr<RequantizeStage> requantize = std::make_shared<RequantizeStage>(); // ABX only, off otherwise
    const std::shared_ptr<SafetyLimiter> limiter = std::make_shared<SafetyLimiter>();

private:
    void applyEq();

    std::shared_ptr<ProcessingChain> chain_;
    ListeningSettings settings_;
    std::optional<std::size_t> solo_;
};

} // namespace ap::engine
