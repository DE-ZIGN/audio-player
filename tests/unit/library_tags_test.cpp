// Task 5-3: reading tags with TagLib, and the mojibake samples (完了条件：
// 文字化けサンプルで正しく判別). The files are built byte by byte here: an MP3
// with ID3v2.3 frames marked ISO-8859-1 that hold Shift_JIS (as Japanese
// Windows taggers wrote them), an MP3 with only ID3v1, a WAV with a RIFF INFO
// chunk, and for contrast tags that declare their encoding (ID3v2 UTF-16,
// FLAC Vorbis comments), which must come through untouched.

#include <ap/library/library_db.h>
#include <ap/library/scanner.h>
#include <ap/library/tag_importer.h>
#include <ap/library/tag_reader.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

using namespace ap::library;
namespace fs = std::filesystem;
using Strings = std::vector<std::string>;

namespace {

using Buf = std::vector<unsigned char>;

std::string u8(const char8_t* s)
{
    return std::string(reinterpret_cast<const char*>(s));
}

Buf encode(const std::string& utf8, unsigned codePage)
{
    const int wn = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(wn), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), wn);
    if (codePage == 1200) // UTF-16LE with a byte order mark
    {
        Buf b{0xFF, 0xFE};
        for (const wchar_t c : w)
        {
            b.push_back(static_cast<unsigned char>(c & 0xFF));
            b.push_back(static_cast<unsigned char>(c >> 8));
        }
        return b;
    }
    const int n = WideCharToMultiByte(codePage, 0, w.data(), wn, nullptr, 0, nullptr, nullptr);
    Buf b(static_cast<std::size_t>(n));
    WideCharToMultiByte(codePage, 0, w.data(), wn, reinterpret_cast<char*>(b.data()), n, nullptr, nullptr);
    return b;
}

void append(Buf& b, const Buf& more)
{
    b.insert(b.end(), more.begin(), more.end());
}

void append(Buf& b, const std::string& ascii)
{
    b.insert(b.end(), ascii.begin(), ascii.end());
}

void be32(Buf& b, std::uint32_t v)
{
    for (int s = 24; s >= 0; s -= 8)
        b.push_back(static_cast<unsigned char>(v >> s));
}

void le32(Buf& b, std::uint32_t v)
{
    for (int s = 0; s <= 24; s += 8)
        b.push_back(static_cast<unsigned char>(v >> s));
}

void le16(Buf& b, std::uint16_t v)
{
    b.push_back(static_cast<unsigned char>(v));
    b.push_back(static_cast<unsigned char>(v >> 8));
}

// An ID3v2.3 text frame. encoding 0 = "ISO-8859-1" (whatever the bytes are), 1 = UTF-16.
Buf frame(const std::string& id, unsigned char encoding, const Buf& text)
{
    Buf f;
    append(f, id);
    be32(f, static_cast<std::uint32_t>(text.size() + 1));
    f.push_back(0);
    f.push_back(0);
    f.push_back(encoding);
    append(f, text);
    return f;
}

Buf id3v23(const std::vector<Buf>& frames)
{
    Buf body;
    for (const auto& f : frames)
        append(body, f);
    Buf t{'I', 'D', '3', 3, 0, 0};
    const auto n = static_cast<std::uint32_t>(body.size());
    for (int s = 21; s >= 0; s -= 7) // sync-safe size
        t.push_back(static_cast<unsigned char>((n >> s) & 0x7F));
    append(t, body);
    return t;
}

// A few silent MPEG-1 Layer III frames (128 kbit/s, 44.1 kHz: 417 bytes each).
Buf mpegFrames()
{
    Buf b;
    for (int i = 0; i < 8; ++i)
    {
        Buf f(417, 0);
        f[0] = 0xFF;
        f[1] = 0xFB;
        f[2] = 0x90;
        f[3] = 0x00;
        append(b, f);
    }
    return b;
}

Buf id3v1(const Buf& title, const Buf& artist, const Buf& album)
{
    auto field = [](const Buf& v, std::size_t n) {
        Buf f(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(std::min(v.size(), n)));
        f.resize(n, 0);
        return f;
    };
    Buf t{'T', 'A', 'G'};
    append(t, field(title, 30));
    append(t, field(artist, 30));
    append(t, field(album, 30));
    append(t, field(Buf{'2', '0', '0', '1'}, 4));
    append(t, field({}, 30));
    t.push_back(255); // no genre
    return t;
}

Buf wavWithInfo(const Buf& title, const Buf& artist)
{
    auto sub = [](const std::string& id, Buf v) {
        v.push_back(0); // INFO strings end with a zero
        Buf c;
        append(c, id);
        le32(c, static_cast<std::uint32_t>(v.size()));
        append(c, v);
        if (v.size() % 2)
            c.push_back(0);
        return c;
    };
    Buf info;
    append(info, "INFO");
    append(info, sub("INAM", title));
    append(info, sub("IART", artist));

    Buf body;
    append(body, "WAVE");
    append(body, "fmt ");
    le32(body, 16);
    le16(body, 1);
    le16(body, 2);
    le32(body, 44100);
    le32(body, 44100 * 4);
    le16(body, 4);
    le16(body, 16);
    append(body, "data");
    le32(body, 400);
    append(body, Buf(400, 0));
    append(body, "LIST");
    le32(body, static_cast<std::uint32_t>(info.size()));
    append(body, info);

    Buf w;
    append(w, "RIFF");
    le32(w, static_cast<std::uint32_t>(body.size()));
    append(w, body);
    return w;
}

Buf flacWithComments(const std::vector<std::string>& comments)
{
    Buf b;
    append(b, "fLaC");
    b.push_back(0x00); // STREAMINFO, not last
    b.push_back(0);
    b.push_back(0);
    b.push_back(34);
    b.push_back(0x10); // min block size 4096 (big-endian)
    b.push_back(0x00);
    b.push_back(0x10); // max block size 4096
    b.push_back(0x00);
    append(b, Buf(6, 0)); // frame sizes unknown
    const std::uint64_t packed = (std::uint64_t{44100} << 44) | (std::uint64_t{1} << 41) | (std::uint64_t{15} << 36);
    for (int s = 56; s >= 0; s -= 8)
        b.push_back(static_cast<unsigned char>(packed >> s));
    append(b, Buf(16, 0)); // MD5

    Buf vc;
    const std::string vendor = "test";
    le32(vc, static_cast<std::uint32_t>(vendor.size()));
    append(vc, vendor);
    le32(vc, static_cast<std::uint32_t>(comments.size()));
    for (const auto& c : comments)
    {
        le32(vc, static_cast<std::uint32_t>(c.size()));
        append(vc, c);
    }
    b.push_back(0x84); // VORBIS_COMMENT, last
    b.push_back(static_cast<unsigned char>(vc.size() >> 16));
    b.push_back(static_cast<unsigned char>(vc.size() >> 8));
    b.push_back(static_cast<unsigned char>(vc.size()));
    append(b, vc);
    return b;
}

fs::path write(const fs::path& p, const Buf& bytes)
{
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return p;
}

Buf sjisMp3(const std::string& title, const std::string& artist, const std::string& album)
{
    auto b = id3v23({frame("TIT2", 0, encode(title, 932)), frame("TPE1", 0, encode(artist, 932)),
                     frame("TALB", 0, encode(album, 932))});
    append(b, mpegFrames());
    return b;
}

} // namespace

TEST_CASE("Tags: Shift_JIS in ID3v2.3 \"ISO-8859-1\" frames reads as Japanese", "[library][tags]")
{
    ap::test::TempDir dir;
    const auto p = write(dir.path() / "a.mp3", sjisMp3(u8(u8"夜明けのスキャット"), u8(u8"由紀さおり"), u8(u8"ベスト")));
    const auto t = readTags(p);
    CHECK(t.source == "mpeg");
    CHECK(t.legacyCharset == LegacyCharset::ShiftJis);
    CHECK(t.fields.at("title") == Strings{u8(u8"夜明けのスキャット")});
    CHECK(t.fields.at("artist") == Strings{u8(u8"由紀さおり")});
    CHECK(t.fields.at("album") == Strings{u8(u8"ベスト")});
}

TEST_CASE("Tags: Shift_JIS in ID3v1 reads as Japanese", "[library][tags]")
{
    ap::test::TempDir dir;
    auto b = mpegFrames();
    append(b, id3v1(encode(u8(u8"千本桜"), 932), encode(u8(u8"黒うさＰ"), 932), encode(u8(u8"ﾃｽﾄ"), 932)));
    const auto t = readTags(write(dir.path() / "v1.mp3", b));
    CHECK(t.legacyCharset == LegacyCharset::ShiftJis);
    CHECK(t.fields.at("title") == Strings{u8(u8"千本桜")});
    CHECK(t.fields.at("artist") == Strings{u8(u8"黒うさＰ")});
    CHECK(t.fields.at("album") == Strings{u8(u8"ﾃｽﾄ")});
}

TEST_CASE("Tags: Shift_JIS in a WAV's RIFF INFO reads as Japanese", "[library][tags]")
{
    ap::test::TempDir dir;
    const auto p = write(dir.path() / "a.wav", wavWithInfo(encode(u8(u8"録音テスト"), 932), encode(u8(u8"山田"), 932)));
    const auto t = readTags(p);
    CHECK(t.source == "wav");
    CHECK(t.legacyCharset == LegacyCharset::ShiftJis);
    CHECK(t.fields.at("title") == Strings{u8(u8"録音テスト")});
    CHECK(t.fields.at("artist") == Strings{u8(u8"山田")});
}

TEST_CASE("Tags: western text in the same kind of frames stays western", "[library][tags]")
{
    ap::test::TempDir dir;
    auto b = id3v23({frame("TIT2", 0, encode(u8(u8"Hélène"), 1252)), frame("TPE1", 0, encode(u8(u8"Björk"), 1252)),
                     frame("TALB", 0, encode(u8(u8"Don’t Stop"), 1252))});
    append(b, mpegFrames());
    const auto t = readTags(write(dir.path() / "w.mp3", b));
    CHECK(t.legacyCharset == LegacyCharset::Windows1252);
    CHECK(t.fields.at("title") == Strings{u8(u8"Hélène")});
    CHECK(t.fields.at("artist") == Strings{u8(u8"Björk")});
    CHECK(t.fields.at("album") == Strings{u8(u8"Don’t Stop")});
}

TEST_CASE("Tags: declared encodings come through untouched", "[library][tags]")
{
    ap::test::TempDir dir;
    // ID3v2 UTF-16, next to an ASCII "ISO-8859-1" frame.
    auto b = id3v23({frame("TIT2", 1, encode(u8(u8"夜明け"), 1200)), frame("TPE1", 0, encode("ASCII Artist", 932))});
    append(b, mpegFrames());
    auto t = readTags(write(dir.path() / "u.mp3", b));
    CHECK_FALSE(t.legacyCharset.has_value()); // nothing undeclared beyond ASCII
    CHECK(t.fields.at("title") == Strings{u8(u8"夜明け")});
    CHECK(t.fields.at("artist") == Strings{"ASCII Artist"});

    // FLAC Vorbis comments are UTF-8, several values of one field in order.
    t = readTags(write(dir.path() / "f.flac", flacWithComments({u8(u8"TITLE=夜明け"), "ARTIST=B", "ARTIST=A",
                                                                  "ALBUMARTIST=Various"})));
    CHECK(t.source == "flac");
    CHECK_FALSE(t.legacyCharset.has_value());
    CHECK(t.fields.at("title") == Strings{u8(u8"夜明け")});
    CHECK(t.fields.at("artist") == Strings{"B", "A"});
    CHECK(t.fields.at("albumartist") == Strings{"Various"});
}

TEST_CASE("Tags: the user's choice of character set is used instead", "[library][tags]")
{
    ap::test::TempDir dir;
    const auto p = write(dir.path() / "a.mp3", sjisMp3(u8(u8"テスト"), u8(u8"テスト"), u8(u8"テスト")));
    const auto t = readTags(p, LegacyCharset::Windows1252);
    CHECK(t.legacyOverridden);
    CHECK(t.legacyCharset == LegacyCharset::Windows1252);
    CHECK(t.fields.at("title") == Strings{u8(u8"ƒeƒXƒg")}); // the mojibake the user asked for
}

TEST_CASE("Tags: a file TagLib cannot read is an error", "[library][tags]")
{
    ap::test::TempDir dir;
    CHECK_THROWS_AS(readTags(write(dir.path() / "bad.flac", Buf(100, 'x'))), TagReadError);
    CHECK_THROWS_AS(readTags(dir.path() / "missing.mp3"), TagReadError);
}

TEST_CASE("Import: the scan's marked files are read into the tag layer", "[library][tags]")
{
    ap::test::TempDir dir;
    const auto root = dir.path() / "Music";
    write(root / "a.mp3", sjisMp3(u8(u8"夜明けのスキャット"), u8(u8"由紀さおり"), u8(u8"ベスト")));
    write(root / "f.flac", flacWithComments({"TITLE=Hello", "ARTIST=World"}));
    write(root / "broken.flac", Buf(100, 'x'));
    auto lib = LibraryDb::openInMemory();
    const auto rootId = lib.addRoot(root);
    Scanner(lib).scanRoot(rootId);

    TagImporter importer(lib);
    auto r = importer.readPending();
    CHECK(r.read == 2);
    CHECK(r.failed == 1);
    CHECK(r.shiftJis == 1);

    auto trackOf = [&](const std::string& name) {
        auto st = lib.database().prepare(
            "SELECT tracks.id FROM tracks JOIN files ON files.id = tracks.file_id WHERE files.path LIKE ?");
        st.bind(1, "%" + name);
        REQUIRE(st.step());
        return st.integer(0);
    };
    const auto a = trackOf("a.mp3");
    CHECK(lib.values(a, "title") == Strings{u8(u8"夜明けのスキャット")});
    CHECK(lib.shownLayer(a, "title") == Layer::Tag);
    auto charset = lib.database().prepare("SELECT tag_charset, needs_read FROM files WHERE path LIKE '%a.mp3'");
    REQUIRE(charset.step());
    CHECK(charset.text(0) == "shift_jis");
    CHECK(charset.integer(1) == 0);
    auto failed = lib.database().prepare("SELECT read_error, needs_read FROM files WHERE path LIKE '%broken.flac'");
    REQUIRE(failed.step());
    CHECK_FALSE(failed.isNull(0));
    CHECK(failed.integer(1) == 0); // not retried until it changes

    // Nothing more to read.
    CHECK(importer.readPending().read == 0);

    // The user's edit stays over a tag read again; the user's choice of
    // character set reads the file again with it.
    lib.setValues(a, Layer::User, "title", Strings{u8(u8"夜明けのスキャット（修正）")});
    setTagCharset(lib, a, LegacyCharset::Windows1252);
    r = importer.readPending();
    CHECK(r.read == 1);
    CHECK(lib.values(a, "title") == Strings{u8(u8"夜明けのスキャット（修正）")});
    CHECK(lib.values(a, "artist") != Strings{u8(u8"由紀さおり")}); // read as Windows-1252 now
    setTagCharset(lib, a, std::nullopt);                          // back to detection
    importer.readPending();
    CHECK(lib.values(a, "artist") == Strings{u8(u8"由紀さおり")});
}

TEST_CASE("Tags: the audio facts in the headers are read too (task 5-6)", "[library][tags]")
{
    ap::test::TempDir dir;
    auto t = readTags(write(dir.path() / "a.wav", wavWithInfo(encode("x", 932), encode("y", 932))));
    CHECK(t.audio.container == "WAV");
    CHECK(t.audio.sampleRate == 44100u);
    CHECK(t.audio.bits == 16);
    CHECK(t.audio.channels == 2);
    CHECK(t.audio.frames == 100u); // 400 bytes of 16-bit stereo
    CHECK_FALSE(t.audio.dsdRate.has_value());

    t = readTags(write(dir.path() / "f.flac", flacWithComments({"TITLE=x"})));
    CHECK(t.audio.container == "FLAC");
    CHECK(t.audio.sampleRate == 44100u);
    CHECK(t.audio.bits == 16);
    CHECK(t.audio.channels == 2);

    auto mp3 = sjisMp3("a", "b", "c");
    t = readTags(write(dir.path() / "m.mp3", mp3));
    CHECK(t.audio.container == "MP3");
    CHECK(t.audio.sampleRate == 44100u);
    CHECK_FALSE(t.audio.bits.has_value()); // lossy: no bit depth
}
