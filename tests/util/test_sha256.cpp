#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "util/sha256.hpp"
#include "support/mupdf_fixture.hpp"

using pdfread::Sha256;

TEST_CASE("SHA-256 known vectors", "[util][sha256]") {
    CHECK(Sha256::hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256::hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Sha256::hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // 1 000 000 lần 'a'.
    CHECK(Sha256::hex(std::string(1000000, 'a')) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("SHA-256 incremental updates match one-shot", "[util][sha256]") {
    std::string data;
    for (int i = 0; i < 1000; ++i) data += static_cast<char>(i * 7 + 3);
    const std::string expected = Sha256::hex(data);
    for (std::size_t step : {1u, 3u, 63u, 64u, 65u, 200u}) {
        Sha256 s;
        for (std::size_t i = 0; i < data.size(); i += step) s.update(std::string_view(data).substr(i, step));
        CHECK(Sha256::to_hex(s.finish()) == expected);
    }
}

TEST_CASE("SHA-256 of a file with progress and cancel", "[util][sha256]") {
    pdfread::testing::TempFile tmp("pdfread_sha256_test.bin");
    const std::string content(3 * 1024 * 1024 + 17, 'x');
    {
        std::ofstream out(tmp.path, std::ios::binary);
        out << content;
    }
    std::uint64_t last = 0;
    const auto hex = pdfread::sha256_file(tmp.path, [&](std::uint64_t done) {
        last = done;
        return true;
    });
    REQUIRE(hex.has_value());
    CHECK(*hex == Sha256::hex(content));
    CHECK(last == content.size());

    CHECK_FALSE(pdfread::sha256_file(tmp.path, [](std::uint64_t) { return false; }).has_value());
    CHECK_FALSE(pdfread::sha256_file(tmp.path.string() + ".missing").has_value());
}
