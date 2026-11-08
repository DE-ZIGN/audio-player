#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace ap::engine {

using Sha256Digest = std::array<std::uint8_t, 32>;

// Incremental SHA-256 using Windows CNG (bcrypt.dll).
class Sha256
{
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    void update(std::span<const std::byte> data);
    Sha256Digest finish(); // may be called once

private:
    void* algorithm_ = nullptr;
    void* hash_ = nullptr;
};

Sha256Digest sha256(std::span<const std::byte> data);

// Hash of a whole file, or of [offset, offset + length) when length is given.
Sha256Digest sha256File(const std::filesystem::path& path, std::uint64_t offset = 0,
                        std::uint64_t length = UINT64_MAX);

std::string toHex(const Sha256Digest& digest);

} // namespace ap::engine
