#pragma once

#include <chrono>
#include <cstdint>

namespace ap::engine {

// Detects buffers handed to the device too late (device-side dropouts).
//
// Model: after start(), buffer k (k = 0, 1, ...) is due by
//   start + (k + slackPeriods) * period
// With the usual double buffering one period of slack is available: while
// the device plays one buffer, the next one must be ready. A late buffer is
// counted once, and the schedule restarts from that moment so one stall is
// not counted again for every following buffer.
class DeadlineTracker
{
public:
    using Clock = std::chrono::steady_clock;

    void start(Clock::time_point t0, Clock::duration period, int slackPeriods = 1)
    {
        start_ = t0;
        period_ = period;
        slack_ = slackPeriods;
        delivered_ = 0;
    }

    // Call when buffer number `delivered()` has been handed over. Returns true if it was late.
    bool onDelivered(Clock::time_point now)
    {
        const auto due = start_ + period_ * (static_cast<std::int64_t>(delivered_) + slack_);
        ++delivered_;
        if (now <= due)
            return false;
        ++late_;
        // Restart the schedule: the device resumes from here.
        start_ = now - period_ * (static_cast<std::int64_t>(delivered_) + slack_ - 1);
        return true;
    }

    std::uint64_t delivered() const noexcept { return delivered_; }
    std::uint32_t lateCount() const noexcept { return late_; }

private:
    Clock::time_point start_{};
    Clock::duration period_{};
    int slack_ = 1;
    std::uint64_t delivered_ = 0;
    std::uint32_t late_ = 0;
};

} // namespace ap::engine
