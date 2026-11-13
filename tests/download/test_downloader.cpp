// Bộ tải file: kiểm tra bằng URL file:// (không cần mạng). Tải tiếp qua HTTP Range được
// kiểm tra thủ công với Hugging Face (xem docs/DECISIONS.md).

#include <atomic>
#include <fstream>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "download/downloader.hpp"
#include "support/mupdf_fixture.hpp"
#include "util/sha256.hpp"

using namespace pdfread::download;
using pdfread::testing::TempFile;

namespace {

std::string file_url(const std::filesystem::path& p) { return "file:///" + p.generic_string(); }

std::string read_all(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

struct Fixture {
    TempFile src{"pdfread_dl_src.bin"};
    TempFile dest{"pdfread_dl_dest.bin"};
    TempFile part{"pdfread_dl_dest.bin.part"};
    std::string content;
    Fixture() {
        for (int i = 0; i < 300000; ++i) content += static_cast<char>('a' + i % 23);
        std::ofstream(src.path, std::ios::binary) << content;
        std::error_code ec;
        std::filesystem::remove(dest.path, ec);
        std::filesystem::remove(part.path, ec);
    }
    DownloadSpec spec() const {
        return {.url = file_url(src.path),
                .dest = dest.path,
                .expected_size = content.size(),
                .sha256 = pdfread::Sha256::hex(content),
                .max_retries = 0};
    }
};

}  // namespace

TEST_CASE("Download verifies SHA-256 and renames the part file", "[download]") {
    Fixture f;
    std::atomic<bool> cancel{false};
    int reports = 0;
    const auto p = download_file(f.spec(), [&](const DownloadProgress&) { ++reports; }, cancel);
    INFO(p.error);
    CHECK(p.state == DownloadState::Done);
    CHECK(read_all(f.dest.path) == f.content);
    CHECK_FALSE(std::filesystem::exists(f.part.path));
    CHECK(reports > 0);
}

TEST_CASE("Download resumes from an existing part file", "[download]") {
    Fixture f;
    std::ofstream(f.part.path, std::ios::binary) << f.content.substr(0, 100000);
    std::atomic<bool> cancel{false};
    const auto p = download_file(f.spec(), [](const DownloadProgress&) {}, cancel);
    INFO(p.error);
    CHECK(p.state == DownloadState::Done);
    CHECK(read_all(f.dest.path) == f.content);
}

TEST_CASE("Download with a complete part file only verifies", "[download]") {
    Fixture f;
    std::ofstream(f.part.path, std::ios::binary) << f.content;
    DownloadSpec spec = f.spec();
    spec.url = "file:///does/not/exist.bin";  // không cần tải nữa
    std::atomic<bool> cancel{false};
    const auto p = download_file(spec, [](const DownloadProgress&) {}, cancel);
    INFO(p.error);
    CHECK(p.state == DownloadState::Done);
    CHECK(read_all(f.dest.path) == f.content);
}

TEST_CASE("Download rejects a checksum mismatch and removes the file", "[download]") {
    Fixture f;
    DownloadSpec spec = f.spec();
    spec.sha256 = std::string(64, '0');
    std::atomic<bool> cancel{false};
    const auto p = download_file(spec, [](const DownloadProgress&) {}, cancel);
    CHECK(p.state == DownloadState::Failed);
    CHECK(p.error.find("SHA-256") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(f.dest.path));
    CHECK_FALSE(std::filesystem::exists(f.part.path));
}

TEST_CASE("Download reports missing source and cancellation", "[download]") {
    Fixture f;
    std::atomic<bool> cancel{false};
    DownloadSpec spec = f.spec();
    spec.url = file_url(f.src.path.string() + ".missing");
    CHECK(download_file(spec, [](const DownloadProgress&) {}, cancel).state == DownloadState::Failed);

    cancel = true;
    CHECK(download_file(f.spec(), [](const DownloadProgress&) {}, cancel).state == DownloadState::Cancelled);
    CHECK_FALSE(std::filesystem::exists(f.dest.path));
}

TEST_CASE("Background downloader finishes and exposes progress", "[download]") {
    Fixture f;
    std::atomic<int> wakes{0};
    {
        Downloader d([&] { ++wakes; });
        d.start(f.spec());
        while (d.running()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const auto p = d.progress();
        INFO(p.error);
        CHECK(p.state == DownloadState::Done);
        CHECK(p.done == f.content.size());
        CHECK(d.dest() == f.dest.path);
    }
    CHECK(wakes.load() > 0);
    CHECK(part_path("a/b.gguf") == std::filesystem::path("a/b.gguf.part"));
}
