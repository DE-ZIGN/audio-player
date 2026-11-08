#include "PlayerBridge.h"

#include <ap/engine/output/wasapi_exclusive_output.h>
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
        case PlayerErrorKind::Other:              return "other";
    }
    return "other";
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

PlayerBridge::PlayerBridge(Player::OutputFactory outputFactory)
    : player_(outputFactory ? std::move(outputFactory)
                            : Player::OutputFactory([] { return std::make_unique<WasapiExclusiveOutput>(); }))
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
    try
    {
        WavFileSource wav(std::filesystem::path(file.getFullPathName().toWideCharPointer()));
        // Read the whole file first so the playback thread never waits for the
        // disk (dropout prevention; proper prefetching comes in task 1-3).
        auto memory = std::make_unique<MemoryPcmSource>(MemoryPcmSource::loadAll(wav));
        player_.load(std::move(memory));
        fileName_ = file.getFileName();
        pushStatus(true);
        return {};
    }
    catch (const WavFormatError& e)
    {
        return juce::String::fromUTF8("このファイルは読めません（WAV のみ対応）: ") + e.what();
    }
    catch (const std::exception& e)
    {
        return juce::String::fromUTF8("ファイルを開けませんでした: ") + e.what();
    }
}

void PlayerBridge::openFileAsync(juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    chooser_ = std::make_unique<juce::FileChooser>(juce::String::fromUTF8("音声ファイルを開く"), juce::File{}, "*.wav");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, completion = std::move(completion)](const juce::FileChooser& fc) {
                              auto* result = new juce::DynamicObject();
                              const auto file = fc.getResult();
                              if (file == juce::File{})
                              {
                                  result->setProperty("ok", false);
                                  result->setProperty("cancelled", true);
                              }
                              else
                              {
                                  const auto message = load(file);
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
    o->setProperty("fileName", fileName_);
    o->setProperty("sourceFormat", s.sourceFormat ? formatVar(*s.sourceFormat) : juce::var());
    o->setProperty("outputFormat", s.plan ? formatVar(s.plan->output) : juce::var());
    o->setProperty("repack", s.plan ? toString(s.plan->repack) : "none");
    o->setProperty("outputName", juce::String::fromUTF8(s.outputName.c_str()));
    o->setProperty("positionFrames", static_cast<juce::int64>(s.positionFrames));
    o->setProperty("totalFrames", static_cast<juce::int64>(s.totalFrames));

    auto* e = new juce::DynamicObject();
    e->setProperty("kind", toString(s.error.kind));
    e->setProperty("reason", toString(s.error.formatCheck.reason));
    e->setProperty("outputError", toString(s.error.outputError));
    e->setProperty("detail", juce::String::fromUTF8(s.error.detail.c_str()));
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
