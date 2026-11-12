#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "util/log.hpp"
#include "util/timer.hpp"

namespace plog = pdfread::log;
using namespace std::chrono_literals;

namespace {

// Bắt log vào vector và khôi phục cấu hình log khi ra khỏi scope.
struct LogCapture {
    std::vector<std::pair<plog::Level, std::string>> lines;

    LogCapture() {
        plog::set_stderr_enabled(false);
        plog::set_sink([this](plog::Level lv, std::string_view msg) { lines.emplace_back(lv, std::string(msg)); });
    }
    ~LogCapture() {
        plog::set_sink(nullptr);
        plog::set_stderr_enabled(true);
        plog::set_level(plog::Level::Info);
        plog::set_timing_enabled(false);
    }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;
};

}  // namespace

TEST_CASE("log filters by level", "[util][log]") {
    LogCapture cap;
    plog::set_level(plog::Level::Warn);

    plog::debug("không thấy");
    plog::info("không thấy");
    plog::warn("cảnh báo {}", 1);
    plog::error("lỗi {}", "x");

    REQUIRE(cap.lines.size() == 2);
    CHECK(cap.lines[0] == std::pair{plog::Level::Warn, std::string("cảnh báo 1")});
    CHECK(cap.lines[1] == std::pair{plog::Level::Error, std::string("lỗi x")});

    plog::set_level(plog::Level::Off);
    plog::error("tắt hẳn");
    CHECK(cap.lines.size() == 2);
}

TEST_CASE("ScopedTimer writes only when timing is enabled", "[util][log][timer]") {
    LogCapture cap;

    plog::set_timing_enabled(false);
    { pdfread::ScopedTimer t("tắt"); }
    CHECK(cap.lines.empty());

    plog::set_timing_enabled(true);
    plog::set_level(plog::Level::Error);  // timing không phụ thuộc level
    { pdfread::ScopedTimer t("render trang 1"); }
    REQUIRE(cap.lines.size() == 1);
    const std::string& msg = cap.lines[0].second;
    CHECK(msg.starts_with("[timing] render trang 1: "));
    CHECK(msg.ends_with(" ms"));
}

TEST_CASE("Stopwatch measures elapsed time", "[util][timer]") {
    pdfread::Stopwatch sw;
    std::this_thread::sleep_for(10ms);
    CHECK(sw.elapsed_ms() >= 9.0);
    sw.reset();
    CHECK(sw.elapsed_ms() < 1000.0);
}

TEST_CASE("log writes UTF-8 to a file", "[util][log]") {
    const auto path = std::filesystem::temp_directory_path() / "pdfread_log_test.txt";
    std::filesystem::remove(path);

    plog::set_stderr_enabled(false);
    REQUIRE(plog::set_file(path));
    plog::info("Xin chào — bộ điều khiển PID, ∂u/∂t");
    REQUIRE(plog::set_file({}));
    plog::set_stderr_enabled(true);

    std::ifstream in(path, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::filesystem::remove(path);

    CHECK(content.find("INFO") != std::string::npos);
    CHECK(content.find("Xin chào — bộ điều khiển PID, ∂u/∂t") != std::string::npos);
}
