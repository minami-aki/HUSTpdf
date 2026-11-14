#pragma once
// Chế độ đọc: màu trang (ánh xạ đen -> ink, trắng -> paper khi render tile) và theme giao diện.

#include <cstdint>
#include <optional>
#include <string_view>

#include "ui/viewer.hpp"

namespace pdfread::ui {

enum class ReadingMode : int {
    Normal = 0,  // giấy trắng, chữ đen
    Sepia = 1,   // giấy ngà, chữ nâu sẫm — dễ chịu khi đọc lâu
    Dark = 2,    // nền xám đen, chữ xám sáng (không dùng đen/trắng tuyệt đối để đỡ chói)
    Warm = 3,    // giảm ánh xanh (đọc buổi tối)
};
inline constexpr int kReadingModeCount = 4;

[[nodiscard]] PageColors page_colors(ReadingMode mode);
[[nodiscard]] bool is_dark(ReadingMode mode) noexcept;
[[nodiscard]] const char* reading_mode_name(ReadingMode mode) noexcept;   // tiếng Việt, để hiển thị
[[nodiscard]] const char* reading_mode_key(ReadingMode mode) noexcept;    // lưu cài đặt
[[nodiscard]] std::optional<ReadingMode> reading_mode_from_key(std::string_view key) noexcept;

// Theme giao diện hiện tại (popup dịch dùng để chọn màu nền/chữ). Đặt bởi apply_style().
[[nodiscard]] bool dark_ui() noexcept;

}  // namespace pdfread::ui
