#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ap::engine {

struct OutputDeviceInfo
{
    std::string id;   // endpoint ID (UTF-8); may change, e.g. when a driver re-creates its endpoint
    std::string name; // friendly name (UTF-8)
    bool isDefault = false;
};

// What the app remembers about the user's chosen device.
struct SavedOutputDevice
{
    std::string id;
    std::string name;
};

enum class DeviceMatch
{
    SavedId,       // the remembered endpoint ID is present
    SavedName,     // the ID changed but a device with the remembered name exists
    DefaultDevice, // nothing remembered, or the remembered device is gone: using the default
    NotFound,      // there are no output devices
};

struct DeviceResolution
{
    std::optional<OutputDeviceInfo> device;
    DeviceMatch match = DeviceMatch::NotFound;
};

// Finds the device to use. Endpoint IDs are not stable for every driver
// (the SSL 2 MKII driver uses a different ID per sample rate), so after the
// ID the name is tried; failing both, the default device is used and the
// caller should tell the user.
DeviceResolution resolveOutputDevice(const SavedOutputDevice& saved, const std::vector<OutputDeviceInfo>& devices);

// Whether the output just opened may be sent DSD as DoP. The permission is
// for the one output it was given for: an endpoint ID, or an ASIO driver's
// CLSID. None for "the Windows default" (an empty ID), and none for a device
// found by its name or used instead of a missing one: it may be another DAC,
// and on one without DoP it is loud noise.
bool mayReceiveDop(bool allowed, std::string_view allowedId, std::string_view openedId);

} // namespace ap::engine
