#include <ap/engine/output/deadline_tracker.h>

#include <catch2/catch_test_macros.hpp>

using namespace ap::engine;
using namespace std::chrono_literals;
using Clock = DeadlineTracker::Clock;

TEST_CASE("On-time buffers are not counted", "[engine][dropout]")
{
    DeadlineTracker t;
    const auto t0 = Clock::time_point{} + 1s;
    t.start(t0, 10ms);
    // Each buffer delivered just before its own period ends (well within the slack).
    for (int k = 0; k < 100; ++k)
        CHECK_FALSE(t.onDelivered(t0 + 10ms * k + 9ms));
    CHECK(t.lateCount() == 0);
    CHECK(t.delivered() == 100);
}

TEST_CASE("Using the slack period is still on time", "[engine][dropout]")
{
    DeadlineTracker t;
    const auto t0 = Clock::time_point{} + 1s;
    t.start(t0, 10ms);
    CHECK_FALSE(t.onDelivered(t0 + 10ms)); // buffer 0 due by t0 + 10 ms (one period of slack)
    CHECK_FALSE(t.onDelivered(t0 + 20ms)); // buffer 1 due by t0 + 20 ms
    CHECK(t.lateCount() == 0);
}

TEST_CASE("A stall is counted once, not for every following buffer", "[engine][dropout]")
{
    DeadlineTracker t;
    const auto t0 = Clock::time_point{} + 1s;
    t.start(t0, 10ms);
    CHECK_FALSE(t.onDelivered(t0 + 5ms));
    CHECK(t.onDelivered(t0 + 80ms)); // buffer 1 was due by t0 + 20 ms
    // Back to normal pace after the stall.
    CHECK_FALSE(t.onDelivered(t0 + 85ms));
    CHECK_FALSE(t.onDelivered(t0 + 95ms));
    CHECK_FALSE(t.onDelivered(t0 + 105ms));
    CHECK(t.lateCount() == 1);
}

TEST_CASE("Separate stalls are counted separately", "[engine][dropout]")
{
    DeadlineTracker t;
    const auto t0 = Clock::time_point{} + 1s;
    t.start(t0, 10ms);
    auto now = t0;
    int late = 0;
    for (int k = 0; k < 50; ++k)
    {
        now += (k == 10 || k == 30) ? 40ms : 10ms;
        late += t.onDelivered(now) ? 1 : 0;
    }
    CHECK(late == 2);
    CHECK(t.lateCount() == 2);
}
