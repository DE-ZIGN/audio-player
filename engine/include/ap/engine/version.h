#pragma once

namespace ap::engine {

struct Version
{
    int major;
    int minor;
    int patch;
};

// Version of the engine library (same as the CMake project version).
Version version() noexcept;

// "major.minor.patch"; the returned pointer has static storage duration.
const char* versionString() noexcept;

} // namespace ap::engine
