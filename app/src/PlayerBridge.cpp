#include "PlayerBridge.h"

#include "EqVar.h"

#include "AsioCheck.h"

#include <cmath>
#include <ap/engine/output/asio_output.h>
#include <ap/engine/output/device_selection.h>
#include <ap/engine/output/wasapi_exclusive_output.h>
#include <ap/engine/audio_file.h>
#include <ap/engine/dsp/comparison.h>
#include <ap/engine/looping_source.h>
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
        ro->setProperty("vu", peaksVar(r.vu)); // the standard view's needles (task 6-8)
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

PlayerBridge::PlayerBridge(Player::OutputFactory outputFactory, const AppSettings& settings)
    : output_(settings.output),
      player_(outputFactory ? std::move(outputFactory) : Player::OutputFactory([this] { return openChosenOutput(); }),
              PlaybackOptions{.allowRepack = true,
                              .allowDop = settings.output.allowDop,
                              // The volume (task 6-2) and the settings screen's stages
                              // (task 6-3). With nothing active the chain is bypassed:
                              // the direct, bit-perfect path (task 3-2).
                              .processing = chain_.chain()})
{
    listening_ = settings.listening;
    tubeAuto_ = settings.tubeAuto;
    chain_.apply(settings.listening);
    startTimerHz(10);
}

PlayerBridge::~PlayerBridge()
{
    *alive_ = false;
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
        .withNativeFunction("playAt", [this](Args args, Completion done) {
            if (!args.isEmpty())
                player_.playAt(static_cast<std::size_t>(std::max<juce::int64>(0, static_cast<juce::int64>(args[0]))));
            pushStatus(true);
            done(true);
        })
        .withNativeFunction("getQueue", [this](Args, Completion done) {
            juce::Array<juce::var> items;
            for (const auto& m : queueMeta_)
            {
                auto* o = new juce::DynamicObject();
                o->setProperty("id", static_cast<juce::int64>(m.libraryId));
                o->setProperty("name", juce::String::fromUTF8(m.name.c_str()));
                o->setProperty("artist", juce::String::fromUTF8(m.artist.c_str()));
                o->setProperty("seconds", m.seconds ? juce::var(*m.seconds) : juce::var());
                items.add(juce::var(o));
            }
            auto* q = new juce::DynamicObject();
            q->setProperty("version", queueVersion_);
            q->setProperty("items", items);
            done(juce::var(q));
        })
        .withNativeFunction("setVolume", [this](Args args, Completion done) {
            if (!args.isEmpty())
            {
                const auto& v = args[0];
                const double db = v.isString() ? -HUGE_VAL : static_cast<double>(v);
                chain_.volume->setGainDb(std::isnan(db) ? 0.0 : std::min(0.0, db));
            }
            pushStatus(true);
            done(true);
        })
        .withNativeFunction("setRepeat", [this](Args args, Completion done) {
            const auto m = args.isEmpty() ? juce::String() : args[0].toString();
            player_.setRepeat(m == "all" ? ap::engine::RepeatMode::All
                              : m == "one" ? ap::engine::RepeatMode::One
                                           : ap::engine::RepeatMode::Off);
            done(true);
        })
        .withNativeFunction("abxStart",
                            [this](Args args, Completion done) {
                                abxStartAsync(args.isEmpty() ? juce::var() : args[0], std::move(done));
                            })
        .withNativeFunction("abxChoose",
                            [this](Args args, Completion done) {
                                if (abx_ && abx_->running && !args.isEmpty())
                                {
                                    const auto c = args[0].toString();
                                    const auto choice = c == "B" ? AbxChoice::B : c == "X" ? AbxChoice::X : AbxChoice::A;
                                    abx_->listening = choice;
                                    chain_.chain()->comparison().select(abx_->test.sideFor(choice));
                                }
                                pushStatus(true);
                                done(true);
                            })
        .withNativeFunction("abxAnswer",
                            [this](Args args, Completion done) {
                                if (abx_ && abx_->running && !args.isEmpty())
                                {
                                    abx_->test.answer(args[0].toString() == "B" ? AbxChoice::B : AbxChoice::A);
                                    // The sound being heard stays; a new X is drawn for the next trial.
                                    if (abx_->listening == AbxChoice::X)
                                        chain_.chain()->comparison().select(abx_->test.sideFor(AbxChoice::X));
                                    if (abx_->test.finished())
                                        abxFinish();
                                }
                                pushStatus(true);
                                done(true);
                            })
        .withNativeFunction("abxAbort",
                            [this](Args, Completion done) {
                                if (abx_ && abx_->running)
                                {
                                    abx_->test.abort();
                                    abxFinish();
                                }
                                pushStatus(true);
                                done(true);
                            })
        .withNativeFunction("abxClose",
                            [this](Args, Completion done) {
                                if (abx_ && !abx_->running)
                                    abx_.reset();
                                pushStatus(true);
                                done(true);
                            })
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
            // The engine's texts are UTF-8 (the file's name may be in them).
            return juce::String::fromUTF8("このファイルは読めません（WAV、FLAC、AIFF、ALAC・AAC（.m4a）、MP3、DSF、DFF に対応）: ") + file.getFileName() + " ("
                 + juce::String::fromUTF8(e.what()) + ")";
        }
        // Read ahead on a separate thread so the playback thread never waits
        // for the disk (dropout prevention only). The next track is opened
        // and filled in the background while the current one plays.
        items.push_back({file.getFileName().toStdString(), [path] {
                             return std::make_unique<PrefetchingSource>(openAudioFile(path));
                         }});
    }
    queueMeta_.clear();
    for (const auto& file : files)
        queueMeta_.push_back({file.getFileName().toStdString(),
                              std::filesystem::path(file.getFullPathName().toWideCharPointer()), std::nullopt, 0, {},
                              std::nullopt});
    ++queueVersion_;
    chain_.limiter->resetStats();

    player_.setQueue(std::move(items));
    pushStatus(true);
    const auto s = player_.status();
    if (s.state == PlayerState::Error)
        return juce::String::fromUTF8("ファイルを開けませんでした: ") + juce::String::fromUTF8(s.error.detail.c_str());
    return {};
}

QueueItem PlayerBridge::itemFor(const QueueFile& f) const
{
    auto path = f.path;
    const auto choice = tubeChoiceFor(f);
    return QueueItem{f.name, [path] { return std::make_unique<PrefetchingSource>(openAudioFile(path)); }, f.replayGain,
                     choice ? std::optional(choice->type) : std::nullopt};
}

std::optional<TubeChoice> PlayerBridge::tubeChoiceFor(const QueueFile& f) const
{
    return chooseTubeType(tubeAuto_, f.genres, f.albumKey);
}

void PlayerBridge::setTubeAuto(const TubeAutoSettings& settings)
{
    tubeAuto_ = settings;
    if (!abx_)
        refreshTubeTypes(); // during an ABX the sides' types play; the queue is put back with the new choices
    pushStatus(true);
}

void PlayerBridge::refreshTubeTypes()
{
    std::vector<std::optional<TubeType>> types;
    for (const auto& f : queueMeta_)
    {
        const auto c = tubeChoiceFor(f);
        types.push_back(c ? std::optional(c->type) : std::nullopt);
    }
    player_.setTubeTypes(types);
    const auto i = player_.status().queueIndex;
    chain_.tube->setTrackType(i < types.size() ? types[i] : std::nullopt);
}

void PlayerBridge::replaceUpcoming(std::vector<QueueFile> files)
{
    const auto current = player_.status().queueIndex;
    std::vector<QueueItem> items;
    for (const auto& f : files)
        items.push_back(itemFor(f));
    player_.replaceUpcoming(std::move(items));
    if (queueMeta_.size() > current + 1)
        queueMeta_.resize(current + 1);
    for (auto& f : files)
        queueMeta_.push_back(std::move(f));
    ++queueVersion_;
    pushStatus(true);
}

juce::String PlayerBridge::playQueue(std::vector<QueueFile> files, std::size_t start)
{
    std::vector<QueueItem> items;
    for (const auto& f : files)
        items.push_back(itemFor(f));
    if (items.empty())
        return juce::String::fromUTF8("再生する曲がありません");
    queueMeta_.clear();
    for (auto& f : files)
        queueMeta_.push_back(std::move(f));
    ++queueVersion_;
    chain_.limiter->resetStats();
    player_.setQueue(std::move(items), std::min(start, files.size() - 1));
    auto s = player_.status();
    if (s.state == PlayerState::Error)
    {
        pushStatus(true);
        return juce::String::fromUTF8("ファイルを開けませんでした: ") + juce::String::fromUTF8(s.error.detail.c_str());
    }
    player_.play();
    pushStatus(true);
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
    o->setProperty("queueVersion", queueVersion_);
    const double vol = chain_.volume->gainDb();
    o->setProperty("volumeDb", std::isfinite(vol) ? juce::var(vol) : juce::var("-inf"));

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
    {
        std::lock_guard lock(outputMutex_);
        o->setProperty("outputNotice", juce::String::fromUTF8(outputNotice_.c_str()));
    }
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
        if (!st.detail.empty())
            so->setProperty("detail", juce::String(st.detail));
        stages.add(juce::var(so));
    }
    o->setProperty("stages", stages);
    // The tube type by genre (task 9-7): the current track's album (for an
    // album's choice) and what chose its type, if anything did.
    {
        auto* ta = new juce::DynamicObject();
        const QueueFile* f = !abx_ && s.queueIndex < queueMeta_.size() ? &queueMeta_[s.queueIndex] : nullptr;
        if (f && !f->albumKey.empty())
        {
            auto* al = new juce::DynamicObject();
            al->setProperty("key", juce::String::fromUTF8(f->albumKey.c_str()));
            al->setProperty("title", juce::String::fromUTF8(f->album.c_str()));
            ta->setProperty("album", juce::var(al));
        }
        else
        {
            ta->setProperty("album", juce::var());
        }
        juce::Array<juce::var> genres;
        if (f)
            for (const auto& g : f->genres)
                genres.add(juce::String::fromUTF8(g.c_str()));
        ta->setProperty("genres", genres);
        const auto c = f ? tubeChoiceFor(*f) : std::nullopt;
        if (c)
        {
            auto* co = new juce::DynamicObject();
            co->setProperty("type", juce::String(tubeTypeKey(c->type)));
            co->setProperty("reason", c->reason == TubeChoiceReason::Album ? "album" : "genre");
            co->setProperty("matched", juce::String::fromUTF8(c->matched.c_str()));
            ta->setProperty("choice", juce::var(co));
        }
        else
        {
            ta->setProperty("choice", juce::var());
        }
        o->setProperty("tubeAuto", juce::var(ta));
    }
    // The safety limiter (CLAUDE.md chapter 4: how often and by how much it
    // acted), since the settings or the queue last changed.
    {
        const auto ls = chain_.limiter->stats();
        auto* lo = new juce::DynamicObject();
        lo->setProperty("events", static_cast<double>(ls.events));
        lo->setProperty("limitedFrames", static_cast<double>(ls.limitedFrames));
        lo->setProperty("maxReductionDb", ls.maxReductionDb);
        o->setProperty("limiter", juce::var(lo));
    }
    o->setProperty("abx", abxVar());
    o->setProperty("eqSolo", chain_.eqSolo() ? juce::var(static_cast<int>(*chain_.eqSolo())) : juce::var());
    // For the output check (task 6-4): the mode, the ISP-safe gain of that
    // mode, and the current track's measured true peak (the ISP warning).
    const bool processed = listening_.mode == ListeningMode::Processed;
    o->setProperty("mode", processed ? "processed" : "original");
    o->setProperty("tubeWarmUpSeconds", warmUpSeconds()); // task 9-8
    const auto isp = processed ? listening_.ispSafeProcessed : listening_.ispSafeOriginal;
    o->setProperty("ispSafe", isp == IspSafeMode::Measured ? "measured" : isp == IspSafeMode::Fixed ? "fixed" : "off");
    o->setProperty("trackTruePeakDb", s.trackTruePeak && *s.trackTruePeak > 0.0
                                          ? juce::var(20.0 * std::log10(*s.trackTruePeak))
                                          : juce::var());
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

void PlayerBridge::setOutput(const OutputChoice& output)
{
    const bool wasPlaying = player_.status().state == PlayerState::Playing;
    if (wasPlaying)
        player_.stop(); // keeps the track and the position
    {
        std::lock_guard lock(outputMutex_);
        output_ = output;
        outputNotice_.clear();
    }
    player_.setAllowDop(output.allowDop);
    if (wasPlaying)
        player_.play();
    pushStatus(true);
}

void PlayerBridge::setListening(const ListeningSettings& settings)
{
    listening_ = settings;
    chain_.apply(settings); // the player reopens the output if the path changes
    chain_.limiter->resetStats(); // the count shown is for the settings in use
    pushStatus(true);
}

void PlayerBridge::setEqSolo(std::optional<std::size_t> band)
{
    chain_.setEqSolo(band);
    pushStatus(true);
}

std::uint32_t PlayerBridge::currentSampleRate() const
{
    const auto s = player_.status();
    return s.sourceFormat ? s.sourceFormat->sampleRate : 0;
}

std::unique_ptr<AudioOutput> PlayerBridge::openChosenOutput()
{
    OutputChoice c;
    {
        std::lock_guard lock(outputMutex_);
        c = output_;
    }
    auto setNotice = [this](std::string text) {
        std::lock_guard lock(outputMutex_);
        outputNotice_ = std::move(text);
    };

    if (c.kind == OutputKind::Asio)
    {
        const auto drivers = listAsioDrivers();
        const AsioDriverInfo* driver = nullptr;
        for (const auto& d : drivers)
            if (_stricmp(d.clsid.c_str(), c.id.c_str()) == 0)
                driver = &d;
        for (const auto& d : drivers)
            if (driver == nullptr && d.name == c.name)
                driver = &d;
        if (driver == nullptr)
        {
            setNotice("選んだ ASIO ドライバー（" + c.name + "）が見つかりません。設定で出力を選び直してください");
            throw OutputException(OutputError::DeviceNotFound, "ASIO driver not found: " + c.name);
        }
        // A driver that crashes its host is caught in a child process first (task 1-4).
        const auto check = checkAsioDriver(*driver);
        if (check.status != AsioProbeResult::Status::Ok)
        {
            setNotice(asioProblemText(check));
            throw OutputException(OutputError::DeviceError, "ASIO driver check failed: " + check.message);
        }
        setNotice({});
        return std::make_unique<AsioOutput>(driver->name, AsioOutput::factoryFor(*driver));
    }

    std::string id; // empty: the Windows default device
    std::string notice;
    if (!c.id.empty() || !c.name.empty())
    {
        const auto r = resolveOutputDevice({c.id, c.name}, WasapiExclusiveOutput::listDevices());
        if (r.match == DeviceMatch::DefaultDevice)
            notice = "選んだ出力（" + c.name + "）が見つからないため、Windows の既定のデバイスで再生しています";
        if (r.device)
            id = r.device->id;
    }
    setNotice(std::move(notice));
    return std::make_unique<WasapiExclusiveOutput>(id);
}

// --- ABX (task 6-6) -----------------------------------------------------------------

void PlayerBridge::abxStartAsync(const juce::var& request, juce::WebBrowserComponent::NativeFunctionCompletion done)
{
    auto fail = [&done](const char* message) {
        auto* o = new juce::DynamicObject();
        o->setProperty("ok", false);
        o->setProperty("message", juce::String::fromUTF8(message));
        done(juce::var(o));
    };
    if ((abx_ && abx_->running) || abxPreparing_)
        return fail("ABX はもう始まっています");
    abx_.reset();
    const auto s = player_.status();
    if (s.state == PlayerState::Empty || s.queueIndex >= queueMeta_.size() || !s.sourceFormat)
        return fail("ABX で聴く曲を、先に再生してください");
    if (s.dop)
        return fail("DSD（DoP）の曲は ABX で比べられません（DoP のデータには処理をかけないため）");
    const auto file = queueMeta_[s.queueIndex];
    const auto kindName = request.getProperty("kind", "processed").toString();
    const std::string kind = kindName == "16bit" ? "16bit" : kindName == "presets" ? "presets" : "processed";
    // 設定 A と設定 B (task 6-9): two EQ presets, each on top of the current
    // settings in 音作りモード (ReplayGain, ISP-safe and the volume as they are).
    ListeningSettings settingsA, settingsB;
    std::string nameA, nameB;
    bool coldA = false, coldB = false;
    if (kind == "presets")
    {
        const auto a = presetFrom(request.getProperty("a", {}));
        const auto b = presetFrom(request.getProperty("b", {}));
        if (!a || !b)
            return fail("比べる 2 つの設定を選んでください");
        const auto sideA = request.getProperty("a", {}), sideB = request.getProperty("b", {});
        for (auto [settings, preset, side] :
             {std::tuple{&settingsA, &*a, &sideA}, std::tuple{&settingsB, &*b, &sideB}})
        {
            *settings = listening_;
            settings->mode = ListeningMode::Processed;
            settings->eqEnabled = true;
            applyEqPreset(*settings, *preset);
            // The tone and the tube (task 9-4), when the side says: otherwise as they are now.
            const auto number = [side](const char* key, double now, double lo, double hi) {
                if (!side->hasProperty(key))
                    return now;
                const double x = side->getProperty(key, now);
                return std::isfinite(x) ? std::clamp(x, lo, hi) : now;
            };
            settings->toneBassDb = number("toneBassDb", settings->toneBassDb, -ToneStage::kMaxDb, ToneStage::kMaxDb);
            settings->toneTrebleDb = number("toneTrebleDb", settings->toneTrebleDb, -ToneStage::kMaxDb, ToneStage::kMaxDb);
            if (side->hasProperty("tubeEnabled"))
                settings->tubeEnabled = static_cast<bool>(side->getProperty("tubeEnabled", false));
            if (side->hasProperty("tubeType"))
                settings->tubeType =
                    tubeTypeFrom(side->getProperty("tubeType", "").toString().toStdString()).value_or(settings->tubeType);
            settings->tubeDriveDb =
                number("tubeDriveDb", settings->tubeDriveDb, TubeStage::kMinDriveDb, TubeStage::kMaxDriveDb);
            // The warm-up and the units (task 9-8). "tubeWarmUpCold": the warm-up
            // held at switch-on for this side.
            if (side->hasProperty("tubeWarmUp"))
                settings->tubeWarmUp = static_cast<bool>(side->getProperty("tubeWarmUp", false));
            if (side->hasProperty("tubeUnits"))
                settings->tubeUnits = static_cast<bool>(side->getProperty("tubeUnits", false));
            settings->tubeUnit = static_cast<int>(
                number("tubeUnit", settings->tubeUnit, kMinTubeUnit, kMaxTubeUnit));
        }
        coldA = settingsA.tubeWarmUp && static_cast<bool>(sideA.getProperty("tubeWarmUpCold", false));
        coldB = settingsB.tubeWarmUp && static_cast<bool>(sideB.getProperty("tubeWarmUpCold", false));
        nameA = a->name;
        nameB = b->name;
    }
    const auto rate = s.sourceFormat->sampleRate;
    const double startSeconds = std::max(0.0, static_cast<double>(request.getProperty("startSeconds", 0.0)));
    const double seconds = std::clamp(static_cast<double>(request.getProperty("seconds", 30.0)), 2.0, 600.0);
    const int trials = std::clamp(static_cast<int>(request.getProperty("trials", AbxTest::kDefaultTrials)), 1, 100);
    auto start = static_cast<std::uint64_t>(startSeconds * rate);
    auto length = static_cast<std::uint64_t>(seconds * rate);
    if (s.totalFrames > 0)
    {
        start = std::min(start, s.totalFrames - 1);
        length = std::min(length, s.totalFrames - start);
    }
    ListeningSettings listening = listening_;
    const double volumeDb = chain_.volume->gainDb();
    const double warmUp = warmUpSeconds();
    abxPreparing_ = true;

    // The loudness match over the region, with the same settings as will play.
    juce::Thread::launch([this, alive = alive_, file, kind, rate, start, length, trials, listening, volumeDb, saved = s,
                          settingsA, settingsB, nameA, nameB, coldA, coldB, warmUp, done = std::move(done)]() mutable {
        double match = 0.0;
        std::string error;
        try
        {
            // A chain like the one that will play: the settings (or, for 16 bit,
            // only the requantization) and the volume.
            ListeningChain offline, reference;
            if (kind == "processed")
            {
                offline.apply(listening);
                offline.tube->setWarmUpSeconds(warmUp);
            }
            else if (kind == "presets")
            {
                offline.apply(settingsB);
                reference.apply(settingsA);
                reference.volume->setGainDb(volumeDb);
                offline.tube->setWarmUpSeconds(coldB ? 0.0 : warmUp);
                reference.tube->setWarmUpSeconds(coldA ? 0.0 : warmUp);
            }
            else
            {
                ListeningSettings plain;
                offline.apply(plain);
                offline.requantize->setBits(16);
            }
            offline.volume->setGainDb(volumeDb);
            auto src = openAudioFile(file.path);
            src->seek(start);
            match = measureMatchDb(*src, *offline.chain(), TrackInfo{0, file.replayGain}, length,
                                   kind == "presets" ? reference.chain().get() : nullptr);
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
        juce::MessageManager::callAsync([this, alive, file, kind, rate, start, length, trials, match, error, saved,
                                         settingsA, settingsB, nameA, nameB, coldA, coldB,
                                         done = std::move(done)]() mutable {
            if (!*alive)
                return;
            abxPreparing_ = false;
            auto* o = new juce::DynamicObject();
            if (!error.empty())
            {
                o->setProperty("ok", false);
                o->setProperty("message", juce::String::fromUTF8("区間を解析できませんでした: ") + juce::String::fromUTF8(error.c_str()));
                done(juce::var(o));
                return;
            }
            AbxSession session{AbxTest(trials)};
            session.kind = kind;
            session.start = start;
            session.length = length;
            session.rate = rate;
            session.matchDb = match;
            session.nameA = nameA;
            session.nameB = nameB;
            session.queue = queueMeta_;
            session.index = saved.queueIndex;
            session.position = saved.positionFrames;

            player_.stop();
            if (kind == "16bit")
            {
                // Only the requantization on the processed side.
                ListeningSettings plain;
                plain.ispSafeOriginal = IspSafeMode::Off;
                chain_.apply(plain);
                chain_.requantize->setBits(16);
            }
            else if (kind == "presets")
            {
                // B plays through the main chain, A through a second one fed the same input.
                chain_.apply(settingsB);
                abxReference_ = std::make_shared<ListeningChain>();
                abxReference_->apply(settingsA);
                abxReference_->volume->setGainDb(chain_.volume->gainDb());
                chain_.chain()->setReference(abxReference_->chain());
            }
            abxHold_ = {coldA, coldB};
            updateWarmUp();
            auto& ab = chain_.chain()->comparison();
            ab.setFixedMatchDb(match);
            ab.select(ComparisonSide::Original); // A first
            chain_.chain()->setComparing(true);

            const auto fade = static_cast<std::uint32_t>(std::lround(rate * LoopingSource::kFadeSeconds));
            const auto path = file.path;
            std::vector<QueueItem> items{QueueItem{
                file.name,
                [path, start, length, fade] {
                    return std::make_unique<PrefetchingSource>(
                        std::make_unique<LoopingSource>(openAudioFile(path), start, length, fade));
                },
                file.replayGain}};
            queueMeta_ = {file};
            ++queueVersion_;
            abx_.emplace(std::move(session));
            player_.setQueue(std::move(items));
            player_.play();
            pushStatus(true);
            o->setProperty("ok", true);
            o->setProperty("matchDb", match);
            done(juce::var(o));
        });
    });
}

void PlayerBridge::abxFinish()
{
    if (!abx_ || !abx_->running)
        return;
    abx_->running = false;
    abxHold_ = {false, false};
    player_.stop();
    auto& ab = chain_.chain()->comparison();
    chain_.chain()->setComparing(false);
    chain_.chain()->setReference(nullptr);
    abxReference_.reset();
    ab.setFixedMatchDb(std::nullopt);
    chain_.requantize->setBits(0);
    chain_.apply(listening_);

    // Put back what was playing, stopped at the same place.
    std::vector<QueueItem> items;
    for (const auto& f : abx_->queue)
        items.push_back(itemFor(f));
    queueMeta_ = abx_->queue;
    ++queueVersion_;
    if (!items.empty())
    {
        player_.setQueue(std::move(items), std::min(abx_->index, abx_->queue.size() - 1));
        player_.seek(abx_->position);
    }
}

juce::var PlayerBridge::abxVar() const
{
    if (!abx_)
        return {};
    const auto& a = *abx_;
    auto* o = new juce::DynamicObject();
    o->setProperty("running", a.running);
    o->setProperty("preparing", false);
    o->setProperty("kind", juce::String(a.kind));
    if (a.kind == "presets")
    {
        o->setProperty("aName", juce::String::fromUTF8(a.nameA.c_str()));
        o->setProperty("bName", juce::String::fromUTF8(a.nameB.c_str()));
    }
    o->setProperty("trial", std::min(a.test.trial(), a.test.trials()));
    o->setProperty("trials", a.test.trials());
    o->setProperty("listening", a.listening == AbxChoice::A ? "A" : a.listening == AbxChoice::B ? "B" : "X");
    o->setProperty("startFrames", static_cast<double>(a.start));
    o->setProperty("lengthFrames", static_cast<double>(a.length));
    o->setProperty("rate", static_cast<int>(a.rate));
    o->setProperty("matchDb", a.matchDb);
    const auto r = a.test.result();
    o->setProperty("answered", r.answered);
    if (!a.running)
    {
        auto* ro = new juce::DynamicObject();
        ro->setProperty("finished", r.finished);
        ro->setProperty("aborted", r.aborted);
        ro->setProperty("trials", r.trials);
        ro->setProperty("correct", r.correct ? juce::var(*r.correct) : juce::var());
        ro->setProperty("pValue", r.pValue ? juce::var(*r.pValue) : juce::var());
        ro->setProperty("distinguished", r.distinguished);
        ro->setProperty("levelDifferenceDb", r.levelDifferenceDb ? juce::var(*r.levelDifferenceDb) : juce::var());
        ro->setProperty("levelMatched", r.levelMatched);
        o->setProperty("result", juce::var(ro));
    }
    else
        o->setProperty("result", juce::var());
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

void PlayerBridge::updateWarmUp()
{
    const double t = warmUpSeconds();
    chain_.tube->setWarmUpSeconds(abxHold_.second ? 0.0 : t);
    if (abxReference_)
        abxReference_->tube->setWarmUpSeconds(abxHold_.first ? 0.0 : t);
}

void PlayerBridge::timerCallback()
{
    updateWarmUp();
    if (abx_ && abx_->running)
    {
        // The loudness difference of what came out, once the comparison has warmed up (task 4-1).
        const auto s = player_.status();
        const auto& ab = chain_.chain()->comparison();
        if (s.path && s.path->comparison && ab.levelDifferenceReady())
            abx_->test.noteLevelDifference(ab.levelDifferenceDb());
    }
    if (onPoll)
    {
        const auto s = player_.status();
        const auto id = s.queueIndex < queueMeta_.size() && !abx_ ? queueMeta_[s.queueIndex].libraryId : 0;
        onPoll(s, id);
    }
    pushStatus(false);
}

} // namespace ap::app
