#pragma once

// EQ bands and named EQ presets to and from the page's values (tasks 6-3 and
// 6-9). Shared by the settings bridge (saving presets) and the player bridge
// (ABX of 設定 A と設定 B). See ui/src/settings.ts for the shapes.

#include <ap/engine/dsp/eq.h>
#include <ap/engine/listening_settings.h>

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace ap::app {

const char* filterName(ap::engine::EqFilterType type);
std::optional<ap::engine::EqFilterType> filterFrom(const juce::String& name);

juce::var bandsVar(const std::vector<ap::engine::EqBand>& bands);
// Bands with a known type and finite, positive frequency and Q. At most 32.
std::vector<ap::engine::EqBand> bandsFrom(const juce::var& v);

// { name, bands, autoPreamp, manualPreampDb }
juce::var presetVar(const ap::engine::NamedEqPreset& preset);
juce::var presetsVar(const std::vector<ap::engine::NamedEqPreset>& presets);
// The name trimmed, one line, at most kMaxPresetName characters; the preamp
// within -30..0 dB. nullopt without a name.
std::optional<ap::engine::NamedEqPreset> presetFrom(const juce::var& v);
// Presets as above; a name used again keeps the first; at most kMaxEqPresets.
std::vector<ap::engine::NamedEqPreset> presetsFrom(const juce::var& v);

inline constexpr int kMaxPresetName = 60;

} // namespace ap::app
