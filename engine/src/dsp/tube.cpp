#include <ap/engine/dsp/tube.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace ap::engine {

const char* tubeTypeKey(TubeType type) noexcept
{
    switch (type)
    {
        case TubeType::Se300B:   return "se300b";
        case TubeType::PpEl34:   return "ppel34";
        case TubeType::PpClassA: return "ppclassa";
    }
    return "se300b";
}

std::optional<TubeType> tubeTypeFrom(std::string_view key) noexcept
{
    for (const auto t : {TubeType::Se300B, TubeType::PpEl34, TubeType::PpClassA})
        if (key == tubeTypeKey(t))
            return t;
    return std::nullopt;
}

// --- the tube models ---------------------------------------------------------------------
//
// Norman Koren's equations ("Improved VT models for SPICE simulations"), with
// his published parameters for the 300B (triode) and the EL34 (pentode).

namespace {

double softplus(double a) noexcept
{
    return a > 40.0 ? a : std::log1p(std::exp(a));
}

struct Koren
{
    bool pentode;
    double mu, ex, kg1, kp, kvb;
    double screen = 0.0; // pentode: the screen grid's voltage

    // Plate current (A) at grid `vg` and plate `vp` (volts, from the cathode).
    double current(double vg, double vp) const noexcept
    {
        if (vp <= 0.0)
            return 0.0;
        double e1;
        if (pentode)
            e1 = screen / kp * softplus(kp * (1.0 / mu + vg / screen));
        else
            e1 = vp / kp * softplus(kp * (1.0 / mu + vg / std::sqrt(kvb + vp * vp)));
        if (e1 <= 0.0)
            return 0.0;
        const double i = 2.0 * std::pow(e1, ex) / kg1;
        return pentode ? i * std::atan(vp / kvb) : i;
    }
};

constexpr Koren k300B{false, 3.95, 1.4, 1550.0, 65.0, 300.0};
constexpr Koren kEl34{true, 11.0, 1.35, 650.0, 60.0, 24.0, 400.0};

// Grid current starts at 0 V: the swing beyond it is softly limited (the
// driver stage cannot push the grid much further).
constexpr double kGridLimitVolts = 2.0;

double limitGrid(double vg) noexcept
{
    return vg > 0.0 ? kGridLimitVolts * std::tanh(vg / kGridLimitVolts) : vg;
}

template <class F>
double bisect(F f, double lo, double hi) noexcept
{
    // f rises from lo to hi.
    for (int i = 0; i < 100; ++i)
    {
        const double mid = 0.5 * (lo + hi);
        (f(mid) > 0.0 ? hi : lo) = mid;
    }
    return 0.5 * (lo + hi);
}

} // namespace

// One type's circuit: the tube(s), the idle point and the load line.
struct TubeCurve::Circuit
{
    Koren tube;
    bool pushPull;
    double plateVolts;  // idle plate voltage
    double idleCurrent; // per tube (A)
    double load;        // SE: the plate load; PP: a quarter of the plate-to-plate load (each tube's share)
    double bias = 0.0;  // grid bias (negative volts), worked out from the idle point

    void findBias()
    {
        bias = bisect([this](double vg) { return tube.current(vg, plateVolts) - idleCurrent; }, -400.0, 0.0);
    }

    Point solve(double vin) const
    {
        if (!pushPull)
        {
            // The plate moves along the load line through the idle point.
            const double vg = limitGrid(bias + vin);
            const double maxI = idleCurrent + plateVolts / load;
            const double i = bisect(
                [&](double ip) { return ip - tube.current(vg, plateVolts - (ip - idleCurrent) * load); }, 0.0, maxI);
            return {(i - idleCurrent) * load, i};
        }
        // Push-pull: the grids swing in opposite phase; the output transformer
        // takes the difference of the plate currents.
        const double g1 = limitGrid(bias + vin), g2 = limitGrid(bias - vin);
        const double span = 0.98 * plateVolts;
        const double v = bisect(
            [&](double vo) {
                return vo - (tube.current(g1, plateVolts - vo) - tube.current(g2, plateVolts + vo)) * load;
            },
            -span, span);
        return {v, tube.current(g1, plateVolts - v) + tube.current(g2, plateVolts + v)};
    }
};

struct TubeCurveBuilder
{
    static constexpr std::size_t kPoints = 8193; // odd: 0 V is a point

    static TubeCurve build(TubeType type)
    {
        TubeCurve c;
        c.type_ = type;
        TubeCurve::Circuit k{};
        switch (type)
        {
            case TubeType::Se300B:
                // 350 V, 70 mA, 3.5 kohm primary; cathode (self) bias.
                k = {k300B, false, 350.0, 0.070, 3500.0};
                break;
            case TubeType::PpEl34:
                // 450 V, 30 mA a tube (class AB), 3.4 kohm plate to plate; fixed bias.
                k = {kEl34, true, 450.0, 0.030, 3400.0 / 4.0};
                break;
            case TubeType::PpClassA:
                // 400 V, 55 mA a tube (class A), 5 kohm plate to plate; shared cathode resistor.
                k = {kEl34, true, 400.0, 0.055, 5000.0 / 4.0};
                break;
        }
        k.findBias();
        c.gridToZero_ = -k.bias;
        c.range_ = 4.0 * c.gridToZero_;
        c.step_ = 2.0 * c.range_ / static_cast<double>(kPoints - 1);

        const double d = 1e-3 * c.gridToZero_;
        const auto up = k.solve(d), down = k.solve(-d), idle = k.solve(0.0);
        c.gain_ = (up.vout - down.vout) / (2.0 * d);
        c.currentSlope_ = (up.totalCurrent - down.totalCurrent) / (2.0 * d);
        c.idleCurrent_ = idle.totalCurrent;

        c.out_.resize(kPoints);
        c.current_.resize(kPoints);
        c.integral_.resize(kPoints);
        for (std::size_t i = 0; i < kPoints; ++i)
        {
            const double v = -c.range_ + c.step_ * static_cast<double>(i);
            const auto p = (i == kPoints / 2) ? idle : k.solve(v);
            c.out_[i] = p.vout - idle.vout;
            c.current_[i] = p.totalCurrent - c.idleCurrent_ - c.currentSlope_ * v;
        }
        c.out_[kPoints / 2] = 0.0;
        c.current_[kPoints / 2] = 0.0;
        c.integral_[0] = 0.0;
        for (std::size_t i = 1; i < kPoints; ++i)
            c.integral_[i] = c.integral_[i - 1] + 0.5 * c.step_ * (c.out_[i - 1] + c.out_[i]);

        switch (type)
        {
            case TubeType::Se300B:
                c.cathodeOhms = c.gridToZero_ / k.idleCurrent; // self bias: Vk = Ik Rk
                c.biasSeconds = 0.1;
                c.supplyOhms = 100.0;
                c.supplyVolts = k.plateVolts + c.gridToZero_ + 10.0;
                c.sagSeconds = 0.05;
                break;
            case TubeType::PpEl34:
                c.cathodeOhms = 0.0; // fixed bias
                c.supplyOhms = 50.0;
                c.supplyVolts = k.plateVolts + 10.0;
                c.sagSeconds = 0.05;
                break;
            case TubeType::PpClassA:
                // The pair's shared cathode resistor moves both grids together
                // (common mode), which this one-dimensional curve cannot show:
                // only the sag is used.
                c.cathodeOhms = 0.0;
                c.supplyOhms = 50.0;
                c.supplyVolts = k.plateVolts + c.gridToZero_ + 10.0;
                c.sagSeconds = 0.05;
                break;
        }
        c.circuit_ = std::make_shared<const TubeCurve::Circuit>(k);
        return c;
    }
};

const TubeCurve& TubeCurve::forType(TubeType type)
{
    static const std::array<TubeCurve, 3> curves{TubeCurveBuilder::build(TubeType::Se300B),
                                                 TubeCurveBuilder::build(TubeType::PpEl34),
                                                 TubeCurveBuilder::build(TubeType::PpClassA)};
    return curves[static_cast<std::size_t>(type)];
}

TubeCurve::Point TubeCurve::solve(double vin) const
{
    const auto p = circuit_->solve(vin), idle = circuit_->solve(0.0);
    return {p.vout - idle.vout, p.totalCurrent};
}

double TubeCurve::out(double v) const noexcept
{
    v = std::clamp(v, -range_, range_);
    const double x = (v + range_) / step_;
    const auto i = std::min(static_cast<std::size_t>(x), out_.size() - 2);
    const double t = x - static_cast<double>(i);
    return out_[i] + t * (out_[i + 1] - out_[i]);
}

double TubeCurve::antiderivative(double v) const noexcept
{
    if (v >= range_)
        return integral_.back() + out_.back() * (v - range_);
    if (v <= -range_)
        return out_.front() * (v + range_);
    const double x = (v + range_) / step_;
    const auto i = std::min(static_cast<std::size_t>(x), out_.size() - 2);
    const double t = x - static_cast<double>(i);
    return integral_[i] + step_ * (out_[i] * t + 0.5 * (out_[i + 1] - out_[i]) * t * t);
}

double TubeCurve::nonlinearCurrent(double v) const noexcept
{
    v = std::clamp(v, -range_, range_);
    const double x = (v + range_) / step_;
    const auto i = std::min(static_cast<std::size_t>(x), current_.size() - 2);
    const double t = x - static_cast<double>(i);
    return current_[i] + t * (current_[i + 1] - current_[i]);
}

// --- the stage ----------------------------------------------------------------------------

namespace {

double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

// A Kaiser-windowed sinc low-pass, `taps` long, cut at `cutoff` (cycles a sample).
std::vector<double> lowPass(std::size_t taps, double cutoff, double beta)
{
    std::vector<double> h(taps);
    const double m = static_cast<double>(taps - 1) / 2.0;
    double sum = 0.0;
    for (std::size_t n = 0; n < taps; ++n)
    {
        const double t = static_cast<double>(n) - m;
        const double sinc = t == 0.0 ? 2.0 * cutoff : std::sin(2.0 * std::numbers::pi * cutoff * t) / (std::numbers::pi * t);
        const double r = t / m;
        h[n] = sinc * besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / besselI0(beta);
        sum += h[n];
    }
    for (auto& v : h)
        v /= sum;
    return h;
}

constexpr double kKaiserBeta = 10.06; // about 100 dB
constexpr double kDcBlockHz = 2.0;

} // namespace

struct TubeStage::Impl
{
    struct TypeState
    {
        double prev = 0.0; // the last input, for ADAA
        double bias = 0.0; // the operating point's shift (volts)
        double sag = 0.0;  // the average nonlinear current (A)
    };
    struct Channel
    {
        std::vector<double> upHistory;  // the last inputs, newest last (twice, for a contiguous window)
        std::size_t upPos = 0;
        std::vector<double> downHistory; // the last oversampled outputs
        std::size_t downPos = 0;
        std::array<TypeState, 3> types{};
        double dcIn = 0.0, dcOut = 0.0;
    };

    unsigned factor = 1;
    std::uint32_t rate = 48000;
    std::vector<double> filter; // shared by the up and down sides
    std::size_t upTaps = 0;     // taps per phase
    std::vector<Channel> channels;
    bool adaa = true;

    double drive = 1.0;        // smoothed linear drive
    double smoothing = 0.0;    // per oversampled sample
    double biasCoef[3]{}, sagCoef[3]{};
    double dcCoef = 0.0;
    TubeType current = TubeType::Se300B;
    TubeType from = TubeType::Se300B;
    std::size_t fadeLeft = 0, fadeLength = 1;
    bool fresh = true; // nothing processed since prepare()
    std::vector<double> scratch; // the oversampled frame

    double shape(TubeType type, TypeState& s, double u, double drv) const noexcept
    {
        const auto& c = TubeCurve::forType(type);
        const double swing = c.gridToZero() * drv;
        const double limit = 0.999 * c.range();
        const double v = std::clamp(u * swing - s.bias, -limit, limit);
        double y;
        if (adaa)
        {
            const double d = v - s.prev;
            y = std::abs(d) > 1e-9 * c.range() ? (c.antiderivative(v) - c.antiderivative(s.prev)) / d
                                                : c.out(0.5 * (v + s.prev));
        }
        else
            y = c.out(v);
        s.prev = v;
        const auto k = static_cast<std::size_t>(type);
        const double nl = c.nonlinearCurrent(v);
        s.bias += (c.cathodeOhms * nl - s.bias) * biasCoef[k];
        s.sag += (nl - s.sag) * sagCoef[k];
        const double sagGain = 1.0 - c.supplyOhms * s.sag / c.supplyVolts;
        return y / (c.smallSignalGain() * swing) * sagGain;
    }
};

TubeStage::TubeStage() : impl_(std::make_unique<Impl>()) {}

TubeStage::~TubeStage() = default;

void TubeStage::setDriveDb(double db) noexcept
{
    driveDb_ = std::isfinite(db) ? std::clamp(db, kMinDriveDb, kMaxDriveDb) : kDefaultDriveDb;
}

void TubeStage::prepare(std::uint32_t sampleRate, std::uint16_t channels)
{
    auto& m = *impl_;
    m.rate = sampleRate;
    m.factor = sampleRate <= 50000 ? 4 : sampleRate <= 100000 ? 2 : 1;
    m.adaa = m.factor > 1; // above 100 kHz what would fold back lies above 90 kHz
    factor_ = m.factor;
    if (m.factor > 1)
    {
        // Passband to 0.45 fs, stopband from 0.55 fs (of the original rate).
        const std::size_t taps = m.factor == 4 ? 257 : 129;
        m.filter = lowPass(taps, 0.5 / m.factor, kKaiserBeta);
        m.upTaps = (taps + m.factor - 1) / m.factor;
        latency_ = (taps - 1) / m.factor; // (taps-1)/2 on each side, at the oversampled rate
    }
    else
    {
        m.filter.clear();
        m.upTaps = 0;
        latency_ = 0;
    }
    const double osRate = static_cast<double>(sampleRate) * m.factor;
    m.channels.assign(channels, {});
    for (auto& c : m.channels)
    {
        c.upHistory.assign(2 * std::max<std::size_t>(m.upTaps, 1), 0.0);
        c.downHistory.assign(2 * std::max<std::size_t>(m.filter.size(), 1), 0.0);
    }
    m.scratch.assign(m.factor, 0.0);
    m.smoothing = 1.0 - std::exp(-1.0 / (kSmoothingSeconds / 4.0 * osRate));
    for (const auto t : {TubeType::Se300B, TubeType::PpEl34, TubeType::PpClassA})
    {
        const auto& c = TubeCurve::forType(t);
        const auto k = static_cast<std::size_t>(t);
        m.biasCoef[k] = 1.0 - std::exp(-1.0 / (c.biasSeconds * osRate));
        m.sagCoef[k] = 1.0 - std::exp(-1.0 / (c.sagSeconds * osRate));
    }
    m.dcCoef = std::exp(-2.0 * std::numbers::pi * kDcBlockHz / sampleRate);
    m.drive = std::pow(10.0, driveDb_ / 20.0);
    m.current = m.from = playingType();
    m.fresh = true;
    m.fadeLeft = 0;
    m.fadeLength = std::max<std::size_t>(1, static_cast<std::size_t>(kSmoothingSeconds * osRate));
}

void TubeStage::process(std::span<double> samples, std::uint16_t channels) noexcept
{
    auto& m = *impl_;
    if (channels != m.channels.size() || channels == 0)
        return; // not prepared for this
    const std::size_t frames = samples.size() / channels;
    const unsigned L = m.factor;
    const double target = std::pow(10.0, driveDb_ / 20.0);
    const TubeType wanted = playingType();
    if (m.fresh)
    {
        // Nothing played since prepare(): start in the type that plays (the
        // session's first track may have its own), without a fade.
        m.fresh = false;
        m.current = m.from = wanted;
    }
    if (wanted != m.current && m.fadeLeft == 0)
    {
        // Crossfade from the old type to the new one; the new one starts at its idle point.
        m.from = m.current;
        m.current = wanted;
        m.fadeLeft = m.fadeLength;
        for (auto& c : m.channels)
        {
            auto& n = c.types[static_cast<std::size_t>(wanted)];
            n = Impl::TypeState{};
            n.prev = c.types[static_cast<std::size_t>(m.from)].prev;
        }
    }

    for (std::size_t f = 0; f < frames; ++f)
    {
        // Shared by the channels: the drive and the fade, one value per oversampled sample.
        const double driveStart = m.drive;
        const std::size_t fadeStart = m.fadeLeft;
        for (std::uint16_t ch = 0; ch < channels; ++ch)
        {
            auto& c = m.channels[ch];
            double& x = samples[f * channels + ch];
            double drv = driveStart;
            std::size_t fade = fadeStart;

            // Up: polyphase, the newest input at the end of the window.
            if (L > 1)
            {
                const std::size_t n = m.upTaps;
                c.upHistory[c.upPos] = c.upHistory[c.upPos + n] = x;
                c.upPos = (c.upPos + 1) % n;
                const double* w = c.upHistory.data() + c.upPos; // oldest .. newest
                for (unsigned p = 0; p < L; ++p)
                {
                    double acc = 0.0;
                    for (std::size_t k = 0; k < n; ++k)
                    {
                        const std::size_t tap = p + k * L;
                        if (tap < m.filter.size())
                            acc += m.filter[tap] * w[n - 1 - k];
                    }
                    m.scratch[p] = acc * L;
                }
            }
            else
                m.scratch[0] = x;

            // The curve, oversampled.
            for (unsigned p = 0; p < L; ++p)
            {
                drv += (target - drv) * m.smoothing;
                double y = m.shape(m.current, c.types[static_cast<std::size_t>(m.current)], m.scratch[p], drv);
                if (fade > 0)
                {
                    const double w = static_cast<double>(fade) / static_cast<double>(m.fadeLength); // 1 -> 0
                    const double g = 0.5 - 0.5 * std::cos(std::numbers::pi * w);
                    const double old = m.shape(m.from, c.types[static_cast<std::size_t>(m.from)], m.scratch[p], drv);
                    y = g * old + (1.0 - g) * y;
                    --fade;
                }
                m.scratch[p] = y;
            }

            // Down: keep the oversampled outputs, compute one in L - the one in
            // step with this frame's first oversampled sample, so the delay is
            // exactly half the filter on each side.
            double y = 0.0;
            if (L > 1)
            {
                const std::size_t n = m.filter.size();
                for (unsigned p = 0; p < L; ++p)
                {
                    c.downHistory[c.downPos] = c.downHistory[c.downPos + n] = m.scratch[p];
                    c.downPos = (c.downPos + 1) % n;
                    if (p == 0)
                    {
                        const double* w = c.downHistory.data() + c.downPos; // oldest .. newest
                        double acc = 0.0;
                        for (std::size_t k = 0; k < n; ++k)
                            acc += m.filter[k] * w[n - 1 - k];
                        y = acc;
                    }
                }
            }
            else
                y = m.scratch[0];

            // DC: the bias shift and the curve's asymmetry.
            const double out = y - c.dcIn + m.dcCoef * c.dcOut;
            c.dcIn = y;
            c.dcOut = out;
            x = out;

            if (ch + 1 == channels)
            {
                m.drive = drv;
                m.fadeLeft = fade;
            }
        }
    }
}

} // namespace ap::engine
