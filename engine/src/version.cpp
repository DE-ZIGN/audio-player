#include <ap/engine/version.h>

namespace ap::engine {

Version version() noexcept
{
    return {AP_VERSION_MAJOR, AP_VERSION_MINOR, AP_VERSION_PATCH};
}

const char* versionString() noexcept
{
    return AP_VERSION_STRING;
}

} // namespace ap::engine
