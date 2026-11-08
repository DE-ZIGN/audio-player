#include <ap/engine/output/asio_output.h>

#include <ap/engine/output/asio_sample_types.h>
#include <ap/engine/output/format_probe.h>

#include <windows.h>
#include <objbase.h>

#include <iasiodrv.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <format>
#include <future>
#include <mutex>
#include <thread>

namespace ap::engine {

namespace {

std::string toUtf8(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring toWide(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring readString(HKEY root, const std::wstring& subKey, const wchar_t* value)
{
    DWORD bytes = 0;
    const DWORD flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ;
    if (RegGetValueW(root, subKey.c_str(), value, flags, nullptr, nullptr, &bytes) != ERROR_SUCCESS || bytes == 0)
        return {};
    std::wstring s(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, subKey.c_str(), value, flags, nullptr, s.data(), &bytes) != ERROR_SUCCESS)
        return {};
    s.resize(wcsnlen(s.c_str(), s.size()));
    return s;
}

std::string asioErrorText(ASIOError e)
{
    switch (e)
    {
        case ASE_NotPresent:       return "ASE_NotPresent";
        case ASE_HWMalfunction:    return "ASE_HWMalfunction";
        case ASE_InvalidParameter: return "ASE_InvalidParameter";
        case ASE_InvalidMode:      return "ASE_InvalidMode";
        case ASE_SPNotAdvancing:   return "ASE_SPNotAdvancing";
        case ASE_NoClock:          return "ASE_NoClock";
        case ASE_NoMemory:         return "ASE_NoMemory";
        default:                   return std::format("ASIOError {}", e);
    }
}

[[noreturn]] void throwAsio(ASIOError e, const char* what)
{
    const OutputError code = e == ASE_NotPresent ? OutputError::DeviceNotFound : OutputError::DeviceError;
    throw OutputException(code, std::string(what) + " failed: " + asioErrorText(e), e);
}

// A thread in a single-threaded apartment with a message pump, where every
// call into the driver is made.
class StaWorker
{
public:
    StaWorker()
    {
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        std::promise<void> ready;
        auto started = ready.get_future();
        thread_ = std::thread([this, &ready] {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            // Some drivers want a window handle in init() (parent for their dialogs).
            window_ = CreateWindowExW(0, L"STATIC", L"ASIO host", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                      GetModuleHandleW(nullptr), nullptr);
            ready.set_value();
            loop();
            if (window_)
                DestroyWindow(window_);
            CoUninitialize();
        });
        started.wait();
    }

    ~StaWorker()
    {
        {
            std::lock_guard lock(mutex_);
            quit_ = true;
        }
        SetEvent(event_);
        thread_.join();
        CloseHandle(event_);
    }

    HWND window() const noexcept { return window_; }

    template <typename F>
    auto invoke(F&& f) -> decltype(f())
    {
        using R = decltype(f());
        if (std::this_thread::get_id() == thread_.get_id())
            return f();
        std::packaged_task<R()> task(std::forward<F>(f));
        auto result = task.get_future();
        {
            std::lock_guard lock(mutex_);
            tasks_.emplace_back([&task] { task(); });
        }
        SetEvent(event_);
        return result.get();
    }

private:
    void loop()
    {
        for (;;)
        {
            MsgWaitForMultipleObjects(1, &event_, FALSE, INFINITE, QS_ALLINPUT);
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            std::deque<std::function<void()>> todo;
            {
                std::lock_guard lock(mutex_);
                todo.swap(tasks_);
                if (quit_ && todo.empty())
                    return;
            }
            for (auto& t : todo)
                t();
        }
    }

    std::thread thread_;
    HANDLE event_ = nullptr;
    HWND window_ = nullptr;
    std::mutex mutex_;
    std::deque<std::function<void()>> tasks_;
    bool quit_ = false;
};

} // namespace

// ---------------------------------------------------------------------------

struct AsioOutput::Impl
{
    std::string displayName;
    StaWorker worker;
    IASIO* driver = nullptr;
    AsioDriverDetails details;

    // Session (valid between open() and close()).
    PcmFormat format;
    bool opened = false;
    bool buffersCreated = false;
    bool started = false;
    std::vector<ASIOBufferInfo> infos;
    std::vector<void*> channelPtrs[2];
    long bufferSize = 0;
    long sampleType = 0;
    bool useOutputReady = false;
    std::vector<std::byte> scratch;
    double periodSeconds = 0.0;
    double rateSet = 0.0;
    ASIOCallbacks callbacks{};

    // Run state (bufferSwitch runs on the driver's thread).
    const RenderCallback* render = nullptr;
    HANDLE done = nullptr;
    std::atomic<bool> stop{false};
    std::atomic<bool> ended{false};
    std::atomic<bool> resetRequested{false};
    std::atomic<int> drainSwitches{0};
    std::atomic<std::uint64_t> switches{0};
    std::atomic<std::uint64_t> framesRendered{0};

    std::atomic<std::uint32_t> lateBuffers{0};
    std::atomic<std::uint32_t> timeouts{0};
    std::atomic<bool> realtime{false};

    void onBufferSwitch(long index);
    long onMessage(long selector, long value);
    void onSampleRateChanged(double rate);
    void closeOnWorker() noexcept;
};

namespace {

std::atomic<AsioOutput::Impl*> g_active{nullptr};

void cbBufferSwitch(long index, ASIOBool)
{
    if (auto* p = g_active.load(std::memory_order_acquire))
        p->onBufferSwitch(index);
}

ASIOTime* cbBufferSwitchTimeInfo(ASIOTime* params, long index, ASIOBool direct)
{
    cbBufferSwitch(index, direct);
    return params;
}

void cbSampleRateDidChange(ASIOSampleRate rate)
{
    if (auto* p = g_active.load(std::memory_order_acquire))
        p->onSampleRateChanged(rate);
}

long cbAsioMessage(long selector, long value, void*, double*)
{
    if (auto* p = g_active.load(std::memory_order_acquire))
        return p->onMessage(selector, value);
    return 0;
}

} // namespace

void AsioOutput::Impl::onBufferSwitch(long index)
{
    const auto t0 = std::chrono::steady_clock::now();
    if (!realtime.load(std::memory_order_relaxed) && GetThreadPriority(GetCurrentThread()) >= THREAD_PRIORITY_HIGHEST)
        realtime = true;

    const std::size_t bpf = format.bytesPerFrame();
    const std::size_t n = static_cast<std::size_t>(bufferSize);
    const bool wasEnded = ended.load(std::memory_order_relaxed);

    if (!wasEnded && !stop.load(std::memory_order_relaxed) && render)
    {
        const std::size_t frames = (*render)(std::span(scratch).first(n * bpf));
        framesRendered += frames;
        if (frames < n)
        {
            std::memset(scratch.data() + frames * bpf, 0, (n - frames) * bpf);
            ended = true;
        }
    }
    else
    {
        std::memset(scratch.data(), 0, n * bpf);
    }

    asio::deinterleave(sampleType, format, scratch, n, channelPtrs[index & 1]);
    if (useOutputReady)
        driver->outputReady();
    ++switches;

    // After the last data buffer: one switch while it plays, one more until it has played.
    if (wasEnded && ++drainSwitches >= 2)
        SetEvent(done);

    if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > periodSeconds)
        ++lateBuffers;
}

long AsioOutput::Impl::onMessage(long selector, long value)
{
    switch (selector)
    {
        case kAsioSelectorSupported:
            return (value == kAsioEngineVersion || value == kAsioResetRequest || value == kAsioResyncRequest
                    || value == kAsioLatenciesChanged || value == kAsioOverload || value == kAsioSupportsTimeInfo)
                     ? 1
                     : 0;
        case kAsioEngineVersion:
            return 2;
        case kAsioResetRequest:
            // Settings changed in the driver (rate, buffer size, ...): the session must end.
            resetRequested = true;
            SetEvent(done);
            return 1;
        case kAsioResyncRequest:
            ++lateBuffers; // the driver lost sync; audible as a dropout
            return 1;
        case kAsioOverload:
            ++lateBuffers;
            return 1;
        case kAsioLatenciesChanged:
            return 1;
        case kAsioSupportsTimeInfo:
            return 0; // use plain bufferSwitch
        default:
            return 0;
    }
}

void AsioOutput::Impl::onSampleRateChanged(double rate)
{
    // The rate changed behind our back (e.g. in the driver's panel): stop rather than play at the wrong rate.
    if (opened && rate != static_cast<double>(format.sampleRate))
    {
        resetRequested = true;
        SetEvent(done);
    }
}

void AsioOutput::Impl::closeOnWorker() noexcept
{
    if (started)
    {
        driver->stop();
        started = false;
    }
    if (buffersCreated)
    {
        driver->disposeBuffers();
        buffersCreated = false;
    }
    Impl* self = this;
    g_active.compare_exchange_strong(self, nullptr);
    opened = false;
}

// ---------------------------------------------------------------------------

std::vector<AsioDriverInfo> listAsioDrivers()
{
    std::vector<AsioDriverInfo> out;
    HKEY asioKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_READ | KEY_WOW64_64KEY, &asioKey) != ERROR_SUCCESS)
        return out;
    for (DWORD i = 0;; ++i)
    {
        wchar_t name[256];
        DWORD nameLength = 256;
        if (RegEnumKeyExW(asioKey, i, name, &nameLength, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        AsioDriverInfo d;
        d.name = toUtf8(name);
        const std::wstring clsid = readString(asioKey, name, L"CLSID");
        d.clsid = toUtf8(clsid);
        d.description = toUtf8(readString(asioKey, name, L"Description"));
        if (!clsid.empty())
            d.dllPath = toUtf8(readString(HKEY_CLASSES_ROOT, L"CLSID\\" + clsid + L"\\InprocServer32", nullptr));
        out.push_back(std::move(d));
    }
    RegCloseKey(asioKey);
    return out;
}

AsioOutput::DriverFactory AsioOutput::factoryFor(const AsioDriverInfo& driver)
{
    return [clsidText = driver.clsid, name = driver.name]() -> IASIO* {
        CLSID clsid{};
        if (FAILED(CLSIDFromString(toWide(clsidText).c_str(), &clsid)))
            throw OutputException(OutputError::DeviceNotFound, "invalid ASIO driver CLSID for " + name);
        IASIO* p = nullptr;
        // The ASIO convention passes the CLSID as the interface ID as well.
        const HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, reinterpret_cast<void**>(&p));
        if (FAILED(hr) || !p)
            throw OutputException(OutputError::DeviceNotFound,
                                  std::format("cannot load ASIO driver {} (0x{:08X})", name, static_cast<unsigned long>(hr)),
                                  static_cast<long>(hr));
        return p;
    };
}

AsioOutput::AsioOutput(std::string displayName, DriverFactory factory) : impl_(std::make_unique<Impl>())
{
    impl_->displayName = std::move(displayName);
    impl_->done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    impl_->callbacks.bufferSwitch = &cbBufferSwitch;
    impl_->callbacks.sampleRateDidChange = &cbSampleRateDidChange;
    impl_->callbacks.asioMessage = &cbAsioMessage;
    impl_->callbacks.bufferSwitchTimeInfo = &cbBufferSwitchTimeInfo;

    impl_->worker.invoke([&] {
        auto& d = *impl_;
        d.driver = factory();
        if (!d.driver->init(d.worker.window()))
        {
            char message[128] = {};
            d.driver->getErrorMessage(message);
            d.driver->Release();
            d.driver = nullptr;
            throw OutputException(OutputError::DeviceError, std::string("ASIO driver init failed: ") + message);
        }
        char name[64] = {};
        d.driver->getDriverName(name);
        d.details.driverName = name;
        d.details.driverVersion = d.driver->getDriverVersion();
        d.driver->getChannels(&d.details.inputChannels, &d.details.outputChannels);
        d.driver->getBufferSize(&d.details.minBufferSize, &d.details.maxBufferSize, &d.details.preferredBufferSize,
                                &d.details.granularity);
        ASIOSampleRate rate = 0;
        if (d.driver->getSampleRate(&rate) == ASE_OK)
            d.details.currentSampleRate = rate;
        ASIOChannelInfo info{};
        info.channel = 0;
        info.isInput = ASIOFalse;
        if (d.details.outputChannels > 0 && d.driver->getChannelInfo(&info) == ASE_OK)
            d.details.sampleType = info.type;
    });
}

AsioOutput::~AsioOutput()
{
    close();
    impl_->worker.invoke([this] {
        if (impl_->driver)
        {
            impl_->driver->Release();
            impl_->driver = nullptr;
        }
    });
    CloseHandle(impl_->done);
}

std::string AsioOutput::deviceName() const
{
    return impl_->displayName;
}

AsioDriverDetails AsioOutput::details() const
{
    return impl_->details;
}

std::vector<std::uint32_t> AsioOutput::supportedRates()
{
    return impl_->worker.invoke([this] {
        std::vector<std::uint32_t> out;
        for (const auto rate : standardSampleRates())
            if (impl_->driver->canSampleRate(static_cast<ASIOSampleRate>(rate)) == ASE_OK)
                out.push_back(rate);
        return out;
    });
}

double AsioOutput::rateSetByOpen() const noexcept
{
    return impl_->rateSet;
}

FormatCheck AsioOutput::checkFormat(const PcmFormat& format)
{
    return impl_->worker.invoke([&] {
        auto& d = *impl_;
        return checkFormatWith(format, [&d](const PcmFormat& f) {
            return f.channels <= d.details.outputChannels && asio::accepts(d.details.sampleType, f)
                && d.driver->canSampleRate(static_cast<ASIOSampleRate>(f.sampleRate)) == ASE_OK;
        });
    });
}

void AsioOutput::open(const PcmFormat& format)
{
    close();
    const auto check = checkFormat(format);
    if (!check.supported)
        throw OutputException(OutputError::FormatNotSupported, "ASIO driver rejects " + describe(format));

    impl_->worker.invoke([&] {
        auto& d = *impl_;
        Impl* expected = nullptr;
        if (!g_active.compare_exchange_strong(expected, &d))
            throw OutputException(OutputError::DeviceInUse, "another ASIO output is active in this process");

        try
        {
            ASIOSampleRate current = 0;
            d.driver->getSampleRate(&current);
            d.rateSet = 0;
            if (current != static_cast<ASIOSampleRate>(format.sampleRate))
            {
                const ASIOError e = d.driver->setSampleRate(static_cast<ASIOSampleRate>(format.sampleRate));
                if (e != ASE_OK)
                    throwAsio(e, "ASIOSetSampleRate");
                d.rateSet = format.sampleRate;
                d.details.currentSampleRate = format.sampleRate;
            }

            long mn = 0, mx = 0, pref = 0, gran = 0;
            const ASIOError be = d.driver->getBufferSize(&mn, &mx, &pref, &gran);
            if (be != ASE_OK)
                throwAsio(be, "ASIOGetBufferSize");
            d.bufferSize = pref;

            d.infos.assign(format.channels, ASIOBufferInfo{});
            for (long c = 0; c < static_cast<long>(format.channels); ++c)
            {
                d.infos[static_cast<std::size_t>(c)].isInput = ASIOFalse;
                d.infos[static_cast<std::size_t>(c)].channelNum = c;
            }
            const ASIOError ce = d.driver->createBuffers(d.infos.data(), static_cast<long>(format.channels), pref, &d.callbacks);
            if (ce != ASE_OK)
                throwAsio(ce, "ASIOCreateBuffers");
            d.buffersCreated = true;

            ASIOChannelInfo info{};
            info.channel = 0;
            info.isInput = ASIOFalse;
            if (d.driver->getChannelInfo(&info) == ASE_OK)
                d.sampleType = info.type;
            else
                d.sampleType = d.details.sampleType;
            if (!asio::accepts(d.sampleType, format))
                throw OutputException(OutputError::FormatNotSupported,
                                      "ASIO channel type " + asio::sampleTypeName(d.sampleType) + " does not take "
                                          + describe(format));

            const std::size_t bps = asio::bytesPerSample(d.sampleType);
            for (int half = 0; half < 2; ++half)
            {
                d.channelPtrs[half].clear();
                for (auto& bi : d.infos)
                {
                    std::memset(bi.buffers[half], 0, static_cast<std::size_t>(pref) * bps);
                    d.channelPtrs[half].push_back(bi.buffers[half]);
                }
            }
            d.useOutputReady = d.driver->outputReady() == ASE_OK;
        }
        catch (...)
        {
            d.closeOnWorker();
            throw;
        }

        d.format = format;
        d.scratch.assign(static_cast<std::size_t>(d.bufferSize) * format.bytesPerFrame(), std::byte{0});
        d.periodSeconds = static_cast<double>(d.bufferSize) / format.sampleRate;
        d.stop = false;
        d.opened = true;
    });
}

RunStats AsioOutput::run(const RenderCallback& render)
{
    auto& d = *impl_;
    if (!d.opened)
        throw OutputException(OutputError::DeviceError, "ASIO output is not open");

    d.render = &render;
    d.ended = false;
    d.resetRequested = false;
    d.drainSwitches = 0;
    d.framesRendered = 0;
    ResetEvent(d.done);
    if (d.stop)
        SetEvent(d.done);

    d.worker.invoke([&d] {
        const ASIOError e = d.driver->start();
        if (e != ASE_OK)
            throwAsio(e, "ASIOStart");
        d.started = true;
    });

    // Wait for the end of data, a stop request or a driver reset; watch that the driver keeps calling.
    int stalls = 0;
    std::uint64_t lastSwitches = 0;
    for (;;)
    {
        if (WaitForSingleObject(d.done, 2000) == WAIT_OBJECT_0)
            break;
        const auto now = d.switches.load();
        if (now == lastSwitches)
        {
            ++d.timeouts;
            if (++stalls >= 3)
            {
                d.worker.invoke([&d] {
                    d.driver->stop();
                    d.started = false;
                });
                d.render = nullptr;
                throw OutputException(OutputError::Timeout, "the ASIO driver stopped calling back");
            }
        }
        else
        {
            stalls = 0;
        }
        lastSwitches = now;
    }

    d.worker.invoke([&d] {
        d.driver->stop();
        d.started = false;
    });
    d.render = nullptr;

    if (d.resetRequested)
        throw OutputException(OutputError::DeviceInvalidated,
                              "the ASIO driver asked for a reset (its settings or sample rate changed)");

    RunStats stats;
    stats.framesRendered = d.framesRendered;
    stats.periodFrames = static_cast<std::uint32_t>(d.bufferSize);
    stats.periodMilliseconds = d.periodSeconds * 1000.0;
    stats.stoppedByRequest = d.stop;
    return stats;
}

void AsioOutput::requestStop() noexcept
{
    impl_->stop = true;
    if (impl_->done)
        SetEvent(impl_->done);
}

void AsioOutput::close() noexcept
{
    try
    {
        impl_->worker.invoke([this] { impl_->closeOnWorker(); });
    }
    catch (...)
    {
    }
}

OutputHealth AsioOutput::health() const noexcept
{
    return {impl_->lateBuffers.load(), impl_->timeouts.load(), impl_->realtime.load()};
}

void AsioOutput::showControlPanel()
{
    impl_->worker.invoke([this] { impl_->driver->controlPanel(); });
}

} // namespace ap::engine
