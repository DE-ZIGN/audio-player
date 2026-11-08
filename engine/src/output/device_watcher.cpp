#include <ap/engine/output/device_watcher.h>

#include <ap/engine/output/audio_output.h>

#include <windows.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <atomic>
#include <format>

namespace ap::engine {

using Microsoft::WRL::ComPtr;

namespace {

std::string toUtf8(LPCWSTR w)
{
    if (!w || !*w)
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// Render-endpoint filter: Added/Removed/StateChanged come for capture devices
// too; the enumerator is asked for the data-flow direction of each ID.
class NotificationClient final : public IMMNotificationClient
{
public:
    NotificationClient(ComPtr<IMMDeviceEnumerator> enumerator, std::function<void(const DeviceEvent&)> callback)
        : enumerator_(std::move(enumerator)), callback_(std::move(callback))
    {
    }

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG r = --refs_;
        if (r == 0)
            delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient))
        {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    // IMMNotificationClient
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id, DWORD) override
    {
        emit(DeviceEventKind::StateChanged, id, true);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR id) override
    {
        emit(DeviceEventKind::Added, id, true);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR id) override
    {
        // A removed endpoint can no longer be asked for its direction.
        emit(DeviceEventKind::Removed, id, false);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR id) override
    {
        if (flow == eRender && role == eConsole)
            emit(DeviceEventKind::DefaultChanged, id, false);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    void emit(DeviceEventKind kind, LPCWSTR id, bool renderOnly)
    {
        if (renderOnly && !isRender(id))
            return;
        try
        {
            callback_(DeviceEvent{kind, toUtf8(id)});
        }
        catch (...)
        {
            // never let an exception cross the COM boundary
        }
    }

    bool isRender(LPCWSTR id)
    {
        if (!id)
            return false;
        ComPtr<IMMDevice> device;
        if (FAILED(enumerator_->GetDevice(id, &device)))
            return true; // cannot tell; report it
        ComPtr<IMMEndpoint> endpoint;
        EDataFlow flow = eAll;
        if (SUCCEEDED(device.As(&endpoint)) && SUCCEEDED(endpoint->GetDataFlow(&flow)))
            return flow == eRender;
        return true;
    }

    std::atomic<ULONG> refs_{1};
    ComPtr<IMMDeviceEnumerator> enumerator_;
    std::function<void(const DeviceEvent&)> callback_;
};

} // namespace

struct DeviceWatcher::Impl
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    NotificationClient* client = nullptr;
};

DeviceWatcher::DeviceWatcher(std::function<void(const DeviceEvent&)> callback) : impl_(std::make_unique<Impl>())
{
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&impl_->enumerator));
    if (FAILED(hr))
        throw OutputException(OutputError::DeviceError,
                              std::format("CoCreateInstance(MMDeviceEnumerator) failed: 0x{:08X}", static_cast<unsigned long>(hr)),
                              static_cast<long>(hr));
    impl_->client = new NotificationClient(impl_->enumerator, std::move(callback));
    hr = impl_->enumerator->RegisterEndpointNotificationCallback(impl_->client);
    if (FAILED(hr))
    {
        impl_->client->Release();
        impl_->client = nullptr;
        throw OutputException(OutputError::DeviceError,
                              std::format("RegisterEndpointNotificationCallback failed: 0x{:08X}", static_cast<unsigned long>(hr)),
                              static_cast<long>(hr));
    }
}

DeviceWatcher::~DeviceWatcher()
{
    if (impl_->client)
    {
        // After this returns no further callbacks are made.
        impl_->enumerator->UnregisterEndpointNotificationCallback(impl_->client);
        impl_->client->Release();
    }
}

const char* toString(DeviceEventKind kind)
{
    switch (kind)
    {
        case DeviceEventKind::Added:          return "added";
        case DeviceEventKind::Removed:        return "removed";
        case DeviceEventKind::StateChanged:   return "stateChanged";
        case DeviceEventKind::DefaultChanged: return "defaultChanged";
    }
    return "stateChanged";
}

} // namespace ap::engine
