#pragma once
// Làm sạch ký tự lấy từ PDF.

#include <string>

namespace pdfread::text {

// Ký tự khoảng trắng (gồm khoảng trắng Unicode: NBSP, thin space…).
[[nodiscard]] bool is_space(char32_t c) noexcept;
// Gạch nối có thể dùng để ngắt từ cuối dòng: '-', U+00AD (soft), U+2010, U+2011.
[[nodiscard]] bool is_hyphen(char32_t c) noexcept;
[[nodiscard]] bool is_lower(char32_t c) noexcept;  // chữ thường Latin (gồm Latin-1, Latin Extended)
[[nodiscard]] bool is_upper(char32_t c) noexcept;
[[nodiscard]] bool is_digit(char32_t c) noexcept;
[[nodiscard]] bool is_letter(char32_t c) noexcept;

// Ghi dạng đã làm sạch của `c` vào `out`:
//  - ký tự ghép (ﬀ ﬁ ﬂ ﬃ ﬄ ﬅ ﬆ) -> các chữ cái rời
//  - mọi khoảng trắng Unicode, tab -> ' '
//  - ký tự vô hình (zero-width, BOM, soft hyphen giữa dòng) -> bỏ
//  - còn lại giữ nguyên
// Trả số ký tự đã ghi (0, 1, 2 hoặc 3).
std::size_t append_normalized(std::u32string& out, char32_t c);

}  // namespace pdfread::text
