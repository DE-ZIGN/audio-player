#include <ap/engine/output/asio_probe.h>

#include <windows.h>

#include <format>
#include <sstream>
#include <thread>
#include <vector>

namespace ap::engine {

namespace {

std::string percentEncode(const std::string& s)
{
    std::string out;
    for (unsigned char c : s)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
            out.push_back(static_cast<char>(c));
        else
            out += std::format("%{:02X}", c);
    }
    return out;
}

std::string percentDecode(const std::string& s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size())
        {
            out.push_back(static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        }
        else
        {
            out.push_back(s[i]);
        }
    }
    return out;
}

const char* statusName(AsioProbeResult::Status s)
{
    switch (s)
    {
        case AsioProbeResult::Status::Ok:         return "ok";
        case AsioProbeResult::Status::LoadFailed: return "loadFailed";
        case AsioProbeResult::Status::Crashed:    return "crashed";
        case AsioProbeResult::Status::TimedOut:   return "timedOut";
    }
    return "loadFailed";
}

} // namespace

AsioProbeResult probeAsioDriverInProcess(const AsioDriverInfo& driver)
{
    AsioProbeResult r;
    try
    {
        AsioOutput out("probe", AsioOutput::factoryFor(driver));
        r.details = out.details();
        r.rates = out.supportedRates();
        r.status = AsioProbeResult::Status::Ok;
    }
    catch (const std::exception& e)
    {
        r.status = AsioProbeResult::Status::LoadFailed;
        r.message = e.what();
    }
    return r;
}

std::string formatProbeLine(const AsioProbeResult& r)
{
    std::string rates;
    for (const auto x : r.rates)
        rates += (rates.empty() ? "" : ",") + std::to_string(x);
    const auto& d = r.details;
    return std::format("AP_ASIO_PROBE status={} message={} name={} version={} in={} out={} type={} min={} max={} pref={} "
                       "gran={} rate={} rates={}",
                       statusName(r.status), percentEncode(r.message), percentEncode(d.driverName), d.driverVersion,
                       d.inputChannels, d.outputChannels, d.sampleType, d.minBufferSize, d.maxBufferSize,
                       d.preferredBufferSize, d.granularity, d.currentSampleRate, rates.empty() ? "-" : rates);
}

std::optional<AsioProbeResult> parseProbeLine(const std::string& line)
{
    static const std::string kPrefix = "AP_ASIO_PROBE ";
    if (!line.starts_with(kPrefix))
        return std::nullopt;

    AsioProbeResult r;
    std::istringstream in(line.substr(kPrefix.size()));
    std::string token;
    try
    {
        while (in >> token)
        {
            const auto eq = token.find('=');
            if (eq == std::string::npos)
                continue;
            const std::string key = token.substr(0, eq);
            const std::string value = token.substr(eq + 1);
            auto& d = r.details;
            if (key == "status")
                r.status = value == "ok"         ? AsioProbeResult::Status::Ok
                         : value == "crashed"    ? AsioProbeResult::Status::Crashed
                         : value == "timedOut"   ? AsioProbeResult::Status::TimedOut
                                                 : AsioProbeResult::Status::LoadFailed;
            else if (key == "message") r.message = percentDecode(value);
            else if (key == "name")    d.driverName = percentDecode(value);
            else if (key == "version") d.driverVersion = std::stol(value);
            else if (key == "in")      d.inputChannels = std::stol(value);
            else if (key == "out")     d.outputChannels = std::stol(value);
            else if (key == "type")    d.sampleType = std::stol(value);
            else if (key == "min")     d.minBufferSize = std::stol(value);
            else if (key == "max")     d.maxBufferSize = std::stol(value);
            else if (key == "pref")    d.preferredBufferSize = std::stol(value);
            else if (key == "gran")    d.granularity = std::stol(value);
            else if (key == "rate")    d.currentSampleRate = std::stod(value);
            else if (key == "rates" && value != "-")
            {
                std::istringstream rs(value);
                std::string part;
                while (std::getline(rs, part, ','))
                    r.rates.push_back(static_cast<std::uint32_t>(std::stoul(part)));
            }
        }
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
    return r;
}

AsioProbeResult probeAsioDriverIsolated(const std::filesystem::path& executable, const std::wstring& arguments,
                                        std::chrono::milliseconds timeout)
{
    AsioProbeResult failed;

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0))
    {
        failed.message = "CreatePipe failed";
        return failed;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    // A job, so that on a timeout the probe and anything it started are ended
    // together, and so that the probe ends with this process (the job's only
    // handle is ours: when this process ends at any moment, the job closes).
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job && !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
    {
        CloseHandle(job);
        job = nullptr;
    }

    // The probe process inherits only the pipe it answers on (not this
    // process's other inheritable handles, such as a shell's stdout pipe), and
    // is in the job from its creation: there is no moment in which this process
    // could end and leave the probe behind.
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 2, 0, &attrSize);
    std::vector<std::byte> attrBuffer(attrSize);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuffer.data());
    bool attrsReady = InitializeProcThreadAttributeList(attrs, 2, 0, &attrSize) != FALSE;
    const bool attrsInitialized = attrsReady;
    attrsReady = attrsReady && UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &writeEnd,
                                                         sizeof(writeEnd), nullptr, nullptr);
    if (job)
        attrsReady = attrsReady &&
                     UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &job, sizeof(job), nullptr, nullptr);

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput = writeEnd;
    si.StartupInfo.hStdError = writeEnd;
    si.StartupInfo.hStdInput = nullptr; // the probe reads nothing
    si.lpAttributeList = attrs;
    PROCESS_INFORMATION pi{};

    std::wstring commandLine = L"\"" + executable.wstring() + L"\" " + arguments;
    const BOOL created = attrsReady && CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                                                      CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                                                      &si.StartupInfo, &pi);
    const DWORD createError = GetLastError();
    if (attrsInitialized)
        DeleteProcThreadAttributeList(attrs);
    CloseHandle(writeEnd);
    if (!created)
    {
        CloseHandle(readEnd);
        if (job)
            CloseHandle(job);
        failed.message = std::format("cannot start the probe process (error {})", createError);
        return failed;
    }

    // Read the child's output on a separate thread so a full pipe never blocks it.
    std::string output;
    std::thread reader([&] {
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(readEnd, buf, sizeof(buf), &n, nullptr) && n > 0)
            output.append(buf, n);
    });

    const DWORD wait = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeout.count()));
    if (wait == WAIT_TIMEOUT)
    {
        if (job)
            TerminateJobObject(job, 1);
        else
            TerminateProcess(pi.hProcess, 1);
    }
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (job)
        CloseHandle(job); // ends anything left in the job, which closes the pipe
    reader.join();
    CloseHandle(readEnd);

    if (wait == WAIT_TIMEOUT)
    {
        failed.status = AsioProbeResult::Status::TimedOut;
        failed.message = "the probe process did not answer in time";
        return failed;
    }

    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (auto r = parseProbeLine(line))
            return *r;
    }

    failed.status = AsioProbeResult::Status::Crashed;
    failed.message = std::format("the probe process ended with 0x{:08X} without a result", exitCode);
    return failed;
}

} // namespace ap::engine
