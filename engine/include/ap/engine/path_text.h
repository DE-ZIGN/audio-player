#pragma once

#include <filesystem>
#include <string>

namespace ap::engine {

// A path as UTF-8 text, for the messages and names the engine hands to the
// page and to logs: all of the engine's text is UTF-8.
//
// Not path::string(): on Windows that is the system's code page, and it
// throws std::system_error for a name the code page lacks (a Japanese name
// on a Windows that is not Japanese; "é" or Hangul on a Japanese one). An
// error text built with it never arrived: the conversion's own exception
// took the place of the error being reported (found 2026-11-19, when the
// tests first ran on a machine with code page 1252).
inline std::string pathUtf8(const std::filesystem::path& p)
{
    const auto s = p.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}

} // namespace ap::engine
