#include <ap/engine/dsp/room_ir.h>

#include <ap/engine/audio_file.h>
#include <ap/engine/dsp/eq.h>
#include <ap/engine/dsp/sample_convert.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <stdexcept>

namespace ap::engine {

namespace {

constexpr double kSpeedOfSound = 343.0; // m/s at about 20 degrees C

// The deterministic noise: mt19937's output is fixed by the standard; the
// Gaussian is made here (Box-Muller) so it does not depend on the library.
class Noise
{
public:
    explicit Noise(std::uint32_t seed) : state_(seed) {}
    double gaussian()
    {
        if (spare_)
        {
            spare_ = false;
            return saved_;
        }
        const double u1 = (static_cast<double>(next()) + 0.5) / 4294967296.0;
        const double u2 = (static_cast<double>(next()) + 0.5) / 4294967296.0;
        const double r = std::sqrt(-2.0 * std::log(u1));
        saved_ = r * std::sin(2.0 * std::numbers::pi * u2);
        spare_ = true;
        return r * std::cos(2.0 * std::numbers::pi * u2);
    }

private:
    // xorshift-multiply (splitmix32-like): small, fixed, portable.
    std::uint32_t next()
    {
        std::uint32_t z = (state_ += 0x9E3779B9u);
        z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
        z = (z ^ (z >> 13)) * 0xC2B2AE35u;
        return z ^ (z >> 16);
    }
    std::uint32_t state_;
    bool spare_ = false;
    double saved_ = 0.0;
};

struct RoomPlace
{
    double sx, sy, sz; // source
    double lx, ly, lz; // listener (between the two receivers)
};

RoomPlace roomPlace(SpaceRoom room) noexcept
{
    switch (room)
    {
        case SpaceRoom::Club:  return {4.5, 2.5, 1.5, 4.5, 7.0, 1.2};
        case SpaceRoom::Hall:  return {12.5, 5.0, 1.5, 12.5, 20.0, 1.2};
        case SpaceRoom::Arena: return {40.0, 15.0, 3.0, 40.0, 45.0, 2.0};
        case SpaceRoom::Off:   break;
    }
    return {};
}

// Filters x in place with a biquad (transposed direct form II).
void runBiquad(std::vector<double>& x, const Biquad& q)
{
    double z1 = 0.0, z2 = 0.0;
    for (auto& v : x)
    {
        const double y = q.b0 * v + z1;
        z1 = q.b1 * v - q.a1 * y + z2;
        z2 = q.b2 * v - q.a2 * y;
        v = y;
    }
}

// Linkwitz-Riley (two Butterworth sections) low- or high-pass; nothing if the
// frequency cannot be realised at this rate.
void linkwitzRiley(std::vector<double>& x, EqFilterType type, double f, double rate)
{
    const auto q = designBiquad(EqBand{type, f, 0.0, std::numbers::sqrt2 / 2.0, true}, rate);
    if (!q)
        return;
    runBiquad(x, *q);
    runBiquad(x, *q);
}

double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 60; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < sum * 1e-17)
            break;
    }
    return sum;
}

} // namespace

const char* spaceRoomKey(SpaceRoom room) noexcept
{
    switch (room)
    {
        case SpaceRoom::Off:   return "off";
        case SpaceRoom::Club:  return "club";
        case SpaceRoom::Hall:  return "hall";
        case SpaceRoom::Arena: return "arena";
    }
    return "off";
}

std::optional<SpaceRoom> spaceRoomFrom(std::string_view key) noexcept
{
    for (const auto r : {SpaceRoom::Off, SpaceRoom::Club, SpaceRoom::Hall, SpaceRoom::Arena})
        if (key == spaceRoomKey(r))
            return r;
    return std::nullopt;
}

RoomDesign roomDesign(SpaceRoom room) noexcept
{
    switch (room)
    {
        // width (left-right), length (front-back), height
        case SpaceRoom::Club:  return {9.0, 14.0, 3.5, {0.9, 0.75, 0.6, 0.4}, 0.025, 1.5};
        case SpaceRoom::Hall:  return {25.0, 40.0, 16.0, {2.4, 2.0, 1.6, 1.0}, 0.080, 3.0};
        case SpaceRoom::Arena: return {80.0, 110.0, 30.0, {3.5, 3.0, 2.2, 1.3}, 0.150, 4.5};
        case SpaceRoom::Off:   break;
    }
    return {1, 1, 1, {0.1, 0.1, 0.1, 0.1}, 0.01, 0.1};
}

ImpulseResponse synthesizeRoom(SpaceRoom room, double rate)
{
    if (room == SpaceRoom::Off)
        throw std::invalid_argument("synthesizeRoom: no room");
    const RoomDesign d = roomDesign(room);
    const RoomPlace p = roomPlace(room);
    const std::size_t frames = static_cast<std::size_t>(std::llround(d.seconds * rate));
    const double mix = d.mixingSeconds;

    // The walls' pressure reflection coefficient from the mid-band RT60 (Eyring).
    const double volume = d.length * d.width * d.height;
    const double surface = 2.0 * (d.length * d.width + d.length * d.height + d.width * d.height);
    const double rtMid = 0.5 * (d.rt60[1] + d.rt60[2]);
    const double alpha = 1.0 - std::exp(-0.161 * volume / (surface * rtMid));
    const double reflect = std::sqrt(1.0 - alpha);

    ImpulseResponse ir;
    ir.sampleRate = rate;
    const double ear = 0.09; // m from the centre: receivers 18 cm apart
    const double d0 = std::hypot(p.sx - p.lx, p.sy - p.ly, p.sz - p.lz);
    const double earlyEnd = 1.5 * mix; // the reflections fade out from mix to here
    for (int ch = 0; ch < 2; ++ch)
    {
        std::vector<double> h(frames, 0.0);
        const double rx = p.lx + (ch == 0 ? -ear : ear);

        // Early reflections (image sources), time 0 = the direct sound's arrival.
        const double maxPath = d0 + earlyEnd * kSpeedOfSound;
        auto range = [&](double size) { return static_cast<int>(std::ceil(maxPath / (2.0 * size))) + 1; };
        const int nx = range(d.width), ny = range(d.length), nz = range(d.height);
        for (int ix = -nx; ix <= nx; ++ix)
            for (int px = 0; px < 2; ++px)
                for (int iy = -ny; iy <= ny; ++iy)
                    for (int py = 0; py < 2; ++py)
                        for (int iz = -nz; iz <= nz; ++iz)
                            for (int pz = 0; pz < 2; ++pz)
                            {
                                const int order = std::abs(2 * ix - px) + std::abs(2 * iy - py) + std::abs(2 * iz - pz);
                                if (order == 0)
                                    continue; // the direct sound
                                const double x = (1 - 2 * px) * p.sx + 2.0 * ix * d.width;
                                const double y = (1 - 2 * py) * p.sy + 2.0 * iy * d.length;
                                const double z = (1 - 2 * pz) * p.sz + 2.0 * iz * d.height;
                                const double dist = std::hypot(x - rx, y - p.ly, z - p.lz);
                                const double t = (dist - d0) / kSpeedOfSound;
                                if (t < 0.0 || t >= earlyEnd)
                                    continue;
                                double a = std::pow(reflect, order) * d0 / dist;
                                if (t > mix)
                                    a *= 0.5 + 0.5 * std::cos(std::numbers::pi * (t - mix) / (earlyEnd - mix));
                                const double pos = t * rate;
                                const auto i = static_cast<std::size_t>(pos);
                                const double frac = pos - static_cast<double>(i);
                                if (i + 1 < frames)
                                {
                                    h[i] += a * (1.0 - frac);
                                    h[i + 1] += a * frac;
                                }
                            }

        // The early part's power just before the mixing time sets the tail's level.
        const auto from = static_cast<std::size_t>(0.5 * mix * rate);
        const auto to = std::min(frames, static_cast<std::size_t>(mix * rate));
        double energy = 0.0;
        for (std::size_t i = from; i < to; ++i)
            energy += h[i] * h[i];
        const double power = energy / static_cast<double>(std::max<std::size_t>(1, to - from));

        // Late reverberation: four bands of noise, each with its decay.
        Noise noise(0x51A7u + 977u * static_cast<std::uint32_t>(room) + 131u * static_cast<std::uint32_t>(ch));
        std::vector<double> white(frames);
        for (auto& v : white)
            v = noise.gaussian();
        std::array<std::vector<double>, 4> band;
        band[0] = white;
        linkwitzRiley(band[0], EqFilterType::LowPass, 250.0, rate);
        std::vector<double> rest = white;
        linkwitzRiley(rest, EqFilterType::HighPass, 250.0, rate);
        band[1] = rest;
        linkwitzRiley(band[1], EqFilterType::LowPass, 2000.0, rate);
        linkwitzRiley(rest, EqFilterType::HighPass, 2000.0, rate);
        band[2] = rest;
        linkwitzRiley(band[2], EqFilterType::LowPass, 6000.0, rate);
        band[3] = rest;
        linkwitzRiley(band[3], EqFilterType::HighPass, 6000.0, rate);

        const double gain = std::sqrt(power);
        const double fadeFrom = 0.5 * mix;
        for (std::size_t i = 0; i < frames; ++i)
        {
            const double t = static_cast<double>(i) / rate;
            if (t < fadeFrom)
                continue;
            double v = 0.0;
            for (int b = 0; b < 4; ++b)
                v += band[b][i] * std::exp(-6.907755278982137 * (t - mix) / d.rt60[b]); // -60 dB at RT60
            const double in = t < mix ? 0.5 - 0.5 * std::cos(std::numbers::pi * (t - fadeFrom) / (mix - fadeFrom)) : 1.0;
            h[i] += gain * in * v;
        }

        // The last 5 %: a fade-out, so the cut does not click.
        const std::size_t fade = frames / 20;
        for (std::size_t i = 0; i < fade; ++i)
            h[frames - 1 - i] *= 0.5 - 0.5 * std::cos(std::numbers::pi * static_cast<double>(i) / static_cast<double>(fade));
        ir.channels.push_back(std::move(h));
    }
    return ir;
}

std::vector<double> resampleIr(const std::vector<double>& x, double fromRate, double toRate)
{
    if (fromRate == toRate || x.empty())
        return x;
    const double ratio = toRate / fromRate;
    const std::size_t outFrames = static_cast<std::size_t>(std::ceil(static_cast<double>(x.size()) * ratio - 1e-9));
    const double cutoff = 0.5 * std::min(fromRate, toRate) * 0.95; // Hz
    const double fc = cutoff / fromRate;                            // cycles per input sample
    constexpr double kZeroCrossings = 32.0;
    const double halfWidth = kZeroCrossings / (2.0 * fc); // input samples
    constexpr double kBeta = 9.0;
    const double i0Beta = besselI0(kBeta);
    // The window, tabulated over |r| <= 1 (linear interpolation between points).
    constexpr std::size_t kTable = 4096;
    std::vector<double> window(kTable + 2);
    for (std::size_t i = 0; i <= kTable + 1; ++i)
    {
        const double r = std::min(1.0, static_cast<double>(i) / kTable);
        window[i] = besselI0(kBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0Beta;
    }
    std::vector<double> y(outFrames, 0.0);
    for (std::size_t n = 0; n < outFrames; ++n)
    {
        const double u = static_cast<double>(n) / ratio; // position in input samples
        const auto first = static_cast<std::ptrdiff_t>(std::ceil(u - halfWidth));
        const auto last = static_cast<std::ptrdiff_t>(std::floor(u + halfWidth));
        double s = 0.0;
        for (std::ptrdiff_t k = std::max<std::ptrdiff_t>(0, first);
             k <= last && k < static_cast<std::ptrdiff_t>(x.size()); ++k)
        {
            const double tau = u - static_cast<double>(k);
            const double r = std::min(1.0, std::abs(tau) / halfWidth) * kTable;
            const auto ri = static_cast<std::size_t>(r);
            const double w = window[ri] + (window[ri + 1] - window[ri]) * (r - static_cast<double>(ri));
            const double arg = 2.0 * fc * tau;
            const double sinc = std::abs(arg) < 1e-12 ? 1.0 : std::sin(std::numbers::pi * arg) / (std::numbers::pi * arg);
            s += x[static_cast<std::size_t>(k)] * 2.0 * fc * sinc * w;
        }
        y[n] = s;
    }
    return y;
}

LoadedIr loadImpulseResponse(const std::filesystem::path& path)
{
    std::unique_ptr<PcmSource> source;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        throw std::runtime_error("ファイルが見つかりません（移動したか、消したのかもしれません）");
    try
    {
        source = openAudioFile(path);
    }
    catch (const std::exception&)
    {
        throw std::runtime_error("このファイルは IR として読めません（WAV・FLAC・AIFF などの PCM のファイルを選んでください）");
    }
    if (source->dop())
        throw std::runtime_error("DSD のファイルは IR に使えません（PCM のファイルを選んでください）");
    const PcmFormat format = source->format();
    if (format.channels == 0 || format.sampleRate == 0)
        throw std::runtime_error("このファイルは IR として読めません");
    constexpr double kMaxFileSeconds = 60.0;
    if (static_cast<double>(source->totalFrames()) > kMaxFileSeconds * format.sampleRate)
        throw std::runtime_error("IR のファイルが長すぎます（60 秒まで）");

    const std::size_t channels = format.channels;
    std::vector<std::byte> bytes(static_cast<std::size_t>(format.bytesPerFrame()) * 8192);
    std::vector<double> samples;
    std::vector<double> block(channels * 8192);
    for (;;)
    {
        const std::size_t n = source->read(bytes);
        if (n == 0)
            break;
        dsp::toDouble(std::span(bytes).first(n * format.bytesPerFrame()), format, std::span(block).first(n * channels));
        samples.insert(samples.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(n * channels));
    }
    const std::size_t frames = samples.size() / channels;
    const std::size_t used = std::min<std::size_t>(channels, 2);

    LoadedIr out;
    out.fileChannels = format.channels;
    out.ir.sampleRate = format.sampleRate;
    double peak = 0.0;
    for (const double v : samples)
        if (std::isfinite(v))
            peak = std::max(peak, std::abs(v));
    if (frames == 0 || peak <= 0.0)
        throw std::runtime_error("IR のファイルに音が入っていません");

    // The first arrival: the first sample above -80 dB of the peak, in any channel used.
    std::size_t start = frames;
    for (std::size_t f = 0; f < frames && start == frames; ++f)
        for (std::size_t c = 0; c < used; ++c)
            if (std::abs(samples[f * channels + c]) > peak * 1e-4)
            {
                start = f;
                break;
            }
    out.trimmedFrames = start;
    const auto maxFrames = static_cast<std::size_t>(kMaxIrSeconds * format.sampleRate);
    std::size_t length = frames - start;
    if (length > maxFrames)
    {
        length = maxFrames;
        out.truncated = true;
    }
    for (std::size_t c = 0; c < used; ++c)
    {
        std::vector<double> h(length);
        for (std::size_t i = 0; i < length; ++i)
        {
            const double v = samples[(start + i) * channels + c];
            h[i] = std::isfinite(v) ? v : 0.0;
        }
        if (out.truncated)
        {
            const auto fade = std::min(length, static_cast<std::size_t>(0.010 * format.sampleRate));
            for (std::size_t i = 0; i < fade; ++i)
                h[length - 1 - i] *= static_cast<double>(i) / static_cast<double>(fade);
        }
        out.ir.channels.push_back(std::move(h));
    }
    return out;
}

} // namespace ap::engine
