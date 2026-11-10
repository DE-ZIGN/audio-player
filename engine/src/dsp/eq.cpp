#include <ap/engine/dsp/eq.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <complex>
#include <fstream>
#include <format>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace ap::engine {

namespace {

constexpr double kMaxFrequencyRatio = 0.49;

bool isPassFilter(EqFilterType t)
{
    return t == EqFilterType::LowPass || t == EqFilterType::HighPass;
}

// A band that changes nothing (a peak or shelf at 0 dB, or switched off).
bool isNeutral(const EqBand& b)
{
    return !b.enabled || (!isPassFilter(b.type) && b.gainDb == 0.0);
}

double magnitude(const Biquad& c, double frequency, double sampleRate)
{
    const double w = 2.0 * std::numbers::pi * frequency / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    return std::abs((c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2));
}

} // namespace

std::optional<Biquad> designBiquad(const EqBand& band, double sampleRate)
{
    if (!(band.frequency > 0.0) || band.frequency >= kMaxFrequencyRatio * sampleRate || !(band.q > 0.0))
        return std::nullopt;
    const double w0 = 2.0 * std::numbers::pi * band.frequency / sampleRate;
    const double cs = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * band.q);
    const double a = std::pow(10.0, band.gainDb / 40.0);
    const double sa = 2.0 * std::sqrt(a) * alpha;

    double b0, b1, b2, a0, a1, a2;
    switch (band.type)
    {
        case EqFilterType::Peak:
            b0 = 1.0 + alpha * a;
            b1 = -2.0 * cs;
            b2 = 1.0 - alpha * a;
            a0 = 1.0 + alpha / a;
            a1 = -2.0 * cs;
            a2 = 1.0 - alpha / a;
            break;
        case EqFilterType::LowShelf:
            b0 = a * ((a + 1.0) - (a - 1.0) * cs + sa);
            b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cs);
            b2 = a * ((a + 1.0) - (a - 1.0) * cs - sa);
            a0 = (a + 1.0) + (a - 1.0) * cs + sa;
            a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cs);
            a2 = (a + 1.0) + (a - 1.0) * cs - sa;
            break;
        case EqFilterType::HighShelf:
            b0 = a * ((a + 1.0) + (a - 1.0) * cs + sa);
            b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cs);
            b2 = a * ((a + 1.0) + (a - 1.0) * cs - sa);
            a0 = (a + 1.0) - (a - 1.0) * cs + sa;
            a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cs);
            a2 = (a + 1.0) - (a - 1.0) * cs - sa;
            break;
        case EqFilterType::LowPass:
            b0 = (1.0 - cs) / 2.0;
            b1 = 1.0 - cs;
            b2 = (1.0 - cs) / 2.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cs;
            a2 = 1.0 - alpha;
            break;
        case EqFilterType::HighPass:
            b0 = (1.0 + cs) / 2.0;
            b1 = -(1.0 + cs);
            b2 = (1.0 + cs) / 2.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cs;
            a2 = 1.0 - alpha;
            break;
        default:
            return std::nullopt;
    }
    return Biquad{b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

double eqResponseDb(const std::vector<EqBand>& bands, double frequency, double sampleRate)
{
    double m = 1.0;
    for (const auto& b : bands)
        if (!isNeutral(b))
            if (const auto c = designBiquad(b, sampleRate))
                m *= magnitude(*c, frequency, sampleRate);
    return 20.0 * std::log10(m);
}

double eqAutoPreampDb(const std::vector<EqBand>& bands, double sampleRate)
{
    const double lo = 20.0;
    const double hi = kMaxFrequencyRatio * sampleRate;
    double maxDb = 0.0;
    constexpr int kPoints = 2000;
    for (int i = 0; i <= kPoints; ++i)
    {
        const double f = lo * std::pow(hi / lo, static_cast<double>(i) / kPoints);
        maxDb = std::max(maxDb, eqResponseDb(bands, f, sampleRate));
    }
    // Narrow peaks between grid points: also at each band's own frequency.
    for (const auto& b : bands)
        if (b.frequency >= lo && b.frequency <= hi)
            maxDb = std::max(maxDb, eqResponseDb(bands, b.frequency, sampleRate));
    return maxDb > 0.0 ? -maxDb : 0.0;
}

bool eqIsFlat(const std::vector<EqBand>& bands)
{
    return std::all_of(bands.begin(), bands.end(), isNeutral);
}

// --- presets ----------------------------------------------------------------

namespace {

std::string upper(std::string_view s)
{
    std::string r(s);
    for (auto& ch : r)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return r;
}

std::vector<std::string> words(std::string_view line)
{
    std::vector<std::string> w;
    std::string cur;
    for (char ch : line)
    {
        if (std::isspace(static_cast<unsigned char>(ch)))
        {
            if (!cur.empty())
                w.push_back(std::move(cur)), cur.clear();
        }
        else
            cur += ch;
    }
    if (!cur.empty())
        w.push_back(std::move(cur));
    return w;
}

std::optional<double> number(const std::string& s)
{
    double v = 0.0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size())
        return std::nullopt;
    return v;
}

std::optional<EqFilterType> filterType(const std::string& t)
{
    if (t == "PK" || t == "PEQ")
        return EqFilterType::Peak;
    if (t == "LS" || t == "LSC")
        return EqFilterType::LowShelf;
    if (t == "HS" || t == "HSC")
        return EqFilterType::HighShelf;
    if (t == "LP" || t == "LPQ")
        return EqFilterType::LowPass;
    if (t == "HP" || t == "HPQ")
        return EqFilterType::HighPass;
    return std::nullopt;
}

} // namespace

EqPreset parseEqPreset(std::string_view text)
{
    EqPreset preset;
    std::size_t lineNo = 0;
    std::size_t pos = 0;
    while (pos <= text.size())
    {
        const std::size_t end = std::min(text.find('\n', pos), text.size());
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (lineNo == 1 && line.starts_with("\xEF\xBB\xBF"))
            line.remove_prefix(3);
        const auto w = words(line);
        if (w.empty() || w[0].starts_with("#"))
            continue;
        const std::string head = upper(w[0]);

        if (head == "PREAMP:")
        {
            if (w.size() >= 2)
                if (const auto v = number(w[1]))
                {
                    preset.filePreampDb = *v;
                    continue;
                }
            preset.warnings.push_back(std::format("{} 行目: Preamp の値が読めません", lineNo));
            continue;
        }
        if (!head.starts_with("FILTER"))
        {
            preset.warnings.push_back(std::format("{} 行目: 読めない行なので飛ばしました", lineNo));
            continue;
        }
        // "Filter 1:" or "Filter:" then ON/OFF and the type.
        std::size_t i = 1;
        if (!head.ends_with(":"))
        {
            while (i < w.size() && !w[i].ends_with(":"))
                ++i;
            ++i;
        }
        if (i + 1 >= w.size())
        {
            preset.warnings.push_back(std::format("{} 行目: フィルターの書き方が読めません", lineNo));
            continue;
        }
        const std::string state = upper(w[i]);
        const std::string typeName = upper(w[i + 1]);
        if (state == "OFF")
            continue;
        const auto type = filterType(typeName);
        if (state != "ON" || !type)
        {
            preset.warnings.push_back(std::format("{} 行目: 対応していない種類（{}）なので飛ばしました", lineNo, w[i + 1]));
            continue;
        }
        EqBand band;
        band.type = *type;
        bool ok = true, hasFc = false;
        for (std::size_t k = i + 2; k + 1 < w.size(); ++k)
        {
            const std::string key = upper(w[k]);
            if (key == "FC" || key == "GAIN" || key == "Q")
            {
                const auto v = number(w[k + 1]);
                if (!v)
                {
                    ok = false;
                    break;
                }
                if (key == "FC")
                    band.frequency = *v, hasFc = true;
                else if (key == "GAIN")
                    band.gainDb = *v;
                else
                    band.q = *v;
                ++k;
            }
        }
        if (!ok || !hasFc)
        {
            preset.warnings.push_back(std::format("{} 行目: 周波数やゲインの値が読めません", lineNo));
            continue;
        }
        preset.bands.push_back(band);
    }
    return preset;
}

EqPreset loadEqPreset(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open " + path.string());
    std::stringstream ss;
    ss << in.rdbuf();
    return parseEqPreset(ss.str());
}

// --- stage ------------------------------------------------------------------

EqStage::Design EqStage::designFor(const std::vector<EqBand>& bands, double sampleRate, std::size_t* skipped) const
{
    Design d;
    std::size_t skip = 0;
    std::vector<EqBand> used;
    for (const auto& b : bands)
    {
        if (isNeutral(b))
            continue; // no section at all: nothing to run, no state
        const auto c = designBiquad(b, sampleRate);
        if (!c || d.count == kMaxBands)
        {
            ++skip;
            continue;
        }
        d.sections[d.count++] = *c;
        used.push_back(b);
    }
    d.preamp = std::pow(10.0, eqAutoPreampDb(used, sampleRate) / 20.0);
    if (skipped)
        *skipped = skip;
    return d;
}

void EqStage::setBands(std::vector<EqBand> bands)
{
    if (bands.size() > kMaxBands)
        bands.resize(kMaxBands);
    std::lock_guard lock(mutex_);
    bands_ = std::move(bands);
    flat_ = eqIsFlat(bands_);
    if (!flat_)
        settled_ = false;
    const double rate = sampleRate_;
    if (rate > 0.0)
    {
        std::size_t skipped = 0;
        pending_ = designFor(bands_, rate, &skipped);
        skipped_ = skipped;
        preampDb_ = 20.0 * std::log10(pending_.preamp);
        dirty_ = true;
    }
}

std::vector<EqBand> EqStage::bands() const
{
    std::lock_guard lock(mutex_);
    return bands_;
}

void EqStage::prepare(std::uint32_t sampleRate, std::uint16_t channels)
{
    {
        std::lock_guard lock(mutex_);
        sampleRate_ = sampleRate;
        std::size_t skipped = 0;
        pending_ = designFor(bands_, sampleRate, &skipped);
        skipped_ = skipped;
        preampDb_ = 20.0 * std::log10(pending_.preamp);
        current_.design = pending_;
        dirty_ = false;
        settled_ = pending_.count == 0;
    }
    channels_ = channels;
    current_.state.assign(static_cast<std::size_t>(channels) * kMaxBands * 4, 0.0);
    next_.state.assign(current_.state.size(), 0.0);
    fadeLeft_ = 0;
    fadeFrames_ = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(sampleRate * kCrossfadeSeconds)));
}

void EqStage::runBank(Bank& bank, double* frame, std::uint16_t channels) noexcept
{
    const auto& d = bank.design;
    for (std::uint16_t c = 0; c < channels; ++c)
    {
        double x = frame[c];
        double* st = bank.state.data() + static_cast<std::size_t>(c) * kMaxBands * 4;
        for (std::size_t k = 0; k < d.count; ++k, st += 4)
        {
            const auto& q = d.sections[k];
            const double y = q.b0 * x + q.b1 * st[0] + q.b2 * st[1] - q.a1 * st[2] - q.a2 * st[3];
            st[1] = st[0];
            st[0] = x;
            st[3] = st[2];
            st[2] = y;
            x = y;
        }
        frame[c] = x * d.preamp;
    }
}

void EqStage::process(std::span<double> samples, std::uint16_t channels) noexcept
{
    if (channels != channels_ || channels > 32)
        return;
    if (dirty_.load(std::memory_order_acquire) && mutex_.try_lock())
    {
        if (fadeLeft_ > 0)
        {
            // A change during a crossfade: continue from the newer settings.
            std::swap(current_, next_);
            fadeLeft_ = 0;
        }
        next_.design = pending_;
        dirty_ = false;
        mutex_.unlock();
        std::copy(current_.state.begin(), current_.state.end(), next_.state.begin());
        fadeLeft_ = fadeFrames_;
    }

    const std::size_t frames = samples.size() / channels;
    double* s = samples.data();
    std::array<double, 32> a{}, b{};
    for (std::size_t f = 0; f < frames; ++f, s += channels)
    {
        if (fadeLeft_ == 0)
        {
            runBank(current_, s, channels);
            continue;
        }
        std::copy(s, s + channels, a.begin());
        std::copy(s, s + channels, b.begin());
        runBank(current_, a.data(), channels);
        runBank(next_, b.data(), channels);
        const double t = 1.0 - static_cast<double>(fadeLeft_ - 1) / fadeFrames_;
        for (std::uint16_t c = 0; c < channels; ++c)
            s[c] = a[c] + (b[c] - a[c]) * t;
        if (--fadeLeft_ == 0)
            std::swap(current_, next_);
    }
    settled_ = fadeLeft_ == 0 && current_.design.count == 0 && current_.design.preamp == 1.0;
}

} // namespace ap::engine
