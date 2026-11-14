#include <ap/engine/listening_settings.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <sstream>

namespace ap::engine {

namespace {

const char* toKey(ListeningMode m)
{
    return m == ListeningMode::Processed ? "processed" : "original";
}

const char* toKey(ReplayGainMode m)
{
    switch (m)
    {
        case ReplayGainMode::Off:   return "off";
        case ReplayGainMode::Track: return "track";
        case ReplayGainMode::Album: return "album";
    }
    return "off";
}

const char* toKey(IspSafeMode m)
{
    switch (m)
    {
        case IspSafeMode::Off:      return "off";
        case IspSafeMode::Measured: return "measured";
        case IspSafeMode::Fixed:    return "fixed";
    }
    return "off";
}

const char* toKey(EqFilterType t)
{
    switch (t)
    {
        case EqFilterType::Peak:      return "peak";
        case EqFilterType::LowShelf:  return "lowshelf";
        case EqFilterType::HighShelf: return "highshelf";
        case EqFilterType::LowPass:   return "lowpass";
        case EqFilterType::HighPass:  return "highpass";
        case EqFilterType::BandPass:  return "bandpass";
        case EqFilterType::Notch:     return "notch";
    }
    return "peak";
}

std::optional<ReplayGainMode> replayGainFrom(std::string_view v)
{
    if (v == "off")
        return ReplayGainMode::Off;
    if (v == "track")
        return ReplayGainMode::Track;
    if (v == "album")
        return ReplayGainMode::Album;
    return std::nullopt;
}

std::optional<IspSafeMode> ispSafeFrom(std::string_view v)
{
    if (v == "off")
        return IspSafeMode::Off;
    if (v == "measured")
        return IspSafeMode::Measured;
    if (v == "fixed")
        return IspSafeMode::Fixed;
    return std::nullopt;
}

std::optional<EqFilterType> filterFrom(std::string_view v)
{
    for (const auto t : {EqFilterType::Peak, EqFilterType::LowShelf, EqFilterType::HighShelf, EqFilterType::LowPass,
                         EqFilterType::HighPass, EqFilterType::BandPass, EqFilterType::Notch})
        if (v == toKey(t))
            return t;
    return std::nullopt;
}

// Shortest text that reads back as the same double, independent of the locale.
std::string number(double x)
{
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof buf, x);
    return std::string(buf, r.ptr);
}

std::optional<double> parseNumber(std::string_view s)
{
    double x = 0.0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), x);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || !std::isfinite(x))
        return std::nullopt;
    return x;
}

// A value on one line: line breaks become spaces.
std::string oneLine(std::string s)
{
    for (auto& c : s)
        if (c == '\r' || c == '\n')
            c = ' ';
    return s;
}

std::vector<std::string_view> words(std::string_view s)
{
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < s.size())
    {
        while (i < s.size() && s[i] == ' ')
            ++i;
        const auto start = i;
        while (i < s.size() && s[i] != ' ')
            ++i;
        if (i > start)
            out.push_back(s.substr(start, i - start));
    }
    return out;
}

// "on peak 1000 -3.5 0.7071"
std::optional<EqBand> bandFrom(std::string_view v)
{
    const auto w = words(v);
    if (w.size() != 5 || (w[0] != "on" && w[0] != "off"))
        return std::nullopt;
    const auto type = filterFrom(w[1]);
    const auto f = parseNumber(w[2]);
    const auto g = parseNumber(w[3]);
    const auto q = parseNumber(w[4]);
    if (!type || !f || !g || !q)
        return std::nullopt;
    return EqBand{*type, *f, *g, *q, w[0] == "on"};
}

std::string bandText(const EqBand& b)
{
    std::ostringstream out;
    out << (b.enabled ? "on" : "off") << ' ' << toKey(b.type) << ' ' << number(b.frequency) << ' ' << number(b.gainDb)
        << ' ' << number(b.q);
    return out.str();
}

} // namespace

NamedEqPreset eqPresetFrom(const ListeningSettings& l, std::string name)
{
    return NamedEqPreset{std::move(name), l.eqBands, l.eqAutoPreamp, l.eqManualPreampDb};
}

void applyEqPreset(ListeningSettings& l, const NamedEqPreset& p)
{
    l.eqBands = p.bands;
    if (l.eqBands.size() > EqStage::kMaxBands)
        l.eqBands.resize(EqStage::kMaxBands);
    l.eqAutoPreamp = p.autoPreamp;
    l.eqManualPreampDb = std::min(0.0, p.manualPreampDb);
}

std::string settingsToText(const AppSettings& s)
{
    std::ostringstream out;
    out << "# AudioPlayer settings (task 6-3). key=value, one per line.\n";
    out << "output.kind=" << (s.output.kind == OutputKind::Asio ? "asio" : "wasapi") << '\n';
    out << "output.id=" << oneLine(s.output.id) << '\n';
    out << "output.name=" << oneLine(s.output.name) << '\n';
    out << "output.dop=" << (s.output.allowDop ? 1 : 0) << '\n';
    const auto& l = s.listening;
    out << "mode=" << toKey(l.mode) << '\n';
    out << "replaygain=" << toKey(l.replayGain) << '\n';
    out << "isp.original=" << toKey(l.ispSafeOriginal) << '\n';
    out << "isp.processed=" << toKey(l.ispSafeProcessed) << '\n';
    out << "eq.enabled=" << (l.eqEnabled ? 1 : 0) << '\n';
    out << "eq.autopreamp=" << (l.eqAutoPreamp ? 1 : 0) << '\n';
    out << "eq.preamp=" << number(l.eqManualPreampDb) << '\n';
    for (const auto& b : l.eqBands)
        out << "eq.band=" << bandText(b) << '\n';
    out << "tone.bass=" << number(l.toneBassDb) << '\n';
    out << "tone.treble=" << number(l.toneTrebleDb) << '\n';
    out << "tube.enabled=" << (l.tubeEnabled ? 1 : 0) << '\n';
    out << "tube.type=" << tubeTypeKey(l.tubeType) << '\n';
    out << "tube.drive=" << number(l.tubeDriveDb) << '\n';
    out << "crossfeed.enabled=" << (l.crossfeedEnabled ? 1 : 0) << '\n';
    out << "crossfeed.level=" << crossfeedLevelKey(l.crossfeedLevel) << '\n';
    // EQ presets (task 6-9): each starts with its name.
    for (const auto& p : s.eqPresets)
    {
        out << "eqpreset.name=" << oneLine(p.name) << '\n';
        out << "eqpreset.autopreamp=" << (p.autoPreamp ? 1 : 0) << '\n';
        out << "eqpreset.preamp=" << number(p.manualPreampDb) << '\n';
        for (const auto& b : p.bands)
            out << "eqpreset.band=" << bandText(b) << '\n';
    }
    return out.str();
}

AppSettings settingsFromText(std::string_view text)
{
    AppSettings s;
    auto& l = s.listening;
    NamedEqPreset* preset = nullptr; // the preset the eqpreset.* lines belong to
    while (!text.empty())
    {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        const auto eq = line.find('=');
        if (line.empty() || line.front() == '#' || eq == std::string_view::npos)
            continue;
        const auto key = line.substr(0, eq);
        const auto value = line.substr(eq + 1);

        if (key == "output.kind")
        {
            if (value == "asio")
                s.output.kind = OutputKind::Asio;
            else if (value == "wasapi")
                s.output.kind = OutputKind::Wasapi;
        }
        else if (key == "output.id")
            s.output.id = std::string(value);
        else if (key == "output.name")
            s.output.name = std::string(value);
        else if (key == "output.dop")
            s.output.allowDop = value == "1";
        else if (key == "mode")
        {
            if (value == "processed")
                l.mode = ListeningMode::Processed;
            else if (value == "original")
                l.mode = ListeningMode::Original;
        }
        else if (key == "replaygain")
            l.replayGain = replayGainFrom(value).value_or(ReplayGainMode::Off);
        else if (key == "isp.original")
            l.ispSafeOriginal = ispSafeFrom(value).value_or(l.ispSafeOriginal);
        else if (key == "isp.processed")
            l.ispSafeProcessed = ispSafeFrom(value).value_or(l.ispSafeProcessed);
        else if (key == "eq.enabled")
            l.eqEnabled = value != "0";
        else if (key == "eq.autopreamp")
            l.eqAutoPreamp = value != "0";
        else if (key == "eq.preamp")
            l.eqManualPreampDb = std::min(0.0, parseNumber(value).value_or(0.0));
        else if (key == "eq.band" && l.eqBands.size() < EqStage::kMaxBands)
        {
            if (const auto b = bandFrom(value))
                l.eqBands.push_back(*b);
        }
        else if (key == "tone.bass")
            l.toneBassDb = std::clamp(parseNumber(value).value_or(0.0), -ToneStage::kMaxDb, ToneStage::kMaxDb);
        else if (key == "tone.treble")
            l.toneTrebleDb = std::clamp(parseNumber(value).value_or(0.0), -ToneStage::kMaxDb, ToneStage::kMaxDb);
        else if (key == "tube.enabled")
            l.tubeEnabled = value == "1";
        else if (key == "tube.type")
            l.tubeType = tubeTypeFrom(value).value_or(TubeType::Se300B);
        else if (key == "tube.drive")
            l.tubeDriveDb = std::clamp(parseNumber(value).value_or(TubeStage::kDefaultDriveDb), TubeStage::kMinDriveDb,
                                       TubeStage::kMaxDriveDb);
        else if (key == "crossfeed.enabled")
            l.crossfeedEnabled = value == "1";
        else if (key == "crossfeed.level")
            l.crossfeedLevel = crossfeedLevelFrom(value).value_or(CrossfeedLevel::Default);
        else if (key == "eqpreset.name")
        {
            preset = nullptr;
            if (!value.empty() && s.eqPresets.size() < kMaxEqPresets)
                preset = &s.eqPresets.emplace_back(NamedEqPreset{std::string(value)});
        }
        else if (preset && key == "eqpreset.autopreamp")
            preset->autoPreamp = value != "0";
        else if (preset && key == "eqpreset.preamp")
            preset->manualPreampDb = std::min(0.0, parseNumber(value).value_or(0.0));
        else if (preset && key == "eqpreset.band" && preset->bands.size() < EqStage::kMaxBands)
        {
            if (const auto b = bandFrom(value))
                preset->bands.push_back(*b);
        }
    }
    return s;
}

ListeningChain::ListeningChain()
    : chain_(std::make_shared<ProcessingChain>(
          std::vector<std::shared_ptr<ProcessingStage>>{replayGain, eq, tone, tube, crossfeed, volume, ispSafe, requantize, limiter}))
{
}

void ListeningChain::apply(const ListeningSettings& s)
{
    settings_ = s;
    const bool processed = s.mode == ListeningMode::Processed;
    const auto rg = processed ? s.replayGain : ReplayGainMode::Off;
    replayGain->setMode(rg);
    applyEq();
    ispSafe->setUseAlbumPeak(rg == ReplayGainMode::Album);
    ispSafe->setMode(processed ? s.ispSafeProcessed : s.ispSafeOriginal);
    tone->setBassDb(processed ? s.toneBassDb : 0.0);
    tone->setTrebleDb(processed ? s.toneTrebleDb : 0.0);
    tube->setType(s.tubeType);
    tube->setDriveDb(s.tubeDriveDb);
    tube->setEnabled(processed && s.tubeEnabled);
    crossfeed->setLevel(s.crossfeedLevel);
    crossfeed->setEnabled(processed && s.crossfeedEnabled);
}

void ListeningChain::setEqSolo(std::optional<std::size_t> band)
{
    solo_ = band;
    applyEq();
}

void ListeningChain::applyEq()
{
    const auto& s = settings_;
    const bool on = s.mode == ListeningMode::Processed && s.eqEnabled;
    std::vector<EqBand> bands;
    std::optional<double> preamp;
    if (on && solo_ && *solo_ < s.eqBands.size())
    {
        const auto& b = s.eqBands[*solo_];
        bands = {EqBand{EqFilterType::BandPass, b.frequency, 0.0, b.q, true}};
    }
    else if (on)
    {
        bands = s.eqBands;
        if (bands.size() > EqStage::kMaxBands)
            bands.resize(EqStage::kMaxBands);
        if (!s.eqAutoPreamp)
            preamp = std::min(0.0, s.eqManualPreampDb);
    }
    eq->setManualPreamp(preamp);
    if (bands != eq->bands()) // the same bands again would start a needless crossfade
        eq->setBands(std::move(bands));
}

} // namespace ap::engine
