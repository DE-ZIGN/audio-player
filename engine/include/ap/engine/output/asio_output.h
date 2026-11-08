#pragma once

// ASIO output (Steinberg ASIO SDK 2.3, used under GPLv3).
//
// - The driver is created through COM and used only on a dedicated
//   single-threaded-apartment thread owned by this object.
// - The opened format is sent without changing sample values: interleaved
//   PCM is split per channel in the driver's sample type (copy, or sign
//   extension for the right-aligned 32-bit types). Integer and float are
//   never converted into each other.
// - ASIO lets the host set the sample rate, so open() switches the driver to
//   the format's rate when needed.
// - A buffer is counted as late when bufferSwitch() takes longer than one
//   buffer period, and when the driver reports an overload or a resync.
// - ASIO callbacks carry no user pointer, so only one AsioOutput can run at a
//   time (the ASIO model allows one active driver per process anyway).

#include <ap/engine/output/audio_output.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct IASIO; // iasiodrv.h

namespace ap::engine {

struct AsioDriverInfo
{
    std::string name;        // registry key under HKLM\SOFTWARE\ASIO
    std::string clsid;       // "{...}"
    std::string description;
    std::string dllPath;     // HKCR\CLSID\{clsid}\InprocServer32
};

// Reads the registry only; never loads a driver.
std::vector<AsioDriverInfo> listAsioDrivers();

struct AsioDriverDetails
{
    std::string driverName;
    long driverVersion = 0;
    long inputChannels = 0;
    long outputChannels = 0;
    long sampleType = -1; // of output channel 0 (asio::SampleTypeCode)
    long minBufferSize = 0;
    long maxBufferSize = 0;
    long preferredBufferSize = 0;
    long granularity = 0;
    double currentSampleRate = 0.0;
};

class AsioOutput final : public AudioOutput
{
public:
    // Called on the driver thread (STA). Returns an IASIO with one reference
    // owned by the caller; must not return null (throw instead).
    using DriverFactory = std::function<IASIO*()>;

    // CoCreateInstance of a registered driver.
    static DriverFactory factoryFor(const AsioDriverInfo& driver);

    // Creates and initialises the driver. Throws OutputException.
    AsioOutput(std::string displayName, DriverFactory factory);
    ~AsioOutput() override;
    AsioOutput(const AsioOutput&) = delete;
    AsioOutput& operator=(const AsioOutput&) = delete;

    std::string deviceName() const override;
    FormatCheck checkFormat(const PcmFormat& format) override;
    void open(const PcmFormat& format) override;
    RunStats run(const RenderCallback& render) override;
    void requestStop() noexcept override;
    void close() noexcept override;
    OutputHealth health() const noexcept override;

    AsioDriverDetails details() const;

    // Sample rate the driver was set to by the last open() (0 if unchanged).
    double rateSetByOpen() const noexcept;

    // Opens the driver's own settings window.
    void showControlPanel();

    struct Impl; // also used by the C callbacks in asio_output.cpp

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace ap::engine
