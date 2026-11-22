#pragma once

// Where a queue starts when some of the tracks asked for cannot be played
// (their files went after the page read the list).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ap::app {

// `kept` is `asked` without the tracks that cannot be played, in the same
// order. The index in `kept` of the one at `start` in `asked`; none when that
// one cannot be played (another is never played for it).
inline std::optional<std::size_t> startAmongKept(std::span<const std::int64_t> asked, std::size_t start,
                                                 std::span<const std::int64_t> kept)
{
    std::size_t next = 0; // the first of `kept` not yet met in `asked`
    for (std::size_t i = 0; i < asked.size() && next < kept.size(); ++i)
    {
        if (asked[i] == kept[next])
        {
            if (i == start)
                return next;
            ++next;
        }
        else if (i == start)
        {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

} // namespace ap::app
