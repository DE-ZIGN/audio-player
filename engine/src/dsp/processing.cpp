#include <ap/engine/dsp/processing.h>

#include <ap/engine/dsp/comparison.h>

#include <algorithm>
#include <xmmintrin.h>

namespace ap::engine {

namespace {
constexpr unsigned kFlushToZero = 0x8000u;      // MXCSR.FTZ
constexpr unsigned kDenormalsAreZero = 0x0040u; // MXCSR.DAZ
} // namespace

ProcessingChain::ProcessingChain(std::vector<std::shared_ptr<ProcessingStage>> stages)
    : stages_(std::move(stages)), comparison_(std::make_unique<AbComparison>())
{
    std::erase(stages_, nullptr);
}

ProcessingChain::~ProcessingChain() = default;

void ProcessingChain::updateUpstreamGains() const noexcept
{
    double g = 1.0;
    for (const auto& s : stages_)
    {
        s->setUpstreamGain(g);
        g *= s->peakGain();
    }
}

bool ProcessingChain::bypassed() const noexcept
{
    updateUpstreamGains();
    if (comparingRequested_)
        return false;
    return std::all_of(stages_.begin(), stages_.end(), [](const auto& s) { return s->isIdentity(); });
}

std::vector<std::string> ProcessingChain::activeStageNames() const
{
    updateUpstreamGains();
    std::vector<std::string> names;
    for (const auto& s : stages_)
        if (!s->isIdentity() || s->runsWhenProcessing())
            names.push_back(s->name());
    if (comparingRequested_)
        names.push_back("comparison");
    return names;
}

std::vector<StageState> ProcessingChain::activeStageStates() const
{
    updateUpstreamGains();
    std::vector<StageState> states;
    for (const auto& s : stages_)
        if (!s->isIdentity() || s->runsWhenProcessing())
            states.push_back({s->name(), s->displayGainDb()});
    if (comparingRequested_)
        states.push_back({"comparison", std::nullopt});
    return states;
}

std::size_t ProcessingChain::stagesLatency() const noexcept
{
    std::size_t n = 0;
    for (const auto& s : stages_)
        n += s->latencyFrames();
    return n;
}

std::size_t ProcessingChain::latencyFrames() const noexcept
{
    std::size_t n = stagesLatency();
    if (comparingNow_)
        n += comparison_->latencyFrames();
    return n;
}

void ProcessingChain::setReference(std::shared_ptr<ProcessingChain> reference)
{
    std::lock_guard lock(referenceMutex_);
    reference_ = std::move(reference);
}

void ProcessingChain::prepare(std::uint32_t sampleRate, std::uint16_t channels)
{
    for (const auto& s : stages_)
        s->prepare(sampleRate, channels);
    comparingNow_ = comparingRequested_;
    {
        std::lock_guard lock(referenceMutex_);
        referenceNow_ = comparingNow_ ? reference_ : nullptr;
    }
    if (comparingNow_)
    {
        std::size_t aLatency = 0;
        if (referenceNow_)
        {
            referenceNow_->trackStarted(lastTrack_); // the starting track came before prepare()
            referenceNow_->prepare(sampleRate, channels);
            aLatency = referenceNow_->latencyFrames();
        }
        const std::size_t bLatency = stagesLatency();
        // Side A is delayed by what side B takes longer.
        comparison_->prepare(sampleRate, channels, bLatency > aLatency ? bLatency - aLatency : 0);
    }
}

void ProcessingChain::process(std::span<double> samples, std::uint16_t channels) noexcept
{
    updateUpstreamGains();
    if (comparingNow_)
    {
        if (original_.size() < samples.size())
            original_.resize(samples.size()); // the first callback only: the period size is fixed
        std::copy(samples.begin(), samples.end(), original_.begin());
        if (referenceNow_)
            referenceNow_->process(std::span(original_).first(samples.size()), channels); // side A
    }
    for (const auto& s : stages_)
        if (!s->isIdentity() || s->runsWhenProcessing())
            s->process(samples, channels);
    if (comparingNow_)
        comparison_->process(std::span(original_).first(samples.size()), samples, channels);
}

void ProcessingChain::trackStarted(const TrackInfo& track) noexcept
{
    for (const auto& s : stages_)
        s->trackStarted(track);
    lastTrack_ = track;
    if (referenceNow_)
        referenceNow_->trackStarted(track);
}

ScopedFlushDenormals::ScopedFlushDenormals() noexcept : saved_(_mm_getcsr())
{
    _mm_setcsr(saved_ | kFlushToZero | kDenormalsAreZero);
}

ScopedFlushDenormals::~ScopedFlushDenormals()
{
    _mm_setcsr(saved_);
}

} // namespace ap::engine
