#pragma once

// Diagnostics for step S2 (does the player work inside an MSIX package?).
// ASIO drivers are only enumerated and loaded here; playback through ASIO
// arrives with the SDK in task 1-4.

#include <string>
#include <vector>

namespace ap::play {

// Full name of the package this process runs in; empty when not packaged.
std::string packageFullName();

// Path of the running executable.
std::string executablePath();

struct AsioDriverEntry
{
    std::string name;        // registry key name under HKLM\SOFTWARE\ASIO
    std::string clsid;       // "{...}"
    std::string description;
    std::string dllPath;     // HKCR\CLSID\{clsid}\InprocServer32
};

// Reads HKLM\SOFTWARE\ASIO (64-bit view).
std::vector<AsioDriverEntry> listAsioDrivers();

struct AsioLoadResult
{
    bool loaded = false;
    long hresult = 0;
    std::string loadedModulePath; // where the DLL was actually loaded from
};

// Creates the driver's COM object in-process (on an STA thread, as ASIO
// hosts do) and releases it again. Does not call into the driver.
AsioLoadResult tryLoadAsioDriver(const AsioDriverEntry& driver);

} // namespace ap::play
