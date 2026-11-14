#include <ap/engine/dsp/tube_auto.h>

namespace ap::engine {

std::vector<TubeGenreRule> defaultTubeGenreRules()
{
    // A starting point for the user to edit. Kept short: genres the tags of
    // common music libraries use, in English and in Japanese.
    const auto se = TubeType::Se300B, el = TubeType::PpEl34, a = TubeType::PpClassA;
    return {
        {"Jazz", se},       {"Vocal", se},      {"Bossa Nova", se}, {"Folk", se},
        {"Acoustic", se},   {"Singer-Songwriter", se},             {"ジャズ", se},
        {"ボーカル", se},   {"フォーク", se},

        {"Rock", el},       {"Pop", el},        {"Blues", el},      {"Metal", el},
        {"Punk", el},       {"Alternative", el}, {"Indie", el},     {"R&B", el},
        {"Soul", el},       {"Funk", el},       {"Hip Hop", el},    {"Hip-Hop", el},
        {"Electronic", el}, {"Dance", el},      {"Anime", el},      {"ロック", el},
        {"ポップ", el},     {"アニメ", el},

        {"Classical", a},   {"Orchestral", a},  {"Opera", a},       {"Chamber", a},
        {"Baroque", a},     {"Soundtrack", a},  {"Ambient", a},     {"クラシック", a},
        {"サウンドトラック", a},
    };
}

std::string normalizedGenre(std::string_view g)
{
    std::string out;
    out.reserve(g.size());
    bool space = false;
    for (std::size_t i = 0; i < g.size(); ++i)
    {
        auto c = static_cast<unsigned char>(g[i]);
        // Full-width ASCII (U+FF01..U+FF5E: EF BC 81..EF BD 9E) and the
        // ideographic space (U+3000: E3 80 80) as their half-width forms.
        if (c == 0xEF && i + 2 < g.size())
        {
            const auto b1 = static_cast<unsigned char>(g[i + 1]), b2 = static_cast<unsigned char>(g[i + 2]);
            if ((b1 == 0xBC && b2 >= 0x81 && b2 <= 0xBF) || (b1 == 0xBD && b2 >= 0x80 && b2 <= 0x9E))
            {
                c = static_cast<unsigned char>(0x20u + ((b1 & 1u) << 6) + (b2 & 0x3Fu));
                i += 2;
            }
        }
        else if (c == 0xE3 && i + 2 < g.size() && static_cast<unsigned char>(g[i + 1]) == 0x80
                 && static_cast<unsigned char>(g[i + 2]) == 0x80)
        {
            c = ' ';
            i += 2;
        }
        if (c == ' ' || c == '\t')
        {
            space = !out.empty();
            continue;
        }
        if (space)
            out += ' ';
        space = false;
        out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    return out;
}

std::optional<TubeChoice> chooseTubeType(const TubeAutoSettings& s, std::span<const std::string> genres,
                                         std::string_view albumKey)
{
    if (!s.enabled)
        return std::nullopt;
    if (!albumKey.empty())
        for (const auto& a : s.albums)
            if (a.albumKey == albumKey)
                return TubeChoice{a.type, TubeChoiceReason::Album, a.album};

    // The tag's parts, in order, as written and as compared.
    struct Part
    {
        std::string asWritten, key;
    };
    std::vector<Part> parts;
    for (const auto& g : genres)
    {
        std::size_t from = 0;
        while (from <= g.size())
        {
            const auto end = g.find_first_of(";/,", from);
            const auto piece = std::string_view(g).substr(from, end == std::string::npos ? std::string::npos : end - from);
            if (auto key = normalizedGenre(piece); !key.empty())
            {
                const auto first = piece.find_first_not_of(" \t");
                const auto last = piece.find_last_not_of(" \t");
                parts.push_back({std::string(piece.substr(first, last - first + 1)), std::move(key)});
            }
            if (end == std::string::npos)
                break;
            from = end + 1;
        }
    }
    if (parts.empty())
        return std::nullopt;

    std::vector<std::string> keys;
    keys.reserve(s.rules.size());
    for (const auto& r : s.rules)
        keys.push_back(normalizedGenre(r.genre));

    for (const auto& p : parts)
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (!keys[i].empty() && keys[i] == p.key)
                return TubeChoice{s.rules[i].type, TubeChoiceReason::Genre, p.asWritten};
    for (const auto& p : parts)
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (!keys[i].empty() && p.key.find(keys[i]) != std::string::npos)
                return TubeChoice{s.rules[i].type, TubeChoiceReason::Genre, p.asWritten};
    return std::nullopt;
}

} // namespace ap::engine
