#include "EqVar.h"

#include <algorithm>
#include <cmath>

namespace ap::app {

using namespace ap::engine;

const char* filterName(EqFilterType t)
{
    switch (t)
    {
        case EqFilterType::Peak:      return "peak";
        case EqFilterType::LowShelf:  return "lowShelf";
        case EqFilterType::HighShelf: return "highShelf";
        case EqFilterType::LowPass:   return "lowPass";
        case EqFilterType::HighPass:  return "highPass";
        case EqFilterType::BandPass:  return "bandPass";
        case EqFilterType::Notch:     return "notch";
    }
    return "peak";
}

std::optional<EqFilterType> filterFrom(const juce::String& s)
{
    for (const auto t : {EqFilterType::Peak, EqFilterType::LowShelf, EqFilterType::HighShelf, EqFilterType::LowPass,
                         EqFilterType::HighPass, EqFilterType::BandPass, EqFilterType::Notch})
        if (s == filterName(t))
            return t;
    return std::nullopt;
}

juce::var bandsVar(const std::vector<EqBand>& bands)
{
    juce::Array<juce::var> a;
    for (const auto& b : bands)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("type", filterName(b.type));
        o->setProperty("frequency", b.frequency);
        o->setProperty("gainDb", b.gainDb);
        o->setProperty("q", b.q);
        o->setProperty("enabled", b.enabled);
        a.add(juce::var(o));
    }
    return a;
}

std::vector<EqBand> bandsFrom(const juce::var& v)
{
    std::vector<EqBand> out;
    if (const auto* a = v.getArray())
        for (const auto& b : *a)
        {
            const auto type = filterFrom(b.getProperty("type", "").toString());
            const double f = b.getProperty("frequency", 0.0);
            const double g = b.getProperty("gainDb", 0.0);
            const double q = b.getProperty("q", 0.0);
            if (!type || !std::isfinite(f) || !std::isfinite(g) || !std::isfinite(q) || f <= 0.0 || q <= 0.0)
                continue;
            out.push_back(EqBand{*type, f, g, q, static_cast<bool>(b.getProperty("enabled", true))});
            if (out.size() == EqStage::kMaxBands)
                break;
        }
    return out;
}

juce::var presetVar(const NamedEqPreset& p)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("name", juce::String::fromUTF8(p.name.c_str()));
    o->setProperty("bands", bandsVar(p.bands));
    o->setProperty("autoPreamp", p.autoPreamp);
    o->setProperty("manualPreampDb", p.manualPreampDb);
    return juce::var(o);
}

juce::var presetsVar(const std::vector<NamedEqPreset>& presets)
{
    juce::Array<juce::var> a;
    for (const auto& p : presets)
        a.add(presetVar(p));
    return a;
}

std::optional<NamedEqPreset> presetFrom(const juce::var& v)
{
    auto name = v.getProperty("name", "").toString().replaceCharacters("\r\n\t", "   ").trim();
    if (name.isEmpty())
        return std::nullopt;
    if (name.length() > kMaxPresetName)
        name = name.substring(0, kMaxPresetName).trimEnd();
    NamedEqPreset p;
    p.name = name.toStdString();
    p.bands = bandsFrom(v.getProperty("bands", {}));
    p.autoPreamp = static_cast<bool>(v.getProperty("autoPreamp", true));
    const double preamp = v.getProperty("manualPreampDb", 0.0);
    p.manualPreampDb = std::isfinite(preamp) ? std::clamp(preamp, -30.0, 0.0) : 0.0;
    return p;
}

std::vector<NamedEqPreset> presetsFrom(const juce::var& v)
{
    std::vector<NamedEqPreset> out;
    if (const auto* a = v.getArray())
        for (const auto& item : *a)
        {
            auto p = presetFrom(item);
            if (!p || std::any_of(out.begin(), out.end(), [&](const auto& q) { return q.name == p->name; }))
                continue;
            out.push_back(std::move(*p));
            if (out.size() == kMaxEqPresets)
                break;
        }
    return out;
}

} // namespace ap::app
