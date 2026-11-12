#pragma once

#include <filesystem>

namespace pdfread::ui {

inline constexpr float kBaseFontSize = 16.0f;

// Nạp Noto Sans (tiếng Việt, Hy Lạp) + gộp ký hiệu toán từ Noto Sans Math.
// ImGui >= 1.92 tạo glyph theo nhu cầu nên không cần khai báo dải glyph.
// Trả false nếu thiếu file font (khi đó dùng font mặc định của ImGui, không có dấu).
bool load_fonts(const std::filesystem::path& font_dir);

// Đặt style giao diện cho hệ số DPI `scale` (gọi lại khi đổi màn hình).
void apply_style(float scale);

}  // namespace pdfread::ui
