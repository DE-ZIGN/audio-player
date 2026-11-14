// The test programs' entry point (instead of Catch2's own main).
//
// Catch2's main takes argv in the ANSI code page (cp932 on a Japanese
// Windows), but test names are UTF-8 in the sources. ctest passes a test's
// name on the command line in Unicode, so a name with Japanese in it reached
// Catch2 garbled and matched nothing ("No test cases matched"). Here the
// command line is read in Unicode and handed over as UTF-8. The process's
// code page is left as it is: tests that use ANSI paths keep seeing what the
// app sees.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <shellapi.h>

#include <catch2/catch_session.hpp>

#include <string>
#include <vector>

namespace {

std::string utf8(const wchar_t* w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? static_cast<std::size_t>(n) : 1, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    s.resize(s.size() - 1); // the terminating null
    return s;
}

} // namespace

int main()
{
    int argc = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for (int i = 0; i < argc && wide; ++i)
        args.push_back(utf8(wide[i]));
    if (wide)
        LocalFree(wide);
    std::vector<char*> argv;
    for (auto& a : args)
        argv.push_back(a.data());
    argv.push_back(nullptr);
    return Catch::Session().run(static_cast<int>(args.size()), argv.data());
}
