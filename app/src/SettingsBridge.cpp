#include "SettingsBridge.h"

#include "AsioCheck.h"

#include <ap/engine/output/asio_output.h>

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

const char* toString(EqFilterType t)
{
    switch (t)
    {
        case EqFilterType::Peak:      return "peak";
        case EqFilterType::LowShelf:  return "lowShelf";
        case EqFilterType::HighShelf: return "highShelf";
        case EqFilterType::LowPass:   return "lowPass";
        case EqFilterType::HighPass:  return "highPass";
    }
    return "peak";
}

std::optional<EqFilterType> filterFrom(const juce::String& s)
{
    for (const auto t : {EqFilterType::Peak, EqFilterType::LowShelf, EqFilterType::HighShelf, EqFilterType::LowPass,
                         EqFilterType::HighPass})
        if (s == toString(t))
            return t;
    return std::nullopt;
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

juce::var bandsVar(const std::vector<EqBand>& bands)
{
    juce::Array<juce::var> a;
    for (const auto& b : bands)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("type", toString(b.type));
        o->setProperty("frequency", b.frequency);
        o->setProperty("gainDb", b.gainDb);
        o->setProperty("q", b.q);
        o->setProperty("enabled", b.enabled);
        a.add(juce::var(o));
    }
    return a;
}

// Bands with a known type and finite, positive frequency and Q. At most 32.
std::vector<EqBand> bandsFrom(const juce::var& v)
{
    std::vector<EqBand> out;
    if (const auto* a = v.getArray())
        for (const auto& b : *a)
        {
            const auto type = filterFrom(b.getProperty("type", "").toString());
            const double f = b.getProperty("frequency", 0.0);
            const double g = b.getProperty("gainDb", 0.0);
            const double q = b.getProperty("q", 0.0);
            if (!type || !std::isfinite(f) || !std::isfinite(g) || !std::isfinite(q) || f <= 0.0 || q <= 0.0)
                continue;
            out.push_back(EqBand{*type, f, g, q, static_cast<bool>(b.getProperty("enabled", true))});
            if (out.size() == EqStage::kMaxBands)
                break;
        }
    return out;
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
    return juce::var(o);
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
                                                         + file.getFileName() + " (" + e.what() + ")"));
                              }
                          });
}

} // namespace ap::app
