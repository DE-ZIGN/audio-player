#include <ap/library/tag_reader.h>

#include <aifffile.h>
#include <dsdifffile.h>
#include <dsffile.h>
#include <fileref.h>
#include <flacfile.h>
#include <id3v1tag.h>
#include <id3v2tag.h>
#include <infotag.h>
#include <mp4file.h>
#include <mpegfile.h>
#include <tpropertymap.h>
#include <wavfile.h>

#include <windows.h>

#include <mutex>

namespace ap::library {

namespace {

// TagLib decodes undeclared text itself (as ISO-8859-1, or UTF-8 for RIFF
// INFO) before the file's other fields are known. These handlers keep the
// bytes instead: ASCII as it is, every byte from 0x80 up as a private-use
// character U+F780..U+F7FF. After reading, the values with such characters are
// judged together (legacy_text.h) and decoded. Text from declared UTF-16 or
// UTF-8 fields never goes through here.
constexpr wchar_t kMarkBase = 0xF700;

TagLib::String keepBytes(const TagLib::ByteVector& data)
{
    std::wstring w;
    w.reserve(data.size());
    for (const char ch : data)
    {
        const auto b = static_cast<unsigned char>(ch);
        if (b == 0)
            break; // fixed-size fields are padded with zeros
        w.push_back(b < 0x80 ? static_cast<wchar_t>(b) : static_cast<wchar_t>(kMarkBase + b));
    }
    return TagLib::String(w);
}

class KeepBytesId3v2 final : public TagLib::ID3v2::Latin1StringHandler
{
public:
    TagLib::String parse(const TagLib::ByteVector& data) const override { return keepBytes(data); }
};

class KeepBytesId3v1 final : public TagLib::ID3v1::StringHandler
{
public:
    TagLib::String parse(const TagLib::ByteVector& data) const override
    {
        return keepBytes(data).stripWhiteSpace(); // ID3v1 fields are padded with spaces or zeros
    }
};

class KeepBytesInfo final : public TagLib::RIFF::Info::StringHandler
{
public:
    TagLib::String parse(const TagLib::ByteVector& data) const override { return keepBytes(data); }
};

void installHandlers()
{
    static std::once_flag once;
    std::call_once(once, [] {
        // Process-wide in TagLib; kept for the life of the process.
        static KeepBytesId3v2 id3v2;
        static KeepBytesId3v1 id3v1;
        static KeepBytesInfo info;
        TagLib::ID3v2::Tag::setLatin1StringHandler(&id3v2);
        TagLib::ID3v1::Tag::setStringHandler(&id3v1);
        TagLib::RIFF::Info::Tag::setStringHandler(&info);
    });
}

bool isMark(wchar_t c)
{
    return c >= kMarkBase + 0x80 && c <= kMarkBase + 0xFF;
}

std::string toUtf8(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string lower(const std::string& s)
{
    std::string r = s;
    for (auto& c : r)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return r;
}

AudioFacts factsOf(TagLib::File* f)
{
    AudioFacts a;
    const auto* p = f->audioProperties();
    if (!p)
        return a;
    if (p->channels() > 0)
        a.channels = static_cast<std::uint16_t>(p->channels());
    const auto rate = p->sampleRate() > 0 ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(p->sampleRate()))
                                          : std::nullopt;
    auto fromLength = [&] {
        if (rate && p->lengthInMilliseconds() > 0)
            a.frames = static_cast<std::uint64_t>(p->lengthInMilliseconds()) * *rate / 1000;
    };
    a.sampleRate = rate;
    if (auto* x = dynamic_cast<TagLib::FLAC::File*>(f); x && x->audioProperties())
    {
        a.container = "FLAC";
        a.bits = static_cast<std::uint16_t>(x->audioProperties()->bitsPerSample());
        a.frames = x->audioProperties()->sampleFrames();
    }
    else if (auto* w = dynamic_cast<TagLib::RIFF::WAV::File*>(f); w && w->audioProperties())
    {
        a.container = "WAV";
        a.bits = static_cast<std::uint16_t>(w->audioProperties()->bitsPerSample());
        a.frames = w->audioProperties()->sampleFrames();
    }
    else if (auto* i = dynamic_cast<TagLib::RIFF::AIFF::File*>(f); i && i->audioProperties())
    {
        a.container = i->audioProperties()->isAiffC() ? "AIFC" : "AIFF";
        a.bits = static_cast<std::uint16_t>(i->audioProperties()->bitsPerSample());
        a.frames = i->audioProperties()->sampleFrames();
    }
    else if (auto* m = dynamic_cast<TagLib::MP4::File*>(f); m && m->audioProperties())
    {
        const bool alac = m->audioProperties()->codec() == TagLib::MP4::Properties::ALAC;
        a.container = alac ? "ALAC" : "AAC";
        if (alac)
            a.bits = static_cast<std::uint16_t>(m->audioProperties()->bitsPerSample());
        fromLength();
    }
    else if (dynamic_cast<TagLib::MPEG::File*>(f))
    {
        a.container = "MP3";
        fromLength();
    }
    else if (auto* d = dynamic_cast<TagLib::DSF::File*>(f); d && d->audioProperties())
    {
        a.container = "DSF";
        a.dsdRate = rate;
        a.sampleRate.reset();
        a.bits = 1;
        a.frames = static_cast<std::uint64_t>(d->audioProperties()->sampleCount());
    }
    else if (auto* dd = dynamic_cast<TagLib::DSDIFF::File*>(f); dd && dd->audioProperties())
    {
        a.container = "DFF";
        a.dsdRate = rate;
        a.sampleRate.reset();
        a.bits = 1;
        a.frames = static_cast<std::uint64_t>(dd->audioProperties()->sampleCount());
    }
    return a;
}

std::string sourceOf(TagLib::File* f)
{
    if (dynamic_cast<TagLib::MPEG::File*>(f))
        return "mpeg";
    if (dynamic_cast<TagLib::FLAC::File*>(f))
        return "flac";
    if (dynamic_cast<TagLib::MP4::File*>(f))
        return "mp4";
    if (dynamic_cast<TagLib::RIFF::WAV::File*>(f))
        return "wav";
    if (dynamic_cast<TagLib::RIFF::AIFF::File*>(f))
        return "aiff";
    if (dynamic_cast<TagLib::DSF::File*>(f))
        return "dsf";
    if (dynamic_cast<TagLib::DSDIFF::File*>(f))
        return "dsdiff";
    return "other";
}

} // namespace

TagReadResult readTags(const std::filesystem::path& file, std::optional<LegacyCharset> charsetOverride)
{
    installHandlers();
    // The audio properties from the headers only (Fast: no scan of the audio).
    TagLib::FileRef ref(file.c_str(), true, TagLib::AudioProperties::Fast);
    if (ref.isNull())
        throw TagReadError("TagLib could not read the file");

    TagReadResult r;
    r.source = sourceOf(ref.file());
    r.audio = factsOf(ref.file());
    const TagLib::PropertyMap props = ref.file()->properties();

    // First the values as they came, the undeclared ones still as bytes.
    struct Value
    {
        std::string field;
        std::wstring text;
        bool legacy = false;
    };
    std::vector<Value> values;
    std::vector<Bytes> legacyFields;
    for (const auto& [key, list] : props)
    {
        const auto field = lower(key.to8Bit(true));
        for (const auto& v : list)
        {
            Value val{field, v.toWString(), false};
            for (const wchar_t c : val.text)
                val.legacy = val.legacy || isMark(c);
            if (val.legacy)
            {
                Bytes b;
                for (const wchar_t c : val.text)
                    if (c < 0x80 || isMark(c))
                        b.push_back(static_cast<unsigned char>(c < 0x80 ? c : c - kMarkBase));
                legacyFields.push_back(std::move(b));
            }
            values.push_back(std::move(val));
        }
    }

    const auto detected = detectCharset(legacyFields);
    if (charsetOverride && detected)
    {
        r.legacyCharset = charsetOverride;
        r.legacyOverridden = true;
    }
    else
    {
        r.legacyCharset = detected;
    }

    std::size_t legacyIndex = 0;
    for (const auto& v : values)
    {
        std::string text;
        if (v.legacy)
            text = decodeLegacy(legacyFields[legacyIndex++], *r.legacyCharset);
        else
            text = toUtf8(v.text);
        r.fields[v.field].push_back(std::move(text));
    }
    return r;
}

} // namespace ap::library
