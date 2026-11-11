#include <ap/library/covers.h>

#include <fileref.h>
#include <tfile.h>
#include <tvariant.h>

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <system_error>

namespace ap::library {

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace {

// COM for WIC on whatever thread asks (the app's threads may already have it).
class ComScope
{
public:
    ComScope() : hr_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComScope()
    {
        if (SUCCEEDED(hr_))
            CoUninitialize();
    }

private:
    HRESULT hr_;
};

void check(HRESULT hr, const char* what)
{
    if (FAILED(hr))
        throw ImageError(std::string(what) + ": " + std::system_category().message(hr));
}

ComPtr<IWICImagingFactory> factory()
{
    ComPtr<IWICImagingFactory> f;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)), "WIC factory");
    return f;
}

ComPtr<IWICBitmapFrameDecode> decodeFirstFrame(IWICImagingFactory* f, std::span<const unsigned char> image)
{
    if (image.empty())
        throw ImageError("empty image");
    ComPtr<IWICStream> stream;
    check(f->CreateStream(&stream), "WIC stream");
    check(stream->InitializeFromMemory(const_cast<BYTE*>(image.data()), static_cast<DWORD>(image.size())), "WIC stream");
    ComPtr<IWICBitmapDecoder> decoder;
    check(f->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder), "decoding the image");
    ComPtr<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, &frame), "decoding the image");
    return frame;
}

std::string utf8(const fs::path& p)
{
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

fs::path fromUtf8(const std::string& s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::wstring lowerExt(const fs::path& p)
{
    auto e = p.extension().wstring();
    for (auto& c : e)
        c = static_cast<wchar_t>(towlower(c));
    return e;
}

// The cover image in a music file's folder, best name first.
std::optional<fs::path> folderImage(const fs::path& musicFile)
{
    static constexpr std::array<std::wstring_view, 5> kNames = {L"cover", L"folder", L"front", L"album", L"albumart"};
    std::optional<fs::path> best;
    std::size_t bestRank = kNames.size();
    std::error_code ec;
    for (fs::directory_iterator it(musicFile.parent_path(), ec), end; !ec && it != end; it.increment(ec))
    {
        if (!it->is_regular_file(ec))
            continue;
        const auto ext = lowerExt(it->path());
        if (ext != L".jpg" && ext != L".jpeg" && ext != L".png")
            continue;
        auto stem = it->path().stem().wstring();
        for (auto& c : stem)
            c = static_cast<wchar_t>(towlower(c));
        for (std::size_t r = 0; r < bestRank; ++r)
            if (stem == kNames[r])
            {
                best = it->path();
                bestRank = r;
                break;
            }
    }
    return best;
}

std::optional<std::vector<unsigned char>> readFile(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return std::nullopt;
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::int64_t mtimeTicks(const fs::path& p)
{
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    return ec ? 0 : t.time_since_epoch().count();
}

} // namespace

std::optional<Cover> findCover(const fs::path& musicFile)
{
    {
        TagLib::FileRef ref(musicFile.c_str(), false);
        if (!ref.isNull())
        {
            const auto pictures = ref.file()->complexProperties("PICTURE");
            const TagLib::VariantMap* chosen = nullptr;
            for (const auto& p : pictures)
            {
                if (!chosen)
                    chosen = &p;
                if (p.value("pictureType").value<TagLib::String>() == "Front Cover")
                {
                    chosen = &p;
                    break;
                }
            }
            if (chosen)
            {
                const auto data = chosen->value("data").value<TagLib::ByteVector>();
                if (!data.isEmpty())
                    return Cover{std::vector<unsigned char>(data.begin(), data.end()), "embedded"};
            }
        }
    }
    if (const auto img = folderImage(musicFile))
        if (auto bytes = readFile(*img); bytes && !bytes->empty())
            return Cover{std::move(*bytes), utf8(*img)};
    return std::nullopt;
}

std::pair<int, int> imageSize(std::span<const unsigned char> image)
{
    ComScope com;
    auto f = factory();
    auto frame = decodeFirstFrame(f.Get(), image);
    UINT w = 0, h = 0;
    check(frame->GetSize(&w, &h), "image size");
    return {static_cast<int>(w), static_cast<int>(h)};
}

Thumbnail makeThumbnail(std::span<const unsigned char> image, int maxSide)
{
    ComScope com;
    auto f = factory();
    auto frame = decodeFirstFrame(f.Get(), image);
    UINT w = 0, h = 0;
    check(frame->GetSize(&w, &h), "image size");
    if (w == 0 || h == 0)
        throw ImageError("the image has no pixels");
    const double scale = std::min(1.0, static_cast<double>(maxSide) / std::max(w, h)); // never up
    const UINT tw = std::max(1u, static_cast<UINT>(std::lround(w * scale)));
    const UINT th = std::max(1u, static_cast<UINT>(std::lround(h * scale)));

    ComPtr<IWICBitmapScaler> scaler;
    check(f->CreateBitmapScaler(&scaler), "scaling");
    check(scaler->Initialize(frame.Get(), tw, th, WICBitmapInterpolationModeHighQualityCubic), "scaling");
    ComPtr<IWICFormatConverter> converted;
    check(f->CreateFormatConverter(&converted), "converting");
    check(converted->Initialize(scaler.Get(), GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom),
          "converting");

    ComPtr<IStream> out;
    check(CreateStreamOnHGlobal(nullptr, TRUE, &out), "output");
    ComPtr<IWICBitmapEncoder> encoder;
    check(f->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder), "JPEG");
    check(encoder->Initialize(out.Get(), WICBitmapEncoderNoCache), "JPEG");
    ComPtr<IWICBitmapFrameEncode> target;
    ComPtr<IPropertyBag2> props;
    check(encoder->CreateNewFrame(&target, &props), "JPEG");
    PROPBAG2 option{};
    wchar_t name[] = L"ImageQuality";
    option.pstrName = name;
    VARIANT quality;
    VariantInit(&quality);
    quality.vt = VT_R4;
    quality.fltVal = 0.9f;
    props->Write(1, &option, &quality);
    check(target->Initialize(props.Get()), "JPEG");
    check(target->SetSize(tw, th), "JPEG");
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    check(target->SetPixelFormat(&format), "JPEG");
    check(target->WriteSource(converted.Get(), nullptr), "JPEG");
    check(target->Commit(), "JPEG");
    check(encoder->Commit(), "JPEG");

    STATSTG stat{};
    check(out->Stat(&stat, STATFLAG_NONAME), "output");
    Thumbnail t;
    t.width = static_cast<int>(tw);
    t.height = static_cast<int>(th);
    t.jpeg.resize(static_cast<std::size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    check(out->Seek(zero, STREAM_SEEK_SET, nullptr), "output");
    ULONG read = 0;
    check(out->Read(t.jpeg.data(), static_cast<ULONG>(t.jpeg.size()), &read), "output");
    t.jpeg.resize(read);
    return t;
}

std::optional<fs::path> coverThumbnail(LibraryDb& lib, std::int64_t track, const fs::path& cacheFolder, int maxSide)
{
    fs::path music;
    std::int64_t size = 0, modified = 0;
    {
        auto st = lib.database().prepare(
            "SELECT f.path, f.size, f.modified_at FROM tracks AS t JOIN files AS f ON f.id = t.file_id "
            "WHERE t.id = ? AND f.present = 1");
        st.bind(1, track);
        if (!st.step())
            return std::nullopt;
        music = fromUtf8(st.text(0));
        size = st.integer(1);
        modified = st.integer(2);
    }
    // The name follows what the cover can come from: the music file and its folder's image.
    const auto folderImg = folderImage(music);
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](const std::string& s) {
        for (const unsigned char c : s)
        {
            h ^= c;
            h *= 1099511628211ull;
        }
    };
    mix(utf8(music));
    mix("|" + std::to_string(size) + "|" + std::to_string(modified) + "|" + std::to_string(maxSide));
    if (folderImg)
        mix("|" + utf8(*folderImg) + "|" + std::to_string(mtimeTicks(*folderImg)));
    char name[17];
    std::snprintf(name, sizeof name, "%016llx", static_cast<unsigned long long>(h));
    const auto jpg = cacheFolder / (std::string(name) + ".jpg");
    const auto none = cacheFolder / (std::string(name) + ".none");
    std::error_code ec;
    if (fs::exists(jpg, ec))
        return jpg;
    if (fs::exists(none, ec))
        return std::nullopt;

    fs::create_directories(cacheFolder, ec);
    std::optional<Thumbnail> thumb;
    if (const auto cover = findCover(music))
    {
        try
        {
            thumb = makeThumbnail(cover->bytes, maxSide);
        }
        catch (const ImageError&)
        {
            // An image WIC cannot read is as good as none.
        }
    }
    if (!thumb)
    {
        std::ofstream(none).put('\0');
        return std::nullopt;
    }
    // Written to a temporary name first: a reader never sees half a file.
    const auto tmp = cacheFolder / (std::string(name) + ".tmp" + std::to_string(GetCurrentThreadId()));
    {
        std::ofstream out(tmp, std::ios::binary);
        out.write(reinterpret_cast<const char*>(thumb->jpeg.data()), static_cast<std::streamsize>(thumb->jpeg.size()));
    }
    fs::rename(tmp, jpg, ec);
    if (ec)
        fs::remove(tmp, ec); // someone else made it at the same time
    return jpg;
}

} // namespace ap::library
