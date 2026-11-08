#pragma once

// A fake ASIO driver for tests. Implements IASIO, calls the host's
// bufferSwitch from its own thread like a real driver, and records what the
// host wrote into each output channel buffer.

#include <windows.h>
#include <objbase.h>

#include <iasiodrv.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace ap::test {

class FakeAsioDriver final : public IASIO
{
public:
    struct Config
    {
        long outputs = 2;
        long sampleType = 18; // ASIOSTInt32LSB
        std::vector<double> rates{44100, 48000, 88200, 96000};
        double initialRate = 48000;
        long bufferSize = 480;
        bool realtime = false; // sleep one buffer period between switches
    };

    explicit FakeAsioDriver(Config c) : cfg_(std::move(c)), rate_(cfg_.initialRate) {}

    // --- inspection (any thread) -----------------------------------------
    std::vector<std::vector<std::byte>> captured() const
    {
        std::lock_guard lock(mutex_);
        return captured_;
    }
    std::vector<double> rateHistory() const
    {
        std::lock_guard lock(mutex_);
        return rateHistory_;
    }
    int createBuffersCalls() const { return createBuffersCalls_; }
    bool running() const { return running_; }
    // Makes the driver send kAsioResetRequest / kAsioOverload at the next buffer switch.
    void requestReset() { pendingMessage_ = kAsioResetRequest; }
    void reportOverload() { pendingMessage_ = kAsioOverload; }

    // --- IUnknown ------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** out) override
    {
        *out = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG r = --refs_;
        if (r == 0)
            delete this;
        return r;
    }

    // --- IASIO -----------------------------------------------------------------
    ASIOBool init(void*) override { return ASIOTrue; }
    void getDriverName(char* name) override { strcpy_s(name, 32, "Fake ASIO"); }
    long getDriverVersion() override { return 1; }
    void getErrorMessage(char* s) override { strcpy_s(s, 124, "fake error"); }

    ASIOError start() override
    {
        if (!buffersCreated_)
            return ASE_InvalidMode;
        running_ = true;
        thread_ = std::thread([this] { loop(); });
        return ASE_OK;
    }
    ASIOError stop() override
    {
        running_ = false;
        if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id())
            thread_.join();
        return ASE_OK;
    }
    ASIOError getChannels(long* in, long* out) override
    {
        *in = 0;
        *out = cfg_.outputs;
        return ASE_OK;
    }
    ASIOError getLatencies(long* in, long* out) override
    {
        *in = *out = cfg_.bufferSize;
        return ASE_OK;
    }
    ASIOError getBufferSize(long* mn, long* mx, long* pref, long* gran) override
    {
        *mn = *mx = *pref = cfg_.bufferSize;
        *gran = 0;
        return ASE_OK;
    }
    ASIOError canSampleRate(ASIOSampleRate r) override
    {
        for (double x : cfg_.rates)
            if (x == r)
                return ASE_OK;
        return ASE_NoClock;
    }
    ASIOError getSampleRate(ASIOSampleRate* r) override
    {
        *r = rate_;
        return ASE_OK;
    }
    ASIOError setSampleRate(ASIOSampleRate r) override
    {
        if (canSampleRate(r) != ASE_OK)
            return ASE_NoClock;
        rate_ = r;
        std::lock_guard lock(mutex_);
        rateHistory_.push_back(r);
        return ASE_OK;
    }
    ASIOError getClockSources(ASIOClockSource*, long* n) override
    {
        *n = 0;
        return ASE_OK;
    }
    ASIOError setClockSource(long) override { return ASE_OK; }
    ASIOError getSamplePosition(ASIOSamples*, ASIOTimeStamp*) override { return ASE_SPNotAdvancing; }
    ASIOError getChannelInfo(ASIOChannelInfo* info) override
    {
        info->isActive = ASIOFalse;
        info->channelGroup = 0;
        info->type = cfg_.sampleType;
        strcpy_s(info->name, 32, "out");
        return ASE_OK;
    }
    ASIOError createBuffers(ASIOBufferInfo* infos, long numChannels, long bufferSize, ASIOCallbacks* callbacks) override
    {
        ++createBuffersCalls_;
        const std::size_t bps = sampleBytes();
        buffers_.assign(static_cast<std::size_t>(numChannels) * 2,
                        std::vector<std::byte>(static_cast<std::size_t>(bufferSize) * bps, std::byte{0x5A}));
        for (long c = 0; c < numChannels; ++c)
        {
            infos[c].buffers[0] = buffers_[static_cast<std::size_t>(c) * 2].data();
            infos[c].buffers[1] = buffers_[static_cast<std::size_t>(c) * 2 + 1].data();
        }
        channels_ = numChannels;
        bufferSize_ = bufferSize;
        callbacks_ = callbacks;
        std::lock_guard lock(mutex_);
        captured_.assign(static_cast<std::size_t>(numChannels), {});
        buffersCreated_ = true;
        return ASE_OK;
    }
    ASIOError disposeBuffers() override
    {
        stop();
        buffersCreated_ = false;
        return ASE_OK;
    }
    ASIOError controlPanel() override { return ASE_OK; }
    ASIOError future(long, void*) override { return ASE_InvalidParameter; }
    ASIOError outputReady() override { return ASE_OK; }

private:
    std::size_t sampleBytes() const
    {
        switch (cfg_.sampleType)
        {
            case 16: return 2;
            case 17: return 3;
            case 20: return 8;
            default: return 4;
        }
    }

    void loop()
    {
        long index = 0;
        const auto period = std::chrono::duration<double>(static_cast<double>(bufferSize_) / rate_);
        auto next = std::chrono::steady_clock::now();
        while (running_)
        {
            if (const long msg = pendingMessage_.exchange(0))
                callbacks_->asioMessage(msg, 0, nullptr, nullptr);
            callbacks_->bufferSwitch(index, ASIOTrue);
            {
                std::lock_guard lock(mutex_);
                for (long c = 0; c < channels_; ++c)
                {
                    const auto& b = buffers_[static_cast<std::size_t>(c) * 2 + static_cast<std::size_t>(index)];
                    captured_[static_cast<std::size_t>(c)].insert(captured_[static_cast<std::size_t>(c)].end(),
                                                                  b.begin(), b.end());
                }
            }
            index = 1 - index;
            if (cfg_.realtime)
            {
                next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
                std::this_thread::sleep_until(next);
            }
            else
            {
                std::this_thread::yield();
            }
        }
    }

    Config cfg_;
    std::atomic<ULONG> refs_{1};
    double rate_;
    std::vector<std::vector<std::byte>> buffers_;
    long channels_ = 0;
    long bufferSize_ = 0;
    ASIOCallbacks* callbacks_ = nullptr;
    std::atomic<bool> buffersCreated_{false};
    std::atomic<bool> running_{false};
    std::atomic<int> createBuffersCalls_{0};
    std::atomic<long> pendingMessage_{0};
    std::thread thread_;
    mutable std::mutex mutex_;
    std::vector<std::vector<std::byte>> captured_;
    std::vector<double> rateHistory_;
};

} // namespace ap::test
