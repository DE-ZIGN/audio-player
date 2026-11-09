#include <ap/engine/md5.h>

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace ap::engine {

namespace {

void check(NTSTATUS status, const char* what)
{
    if (!BCRYPT_SUCCESS(status))
        throw std::runtime_error(std::string("MD5: ") + what + " failed");
}

} // namespace

Md5::Md5()
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    check(BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, nullptr, 0), "BCryptOpenAlgorithmProvider");
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

Md5::~Md5()
{
    if (hash_)
        BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(hash_));
    if (algorithm_)
        BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(algorithm_), 0);
}

void Md5::update(std::span<const std::byte> data)
{
    constexpr std::size_t kMax = 1u << 30;
    while (!data.empty())
    {
        const auto n = std::min(data.size(), kMax);
        check(BCryptHashData(static_cast<BCRYPT_HASH_HANDLE>(hash_),
                             reinterpret_cast<PUCHAR>(const_cast<std::byte*>(data.data())), static_cast<ULONG>(n), 0),
              "BCryptHashData");
        data = data.subspan(n);
    }
}

Md5Digest Md5::finish()
{
    Md5Digest d{};
    check(BCryptFinishHash(static_cast<BCRYPT_HASH_HANDLE>(hash_), d.data(), static_cast<ULONG>(d.size()), 0),
          "BCryptFinishHash");
    return d;
}

} // namespace ap::engine
