#include <ap/engine/dsp/loudness.h>

#include <ap/engine/dsp/sample_convert.h>

#include <ebur128.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ap::engine {

namespace {
ebur128_state* st(void* p)
{
    return static_cast<ebur128_state*>(p);
}

double finiteOrMinusInf(double lufs)
{
    return std::isfinite(lufs) ? lufs : -std::numeric_limits<double>::infinity();
}
} // namespace

LoudnessMeter::LoudnessMeter(std::uint32_t sampleRate, std::uint16_t channels)
{
    channels_ = channels;
    state_ = ebur128_init(channels, sampleRate, EBUR128_MODE_I);
    if (!state_)
        throw std::runtime_error("ebur128_init failed");
    if (channels == 1)
        ebur128_set_channel(st(state_), 0, EBUR128_DUAL_MONO);
}

LoudnessMeter::~LoudnessMeter()
{
    auto* s = st(state_);
    ebur128_destroy(&s);
}

void LoudnessMeter::process(std::span<const double> samples)
{
    const auto frames = samples.size() / channels_;
    if (frames > 0 && ebur128_add_frames_double(st(state_), samples.data(), frames) != EBUR128_SUCCESS)
        throw std::runtime_error("ebur128_add_frames_double failed");
}

double LoudnessMeter::integratedLufs() const
{
    double lufs = 0.0;
    if (ebur128_loudness_global(st(state_), &lufs) != EBUR128_SUCCESS)
        throw std::runtime_error("ebur128_loudness_global failed");
    return finiteOrMinusInf(lufs);
}

double LoudnessMeter::combinedLufs(std::span<const LoudnessMeter* const> meters)
{
    std::vector<ebur128_state*> states;
    for (const auto* m : meters)
        if (m)
            states.push_back(st(m->state_));
    if (states.empty())
        return -std::numeric_limits<double>::infinity();
    double lufs = 0.0;
    if (ebur128_loudness_global_multiple(states.data(), states.size(), &lufs) != EBUR128_SUCCESS)
        throw std::runtime_error("ebur128_loudness_global_multiple failed");
    return finiteOrMinusInf(lufs);
}

TrackAnalysis analyzeTrack(PcmSource& source)
{
    if (source.dop())
        throw std::invalid_argument("DoP is not PCM audio");
    const PcmFormat f = source.format();
    auto loudness = std::make_shared<LoudnessMeter>(f.sampleRate, f.channels);
    TruePeakMeter peaks(f.channels);

    constexpr std::size_t kChunk = 16384;
    std::vector<std::byte> bytes(kChunk * f.bytesPerFrame());
    std::vector<double> samples(kChunk * f.channels);
    TrackAnalysis a;
    while (const std::size_t n = source.read(bytes))
    {
        const auto block = std::span(samples).first(n * f.channels);
        dsp::toDouble(std::span(bytes).first(n * f.bytesPerFrame()), f, block);
        loudness->process(block);
        peaks.process(block);
        a.peaks.frames += n;
    }
    peaks.finish();
    for (std::uint16_t c = 0; c < f.channels; ++c)
    {
        a.peaks.samplePeak.push_back(peaks.samplePeak(c));
        a.peaks.truePeak.push_back(peaks.truePeak(c));
    }
    a.peaks.samplePeakMax = peaks.samplePeak();
    a.peaks.truePeakMax = peaks.truePeak();
    a.peaks.truePeakPosition = peaks.truePeakPosition();
    a.integratedLufs = loudness->integratedLufs();
    a.meter = std::move(loudness);
    return a;
}

AlbumAnalysis analyzeAlbum(std::span<const TrackAnalysis> tracks)
{
    std::vector<const LoudnessMeter*> meters;
    AlbumAnalysis album;
    for (const auto& t : tracks)
    {
        meters.push_back(t.meter.get());
        album.truePeak = std::max(album.truePeak, t.peaks.truePeakMax);
    }
    album.integratedLufs = LoudnessMeter::combinedLufs(meters);
    return album;
}

} // namespace ap::engine
