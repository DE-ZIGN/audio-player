#include <ap/engine/dsp/space.h>

#include <ap/engine/dsp/eq.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

namespace ap::engine {

namespace {

constexpr std::size_t kScratchFrames = 1024;

std::size_t roomIndex(SpaceRoom room) noexcept
{
    return static_cast<std::size_t>(room) - 1; // Club 0, Hall 1, Arena 2
}

SpaceStage::Params clamped(SpaceStage::Params p)
{
    auto finite = [](double v, double fallback) { return std::isfinite(v) ? v : fallback; };
    p.mixPercent = std::clamp(finite(p.mixPercent, SpaceStage::kDefaultMixPercent), 0.0, 100.0);
    p.preDelayMs = std::clamp(finite(p.preDelayMs, SpaceStage::kDefaultPreDelayMs), 0.0, SpaceStage::kMaxPreDelayMs);
    const double cut = finite(p.lowCutHz, SpaceStage::kDefaultLowCutHz);
    p.lowCutHz = cut <= 0.0 ? 0.0 : std::clamp(cut, SpaceStage::kMinLowCutHz, SpaceStage::kMaxLowCutHz);
    return p;
}

} // namespace

SpaceStage::SpaceStage() = default;
SpaceStage::~SpaceStage() = default;

bool SpaceStage::kernelChanged(const Params& a, const Params& b)
{
    return a.room != b.room || a.preDelayMs != b.preDelayMs || a.lowCutHz != b.lowCutHz || a.ir != b.ir;
}

void SpaceStage::set(const Params& in)
{
    const Params p = clamped(in);
    bool build = false;
    double rate = 0.0;
    std::uint64_t gen = 0;
    std::array<KernelPtr, 8> released; // freed here, outside the lock
    {
        std::lock_guard lock(mutex_);
        build = kernelChanged(params_, p);
        params_ = p;
        released = std::move(trash_);
        trash_ = {};
        rate = rate_;
        if (build)
            gen = ++generation_;
    }
    if (build)
    {
        KernelPtr k;
        if (p.room != SpaceRoom::Off && rate > 0.0)
            k = buildKernel(p, rate);
        std::lock_guard lock(mutex_);
        if (generation_ == gen && rate_ == rate)
            std::swap(pending_, k); // the replaced pending kernel is freed here
    }
    // After the kernel is ready, so a room switched on finds its kernel waiting.
    room_ = p.room;
    mixTarget_ = p.room == SpaceRoom::Off ? 0.0 : p.mixPercent / 100.0;
}

SpaceStage::Params SpaceStage::params() const
{
    std::lock_guard lock(mutex_);
    return params_;
}

double SpaceStage::effectivePreDelayMs() const noexcept
{
    double pre = 0.0;
    {
        std::lock_guard lock(mutex_);
        pre = params_.preDelayMs;
    }
    return std::max(pre, 1000.0 * static_cast<double>(latency_.load()) / preparedRate_.load());
}

std::vector<std::vector<double>> SpaceStage::buildTaps(const Params& p, double rate, std::size_t latency) const
{
    if (p.room == SpaceRoom::Off)
        return {};
    ImpulseResponse ir;
    {
        std::lock_guard lock(buildMutex_);
        const auto& user = p.ir[roomIndex(p.room)];
        if (cacheRoom_ != p.room || cacheRate_ != rate || cacheSource_ != user)
        {
            if (user)
            {
                cache_.sampleRate = rate;
                cache_.channels.clear();
                const auto maxFrames = static_cast<std::size_t>(kMaxIrSeconds * rate);
                for (const auto& c : user->channels)
                {
                    auto h = resampleIr(c, user->sampleRate, rate);
                    if (h.size() > maxFrames)
                        h.resize(maxFrames);
                    cache_.channels.push_back(std::move(h));
                }
            }
            else
                cache_ = synthesizeRoom(p.room, rate);
            cacheRoom_ = p.room;
            cacheRate_ = rate;
            cacheSource_ = user;
        }
        ir = cache_;
    }
    if (ir.channels.empty())
        return {};

    // Unit energy per channel, on average.
    double energy = 0.0;
    for (const auto& c : ir.channels)
        for (const double v : c)
            energy += v * v;
    energy /= static_cast<double>(ir.channels.size());
    const double scale = energy > 0.0 ? 1.0 / std::sqrt(energy) : 0.0;

    std::optional<Biquad> cut;
    if (p.lowCutHz > 0.0)
        cut = designBiquad(EqBand{EqFilterType::HighPass, p.lowCutHz, 0.0, std::numbers::sqrt2 / 2.0, true}, rate);

    const auto pre = std::max<std::size_t>(latency, static_cast<std::size_t>(std::llround(p.preDelayMs * rate / 1000.0)));
    std::vector<std::vector<double>> taps;
    for (auto& c : ir.channels)
    {
        std::vector<double> t(pre + c.size(), 0.0);
        double z1 = 0.0, z2 = 0.0;
        for (std::size_t i = 0; i < c.size(); ++i)
        {
            double v = c[i] * scale;
            if (cut)
            {
                const double y = cut->b0 * v + z1;
                z1 = cut->b1 * v - cut->a1 * y + z2;
                z2 = cut->b2 * v - cut->a2 * y;
                v = y;
            }
            t[pre + i] = v;
        }
        taps.push_back(std::move(t));
    }
    return taps;
}

SpaceStage::KernelPtr SpaceStage::buildKernel(const Params& p, double rate) const
{
    const auto layout = ConvolverLayout::forRate(rate);
    auto taps = buildTaps(p, rate, layout.head);
    if (taps.empty())
        return nullptr;
    // The convolver delays by one head block: it gets the IR without that many leading zeros.
    for (auto& t : taps)
        t.erase(t.begin(), t.begin() + static_cast<std::ptrdiff_t>(layout.head));
    return ConvolutionKernel::make(layout, taps);
}

std::vector<std::vector<double>> SpaceStage::wetResponse(double sampleRate) const
{
    return buildTaps(params(), sampleRate, ConvolverLayout::forRate(sampleRate).head);
}

std::string SpaceStage::displayDetail() const
{
    const SpaceRoom room = room_;
    std::string s = spaceRoomKey(room);
    s += ';';
    s += std::to_string(std::llround(mixPercent()));
    if (room != SpaceRoom::Off)
    {
        std::lock_guard lock(mutex_);
        const auto i = roomIndex(room);
        if (params_.ir[i] && params_.source[i] == SpaceIrSource::Measured)
            s += ";measured";
        else if (params_.ir[i])
            s += ";user";
    }
    return s;
}

void SpaceStage::prepare(std::uint32_t sampleRate, std::uint16_t channels)
{
    const double rate = sampleRate;
    const auto layout = ConvolverLayout::forRate(rate);
    channels_ = channels;
    const auto maxFrames =
        static_cast<std::size_t>(std::ceil((kMaxIrSeconds + kMaxPreDelayMs / 1000.0) * rate)) + layout.head;
    convolver_.prepare(layout, channels, maxFrames);
    wet_.assign(kScratchFrames * std::max<std::size_t>(1, channels), 0.0);
    mixStep_ = 1.0 / (kMixSeconds * rate);
    mixNow_ = 0.0;
    settled_ = true;
    latency_ = layout.head;
    preparedRate_ = rate;
    current_.reset();
    next_.reset();
    for (auto& r : retired_)
        r.reset();
    Params p;
    {
        std::lock_guard lock(mutex_);
        rate_ = rate;
        p = params_;
        pending_.reset();
        for (auto& t : trash_)
            t.reset();
    }
    if (p.room != SpaceRoom::Off)
    {
        current_ = buildKernel(p, rate);
        convolver_.setKernel(current_.get());
    }
}

void SpaceStage::retire(KernelPtr& k) noexcept
{
    if (!k)
        return;
    for (auto& r : retired_)
        if (!r)
        {
            r = std::move(k);
            return;
        }
}

void SpaceStage::process(std::span<double> samples, std::uint16_t channels) noexcept
{
    if (channels != channels_ || channels == 0 || samples.empty())
        return;

    // A crossfade that has finished: the next kernel is the current one.
    if (next_ && !convolver_.crossfading() && convolver_.kernel() == next_.get())
    {
        retire(current_);
        current_ = std::move(next_);
    }

    const double target = mixTarget_;
    const bool resuming = mixNow_ == 0.0 && target > 0.0;
    if (resuming)
        convolver_.reset(); // what it holds is from before the stage stopped running

    if (mutex_.try_lock())
    {
        std::lock_guard lock(mutex_, std::adopt_lock);
        for (auto& r : retired_)
            if (r)
                for (auto& t : trash_)
                    if (!t)
                    {
                        t = std::move(r);
                        break;
                    }
        const auto free = static_cast<std::size_t>(std::count(retired_.begin(), retired_.end(), nullptr));
        if (pending_ && free >= 2)
        {
            if (pending_->layout() != convolver_.layout())
                retire(pending_); // made for another layout (cannot happen: prepare drops it)
            else if (resuming || !current_)
            {
                retire(current_);
                retire(next_);
                current_ = std::move(pending_);
                convolver_.setKernel(current_.get());
            }
            else if (!next_ && !convolver_.crossfading())
            {
                next_ = std::move(pending_);
                convolver_.crossfadeTo(next_.get());
            }
        }
    }

    const std::size_t frames = samples.size() / channels;
    const std::size_t chunk = wet_.size() / channels;
    for (std::size_t done = 0; done < frames;)
    {
        const std::size_t n = std::min(frames - done, chunk);
        double* x = samples.data() + done * channels;
        convolver_.process(x, wet_.data(), n);
        for (std::size_t i = 0; i < n; ++i)
        {
            if (mixNow_ < target)
                mixNow_ = std::min(target, mixNow_ + mixStep_);
            else if (mixNow_ > target)
                mixNow_ = std::max(target, mixNow_ - mixStep_);
            if (mixNow_ == 0.0)
                continue;
            for (std::size_t c = 0; c < channels; ++c)
            {
                double& v = x[i * channels + c];
                v += mixNow_ * (wet_[i * channels + c] - v);
            }
        }
        done += n;
    }
    settled_ = mixNow_ == 0.0 && target == 0.0;
}

} // namespace ap::engine
