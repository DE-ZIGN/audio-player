#include <ap/engine/output/device_selection.h>

#include <algorithm>
#include <iterator>

namespace ap::engine {

namespace {

DeviceResolution fallback(const std::vector<OutputDeviceInfo>& devices)
{
    if (devices.empty())
        return {std::nullopt, DeviceMatch::NotFound};
    const auto def = std::find_if(devices.begin(), devices.end(), [](const auto& d) { return d.isDefault; });
    return {def != devices.end() ? *def : devices.front(), DeviceMatch::DefaultDevice};
}

} // namespace

DeviceResolution resolveOutputDevice(const SavedOutputDevice& saved, const std::vector<OutputDeviceInfo>& devices)
{
    if (devices.empty())
        return {std::nullopt, DeviceMatch::NotFound};

    if (!saved.id.empty())
    {
        const auto byId = std::find_if(devices.begin(), devices.end(), [&](const auto& d) { return d.id == saved.id; });
        if (byId != devices.end())
            return {*byId, DeviceMatch::SavedId};
    }

    if (!saved.name.empty())
    {
        std::vector<OutputDeviceInfo> named;
        std::copy_if(devices.begin(), devices.end(), std::back_inserter(named),
                     [&](const auto& d) { return d.name == saved.name; });
        if (!named.empty())
        {
            const auto def = std::find_if(named.begin(), named.end(), [](const auto& d) { return d.isDefault; });
            return {def != named.end() ? *def : named.front(), DeviceMatch::SavedName};
        }
    }

    return fallback(devices);
}

} // namespace ap::engine
