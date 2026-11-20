#include "AsioCheck.h"

#include <juce_core/juce_core.h>

#include <mutex>
#include <set>

#include <windows.h>

namespace ap::app {

using namespace ap::engine;

namespace {

std::mutex passedMutex;
std::set<std::string> passed; // CLSIDs, upper case

std::string upper(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

AsioProbeResult checkAsioDriver(const AsioDriverInfo& driver)
{
    const auto key = upper(driver.clsid);
    {
        std::lock_guard lock(passedMutex);
        if (passed.contains(key))
        {
            AsioProbeResult r;
            r.status = AsioProbeResult::Status::Ok;
            return r;
        }
    }
    const std::wstring self =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName().toWideCharPointer();
    const std::wstring clsid(driver.clsid.begin(), driver.clsid.end()); // CLSID text is ASCII
    auto r = probeAsioDriverIsolated(self, L"--asio-probe-one " + clsid);
    if (r.status == AsioProbeResult::Status::Ok)
    {
        std::lock_guard lock(passedMutex);
        passed.insert(key);
    }
    return r;
}

int runAsioProbeChild(const std::string& clsid)
{
    AsioProbeResult r;
    r.message = "driver not registered";
    for (const auto& d : listAsioDrivers())
        if (_stricmp(d.clsid.c_str(), clsid.c_str()) == 0)
        {
            r = probeAsioDriverInProcess(d);
            break;
        }
    // A GUI program has no console: write to the handle the parent passed.
    const auto line = formatProbeLine(r) + "\n";
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    return 0;
}

NativeMessage asioProblem(const AsioProbeResult& r)
{
    switch (r.status)
    {
        case AsioProbeResult::Status::Ok:
            return {};
        case AsioProbeResult::Status::LoadFailed:
            return msg::asioLoadFailed(r.message);
        case AsioProbeResult::Status::Crashed:
            return msg::asioCrashed();
        case AsioProbeResult::Status::TimedOut:
            return msg::asioTimedOut();
    }
    return msg::asioUnusable();
}

} // namespace ap::app
