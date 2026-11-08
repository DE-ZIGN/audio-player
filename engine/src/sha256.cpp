#include <ap/engine/sha256.h>

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace ap::engine {

namespace {

void check(NTSTATUS status, const char* what)
{
    if (!BCRYPT_SUCCESS(status))
        throw std::runtime_error(std::string("SHA-256: ") + what + " failed");
}

} // namespace

Sha256::Sha256()
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    check(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0), "BCryptOpenAlgorithmProvider");
    algorithm_ = alg;
    BCRYPT_HASH_HANDLE hash = nullptr;
    const auto status = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status))
    {
        BCryptCloseAlgorithmProvider(alg, 0);
        algorithm_ = nullptr;
        check(status, "BCryptCreateHash");
    }
    hash_ = hash;
}

Sha256::~Sha256()
{
    if (hash_)
        BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(hash_));
    if (algorithm_)
        BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(algorithm_), 0);
}

void Sha256::update(std::span<const std::byte> data)
{
    // BCryptHashData takes a ULONG length; feed large spans in pieces.
    constexpr std::size_t kMax = 1u << 30;
    while (!data.empty())
    {
        const auto n = std::min(data.size(), kMax);
        check(BCryptHashData(static_cast<BCRYPT_HASH_HANDLE>(hash_),
                             reinterpret_cast<PUCHAR>(const_cast<std::byte*>(data.data())),
                             static_cast<ULONG>(n), 0),
              "BCryptHashData");
        data = data.subspan(n);
    }
}

Sha256Digest Sha256::finish()
{
    Sha256Digest d{};
    check(BCryptFinishHash(static_cast<BCRYPT_HASH_HANDLE>(hash_), d.data(), static_cast<ULONG>(d.size()), 0),
          "BCryptFinishHash");
    return d;
}

Sha256Digest sha256(std::span<const std::byte> data)
{
    Sha256 h;
    h.update(data);
    return h.finish();
}

Sha256Digest sha256File(const std::filesystem::path& path, std::uint64_t offset, std::uint64_t length)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("SHA-256: cannot open " + path.string());
    in.seekg(static_cast<std::streamoff>(offset));

    Sha256 h;
    std::vector<std::byte> buf(1 << 20);
    std::uint64_t remaining = length;
    while (remaining > 0 && in)
    {
        const auto want = static_cast<std::streamsize>(std::min<std::uint64_t>(buf.size(), remaining));
        in.read(reinterpret_cast<char*>(buf.data()), want);
        const auto got = static_cast<std::size_t>(in.gcount());
        if (got == 0)
            break;
        h.update(std::span(buf).first(got));
        remaining -= got;
    }
    return h.finish();
}

std::string toHex(const Sha256Digest& digest)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    s.reserve(64);
    for (auto b : digest)
    {
        s.push_back(kHex[b >> 4]);
        s.push_back(kHex[b & 0xF]);
    }
    return s;
}

} // namespace ap::engine
