#include "environment_probe.h"

#include <windows.h>
#include <appmodel.h>
#include <objbase.h>

#include <filesystem>
#include <thread>

namespace ap::play {

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

} // namespace

std::string packageFullName()
{
    UINT32 length = 0;
    if (GetCurrentPackageFullName(&length, nullptr) != ERROR_INSUFFICIENT_BUFFER)
        return {}; // APPMODEL_ERROR_NO_PACKAGE
    std::wstring name(length, L'\0');
    if (GetCurrentPackageFullName(&length, name.data()) != ERROR_SUCCESS)
        return {};
    name.resize(wcsnlen(name.c_str(), name.size()));
    return toUtf8(name);
}

std::string executablePath()
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n < path.size())
        {
            path.resize(n);
            return toUtf8(path);
        }
        path.resize(path.size() * 2);
    }
}

AsioLoadResult tryLoadAsioDriver(const ap::engine::AsioDriverInfo& driver)
{
    AsioLoadResult result;
    // ASIO hosts conventionally create drivers on a single-threaded apartment.
    std::thread worker([&] {
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        CLSID clsid{};
        HRESULT hr = CLSIDFromString(toWide(driver.clsid).c_str(), &clsid);
        if (SUCCEEDED(hr))
        {
            IUnknown* object = nullptr;
            // The ASIO convention passes the CLSID as the interface ID as well.
            hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, reinterpret_cast<void**>(&object));
            if (SUCCEEDED(hr) && object)
            {
                result.loaded = true;
                const auto fileName = std::filesystem::path(toWide(driver.dllPath)).filename().wstring();
                if (HMODULE module = GetModuleHandleW(fileName.c_str()))
                {
                    std::wstring path(MAX_PATH * 2, L'\0');
                    const DWORD n = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
                    path.resize(n);
                    result.loadedModulePath = toUtf8(path);
                }
                object->Release();
            }
        }
        result.hresult = static_cast<long>(hr);
        if (SUCCEEDED(init))
            CoUninitialize();
    });
    worker.join();
    return result;
}

} // namespace ap::play
