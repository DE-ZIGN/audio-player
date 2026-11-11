// Task 6-1: album covers. The picture embedded in a file comes first (the
// front cover when there are several), then an image in the folder; the
// thumbnail is scaled down once and cached, and made again when the music
// file or the folder image changes.

#include <ap/library/covers.h>
#include <ap/library/library_db.h>

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using namespace ap::library;
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
using Buf = std::vector<unsigned char>;

namespace {

// A solid image, encoded with WIC (PNG or JPEG).
Buf image(int w, int h, unsigned char shade, bool png = true)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> f;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
    std::vector<BYTE> pixels(static_cast<std::size_t>(w) * h * 3, shade);
    ComPtr<IWICBitmap> bmp;
    f->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat24bppBGR, w * 3, static_cast<UINT>(pixels.size()), pixels.data(),
                              &bmp);
    ComPtr<IStream> out;
    CreateStreamOnHGlobal(nullptr, TRUE, &out);
    ComPtr<IWICBitmapEncoder> enc;
    f->CreateEncoder(png ? GUID_ContainerFormatPng : GUID_ContainerFormatJpeg, nullptr, &enc);
    enc->Initialize(out.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;
    enc->CreateNewFrame(&frame, nullptr);
    frame->Initialize(nullptr);
    frame->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
    frame->SetPixelFormat(&fmt);
    frame->WriteSource(bmp.Get(), nullptr);
    frame->Commit();
    enc->Commit();
    STATSTG st{};
    out->Stat(&st, STATFLAG_NONAME);
    Buf b(static_cast<std::size_t>(st.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    out->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG read = 0;
    out->Read(b.data(), static_cast<ULONG>(b.size()), &read);
    return b;
}

void be32(Buf& b, std::uint32_t v)
{
    for (int s = 24; s >= 0; s -= 8)
        b.push_back(static_cast<unsigned char>(v >> s));
}

// A FLAC with no audio frames and the given PICTURE blocks (type, mime, data).
Buf flacWithPictures(const std::vector<std::pair<std::uint32_t, Buf>>& pictures)
{
    Buf b{'f', 'L', 'a', 'C', 0x00, 0, 0, 34, 0x10, 0x00, 0x10, 0x00, 0, 0, 0, 0, 0, 0};
    const std::uint64_t packed = (std::uint64_t{44100} << 44) | (std::uint64_t{1} << 41) | (std::uint64_t{15} << 36);
    for (int s = 56; s >= 0; s -= 8)
        b.push_back(static_cast<unsigned char>(packed >> s));
    b.insert(b.end(), 16, 0);
    for (std::size_t i = 0; i < pictures.size(); ++i)
    {
        Buf p;
        be32(p, pictures[i].first);
        const std::string mime = "image/png";
        be32(p, static_cast<std::uint32_t>(mime.size()));
        p.insert(p.end(), mime.begin(), mime.end());
        be32(p, 0); // no description
        be32(p, 0); // width, height, depth, colours: not needed
        be32(p, 0);
        be32(p, 0);
        be32(p, 0);
        be32(p, static_cast<std::uint32_t>(pictures[i].second.size()));
        p.insert(p.end(), pictures[i].second.begin(), pictures[i].second.end());
        b.push_back(static_cast<unsigned char>((i + 1 == pictures.size() ? 0x80 : 0) | 6)); // PICTURE
        b.push_back(static_cast<unsigned char>(p.size() >> 16));
        b.push_back(static_cast<unsigned char>(p.size() >> 8));
        b.push_back(static_cast<unsigned char>(p.size()));
        b.insert(b.end(), p.begin(), p.end());
    }
    if (pictures.empty())
        b[4] = 0x80; // STREAMINFO is the last block
    return b;
}

fs::path write(const fs::path& p, const Buf& b)
{
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return p;
}

Buf readAll(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return Buf(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::int64_t addTrack(LibraryDb& lib, const fs::path& p)
{
    FileFacts f;
    f.path = p;
    f.size = fs::file_size(p);
    f.modifiedAt = fs::last_write_time(p).time_since_epoch().count() / 10000;
    return lib.addTrack(f);
}

} // namespace

TEST_CASE("Covers: the embedded front cover comes first", "[library][covers]")
{
    ap::test::TempDir dir;
    const auto back = image(50, 50, 10);
    const auto front = image(60, 40, 200);
    const auto p = write(dir.path() / "a.flac", flacWithPictures({{4, back}, {3, front}})); // back, then front
    write(dir.path() / "folder.jpg", image(30, 30, 90, false));
    const auto c = findCover(p);
    REQUIRE(c.has_value());
    CHECK(c->source == "embedded");
    CHECK(c->bytes == front);
}

TEST_CASE("Covers: else an image in the folder, by name", "[library][covers]")
{
    ap::test::TempDir dir;
    const auto p = write(dir.path() / "a.flac", flacWithPictures({}));
    CHECK_FALSE(findCover(p).has_value());
    write(dir.path() / "scan.jpg", image(10, 10, 1, false)); // not a cover name
    CHECK_FALSE(findCover(p).has_value());
    const auto folder = image(20, 20, 2, false);
    const auto cover = image(30, 30, 3, false);
    write(dir.path() / "Folder.JPG", folder);
    CHECK(findCover(p)->bytes == folder);
    write(dir.path() / "cover.png", cover); // "cover" is preferred to "folder"
    CHECK(findCover(p)->bytes == cover);
}

TEST_CASE("Covers: a thumbnail fits the size and keeps the shape", "[library][covers]")
{
    const auto t = makeThumbnail(image(1200, 800, 128), 320);
    CHECK(t.width == 320);
    CHECK(t.height == 213);
    CHECK(imageSize(t.jpeg) == std::pair{320, 213}); // a JPEG WIC reads back
    const auto little = makeThumbnail(image(100, 50, 128), 320); // never scaled up
    CHECK(little.width == 100);
    CHECK(little.height == 50);
    CHECK_THROWS_AS(makeThumbnail(Buf{1, 2, 3}, 320), ImageError);
}

TEST_CASE("Covers: thumbnails are cached, and made again when the file changes", "[library][covers]")
{
    ap::test::TempDir dir;
    const auto music = dir.path() / "Music" / "a.flac";
    write(music, flacWithPictures({{3, image(600, 600, 50)}}));
    auto lib = LibraryDb::openInMemory();
    const auto t = addTrack(lib, music);
    const auto cache = dir.path() / "cache";

    const auto first = coverThumbnail(lib, t, cache);
    REQUIRE(first.has_value());
    CHECK(imageSize(readAll(*first)) == std::pair{320, 320});
    const auto made = fs::last_write_time(*first);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(coverThumbnail(lib, t, cache) == first); // the cached one
    CHECK(fs::last_write_time(*first) == made);

    // The music file changed (as the scan records it): a new thumbnail.
    write(music, flacWithPictures({{3, image(300, 150, 60)}}));
    lib.database().exec("UPDATE files SET size = size + 1, modified_at = modified_at + 1000");
    const auto second = coverThumbnail(lib, t, cache);
    REQUIRE(second.has_value());
    CHECK(second != first);
    CHECK(imageSize(readAll(*second)) == std::pair{300, 150});
}

TEST_CASE("Covers: no cover is remembered until the folder changes", "[library][covers]")
{
    ap::test::TempDir dir;
    const auto music = write(dir.path() / "Music" / "a.flac", flacWithPictures({}));
    auto lib = LibraryDb::openInMemory();
    const auto t = addTrack(lib, music);
    const auto cache = dir.path() / "cache";
    CHECK_FALSE(coverThumbnail(lib, t, cache).has_value());
    CHECK_FALSE(coverThumbnail(lib, t, cache).has_value());
    write(dir.path() / "Music" / "cover.jpg", image(400, 400, 70, false)); // added later
    CHECK(coverThumbnail(lib, t, cache).has_value());
}
