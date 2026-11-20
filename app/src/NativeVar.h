#pragma once

// A message for the page (task 11-3, NativeMessages.h) as the bridge sends it:
// { code, values: { name: value } }. The page writes the sentence
// (ui/src/nativeText.ts).

#include <NativeMessages.h>

#include <juce_core/juce_core.h>

namespace ap::app {

inline juce::var nativeVar(const NativeMessage& m)
{
    auto* values = new juce::DynamicObject();
    for (const auto& [name, value] : m.values)
        values->setProperty(juce::Identifier(juce::String(name)), juce::String::fromUTF8(value.c_str()));
    auto* o = new juce::DynamicObject();
    o->setProperty("code", juce::String(m.code));
    o->setProperty("values", juce::var(values));
    return juce::var(o);
}

// A juce::String as UTF-8, for a message's values.
inline std::string utf8Of(const juce::String& s)
{
    return s.toStdString();
}

} // namespace ap::app
