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

std::wstring readString(HKEY root, const std::wstring& subKey, const wchar_t* value, DWORD extraFlags = 0)
{
    DWORD bytes = 0;
    const DWORD flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | extraFlags;
    if (RegGetValueW(root, subKey.c_str(), value, flags, nullptr, nullptr, &bytes) != ERROR_SUCCESS || bytes == 0)
        return {};
    std::wstring s(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, subKey.c_str(), value, flags, nullptr, s.data(), &bytes) != ERROR_SUCCESS)
        return {};
    s.resize(wcsnlen(s.c_str(), s.size()));
    return s;
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

std::vector<AsioDriverEntry> listAsioDrivers()
{
    std::vector<AsioDriverEntry> out;
    HKEY asio = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_READ | KEY_WOW64_64KEY, &asio) != ERROR_SUCCESS)
        return out;

    for (DWORD i = 0;; ++i)
    {
        wchar_t name[256];
        DWORD nameLength = 256;
        if (RegEnumKeyExW(asio, i, name, &nameLength, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;

        AsioDriverEntry e;
        e.name = toUtf8(name);
        const std::wstring clsid = readString(asio, name, L"CLSID");
        e.clsid = toUtf8(clsid);
        e.description = toUtf8(readString(asio, name, L"Description"));
        if (!clsid.empty())
            e.dllPath = toUtf8(readString(HKEY_CLASSES_ROOT, L"CLSID\\" + clsid + L"\\InprocServer32", nullptr));
        out.push_back(std::move(e));
    }
    RegCloseKey(asio);
    return out;
}

AsioLoadResult tryLoadAsioDriver(const AsioDriverEntry& driver)
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
