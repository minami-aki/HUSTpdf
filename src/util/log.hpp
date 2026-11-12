#pragma once
// Log đơn giản, thread-safe. Ghi ra stderr, file (tùy chọn) và một sink tùy chọn.
//
//   pdfread::log::info("Mở {} ({} trang)", path, pages);
//
// Log đo thời gian (ScopedTimer trong timer.hpp) có công tắc riêng
// set_timing_enabled(), độc lập với level.

#include <filesystem>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace pdfread::log {

enum class Level : int { Trace = 0, Debug, Info, Warn, Error, Off };

[[nodiscard]] std::string_view to_string(Level level) noexcept;

void set_level(Level level) noexcept;
[[nodiscard]] Level level() noexcept;
[[nodiscard]] inline bool enabled(Level lv) noexcept { return lv != Level::Off && lv >= level(); }

void set_timing_enabled(bool on) noexcept;
[[nodiscard]] bool timing_enabled() noexcept;

void set_stderr_enabled(bool on) noexcept;

// Ghi thêm (append) vào file. Đường dẫn rỗng = đóng file. Trả false nếu không mở được.
bool set_file(const std::filesystem::path& path);

// Sink bổ sung, nhận level và nội dung thô (chưa có timestamp). nullptr để gỡ.
// Sink được gọi khi đang giữ khóa của log: KHÔNG được gọi log bên trong sink.
using Sink = std::function<void(Level, std::string_view message)>;
void set_sink(Sink sink);

// Tên luồng hiển thị trong log (thread_local). Xem thêm name_current_thread().
void set_thread_name(std::string name);
[[nodiscard]] const std::string& thread_name() noexcept;

// Ghi một dòng. Không lọc theo level: các hàm tiện ích bên dưới đã lọc trước khi format.
void write(Level level, std::string_view message) noexcept;

template <class... Args>
void trace(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Trace)) write(Level::Trace, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Debug)) write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Info)) write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Warn)) write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Error)) write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace pdfread::log
