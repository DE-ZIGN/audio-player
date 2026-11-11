#pragma once

// A source that calls a function once when reading passes a given frame, to
// change settings at a known point while the player runs.

#include <ap/engine/pcm_source.h>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace ap::test {

class TriggerSource final : public engine::PcmSource
{
public:
    using Triggers = std::vector<std::pair<std::uint64_t, std::function<void()>>>;

    TriggerSource(std::unique_ptr<engine::PcmSource> inner, Triggers at) : inner_(std::move(inner)), at_(std::move(at)) {}

    const engine::PcmFormat& format() const noexcept override { return inner_->format(); }
    std::uint64_t totalFrames() const noexcept override { return inner_->totalFrames(); }
    std::size_t read(std::span<std::byte> dst) override
    {
        const auto n = inner_->read(dst);
        for (auto& [frame, fn] : at_)
            if (fn && inner_->position() >= frame)
            {
                fn();
                fn = nullptr;
            }
        return n;
    }
    bool seek(std::uint64_t f) override { return inner_->seek(f); }
    std::uint64_t position() const noexcept override { return inner_->position(); }

private:
    std::unique_ptr<engine::PcmSource> inner_;
    Triggers at_;
};

} // namespace ap::test
