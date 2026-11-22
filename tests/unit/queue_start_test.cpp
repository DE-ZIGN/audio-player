// The track pressed is the track played, also when files went after the page
// read the list (LibraryBridge drops those from the queue).

#include <QueueStart.h>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

using ap::app::startAmongKept;
using Ids = std::vector<std::int64_t>;

TEST_CASE("A queue starts at the track pressed, by its id", "[queue][start]")
{
    const Ids asked{10, 11, 12, 13, 14};
    const auto start = [&](std::size_t pressed, const Ids& kept) { return startAmongKept(asked, pressed, kept); };

    CHECK(start(2, asked) == 2u);
    CHECK(start(2, {11, 12, 13, 14}) == 1u); // one before it went
    CHECK(start(2, {12}) == 0u);             // all the others went
    CHECK(start(2, {10, 11, 12, 13}) == 2u); // one after it went
    CHECK(start(4, {11, 14}) == 1u);
}

TEST_CASE("Nothing is played for a track pressed that cannot be", "[queue][start]")
{
    const Ids asked{10, 11, 12, 13, 14};
    CHECK(startAmongKept(asked, 2, Ids{10, 11, 13, 14}) == std::nullopt);
    CHECK(startAmongKept(asked, 4, Ids{10, 11, 12, 13}) == std::nullopt); // not the last in its place
    CHECK(startAmongKept(asked, 0, Ids{}) == std::nullopt);
    CHECK(startAmongKept(asked, 9, asked) == std::nullopt);
    // Shuffle on: the pressed one is first; the mixed ones do not take its place.
    CHECK(startAmongKept(Ids{12, 14, 10}, 0, Ids{14, 10}) == std::nullopt);
}

TEST_CASE("A track twice in the queue starts at the one pressed", "[queue][start]")
{
    CHECK(startAmongKept(Ids{7, 8, 7, 9}, 2, Ids{7, 7, 9}) == 1u);
    CHECK(startAmongKept(Ids{7, 8, 7, 9}, 2, Ids{7, 8, 7, 9}) == 2u);
}
