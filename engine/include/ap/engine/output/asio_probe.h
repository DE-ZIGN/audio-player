#pragma once

// Checking ASIO drivers without risking the host process. Some drivers crash
// the process that loads them (Realtek ASIO did on the development machine,
// even before any playback), so the app loads each driver first in a short-
// lived child process, which prints one line with what it found.

#include <ap/engine/output/asio_output.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ap::engine {

struct AsioProbeResult
{
    enum class Status
    {
        Ok,
        LoadFailed, // the driver refused to load or initialise (message says why)
        Crashed,    // the probe process ended abnormally while loading the driver
        TimedOut,   // the probe process did not answer in time
    };

    Status status = Status::LoadFailed;
    std::string message;
    AsioDriverDetails details;
    std::vector<std::uint32_t> rates; // standard rates the driver can run at
};

// Loads the driver in this process (may crash it). Used by the child process.
AsioProbeResult probeAsioDriverInProcess(const AsioDriverInfo& driver);

// One line, starting with "AP_ASIO_PROBE ", for the child to print.
std::string formatProbeLine(const AsioProbeResult& r);
std::optional<AsioProbeResult> parseProbeLine(const std::string& line);

// Runs `executable` with `arguments` (which must make it call
// probeAsioDriverInProcess() and print formatProbeLine()) and reads the result.
AsioProbeResult probeAsioDriverIsolated(const std::filesystem::path& executable, const std::wstring& arguments,
                                        std::chrono::milliseconds timeout = std::chrono::seconds(10));

} // namespace ap::engine
