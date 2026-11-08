#include <ap/engine/sha256.h>

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <vector>

using namespace ap::engine;

namespace {

std::span<const std::byte> bytesOf(std::string_view s)
{
    return std::as_bytes(std::span(s.data(), s.size()));
}

} // namespace

TEST_CASE("SHA-256 matches FIPS 180-2 test vectors", "[engine][sha256]")
{
    CHECK(toHex(sha256(bytesOf(""))) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(toHex(sha256(bytesOf("abc"))) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(toHex(sha256(bytesOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")))
          == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("Incremental SHA-256 equals one-shot", "[engine][sha256]")
{
    std::vector<std::byte> data(100000);
    for (std::size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<std::byte>(i * 31 + 7);

    Sha256 h;
    h.update(std::span(data).first(1));
    h.update(std::span(data).subspan(1, 4999));
    h.update(std::span(data).subspan(5000));
    CHECK(h.finish() == sha256(data));
}
