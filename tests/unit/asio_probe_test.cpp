#include <ap/engine/output/asio_probe.h>

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <chrono>
#include <future>
#include <thread>

using namespace ap::engine;

TEST_CASE("ASIO probe results survive the round trip through one line", "[engine][asio]")
{
    AsioProbeResult r;
    r.status = AsioProbeResult::Status::Ok;
    r.message = "ok: 日本語 = 100% & spaces";
    r.details.driverName = "SSL USB ASIO Driver";
    r.details.driverVersion = 3;
    r.details.inputChannels = 2;
    r.details.outputChannels = 4;
    r.details.sampleType = 18;
    r.details.minBufferSize = 16;
    r.details.maxBufferSize = 2048;
    r.details.preferredBufferSize = 256;
    r.details.granularity = -1;
    r.details.currentSampleRate = 96000;
    r.rates = {44100, 48000, 88200, 96000, 176400, 192000};

    const auto line = formatProbeLine(r);
    CHECK(line.starts_with("AP_ASIO_PROBE "));
    CHECK(line.find('\n') == std::string::npos);

    const auto back = parseProbeLine(line);
    REQUIRE(back.has_value());
    CHECK(back->status == AsioProbeResult::Status::Ok);
    CHECK(back->message == r.message);
    CHECK(back->details.driverName == r.details.driverName);
    CHECK(back->details.outputChannels == 4);
    CHECK(back->details.sampleType == 18);
    CHECK(back->details.granularity == -1);
    CHECK(back->details.currentSampleRate == 96000.0);
    CHECK(back->rates == r.rates);
}

TEST_CASE("ASIO probe lines that are not ours are ignored", "[engine][asio]")
{
    CHECK_FALSE(parseProbeLine("hello").has_value());
    CHECK_FALSE(parseProbeLine("").has_value());
}

TEST_CASE("ASIO probe of a process that crashes is reported as Crashed", "[engine][asio]")
{
    // cmd.exe exiting with an access-violation code stands in for a crashing driver.
    const auto r = probeAsioDriverIsolated(L"C:\\Windows\\System32\\cmd.exe", L"/c exit -1073741819",
                                           std::chrono::seconds(10));
    CHECK(r.status == AsioProbeResult::Status::Crashed);
    CHECK(r.message.find("0xC0000005") != std::string::npos);
}

TEST_CASE("ASIO probe of a process that hangs is reported as TimedOut", "[engine][asio]")
{
    const auto r = probeAsioDriverIsolated(L"C:\\Windows\\System32\\cmd.exe", L"/c ping -n 6 127.0.0.1 >nul",
                                           std::chrono::milliseconds(500));
    CHECK(r.status == AsioProbeResult::Status::TimedOut);
}

TEST_CASE("ASIO probe: the probe process gets only its own pipe, not the caller's handles", "[engine][asio]")
{
    // A caller's inheritable pipe (like the stdout pipe a shell gives ap_play).
    // If the probe process inherited its write end, reading would wait until
    // the probe process ends; a shell reading ap_play's output then waits for
    // a probe process that is left over (seen on 2026-11-10).
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    REQUIRE(CreatePipe(&readEnd, &writeEnd, &sa, 0));

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    auto probe = std::async(std::launch::async, [] {
        return probeAsioDriverIsolated(L"C:\\Windows\\System32\\cmd.exe", L"/c ping -n 3 127.0.0.1 >nul",
                                       std::chrono::seconds(10));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // the probe process is running now
    CloseHandle(writeEnd);
    const auto readFrom = clock::now();
    char c = 0;
    DWORD n = 0;
    const BOOL got = ReadFile(readEnd, &c, 1, &n, nullptr); // ends at once when no one else holds the write end
    const auto readTook = clock::now() - readFrom;
    CloseHandle(readEnd);
    probe.get();
    const auto probeTook = clock::now() - start;

    CHECK_FALSE(got);
    CHECK(readTook < std::chrono::milliseconds(1000));
    CHECK(probeTook > std::chrono::milliseconds(1500)); // the probe process really was running meanwhile
}
