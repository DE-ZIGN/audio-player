#include "SettingsBridge.h"

#include "EqVar.h"

#include "AsioCheck.h"

#include <ap/engine/output/asio_output.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace ap::app {

using namespace ap::engine;

namespace {

const char* toString(ListeningMode m)
{
    return m == ListeningMode::Processed ? "processed" : "original";
}

const char* toString(ReplayGainMode m)
{
    return m == ReplayGainMode::Track ? "track" : m == ReplayGainMode::Album ? "album" : "off";
}

const char* toString(IspSafeMode m)
{
    return m == IspSafeMode::Measured ? "measured" : m == IspSafeMode::Fixed ? "fixed" : "off";
}

ReplayGainMode replayGainFrom(const juce::String& s)
{
    return s == "track" ? ReplayGainMode::Track : s == "album" ? ReplayGainMode::Album : ReplayGainMode::Off;
}

IspSafeMode ispSafeFrom(const juce::String& s)
{
    return s == "measured" ? IspSafeMode::Measured : s == "fixed" ? IspSafeMode::Fixed : IspSafeMode::Off;
}

std::string utf8(const juce::var& v)
{
    return v.toString().toStdString();
}

juce::var listeningVar(const ListeningSettings& l)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("mode", toString(l.mode));
    o->setProperty("replayGain", toString(l.replayGain));
    o->setProperty("ispSafeOriginal", toString(l.ispSafeOriginal));
    o->setProperty("ispSafeProcessed", toString(l.ispSafeProcessed));
    o->setProperty("eqEnabled", l.eqEnabled);
    o->setProperty("eqBands", bandsVar(l.eqBands));
    o->setProperty("eqAutoPreamp", l.eqAutoPreamp);
    o->setProperty("eqManualPreampDb", l.eqManualPreampDb);
    o->setProperty("toneBassDb", l.toneBassDb);
    o->setProperty("toneTrebleDb", l.toneTrebleDb);
    o->setProperty("tubeEnabled", l.tubeEnabled);
    o->setProperty("tubeType", tubeTypeKey(l.tubeType));
    o->setProperty("tubeDriveDb", l.tubeDriveDb);
    o->setProperty("outputRateMode", outputRateModeKey(l.outputRate.mode));
    o->setProperty("outputRateHz", static_cast<int>(l.outputRate.rate));
    o->setProperty("outputRateMultiple", static_cast<int>(l.outputRate.multiple));
    o->setProperty("tubeWarmUp", l.tubeWarmUp);
    o->setProperty("tubeUnits", l.tubeUnits);
    o->setProperty("tubeUnit", l.tubeUnit);
    o->setProperty("crossfeedEnabled", l.crossfeedEnabled);
    o->setProperty("crossfeedLevel", crossfeedLevelKey(l.crossfeedLevel));
    o->setProperty("spaceRoom", spaceRoomKey(l.spaceRoom));
    o->setProperty("spaceMixPercent", l.spaceMixPercent);
    o->setProperty("spacePreDelayMs", l.spacePreDelayMs);
    o->setProperty("spaceLowCutHz", l.spaceLowCutHz);
    juce::Array<juce::var> irs, sources;
    for (const auto& p : l.spaceIr)
        irs.add(juce::String::fromUTF8(p.c_str()));
    for (const auto s : l.spaceSource)
        sources.add(spaceIrSourceKey(s));
    o->setProperty("spaceIr", irs);
    o->setProperty("spaceSource", sources);
    return juce::var(o);
}

// What became of the user IR files (task 9-6): [{ path, loaded, error?, seconds,
// sampleRate, channels, fileChannels, truncated }] for CLUB, HALL, ARENA.
juce::var spaceIrsVar(const std::array<SpaceIrStatus, 3>& st)
{
    juce::Array<juce::var> list;
    for (const auto& s : st)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("path", juce::String::fromUTF8(s.path.c_str()));
        o->setProperty("loaded", s.loaded);
        if (!s.error.empty())
            o->setProperty("error", juce::String::fromUTF8(s.error.c_str()));
        o->setProperty("seconds", s.seconds);
        o->setProperty("sampleRate", s.sampleRate);
        o->setProperty("channels", static_cast<int>(s.channels));
        o->setProperty("fileChannels", static_cast<int>(s.fileChannels));
        o->setProperty("truncated", s.truncated);
        list.add(juce::var(o));
    }
    return list;
}

ListeningSettings listeningFrom(const juce::var& v)
{
    ListeningSettings l;
    l.mode = v.getProperty("mode", "") == juce::var("processed") ? ListeningMode::Processed : ListeningMode::Original;
    l.replayGain = replayGainFrom(v.getProperty("replayGain", "").toString());
    l.ispSafeOriginal = ispSafeFrom(v.getProperty("ispSafeOriginal", "").toString());
    l.ispSafeProcessed = ispSafeFrom(v.getProperty("ispSafeProcessed", "").toString());
    l.eqEnabled = static_cast<bool>(v.getProperty("eqEnabled", true));
    l.eqBands = bandsFrom(v.getProperty("eqBands", {}));
    l.eqAutoPreamp = static_cast<bool>(v.getProperty("eqAutoPreamp", true));
    const double preamp = v.getProperty("eqManualPreampDb", 0.0);
    l.eqManualPreampDb = std::isfinite(preamp) ? std::clamp(preamp, -30.0, 0.0) : 0.0;
    const auto number = [&v](const char* key, double fallback, double lo, double hi) {
        const double x = v.getProperty(key, fallback);
        return std::isfinite(x) ? std::clamp(x, lo, hi) : fallback;
    };
    l.toneBassDb = number("toneBassDb", 0.0, -ToneStage::kMaxDb, ToneStage::kMaxDb);
    l.toneTrebleDb = number("toneTrebleDb", 0.0, -ToneStage::kMaxDb, ToneStage::kMaxDb);
    l.tubeEnabled = static_cast<bool>(v.getProperty("tubeEnabled", false));
    l.tubeType = tubeTypeFrom(v.getProperty("tubeType", "").toString().toStdString()).value_or(TubeType::Se300B);
    l.tubeDriveDb = number("tubeDriveDb", TubeStage::kDefaultDriveDb, TubeStage::kMinDriveDb, TubeStage::kMaxDriveDb);
    // The output rate (task 10-4): unknown values keep the defaults.
    l.outputRate.mode =
        outputRateModeFrom(v.getProperty("outputRateMode", "").toString().toStdString()).value_or(OutputRateMode::Track);
    if (const int hz = v.getProperty("outputRateHz", 0); hz > 0 && isOutputRateChoiceRate(static_cast<std::uint32_t>(hz)))
        l.outputRate.rate = static_cast<std::uint32_t>(hz);
    if (const int m = v.getProperty("outputRateMultiple", 0); m > 0 && isOutputRateMultiple(static_cast<std::uint32_t>(m)))
        l.outputRate.multiple = static_cast<std::uint32_t>(m);
    l.tubeWarmUp = static_cast<bool>(v.getProperty("tubeWarmUp", false));
    l.tubeUnits = static_cast<bool>(v.getProperty("tubeUnits", false));
    l.tubeUnit = static_cast<int>(number("tubeUnit", kMinTubeUnit, kMinTubeUnit, kMaxTubeUnit));
    l.crossfeedEnabled = static_cast<bool>(v.getProperty("crossfeedEnabled", false));
    l.crossfeedLevel =
        crossfeedLevelFrom(v.getProperty("crossfeedLevel", "").toString().toStdString()).value_or(CrossfeedLevel::Default);
    l.spaceRoom = spaceRoomFrom(v.getProperty("spaceRoom", "").toString().toStdString()).value_or(SpaceRoom::Off);
    l.spaceMixPercent = number("spaceMixPercent", SpaceStage::kDefaultMixPercent, 0.0, 100.0);
    l.spacePreDelayMs = number("spacePreDelayMs", SpaceStage::kDefaultPreDelayMs, 0.0, SpaceStage::kMaxPreDelayMs);
    const double cut = number("spaceLowCutHz", SpaceStage::kDefaultLowCutHz, 0.0, SpaceStage::kMaxLowCutHz);
    l.spaceLowCutHz = cut <= 0.0 ? 0.0 : std::max(cut, SpaceStage::kMinLowCutHz);
    if (const auto* irs = v.getProperty("spaceIr", {}).getArray())
        for (int i = 0; i < std::min(irs->size(), 3); ++i)
            l.spaceIr[static_cast<std::size_t>(i)] = (*irs)[i].toString().toStdString();
    if (const auto* sources = v.getProperty("spaceSource", {}).getArray())
        for (int i = 0; i < std::min(sources->size(), 3); ++i)
        {
            const auto room = static_cast<SpaceRoom>(i + 1);
            const auto s = spaceIrSourceFrom((*sources)[i].toString().toStdString()).value_or(defaultSpaceIrSource(room));
            l.spaceSource[static_cast<std::size_t>(i)] =
                s == SpaceIrSource::Measured && !hasMeasuredIr(room) ? SpaceIrSource::Synthesized : s;
        }
    return l;
}

juce::var outputVar(const OutputChoice& c)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("kind", c.kind == OutputKind::Asio ? "asio" : "wasapi");
    o->setProperty("id", juce::String::fromUTF8(c.id.c_str()));
    o->setProperty("name", juce::String::fromUTF8(c.name.c_str()));
    o->setProperty("allowDop", c.allowDop);
    return juce::var(o);
}

OutputChoice outputFrom(const juce::var& v)
{
    OutputChoice c;
    c.kind = v.getProperty("kind", "") == juce::var("asio") ? OutputKind::Asio : OutputKind::Wasapi;
    c.id = utf8(v.getProperty("id", ""));
    c.name = utf8(v.getProperty("name", ""));
    c.allowDop = static_cast<bool>(v.getProperty("allowDop", false));
    return c;
}

juce::var result(bool ok, const juce::String& message = {})
{
    auto* o = new juce::DynamicObject();
    o->setProperty("ok", ok);
    if (message.isNotEmpty())
        o->setProperty("message", message);
    return juce::var(o);
}

juce::var eqInfo(const std::vector<EqBand>& bands, double rate)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("sampleRate", rate);
    o->setProperty("preampDb", eqAutoPreampDb(bands, rate));
    int skipped = 0;
    for (const auto& b : bands)
        if (b.enabled && !designBiquad(b, rate))
            ++skipped;
    o->setProperty("skipped", skipped);
    return juce::var(o);
}


// The tube type by genre (task 9-7), to and from the page.
juce::var tubeAutoVar(const TubeAutoSettings& t)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("enabled", t.enabled);
    juce::Array<juce::var> rules, albums;
    for (const auto& r : t.rules)
    {
        auto* x = new juce::DynamicObject();
        x->setProperty("genre", juce::String::fromUTF8(r.genre.c_str()));
        x->setProperty("type", juce::String(tubeTypeKey(r.type)));
        rules.add(juce::var(x));
    }
    for (const auto& a : t.albums)
    {
        auto* x = new juce::DynamicObject();
        x->setProperty("albumKey", juce::String::fromUTF8(a.albumKey.c_str()));
        x->setProperty("album", juce::String::fromUTF8(a.album.c_str()));
        x->setProperty("type", juce::String(tubeTypeKey(a.type)));
        albums.add(juce::var(x));
    }
    o->setProperty("rules", rules);
    o->setProperty("albums", albums);
    return juce::var(o);
}

// Rules with a known type and a genre (trimmed, one line, at most
// kMaxTubeGenreLength bytes; a genre that compares equal to an earlier one is
// dropped); albums with a known type and a key (one choice per album).
TubeAutoSettings tubeAutoFrom(const juce::var& v)
{
    TubeAutoSettings t;
    t.enabled = static_cast<bool>(v.getProperty("enabled", false));
    t.rules.clear();
    auto text = [](const juce::var& x) {
        return x.toString().replace("\r", " ").replace("\n", " ").replace("\t", " ").trim().toStdString();
    };
    std::vector<std::string> seen;
    if (const auto* rules = v.getProperty("rules", juce::var()).getArray())
        for (const auto& r : *rules)
        {
            const auto type = tubeTypeFrom(r.getProperty("type", "").toString().toStdString());
            auto genre = text(r.getProperty("genre", ""));
            if (genre.size() > kMaxTubeGenreLength)
                genre = juce::String::fromUTF8(genre.c_str(), static_cast<int>(kMaxTubeGenreLength)).toStdString();
            const auto key = normalizedGenre(genre);
            if (!type || key.empty() || std::find(seen.begin(), seen.end(), key) != seen.end()
                || t.rules.size() >= kMaxTubeGenreRules)
                continue;
            seen.push_back(key);
            t.rules.push_back({std::move(genre), *type});
        }
    if (const auto* albums = v.getProperty("albums", juce::var()).getArray())
        for (const auto& a : *albums)
        {
            const auto type = tubeTypeFrom(a.getProperty("type", "").toString().toStdString());
            const auto key = a.getProperty("albumKey", "").toString().replace("\r", " ").replace("\n", " ").replace("\t", " ").toStdString();
            if (!type || key.empty() || t.albums.size() >= kMaxTubeAlbumChoices
                || std::any_of(t.albums.begin(), t.albums.end(), [&](const auto& x) { return x.albumKey == key; }))
                continue;
            t.albums.push_back({key, text(a.getProperty("album", "")), *type});
        }
    return t;
}

} // namespace

AppSettings SettingsBridge::load(const juce::File& file)
{
    if (!file.existsAsFile())
        return {};
    return settingsFromText(file.loadFileAsString().toStdString());
}

SettingsBridge::SettingsBridge(juce::File file, AppSettings settings, PlayerBridge& player)
    : file_(std::move(file)), settings_(std::move(settings)), player_(player)
{
    try
    {
        watcher_ = std::make_unique<DeviceWatcher>([alive = alive_, pending = eventPending_, this](const DeviceEvent&) {
            // A system thread. Several events come at once (a driver re-creating
            // its endpoint): tell the page once, a moment later.
            if (pending->exchange(true))
                return;
            juce::Timer::callAfterDelay(300, [alive, pending, this] {
                pending->store(false);
                if (*alive && browser_ != nullptr)
                    browser_->emitEventIfBrowserIsVisible("outputsChanged", juce::var());
            });
        });
    }
    catch (const std::exception&)
    {
        // Without notifications the list is still read whenever the page asks.
    }
}

SettingsBridge::~SettingsBridge()
{
    *alive_ = false;
    watcher_.reset();
}

void SettingsBridge::save()
{
    file_.getParentDirectory().createDirectory();
    file_.replaceWithText(juce::String::fromUTF8(settingsToText(settings_).c_str()));
}

juce::var SettingsBridge::settingsVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty("output", outputVar(settings_.output));
    o->setProperty("listening", listeningVar(settings_.listening));
    o->setProperty("eqPresets", presetsVar(settings_.eqPresets));
    o->setProperty("tubeAuto", tubeAutoVar(settings_.tubeAuto));
    o->setProperty("tubeAutoDefaultRules", tubeAutoVar(TubeAutoSettings{}).getProperty("rules", juce::var()));
    o->setProperty("spaceIrs", spaceIrsVar(player_.spaceIrStatus()));
    return juce::var(o);
}

juce::WebBrowserComponent::Options SettingsBridge::registerFunctions(juce::WebBrowserComponent::Options options)
{
    using Args = const juce::Array<juce::var>&;
    using Completion = juce::WebBrowserComponent::NativeFunctionCompletion;

    return options
        .withNativeFunction("settingsGet", [this](Args, Completion done) { done(settingsVar()); })
        .withNativeFunction("settingsSetListening",
                            [this](Args args, Completion done) {
                                if (args.isEmpty())
                                {
                                    done(result(false));
                                    return;
                                }
                                settings_.listening = listeningFrom(args[0]);
                                player_.setListening(settings_.listening);
                                save();
                                auto r = result(true);
                                const auto rate = player_.currentSampleRate();
                                r.getDynamicObject()->setProperty(
                                    "eq", eqInfo(settings_.listening.eqBands, rate > 0 ? rate : 48000.0));
                                r.getDynamicObject()->setProperty("spaceIrs", spaceIrsVar(player_.spaceIrStatus()));
                                done(r);
                            })
        .withNativeFunction("settingsSetEqPresets",
                            [this](Args args, Completion done) {
                                // The whole list, as the page has it (task 6-9).
                                if (args.isEmpty())
                                {
                                    done(result(false));
                                    return;
                                }
                                settings_.eqPresets = presetsFrom(args[0]);
                                save();
                                auto r = result(true);
                                r.getDynamicObject()->setProperty("eqPresets", presetsVar(settings_.eqPresets));
                                done(r);
                            })
        .withNativeFunction("settingsSetTubeAuto",
                            [this](Args args, Completion done) {
                                // The whole of it, as the page has it (task 9-7).
                                if (args.isEmpty())
                                {
                                    done(result(false));
                                    return;
                                }
                                settings_.tubeAuto = tubeAutoFrom(args[0]);
                                player_.setTubeAuto(settings_.tubeAuto);
                                save();
                                auto r = result(true);
                                r.getDynamicObject()->setProperty("tubeAuto", tubeAutoVar(settings_.tubeAuto));
                                done(r);
                            })
        .withNativeFunction("settingsOutputs",
                            [](Args, Completion done) {
                                auto* o = new juce::DynamicObject();
                                juce::Array<juce::var> wasapi, asio;
                                try
                                {
                                    for (const auto& d : WasapiExclusiveOutput::listDevices())
                                    {
                                        auto* x = new juce::DynamicObject();
                                        x->setProperty("id", juce::String::fromUTF8(d.id.c_str()));
                                        x->setProperty("name", juce::String::fromUTF8(d.name.c_str()));
                                        x->setProperty("isDefault", d.isDefault);
                                        wasapi.add(juce::var(x));
                                    }
                                }
                                catch (const std::exception&)
                                {
                                    o->setProperty("message",
                                                   juce::String::fromUTF8("出力デバイスの一覧を読めませんでした"));
                                }
                                for (const auto& d : listAsioDrivers()) // the registry only: no driver is loaded
                                {
                                    auto* x = new juce::DynamicObject();
                                    x->setProperty("id", juce::String(d.clsid));
                                    x->setProperty("name", juce::String::fromUTF8(d.name.c_str()));
                                    asio.add(juce::var(x));
                                }
                                o->setProperty("wasapi", wasapi);
                                o->setProperty("asio", asio);
                                done(juce::var(o));
                            })
        .withNativeFunction("settingsSetOutput",
                            [this](Args args, Completion done) {
                                if (args.isEmpty())
                                    done(result(false));
                                else
                                    setOutputAsync(args[0], std::move(done));
                            })
        .withNativeFunction("settingsImportEq", [this](Args, Completion done) { importEqAsync(std::move(done)); })
        .withNativeFunction("settingsChooseIr", [this](Args, Completion done) { chooseIrAsync(std::move(done)); })
        .withNativeFunction("settingsEqSolo",
                            [this](Args args, Completion done) {
                                const int i = args.isEmpty() ? -1 : static_cast<int>(args[0]);
                                player_.setEqSolo(i >= 0 ? std::optional<std::size_t>(static_cast<std::size_t>(i))
                                                         : std::nullopt);
                                done(true);
                            })
        .withNativeFunction("settingsEqCurve", [this](Args args, Completion done) {
            const auto bands = bandsFrom(args.isEmpty() ? juce::var() : args[0]);
            const auto current = player_.currentSampleRate();
            const double rate = current > 0 ? current : 48000.0;
            auto info = eqInfo(bands, rate);
            // 20 Hz to 20 kHz (or just below Nyquist), evenly on a log scale.
            const double top = std::min(20000.0, 0.49 * rate);
            constexpr int kPoints = 160;
            juce::Array<juce::var> points;
            for (int i = 0; i < kPoints; ++i)
            {
                const double f = 20.0 * std::pow(top / 20.0, i / double(kPoints - 1));
                juce::Array<juce::var> p;
                p.add(f);
                p.add(eqResponseDb(bands, f, rate));
                points.add(p);
            }
            info.getDynamicObject()->setProperty("points", points);
            done(info);
        });
}

void SettingsBridge::setOutputAsync(const juce::var& v, juce::WebBrowserComponent::NativeFunctionCompletion done)
{
    auto choice = outputFrom(v);
    auto apply = [this](OutputChoice c) {
        settings_.output = std::move(c);
        player_.setOutput(settings_.output);
        save();
    };
    if (choice.kind == OutputKind::Wasapi)
    {
        apply(std::move(choice));
        done(result(true));
        return;
    }

    std::optional<AsioDriverInfo> driver;
    for (const auto& d : listAsioDrivers())
        if (juce::String(d.clsid).equalsIgnoreCase(juce::String(choice.id)))
            driver = d;
    if (!driver)
    {
        done(result(false, juce::String::fromUTF8("この ASIO ドライバーは登録されていません")));
        return;
    }
    // Loading the driver in a child process takes up to about 10 s: not on the message thread.
    juce::Thread::launch([alive = alive_, driver = *driver, choice, apply, done = std::move(done)]() mutable {
        const auto check = checkAsioDriver(driver);
        juce::MessageManager::callAsync([alive, check, choice = std::move(choice), apply, done = std::move(done)]() mutable {
            if (!*alive)
                return;
            if (check.status != AsioProbeResult::Status::Ok)
            {
                done(result(false, juce::String::fromUTF8(asioProblemText(check).c_str())));
                return;
            }
            apply(std::move(choice));
            done(result(true));
        });
    });
}

void SettingsBridge::chooseIrAsync(juce::WebBrowserComponent::NativeFunctionCompletion done)
{
    chooser_ = std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("空間の IR のファイルを選ぶ（WAV・FLAC・AIFF、モノラルかステレオ）"), juce::File{},
        "*.wav;*.flac;*.aif;*.aiff;*.aifc");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [done = std::move(done)](const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              auto r = result(file != juce::File{});
                              if (file == juce::File{})
                                  r.getDynamicObject()->setProperty("cancelled", true);
                              else
                                  r.getDynamicObject()->setProperty("path", file.getFullPathName());
                              done(r);
                          });
}

void SettingsBridge::importEqAsync(juce::WebBrowserComponent::NativeFunctionCompletion done)
{
    chooser_ = std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("EQ の設定を読み込む（AutoEQ の ParametricEQ.txt、Equalizer APO）"), juce::File{},
        "*.txt;*.cfg");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [done = std::move(done)](const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File{})
                              {
                                  auto r = result(false);
                                  r.getDynamicObject()->setProperty("cancelled", true);
                                  done(r);
                                  return;
                              }
                              try
                              {
                                  const auto preset = loadEqPreset(
                                      std::filesystem::path(file.getFullPathName().toWideCharPointer()));
                                  auto r = result(true);
                                  auto* o = r.getDynamicObject();
                                  o->setProperty("bands", bandsVar(preset.bands));
                                  juce::Array<juce::var> warnings;
                                  for (const auto& w : preset.warnings)
                                      warnings.add(juce::String::fromUTF8(w.c_str()));
                                  o->setProperty("warnings", warnings);
                                  o->setProperty("filePreampDb",
                                                 preset.filePreampDb ? juce::var(*preset.filePreampDb) : juce::var());
                                  done(r);
                              }
                              catch (const std::exception& e)
                              {
                                  done(result(false, juce::String::fromUTF8("このファイルは読めません: ")
                                                         + file.getFileName() + " (" + juce::String::fromUTF8(e.what()) + ")"));
                              }
                          });
}

} // namespace ap::app
