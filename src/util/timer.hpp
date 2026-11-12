#pragma once
// Đo thời gian: Stopwatch (đo thủ công) và ScopedTimer (tự log khi ra khỏi scope).
//
//   { pdfread::ScopedTimer t("render page 5"); ... }
//   -> "[timing] render page 5: 12.34 ms"   (chỉ khi log::timing_enabled())

#include <chrono>
#include <format>
#include <string>
#include <string_view>

#include "util/log.hpp"

namespace pdfread {

class Stopwatch {
public:
    using clock = std::chrono::steady_clock;

    Stopwatch() noexcept : start_(clock::now()) {}

    void reset() noexcept { start_ = clock::now(); }
    [[nodiscard]] clock::duration elapsed() const noexcept { return clock::now() - start_; }
    [[nodiscard]] double elapsed_ms() const noexcept {
        return std::chrono::duration<double, std::milli>(elapsed()).count();
    }

private:
    clock::time_point start_;
};

class ScopedTimer {
public:
    // Khi timing đang tắt, không cấp phát gì và destructor không ghi gì.
    explicit ScopedTimer(std::string_view label) : active_(log::timing_enabled()) {
        if (active_) label_ = label;
    }
    ~ScopedTimer() {
        if (!active_) return;
        try {
            log::write(log::Level::Info, std::format("[timing] {}: {:.2f} ms", label_, watch_.elapsed_ms()));
        } catch (...) {
        }
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

    [[nodiscard]] double elapsed_ms() const noexcept { return watch_.elapsed_ms(); }

private:
    bool active_;
    std::string label_;
    Stopwatch watch_;
};

}  // namespace pdfread
