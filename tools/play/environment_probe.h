#pragma once

// Diagnostics for step S2 (does the player work inside an MSIX package?).
// tryLoadAsioDriver() only creates and releases the COM object (S2 check);
// playback through ASIO is ap::engine::AsioOutput.

#include <ap/engine/output/asio_output.h>

#include <string>
#include <vector>

namespace ap::play {

// Full name of the package this process runs in; empty when not packaged.
std::string packageFullName();

// Path of the running executable.
std::string executablePath();

struct AsioLoadResult
{
    bool loaded = false;
    long hresult = 0;
    std::string loadedModulePath; // where the DLL was actually loaded from
};

// Creates the driver's COM object in-process (on an STA thread, as ASIO
// hosts do) and releases it again. Does not call into the driver.
AsioLoadResult tryLoadAsioDriver(const ap::engine::AsioDriverInfo& driver);

} // namespace ap::play
