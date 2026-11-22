#include <ap/engine/output/device_selection.h>

#include <catch2/catch_test_macros.hpp>

using namespace ap::engine;

namespace {

// IDs as reported on the development machine (docs/phase1-tasks.md, S1).
const OutputDeviceInfo ssl96{"{0.0.0.00000000}.{181ab626-643a-4034-99cc-e5fa4cb5d845}",
                             "Output 1/2 (2- SSL 2 MKII USB Audio Device)", true};
const OutputDeviceInfo ssl44{"{0.0.0.00000000}.{9ffad2c9-2616-4a99-9c72-bfc786c5ea66}",
                             "Output 1/2 (2- SSL 2 MKII USB Audio Device)", true};
const OutputDeviceInfo hdmi{"{0.0.0.00000000}.{2d606f30-4dfa-47ab-8368-c6b69d272d27}",
                            "PL2473H (NVIDIA High Definition Audio)", false};
const OutputDeviceInfo soundgrid{"{0.0.0.00000000}.{5c3ce8a7-3913-4711-b8c8-41c0f4cf8ccb}",
                                 "スピーカー (Waves SoundGrid)", false};

} // namespace

TEST_CASE("The remembered ID wins when present", "[engine][device]")
{
    const auto r = resolveOutputDevice({hdmi.id, hdmi.name}, {ssl96, hdmi, soundgrid});
    CHECK(r.match == DeviceMatch::SavedId);
    REQUIRE(r.device);
    CHECK(r.device->id == hdmi.id);
}

TEST_CASE("A device whose ID changed is found again by name", "[engine][device]")
{
    // Saved while the SSL ran at 96 kHz; its control panel is now at 44.1 kHz.
    const auto r = resolveOutputDevice({ssl96.id, ssl96.name}, {hdmi, soundgrid, ssl44});
    CHECK(r.match == DeviceMatch::SavedName);
    REQUIRE(r.device);
    CHECK(r.device->id == ssl44.id);
}

TEST_CASE("A missing device falls back to the default", "[engine][device]")
{
    const auto r = resolveOutputDevice({"{gone}", "Unplugged DAC"}, {hdmi, ssl96, soundgrid});
    CHECK(r.match == DeviceMatch::DefaultDevice);
    REQUIRE(r.device);
    CHECK(r.device->id == ssl96.id);
}

TEST_CASE("Nothing remembered means the default device", "[engine][device]")
{
    const auto r = resolveOutputDevice({}, {hdmi, ssl96});
    CHECK(r.match == DeviceMatch::DefaultDevice);
    REQUIRE(r.device);
    CHECK(r.device->isDefault);
}

TEST_CASE("Without a default the first device is used", "[engine][device]")
{
    const auto r = resolveOutputDevice({"{gone}", "x"}, {hdmi, soundgrid});
    CHECK(r.match == DeviceMatch::DefaultDevice);
    REQUIRE(r.device);
    CHECK(r.device->id == hdmi.id);
}

TEST_CASE("Several devices with the remembered name: prefer the default one", "[engine][device]")
{
    OutputDeviceInfo twinA{"{a}", "USB DAC", false};
    OutputDeviceInfo twinB{"{b}", "USB DAC", true};
    const auto r = resolveOutputDevice({"{old}", "USB DAC"}, {hdmi, twinA, twinB});
    CHECK(r.match == DeviceMatch::SavedName);
    REQUIRE(r.device);
    CHECK(r.device->id == "{b}");
}

TEST_CASE("No devices at all", "[engine][device]")
{
    const auto r = resolveOutputDevice({ssl96.id, ssl96.name}, {});
    CHECK(r.match == DeviceMatch::NotFound);
    CHECK_FALSE(r.device);
}

TEST_CASE("DoP goes only to the device it was allowed for", "[engine][device][dop]")
{
    const OutputDeviceInfo dac{"{0.0.0.00000000}.{2581a301}", "Speaker (FiiO KA11)", false};
    const auto sent = [](const SavedOutputDevice& saved, const std::vector<OutputDeviceInfo>& devices) {
        const auto r = resolveOutputDevice(saved, devices);
        return r.device && mayReceiveDop(true, saved.id, r.device->id);
    };

    SECTION("that device: sent")
    {
        CHECK(sent({dac.id, dac.name}, {hdmi, ssl96, dac}));
    }
    SECTION("renamed in Windows, the same endpoint: sent")
    {
        CHECK(sent({dac.id, "an earlier name"}, {ssl96, dac}));
    }
    SECTION("unplugged, the default used instead: not sent")
    {
        CHECK_FALSE(sent({dac.id, dac.name}, {hdmi, ssl96}));
    }
    SECTION("another endpoint of the same name (it can be another DAC): not sent")
    {
        CHECK_FALSE(sent({"{old}", dac.name}, {ssl96, dac}));
    }
    SECTION("the Windows default, whatever it is now: not sent")
    {
        auto asDefault = dac;
        asDefault.isDefault = true;
        CHECK_FALSE(sent({}, {hdmi, asDefault}));
        CHECK_FALSE(mayReceiveDop(true, "", ""));
    }
}

TEST_CASE("DoP is not sent unless allowed; IDs ignore letter case", "[engine][device][dop]")
{
    CHECK_FALSE(mayReceiveDop(false, "{A}", "{A}"));
    CHECK(mayReceiveDop(true, "{C0E4A803-D72F}", "{c0e4a803-d72f}"));
    // An ASIO driver found by its name instead of its CLSID.
    CHECK_FALSE(mayReceiveDop(true, "{C0E4A803-D72F}", "{11111111-2222}"));
    CHECK_FALSE(mayReceiveDop(true, "{C0E4}", "{C0E4A803}"));
}
