#include "PlayerBridge.h"

#include <cmath>
#include <ap/engine/output/wasapi_exclusive_output.h>
#include <ap/engine/audio_file.h>
#include <ap/engine/prefetching_source.h>
#include <ap/engine/wav_file_source.h>

namespace ap::app {

using namespace ap::engine;

namespace {

const char* toString(PlayerState s)
{
    switch (s)
    {
        case PlayerState::Empty:   return "empty";
        case PlayerState::Stopped: return "stopped";
        case PlayerState::Playing: return "playing";
        case PlayerState::Ended:   return "ended";
        case PlayerState::Error:   return "error";
    }
    return "empty";
}

const char* toString(Repack r)
{
    switch (r)
    {
        case Repack::None:           return "none";
        case Repack::WiderContainer: return "widerContainer";
        case Repack::ZeroPadded:     return "zeroPadded";
    }
    return "none";
}

const char* toString(FormatRejection r)
{
    switch (r)
    {
        case FormatRejection::None:       return "none";
        case FormatRejection::SampleRate: return "sampleRate";
        case FormatRejection::BitDepth:   return "bitDepth";
        case FormatRejection::Channels:   return "channels";
        case FormatRejection::SampleType: return "sampleType";
        case FormatRejection::Other:      return "other";
    }
    return "other";
}

const char* toString(OutputError e)
{
    switch (e)
    {
        case OutputError::DeviceNotFound:          return "deviceNotFound";
        case OutputError::DeviceInUse:             return "deviceInUse";
        case OutputError::ExclusiveModeNotAllowed: return "exclusiveModeNotAllowed";
        case OutputError::FormatNotSupported:      return "formatNotSupported";
        case OutputError::DeviceInvalidated:       return "deviceInvalidated";
        case OutputError::Timeout:                 return "timeout";
        case OutputError::DeviceError:             return "deviceError";
    }
    return "deviceError";
}

const char* toString(PlayerErrorKind k)
{
    switch (k)
    {
        case PlayerErrorKind::None:               return "none";
        case PlayerErrorKind::FormatNotSupported: return "formatNotSupported";
        case PlayerErrorKind::Output:             return "output";
        case PlayerErrorKind::Load:               return "load";
        case PlayerErrorKind::DopNotAllowed:      return "dopNotAllowed";
        case PlayerErrorKind::Other:              return "other";
    }
    return "other";
}

const char* toString(OutputApi a)
{
    switch (a)
    {
        case OutputApi::Dummy:           return "dummy";
        case OutputApi::WasapiExclusive: return "wasapiExclusive";
        case OutputApi::Asio:            return "asio";
    }
    return "dummy";
}

const char* toString(SampleLayout l)
{
    switch (l)
    {
        case SampleLayout::Interleaved:            return "interleaved";
        case SampleLayout::PerChannel:             return "perChannel";
        case SampleLayout::PerChannelRightAligned: return "perChannelRightAligned";
    }
    return "interleaved";
}

const char* toString(MeterState s)
{
    switch (s)
    {
        case MeterState::Idle: return "idle";
        case MeterState::Measuring: return "measuring";
        case MeterState::NotPcm: return "notPcm";
    }
    return "idle";
}

// A level in dB (dBTP or LUFS), unrounded: the page rounds once for display
// (rounding here too gave -19.95 -> "-19.9" where -19.954 should show "-20.0").
// "-inf" for silence.
juce::var levelVar(double db)
{
    return std::isfinite(db) ? juce::var(db) : juce::var("-inf");
}

juce::var peaksVar(const std::vector<double>& linear)
{
    juce::Array<juce::var> a;
    for (const double x : linear)
        a.add(levelVar(x > 0.0 ? 20.0 * std::log10(x) : -HUGE_VAL));
    return a;
}

// The meters on what goes to the output (task 4-4).
juce::var meterVar(const MeterStatus& m)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("state", toString(m.state));
    o->setProperty("queueIndex", static_cast<int>(m.queueIndex));
    o->setProperty("droppedFrames", static_cast<double>(m.droppedFrames));
    if (m.reading)
    {
        const auto& r = *m.reading;
        auto* ro = new juce::DynamicObject();
        ro->setProperty("channels", static_cast<int>(r.channels));
        ro->setProperty("truePeakRecent", peaksVar(r.truePeakRecent));
        ro->setProperty("truePeakMax", peaksVar(r.truePeakMax));
        ro->setProperty("momentaryLufs", levelVar(r.momentaryLufs));
        ro->setProperty("shortTermLufs", levelVar(r.shortTermLufs));
        ro->setProperty("integratedLufs", levelVar(r.integratedLufs));
        ro->setProperty("trackFrames", static_cast<double>(r.trackFrames));
        o->setProperty("reading", juce::var(ro));
    }
    else
    {
        o->setProperty("reading", juce::var());
    }
    return juce::var(o);
}

const char* toString(IntegrityState s)
{
    switch (s)
    {
        case IntegrityState::NotAvailable: return "notAvailable";
        case IntegrityState::Pending:      return "pending";
        case IntegrityState::Passed:       return "passed";
        case IntegrityState::Failed:       return "failed";
        case IntegrityState::Skipped:      return "skipped";
    }
    return "notAvailable";
}

// The facts of the path that are not already in the status (formats, DoP, repack are).
juce::var pathVar(const SignalPath& p)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("container", juce::String(p.container));
    o->setProperty("processing", p.processing);
    juce::Array<juce::var> stages;
    for (const auto& n : p.processingStages)
        stages.add(juce::String(n));
    o->setProperty("processingStages", stages);
    o->setProperty("processingRequired", p.processingRequired);
    o->setProperty("dither", p.dither);
    o->setProperty("comparison", p.comparison);

    const auto& out = p.output;
    auto* d = new juce::DynamicObject();
    d->setProperty("api", toString(out.api));
    d->setProperty("periodFrames", static_cast<int>(out.periodFrames));
    d->setProperty("periodMilliseconds", out.periodMilliseconds);
    d->setProperty("eventDriven", out.eventDriven);
    d->setProperty("layout", toString(out.layout));
    if (out.asio)
    {
        auto* a = new juce::DynamicObject();
        a->setProperty("driverName", juce::String::fromUTF8(out.asio->driverName.c_str()));
        a->setProperty("driverVersion", static_cast<juce::int64>(out.asio->driverVersion));
        a->setProperty("sampleType", juce::String(out.asio->sampleTypeName));
        a->setProperty("rateChanged", out.asio->rateChanged);
        a->setProperty("rateBefore", out.asio->rateBefore);
        d->setProperty("asio", juce::var(a));
    }
    else
    {
        d->setProperty("asio", juce::var());
    }
    if (out.endpointVolume)
    {
        auto* v = new juce::DynamicObject();
        v->setProperty("scalar", static_cast<double>(out.endpointVolume->scalar));
        v->setProperty("muted", out.endpointVolume->muted);
        v->setProperty("hardware", out.endpointVolume->hardwareVolume);
        d->setProperty("endpointVolume", juce::var(v));
    }
    else
    {
        d->setProperty("endpointVolume", juce::var());
    }
    o->setProperty("output", juce::var(d));
    return juce::var(o);
}

juce::var formatVar(const PcmFormat& f)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("sampleRate", static_cast<int>(f.sampleRate));
    o->setProperty("channels", static_cast<int>(f.channels));
    o->setProperty("containerBits", static_cast<int>(f.containerBits));
    o->setProperty("validBits", static_cast<int>(f.validBits));
    o->setProperty("sampleType", f.type == SampleType::Int ? "int" : "float");
    return juce::var(o);
}

} // namespace

PlayerBridge::PlayerBridge(Player::OutputFactory outputFactory, bool allowDop)
    : player_(outputFactory ? std::move(outputFactory)
                            : Player::OutputFactory([] { return std::make_unique<WasapiExclusiveOutput>(); }),
              PlaybackOptions{.allowRepack = true, .allowDop = allowDop})
{
    startTimerHz(10);
}

PlayerBridge::~PlayerBridge()
{
    stopTimer();
    player_.stop();
}

juce::WebBrowserComponent::Options PlayerBridge::registerFunctions(juce::WebBrowserComponent::Options options)
{
    using Args = const juce::Array<juce::var>&;
    using Completion = juce::WebBrowserComponent::NativeFunctionCompletion;

    return options
        .withNativeFunction("openFile", [this](Args, Completion done) { openFileAsync(std::move(done)); })
        .withNativeFunction("play", [this](Args, Completion done) {
            player_.play();
            pushStatus(true);
            done(true);
        })
        .withNativeFunction("stop", [this](Args, Completion done) {
            player_.stop();
            pushStatus(true);
            done(true);
        })
        .withNativeFunction("seek", [this](Args args, Completion done) {
            if (!args.isEmpty())
                player_.seek(static_cast<std::uint64_t>(std::max<juce::int64>(0, static_cast<juce::int64>(args[0]))));
            pushStatus(true);
            done(true);
        })
        .withNativeFunction("getStatus", [this](Args, Completion done) { done(statusAsVar()); })
        .withNativeFunction("uiReady", [this](Args, Completion done) {
            done(true);
            if (onUiReady)
                onUiReady();
        })
        .withNativeFunction("selfTestResult", [this](Args args, Completion done) {
            done(true);
            if (onSelfTestResult)
                onSelfTestResult(args.isEmpty() ? juce::var() : args[0]);
        });
}

juce::String PlayerBridge::load(const juce::File& file)
{
    return loadFiles({file});
}

juce::String PlayerBridge::loadFiles(juce::Array<juce::File> files)
{
    // Play in file-name order (the dialog does not guarantee an order).
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileName().compareNatural(b.getFileName()) < 0;
    });

    // Check every header now so a wrong file is reported before playback starts.
    std::vector<QueueItem> items;
    for (const auto& file : files)
    {
        const std::filesystem::path path(file.getFullPathName().toWideCharPointer());
        try
        {
            openAudioFile(path); // reads the header only
        }
        catch (const std::exception& e)
        {
            return juce::String::fromUTF8("このファイルは読めません（WAV、FLAC、AIFF、ALAC・AAC（.m4a）、MP3、DSF、DFF に対応）: ") + file.getFileName() + " ("
                 + e.what() + ")";
        }
        // Read ahead on a separate thread so the playback thread never waits
        // for the disk (dropout prevention only). The next track is opened
        // and filled in the background while the current one plays.
        items.push_back({file.getFileName().toStdString(), [path] {
                             return std::make_unique<PrefetchingSource>(openAudioFile(path));
                         }});
    }

    player_.setQueue(std::move(items));
    pushStatus(true);
    const auto s = player_.status();
    if (s.state == PlayerState::Error)
        return juce::String::fromUTF8("ファイルを開けませんでした: ") + juce::String::fromUTF8(s.error.detail.c_str());
    return {};
}

void PlayerBridge::openFileAsync(juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    chooser_ = std::make_unique<juce::FileChooser>(juce::String::fromUTF8("音声ファイルを開く（複数選べます）"),
                                                   juce::File{}, supportedAudioFilePattern());
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, completion = std::move(completion)](const juce::FileChooser& fc) {
                              auto* result = new juce::DynamicObject();
                              const auto files = fc.getResults();
                              if (files.isEmpty())
                              {
                                  result->setProperty("ok", false);
                                  result->setProperty("cancelled", true);
                              }
                              else
                              {
                                  const auto message = loadFiles(files);
                                  result->setProperty("ok", message.isEmpty());
                                  if (message.isNotEmpty())
                                      result->setProperty("message", message);
                              }
                              completion(juce::var(result));
                          });
}

juce::var PlayerBridge::statusAsVar() const
{
    const auto s = player_.status();
    auto* o = new juce::DynamicObject();
    o->setProperty("state", toString(s.state));
    o->setProperty("fileName", juce::String::fromUTF8(s.trackName.c_str()));
    o->setProperty("queueIndex", static_cast<int>(s.queueIndex));
    o->setProperty("queueLength", static_cast<int>(s.queueLength));

    juce::Array<juce::var> transitions;
    const std::size_t first = s.transitions.size() > 5 ? s.transitions.size() - 5 : 0;
    for (std::size_t i = first; i < s.transitions.size(); ++i)
    {
        const auto& t = s.transitions[i];
        auto* to = new juce::DynamicObject();
        to->setProperty("fromIndex", static_cast<int>(t.fromIndex));
        to->setProperty("toIndex", static_cast<int>(t.toIndex));
        to->setProperty("kind", t.kind == TransitionKind::Gapless             ? "gapless"
                                : t.kind == TransitionKind::ProcessingChanged ? "processingChanged"
                                                                              : "reopened");
        to->setProperty("fromFormat", formatVar(t.fromFormat));
        to->setProperty("toFormat", formatVar(t.toFormat));
        to->setProperty("switchMilliseconds", t.switchMilliseconds);
        transitions.add(juce::var(to));
    }
    o->setProperty("transitions", transitions);

    auto* d = new juce::DynamicObject();
    d->setProperty("lateBuffers", static_cast<int>(s.dropouts.lateBuffers));
    d->setProperty("deviceTimeouts", static_cast<int>(s.dropouts.deviceTimeouts));
    d->setProperty("readStarvations", static_cast<int>(s.dropouts.readStarvations));
    d->setProperty("starvedFrames", static_cast<juce::int64>(s.dropouts.starvedFrames));
    d->setProperty("realtimePriority", s.dropouts.realtimePriority);
    o->setProperty("dropouts", juce::var(d));
    o->setProperty("sourceFormat", s.sourceFormat ? formatVar(*s.sourceFormat) : juce::var());
    if (s.dop)
    {
        auto* dop = new juce::DynamicObject();
        dop->setProperty("dsdRate", static_cast<juce::int64>(s.dop->dsdRate));
        dop->setProperty("container", juce::String(s.dop->container));
        o->setProperty("dop", juce::var(dop));
    }
    else
    {
        o->setProperty("dop", juce::var());
    }
    o->setProperty("outputFormat", s.plan ? formatVar(s.plan->output) : juce::var());
    o->setProperty("repack", s.plan ? toString(s.plan->repack) : "none");
    o->setProperty("outputName", juce::String::fromUTF8(s.outputName.c_str()));
    o->setProperty("path", s.path ? pathVar(*s.path) : juce::var());

    // The stages running now with their values, and the BIT-PERFECT judgement (task 4-2).
    juce::Array<juce::var> stages;
    for (const auto& st : s.stages)
    {
        auto* so = new juce::DynamicObject();
        so->setProperty("name", juce::String(st.name));
        so->setProperty("gainDb", st.gainDb && std::isfinite(*st.gainDb) ? juce::var(*st.gainDb)
                                  : st.gainDb                             ? juce::var("-inf")
                                                                          : juce::var());
        stages.add(juce::var(so));
    }
    o->setProperty("stages", stages);
    if (s.bitPerfect)
    {
        const auto& v = *s.bitPerfect;
        auto* bp = new juce::DynamicObject();
        bp->setProperty("bitPerfect", v.bitPerfect);
        bp->setProperty("quantized", v.quantized);
        bp->setProperty("dither", v.dither);
        bp->setProperty("comparison", v.comparison);
        bp->setProperty("repacked", v.repacked);
        bp->setProperty("dop", v.dop);
        bp->setProperty("floatSource", v.floatSource);
        bp->setProperty("dummyOutput", v.dummyOutput);
        bp->setProperty("deviceVolume", v.deviceVolume ? juce::var(*v.deviceVolume) : juce::var());
        bp->setProperty("deviceMuted", v.deviceMuted);
        bp->setProperty("deliveryMismatch", v.deliveryMismatch);
        o->setProperty("bitPerfect", juce::var(bp));
    }
    else
    {
        o->setProperty("bitPerfect", juce::var());
    }
    if (s.delivery)
    {
        // Frame counts fit a double exactly (a JS number) for any realistic session.
        auto* dr = new juce::DynamicObject();
        dr->setProperty("matchedFrames", static_cast<double>(s.delivery->matchedFrames));
        dr->setProperty("mismatchedFrames", static_cast<double>(s.delivery->mismatchedFrames));
        o->setProperty("delivery", juce::var(dr));
    }
    else
    {
        o->setProperty("delivery", juce::var());
    }

    o->setProperty("meter", meterVar(s.meter));

    auto* ic = new juce::DynamicObject();
    ic->setProperty("state", toString(s.integrity.state));
    ic->setProperty("decodeErrors", static_cast<int>(s.integrity.decodeErrors));
    o->setProperty("integrity", juce::var(ic));
    juce::Array<juce::var> failures;
    for (const auto i : s.integrityFailures)
        failures.add(static_cast<int>(i));
    o->setProperty("integrityFailures", failures);
    o->setProperty("positionFrames", static_cast<juce::int64>(s.positionFrames));
    o->setProperty("totalFrames", static_cast<juce::int64>(s.totalFrames));

    auto* e = new juce::DynamicObject();
    e->setProperty("kind", toString(s.error.kind));
    e->setProperty("reason", toString(s.error.formatCheck.reason));
    e->setProperty("outputError", toString(s.error.outputError));
    e->setProperty("detail", juce::String::fromUTF8(s.error.detail.c_str()));
    juce::Array<juce::var> rates;
    for (const auto r : s.error.formatCheck.acceptedRates)
        rates.add(static_cast<int>(r));
    e->setProperty("acceptedRates", rates);
    o->setProperty("error", juce::var(e));
    return juce::var(o);
}

void PlayerBridge::pushStatus(bool force)
{
    if (browser_ == nullptr)
        return;
    const auto status = statusAsVar();
    const auto json = juce::JSON::toString(status, true);
    if (!force && json == lastSent_)
        return;
    lastSent_ = json;
    browser_->emitEventIfBrowserIsVisible("playbackStatus", status);
}

void PlayerBridge::timerCallback()
{
    pushStatus(false);
}

} // namespace ap::app
