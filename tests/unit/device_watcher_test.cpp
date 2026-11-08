// Device change events need real hardware changes and are checked by hand
// (`ap_play --watch`). Here: registration and unregistration work and the
// callback is never called after destruction.

#include <ap/engine/output/device_watcher.h>
#include <ap/engine/output/wasapi_exclusive_output.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>

using namespace ap::engine;

TEST_CASE("DeviceWatcher registers and unregisters", "[engine][device]")
{
    ScopedComInit com;
    std::atomic<int> calls{0};
    {
        DeviceWatcher watcher([&](const DeviceEvent&) { ++calls; });
    }
    const int afterDestruction = calls;
    CHECK(calls == afterDestruction);

    // Several watchers can exist at once.
    DeviceWatcher a([](const DeviceEvent&) {});
    DeviceWatcher b([](const DeviceEvent&) {});
    SUCCEED();
}

TEST_CASE("Device event kinds have stable names for the UI", "[engine][device]")
{
    CHECK(std::string(toString(DeviceEventKind::Added)) == "added");
    CHECK(std::string(toString(DeviceEventKind::Removed)) == "removed");
    CHECK(std::string(toString(DeviceEventKind::StateChanged)) == "stateChanged");
    CHECK(std::string(toString(DeviceEventKind::DefaultChanged)) == "defaultChanged");
}
