#include "util/log.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>

namespace pdfread::log {
namespace {

std::atomic<Level> g_level{Level::Info};
std::atomic<bool> g_timing{false};
std::atomic<bool> g_stderr{true};

std::mutex g_mutex;  // bảo vệ g_file, g_sink và thứ tự các dòng
std::ofstream g_file;
Sink g_sink;

std::string& tls_thread_name() {
    thread_local std::string name;
    return name;
}

std::string timestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return std::format("{:02}:{:02}:{:02}.{:03}", tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
}

}  // namespace

std::string_view to_string(Level level) noexcept {
    switch (level) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO";
        case Level::Warn: return "WARN";
        case Level::Error: return "ERROR";
        case Level::Off: return "OFF";
    }
    return "?";
}

void set_level(Level level) noexcept { g_level.store(level, std::memory_order_relaxed); }
Level level() noexcept { return g_level.load(std::memory_order_relaxed); }

void set_timing_enabled(bool on) noexcept { g_timing.store(on, std::memory_order_relaxed); }
bool timing_enabled() noexcept { return g_timing.load(std::memory_order_relaxed); }

void set_stderr_enabled(bool on) noexcept { g_stderr.store(on, std::memory_order_relaxed); }

bool set_file(const std::filesystem::path& path) {
    std::lock_guard lock(g_mutex);
    if (g_file.is_open()) g_file.close();
    if (path.empty()) return true;
    g_file.open(path, std::ios::out | std::ios::app | std::ios::binary);
    return g_file.is_open();
}

void set_sink(Sink sink) {
    std::lock_guard lock(g_mutex);
    g_sink = std::move(sink);
}

void set_thread_name(std::string name) { tls_thread_name() = std::move(name); }
const std::string& thread_name() noexcept { return tls_thread_name(); }

void write(Level level, std::string_view message) noexcept {
    try {
        const std::string& tn = tls_thread_name();
        const std::string line = std::format("{} {:<5} [{}] {}\n", timestamp(), to_string(level),
                                             tn.empty() ? std::string_view{"?"} : std::string_view{tn},
                                             message);
        std::lock_guard lock(g_mutex);
        if (g_stderr.load(std::memory_order_relaxed)) {
            std::fwrite(line.data(), 1, line.size(), stderr);
        }
        if (g_file.is_open()) {
            g_file.write(line.data(), static_cast<std::streamsize>(line.size()));
            g_file.flush();  // giữ được log nếu app bị crash
        }
        if (g_sink) g_sink(level, message);
    } catch (...) {
        // Log không bao giờ được làm sập app.
    }
}

}  // namespace pdfread::log
