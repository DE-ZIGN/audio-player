#include <ap/engine/output/wasapi_exclusive_output.h>

#include <ap/engine/output/format_probe.h>

#include <windows.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <avrt.h>
#include <wrl/client.h>
#include <initguid.h>
#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>

namespace ap::engine {

using Microsoft::WRL::ComPtr;

namespace {

// KSDATAFORMAT_SUBTYPE_PCM / KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
constexpr GUID kSubtypePcm = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
constexpr GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

// PKEY_AudioEngine_DeviceFormat (mmdeviceapi.h), defined here to avoid GUID-definition order issues.
constexpr PROPERTYKEY kPkeyDeviceFormat = {
    {0xF19F064D, 0x082C, 0x4E27, {0xBC, 0x73, 0x68, 0x82, 0xA1, 0xBB, 0x8E, 0x4C}}, 0};

constexpr DWORD kSpeakerFrontLeft = 0x1;
constexpr DWORD kSpeakerFrontRight = 0x2;
constexpr DWORD kSpeakerFrontCenter = 0x4;

std::string toUtf8(const wchar_t* w)
{
    if (!w || !*w)
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring toWide(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string hrText(HRESULT hr)
{
    return std::format("HRESULT 0x{:08X}", static_cast<unsigned long>(hr));
}

OutputError errorFor(HRESULT hr)
{
    switch (hr)
    {
        case AUDCLNT_E_DEVICE_IN_USE:              return OutputError::DeviceInUse;
        case AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED: return OutputError::ExclusiveModeNotAllowed;
        case AUDCLNT_E_UNSUPPORTED_FORMAT:         return OutputError::FormatNotSupported;
        case AUDCLNT_E_DEVICE_INVALIDATED:         return OutputError::DeviceInvalidated;
        case E_NOTFOUND:                           return OutputError::DeviceNotFound;
        default:                                   return OutputError::DeviceError;
    }
}

[[noreturn]] void throwHr(HRESULT hr, const char* what)
{
    throw OutputException(errorFor(hr), std::string(what) + " failed: " + hrText(hr), static_cast<long>(hr));
}

void check(HRESULT hr, const char* what)
{
    if (FAILED(hr))
        throwHr(hr, what);
}

// Device-level failures that say nothing about the format.
bool isDeviceLevelError(HRESULT hr)
{
    return hr == AUDCLNT_E_DEVICE_IN_USE || hr == AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED
        || hr == AUDCLNT_E_DEVICE_INVALIDATED;
}

WAVEFORMATEXTENSIBLE toWaveFormat(const PcmFormat& f)
{
    WAVEFORMATEXTENSIBLE w{};
    w.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    w.Format.nChannels = f.channels;
    w.Format.nSamplesPerSec = f.sampleRate;
    w.Format.wBitsPerSample = f.containerBits;
    w.Format.nBlockAlign = static_cast<WORD>(f.bytesPerFrame());
    w.Format.nAvgBytesPerSec = f.sampleRate * f.bytesPerFrame();
    w.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    w.Samples.wValidBitsPerSample = f.validBits;
    if (f.channelMask != 0)
        w.dwChannelMask = f.channelMask;
    else if (f.channels == 1)
        w.dwChannelMask = kSpeakerFrontCenter;
    else if (f.channels == 2)
        w.dwChannelMask = kSpeakerFrontLeft | kSpeakerFrontRight;
    w.SubFormat = f.type == SampleType::Int ? kSubtypePcm : kSubtypeFloat;
    return w;
}

// Restores the thread's MMCSS registration on scope exit.
class MmcssScope
{
public:
    MmcssScope()
    {
        DWORD taskIndex = 0;
        handle_ = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    }
    ~MmcssScope()
    {
        if (handle_)
            AvRevertMmThreadCharacteristics(handle_);
    }
    MmcssScope(const MmcssScope&) = delete;
    MmcssScope& operator=(const MmcssScope&) = delete;

private:
    HANDLE handle_ = nullptr;
};

ComPtr<IMMDeviceEnumerator> makeEnumerator()
{
    ComPtr<IMMDeviceEnumerator> e;
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&e)),
          "CoCreateInstance(MMDeviceEnumerator)");
    return e;
}

std::string deviceId(IMMDevice* device)
{
    LPWSTR id = nullptr;
    if (FAILED(device->GetId(&id)))
        return {};
    std::string s = toUtf8(id);
    CoTaskMemFree(id);
    return s;
}

std::string friendlyName(IMMDevice* device)
{
    ComPtr<IPropertyStore> props;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &props)))
        return {};
    PROPVARIANT v;
    PropVariantInit(&v);
    std::string name;
    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
        name = toUtf8(v.pwszVal);
    PropVariantClear(&v);
    return name;
}

} // namespace

// ---------------------------------------------------------------------------

ScopedComInit::ScopedComInit()
{
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    initialized_ = SUCCEEDED(hr); // RPC_E_CHANGED_MODE: already initialized as STA; leave it alone
}

ScopedComInit::~ScopedComInit()
{
    if (initialized_)
        CoUninitialize();
}

// ---------------------------------------------------------------------------

struct WasapiExclusiveOutput::Impl
{
    ComPtr<IMMDevice> device;
    std::string name;

    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> renderClient;
    HANDLE event = nullptr;
    PcmFormat format;
    UINT32 bufferFrames = 0;
    REFERENCE_TIME periodHns = 0;
    bool opened = false;
    std::atomic<bool> stop{false};

    ComPtr<IAudioClient> activateClient() const
    {
        ComPtr<IAudioClient> c;
        check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &c), "IMMDevice::Activate(IAudioClient)");
        return c;
    }

    HRESULT isSupported(IAudioClient* c, const PcmFormat& f) const
    {
        auto wfx = toWaveFormat(f);
        return c->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &wfx.Format, nullptr);
    }

    void release() noexcept
    {
        if (client && opened)
            client->Stop();
        renderClient.Reset();
        client.Reset();
        if (event)
        {
            CloseHandle(event);
            event = nullptr;
        }
        opened = false;
    }
};

std::vector<OutputDeviceInfo> WasapiExclusiveOutput::listDevices()
{
    auto enumerator = makeEnumerator();

    std::string defaultId;
    ComPtr<IMMDevice> def;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &def)))
        defaultId = deviceId(def.Get());

    ComPtr<IMMDeviceCollection> collection;
    check(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection), "EnumAudioEndpoints");
    UINT count = 0;
    check(collection->GetCount(&count), "IMMDeviceCollection::GetCount");

    std::vector<OutputDeviceInfo> out;
    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> d;
        if (FAILED(collection->Item(i, &d)))
            continue;
        OutputDeviceInfo info;
        info.id = deviceId(d.Get());
        info.name = friendlyName(d.Get());
        info.isDefault = !defaultId.empty() && info.id == defaultId;
        out.push_back(std::move(info));
    }
    return out;
}

WasapiExclusiveOutput::WasapiExclusiveOutput(const std::string& id) : impl_(std::make_unique<Impl>())
{
    auto enumerator = makeEnumerator();
    if (id.empty())
        check(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &impl_->device), "GetDefaultAudioEndpoint");
    else
        check(enumerator->GetDevice(toWide(id).c_str(), &impl_->device), "IMMDeviceEnumerator::GetDevice");
    impl_->name = friendlyName(impl_->device.Get());
}

WasapiExclusiveOutput::~WasapiExclusiveOutput()
{
    impl_->release();
}

std::string WasapiExclusiveOutput::deviceName() const
{
    return impl_->name;
}

FormatCheck WasapiExclusiveOutput::checkFormat(const PcmFormat& format)
{
    if (!format.isValid())
        return FormatCheck::rejected(FormatRejection::Other);

    auto c = impl_->activateClient();
    const HRESULT hr = impl_->isSupported(c.Get(), format);
    if (hr == S_OK)
        return FormatCheck::ok();
    if (isDeviceLevelError(hr))
        throwHr(hr, "IAudioClient::IsFormatSupported");

    // Work out the reason and the rates that do work by probing nearby formats.
    auto check = checkFormatWith(format, [&](const PcmFormat& f) {
        const HRESULT probe = impl_->isSupported(c.Get(), f);
        if (isDeviceLevelError(probe))
            throwHr(probe, "IAudioClient::IsFormatSupported");
        return probe == S_OK;
    });
    check.nativeError = static_cast<long>(hr);
    return check;
}

void WasapiExclusiveOutput::open(const PcmFormat& format)
{
    impl_->release();

    auto c = impl_->activateClient();
    REFERENCE_TIME defaultPeriod = 0;
    REFERENCE_TIME minimumPeriod = 0;
    check(c->GetDevicePeriod(&defaultPeriod, &minimumPeriod), "IAudioClient::GetDevicePeriod");

    // Default period: favours stability over latency.
    REFERENCE_TIME period = defaultPeriod;
    auto wfx = toWaveFormat(format);
    HRESULT hr = c->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period,
                               &wfx.Format, nullptr);
    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
    {
        // Documented recovery: align the period to the buffer size the device wants.
        UINT32 frames = 0;
        check(c->GetBufferSize(&frames), "IAudioClient::GetBufferSize");
        period = static_cast<REFERENCE_TIME>(10'000'000.0 * frames / format.sampleRate + 0.5);
        c = impl_->activateClient();
        hr = c->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period,
                           &wfx.Format, nullptr);
    }
    check(hr, "IAudioClient::Initialize (exclusive, event)");

    impl_->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!impl_->event)
        throw OutputException(OutputError::DeviceError, "CreateEvent failed");
    impl_->client = c;
    check(c->SetEventHandle(impl_->event), "IAudioClient::SetEventHandle");
    check(c->GetBufferSize(&impl_->bufferFrames), "IAudioClient::GetBufferSize");
    check(c->GetService(IID_PPV_ARGS(&impl_->renderClient)), "IAudioClient::GetService(IAudioRenderClient)");

    impl_->format = format;
    impl_->periodHns = period;
    impl_->stop = false;
    impl_->opened = true;
}

RunStats WasapiExclusiveOutput::run(const RenderCallback& render)
{
    if (!impl_->opened)
        throw OutputException(OutputError::DeviceError, "WASAPI output is not open");

    MmcssScope mmcss;
    auto& d = *impl_;
    const std::size_t bpf = d.format.bytesPerFrame();

    RunStats stats;
    stats.periodFrames = d.bufferFrames;
    stats.periodMilliseconds = 1000.0 * d.bufferFrames / d.format.sampleRate;

    bool ended = false;
    auto fill = [&]() {
        BYTE* data = nullptr;
        check(d.renderClient->GetBuffer(d.bufferFrames, &data), "IAudioRenderClient::GetBuffer");
        std::size_t frames = 0;
        if (!ended)
            frames = render(std::span(reinterpret_cast<std::byte*>(data), d.bufferFrames * bpf));
        stats.framesRendered += frames;
        if (frames < d.bufferFrames)
        {
            std::memset(data + frames * bpf, 0, (d.bufferFrames - frames) * bpf);
            ended = true;
        }
        check(d.renderClient->ReleaseBuffer(d.bufferFrames, frames == 0 ? AUDCLNT_BUFFERFLAGS_SILENT : 0),
              "IAudioRenderClient::ReleaseBuffer");
    };

    // Exclusive event mode: the first buffer must be filled before Start().
    fill();
    check(d.client->Start(), "IAudioClient::Start");

    const DWORD timeoutMs = std::max<DWORD>(2000, static_cast<DWORD>(stats.periodMilliseconds * 8));
    int consecutiveTimeouts = 0;
    int drainPeriods = 0;
    try
    {
        for (;;)
        {
            const DWORD w = WaitForSingleObject(d.event, timeoutMs);
            if (w == WAIT_TIMEOUT)
            {
                ++stats.lateWakeups;
                if (++consecutiveTimeouts >= 3)
                    throw OutputException(OutputError::Timeout, "the device stopped requesting data");
                continue;
            }
            if (w != WAIT_OBJECT_0)
                throw OutputException(OutputError::DeviceError, "WaitForSingleObject failed");
            consecutiveTimeouts = 0;

            if (d.stop)
            {
                stats.stoppedByRequest = true;
                break;
            }
            // After the last data buffer: one period while it plays, one more until it has played.
            if (ended && ++drainPeriods >= 2)
                break;
            fill();
        }
    }
    catch (...)
    {
        d.client->Stop();
        throw;
    }
    d.client->Stop();
    return stats;
}

void WasapiExclusiveOutput::requestStop() noexcept
{
    impl_->stop = true;
    if (impl_->event)
        SetEvent(impl_->event);
}

void WasapiExclusiveOutput::close() noexcept
{
    impl_->release();
}

EndpointVolumeInfo WasapiExclusiveOutput::endpointVolume() const
{
    EndpointVolumeInfo info;
    ComPtr<IAudioEndpointVolume> vol;
    if (FAILED(impl_->device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &vol)))
        return info;

    float scalar = 1.0f;
    BOOL muted = FALSE;
    DWORD hw = 0;
    if (FAILED(vol->GetMasterVolumeLevelScalar(&scalar)) || FAILED(vol->GetMute(&muted)))
        return info;
    vol->QueryHardwareSupport(&hw);

    info.available = true;
    info.scalar = scalar;
    info.muted = muted != FALSE;
    info.hardwareVolume = (hw & ENDPOINT_HARDWARE_SUPPORT_VOLUME) != 0;
    return info;
}

std::optional<PcmFormat> WasapiExclusiveOutput::windowsDefaultFormat() const
{
    ComPtr<IPropertyStore> props;
    if (FAILED(impl_->device->OpenPropertyStore(STGM_READ, &props)))
        return std::nullopt;
    PROPVARIANT v;
    PropVariantInit(&v);
    std::optional<PcmFormat> result;
    if (SUCCEEDED(props->GetValue(kPkeyDeviceFormat, &v)) && v.vt == VT_BLOB
        && v.blob.cbSize >= sizeof(WAVEFORMATEX))
    {
        const auto* wfx = reinterpret_cast<const WAVEFORMATEX*>(v.blob.pBlobData);
        PcmFormat f;
        f.sampleRate = wfx->nSamplesPerSec;
        f.channels = wfx->nChannels;
        f.containerBits = wfx->wBitsPerSample;
        f.validBits = wfx->wBitsPerSample;
        f.type = wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ? SampleType::Float : SampleType::Int;
        if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && v.blob.cbSize >= sizeof(WAVEFORMATEXTENSIBLE))
        {
            const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
            f.validBits = ext->Samples.wValidBitsPerSample;
            f.channelMask = ext->dwChannelMask;
            f.type = ext->SubFormat == kSubtypeFloat ? SampleType::Float : SampleType::Int;
        }
        result = f;
    }
    PropVariantClear(&v);
    return result;
}

} // namespace ap::engine
