#pragma once

#include <functional>
#include <memory>
#include <string>

namespace ap::engine {

enum class DeviceEventKind
{
    Added,
    Removed,
    StateChanged,   // enabled, disabled, unplugged, ...
    DefaultChanged, // the default render (console) device changed; deviceId is the new default
};

struct DeviceEvent
{
    DeviceEventKind kind = DeviceEventKind::StateChanged;
    std::string deviceId; // UTF-8 endpoint ID
};

// Reports changes to render endpoints (IMMNotificationClient). The callback
// runs on a system thread; keep it short and hand work to your own thread.
// Some drivers re-create their endpoint when a setting changes (the SSL 2
// MKII driver does when its sample rate changes), which shows up as
// Removed/Added or StateChanged events with different IDs.
// COM must be initialized on the constructing thread (ScopedComInit).
class DeviceWatcher
{
public:
    explicit DeviceWatcher(std::function<void(const DeviceEvent&)> callback);
    ~DeviceWatcher();
    DeviceWatcher(const DeviceWatcher&) = delete;
    DeviceWatcher& operator=(const DeviceWatcher&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

const char* toString(DeviceEventKind kind);

} // namespace ap::engine
