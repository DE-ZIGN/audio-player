#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ap::engine {

using Md5Digest = std::array<std::uint8_t, 16>;

// Incremental MD5 using Windows CNG (bcrypt.dll). Used only to compare with
// the MD5 that FLAC files store for their samples, not for security.
class Md5
{
public:
    Md5();
    ~Md5();
    Md5(const Md5&) = delete;
    Md5& operator=(const Md5&) = delete;

    void update(std::span<const std::byte> data);
    Md5Digest finish(); // may be called once

private:
    void* algorithm_ = nullptr;
    void* hash_ = nullptr;
};

} // namespace ap::engine
