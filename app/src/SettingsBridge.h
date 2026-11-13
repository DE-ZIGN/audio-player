#pragma once

#include "PlayerBridge.h"

#include <ap/engine/listening_settings.h>
#include <ap/engine/output/device_watcher.h>
#include <ap/engine/output/wasapi_exclusive_output.h>

#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <memory>

namespace ap::app {

// The settings screen (task 6-3): the output, 原音モード / 音作りモード, the
// ISP-safe gain, ReplayGain and the EQ. Saved in <folder>/settings.txt
// (ap::engine::settingsToText) on every change and applied at start-up, before
// the page has loaded.
//
// Native functions (JavaScript -> C++), see ui/src/backend.ts:
//   settingsGet()              { output, listening } (see ui/src/settings.ts)
//   settingsSetListening(l)    applies and saves; { ok, eq: { preampDb, skipped } }
//   settingsOutputs()          { wasapi: [{ id, name, isDefault }], asio: [{ id, name }], message? }
//   settingsSetOutput(o)       an ASIO driver is checked in a child process first;
//                              { ok, message? }
//   settingsImportEq()         a file dialog for an AutoEQ / Equalizer APO file;
//                              { ok, cancelled?, bands, warnings, filePreampDb?, message? }
//   settingsEqCurve(bands)     { sampleRate, preampDb, skipped, points: [[Hz, dB], ...] }
//                              (preampDb: the automatic one)
//   settingsEqSolo(i)          solo EQ band i (-1: off); not saved
//   settingsSetEqPresets(list) the named EQ presets, the whole list (task 6-9);
//                              saved; { ok, eqPresets }. settingsGet() has them as eqPresets.
// Event (C++ -> JavaScript): "outputsChanged" when an output device was
// added, removed, enabled or disabled, or the Windows default changed.
class SettingsBridge final
{
public:
    static ap::engine::AppSettings load(const juce::File& file);

    SettingsBridge(juce::File file, ap::engine::AppSettings settings, PlayerBridge& player);
    ~SettingsBridge();

    juce::WebBrowserComponent::Options registerFunctions(juce::WebBrowserComponent::Options options);
    void attach(juce::WebBrowserComponent* browser) { browser_ = browser; }

private:
    void save();
    juce::var settingsVar() const;
    void setOutputAsync(const juce::var& choice, juce::WebBrowserComponent::NativeFunctionCompletion done);
    void importEqAsync(juce::WebBrowserComponent::NativeFunctionCompletion done);

    juce::File file_;
    ap::engine::AppSettings settings_;
    PlayerBridge& player_;
    juce::WebBrowserComponent* browser_ = nullptr;
    std::unique_ptr<juce::FileChooser> chooser_;

    ap::engine::ScopedComInit com_;
    std::unique_ptr<ap::engine::DeviceWatcher> watcher_;
    // Cleared on destruction, so work finishing on other threads does nothing.
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<std::atomic<bool>> eventPending_ = std::make_shared<std::atomic<bool>>(false);
};

} // namespace ap::app
