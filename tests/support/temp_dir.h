#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <span>
#include <string>

namespace ap::test {

// A uniquely named directory under the system temp folder, removed on destruction.
class TempDir
{
public:
    TempDir()
    {
        std::random_device rd;
        path_ = std::filesystem::temp_directory_path()
              / ("ap_test_" + std::to_string(rd()) + "_" + std::to_string(rd()));
        std::filesystem::create_directories(path_);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const noexcept { return path_; }

    std::filesystem::path write(const std::string& name, std::span<const std::uint8_t> bytes) const
    {
        const auto p = path_ / name;
        std::ofstream out(p, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return p;
    }

private:
    std::filesystem::path path_;
};

} // namespace ap::test
