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

#include <ap/engine/dsp/crossfeed.h>
#include <ap/engine/dsp/eq.h>
#include <ap/engine/dsp/tube_auto.h>
#include <ap/engine/dsp/isp_safe.h>
#include <ap/engine/dsp/limiter.h>
#include <ap/engine/dsp/processing.h>
#include <ap/engine/dsp/replay_gain.h>
#include <ap/engine/dsp/requantize.h>
#include <ap/engine/dsp/space.h>
#include <ap/engine/dsp/tone.h>
#include <ap/engine/dsp/tube.h>
#include <ap/engine/dsp/volume.h>

#include <array>
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
    // The tone controls and the tube (tasks 9-2, 9-3), in 音作りモード only.
    double toneBassDb = 0.0;
    double toneTrebleDb = 0.0;
    bool tubeEnabled = false;
    TubeType tubeType = TubeType::Se300B;
    double tubeDriveDb = TubeStage::kDefaultDriveDb;
    // The tube's warm-up and the tubes' differences (task 9-8, a show; off by
    // default), with the tube.
    bool tubeWarmUp = false;
    bool tubeUnits = false;
    int tubeUnit = kMinTubeUnit; // kMinTubeUnit .. kMaxTubeUnit, kept while tubeUnits is off
    // Crossfeed for headphones (task 9-5), in 音作りモード only.
    bool crossfeedEnabled = false;
    CrossfeedLevel crossfeedLevel = CrossfeedLevel::Default;
    // The space (task 9-6), in 音作りモード only.
    SpaceRoom spaceRoom = SpaceRoom::Off;
    double spaceMixPercent = SpaceStage::kDefaultMixPercent; // 0 .. 100
    double spacePreDelayMs = SpaceStage::kDefaultPreDelayMs; // 0 .. 100
    double spaceLowCutHz = SpaceStage::kDefaultLowCutHz;     // 0 (none) or 20 .. 500
    // Which IR CLUB, HALL and ARENA play: HALL the measured one by default,
    // the others the synthesized one (a room without a measured IR plays the
    // synthesized one if asked for it).
    std::array<SpaceIrSource, 3> spaceSource{SpaceIrSource::Synthesized, SpaceIrSource::Measured,
                                             SpaceIrSource::Synthesized};
    // The user's IR file for CLUB, HALL and ARENA (UTF-8 paths), used when the
    // source is File (kept when another source is chosen). A file that cannot
    // be read plays the room's default instead.
    std::array<std::string, 3> spaceIr;
    // The output rate (task 10-4), in 音作りモード only: the track's by
    // default. 原音モード never converts.
    OutputRateChoice outputRate;

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
    // noise, and it cannot be detected), so it is off for a newly chosen output,
    // and holds only for the device with this `id` (mayReceiveDop): never for
    // the Windows default, nor for what is used while this device is missing.
    bool allowDop = false;

    bool operator==(const OutputChoice&) const = default;
};

// An EQ curve under a name (task 6-9): the bands and the preamp. Whether the
// EQ is used, and the mode, stay with the settings.
struct NamedEqPreset
{
    std::string name;
    std::vector<EqBand> bands; // at most EqStage::kMaxBands
    bool autoPreamp = true;
    double manualPreampDb = 0.0;

    bool operator==(const NamedEqPreset&) const = default;
};

inline constexpr std::size_t kMaxEqPresets = 100;

NamedEqPreset eqPresetFrom(const ListeningSettings& settings, std::string name);
// Puts the preset's curve into the settings; the rest is left as it was.
void applyEqPreset(ListeningSettings& settings, const NamedEqPreset& preset);

struct AppSettings
{
    OutputChoice output;
    ListeningSettings listening;
    std::vector<NamedEqPreset> eqPresets; // at most kMaxEqPresets, in the user's order
    TubeAutoSettings tubeAuto;            // the tube type by genre (task 9-7)

    bool operator==(const AppSettings&) const = default;
};

std::string settingsToText(const AppSettings& settings);
AppSettings settingsFromText(std::string_view text);

// What became of a user IR file (task 9-6), for the settings screen.
struct SpaceIrStatus
{
    std::string path;   // as set (empty: the built-in room)
    bool loaded = false;
    IrFileProblem problem = IrFileProblem::None; // why it could not be read, when not loaded (the page says it)
    std::string error;                           // the technical text (English), when not loaded
    double seconds = 0.0;
    double sampleRate = 0.0;
    std::uint16_t channels = 0;     // used (1 or 2)
    std::uint16_t fileChannels = 0; // in the file
    bool truncated = false;         // longer than kMaxIrSeconds

    bool operator==(const SpaceIrStatus&) const = default;
};

// The stages in their fixed order (CLAUDE.md chapter 4): ReplayGain, EQ,
// tone, tube, space, crossfeed, volume, ISP-safe gain, (the ABX's
// requantization, task 6-6), safety limiter (then dither, in play()). The
// tube comes before the volume, so how much it bends does not change with the
// listening level (DRIVE decides it). The space comes after the tube (the
// room the amplifier plays in) and before the crossfeed (how that is heard
// on headphones).
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
    const std::shared_ptr<ToneStage> tone = std::make_shared<ToneStage>();
    const std::shared_ptr<TubeStage> tube = std::make_shared<TubeStage>();
    const std::shared_ptr<SpaceStage> space = std::make_shared<SpaceStage>();
    const std::shared_ptr<CrossfeedStage> crossfeed = std::make_shared<CrossfeedStage>();
    const std::shared_ptr<VolumeStage> volume = std::make_shared<VolumeStage>();
    const std::shared_ptr<IspSafeStage> ispSafe = std::make_shared<IspSafeStage>();
    const std::shared_ptr<RequantizeStage> requantize = std::make_shared<RequantizeStage>(); // ABX only, off otherwise
    const std::shared_ptr<SafetyLimiter> limiter = std::make_shared<SafetyLimiter>();

    // The user IR files as last applied (loaded once per path).
    std::array<SpaceIrStatus, 3> spaceIrStatus() const { return irStatus_; }

private:
    void applyEq();
    void loadSpaceIrs(const std::array<std::string, 3>& paths);

    std::shared_ptr<ProcessingChain> chain_;
    ListeningSettings settings_;
    std::optional<std::size_t> solo_;
    std::array<SpaceIrStatus, 3> irStatus_;
    std::array<std::shared_ptr<const ImpulseResponse>, 3> irs_;
};

} // namespace ap::engine
