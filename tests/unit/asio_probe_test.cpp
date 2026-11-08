#include <ap/engine/output/asio_probe.h>

#include <catch2/catch_test_macros.hpp>

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
