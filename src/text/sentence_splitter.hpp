#pragma once
// Tách câu theo luật cho văn bản khoa học – kỹ thuật tiếng Anh.
//
// Ranh giới câu: '.', '!', '?', '…' (có thể kèm dấu đóng ngoặc/nháy) + khoảng trắng +
// chữ hoa / chữ số / dấu mở ngoặc / hết đoạn. KHÔNG tách khi:
//  - từ trước dấu chấm là từ viết tắt: Fig., Eq., e.g., i.e., et al., No., vs., Sec., approx.…
//  - từ trước dấu chấm có dấu chấm bên trong (U.S., e.g.) hoặc là một chữ cái hoa (J. Smith)
//  - câu tới đó chỉ là số thứ tự / mục ("1.", "2.3.", "(a).", "IV.")
//  - sau dấu chấm là chữ thường ("approx. 270", "Vs. the")
//  - dấu chấm không theo sau bởi khoảng trắng (3.14, v1.2, www.example.com)

#include <string_view>
#include <vector>

#include "text/types.hpp"

namespace pdfread::text {

// Tách `paragraph` thành các câu; Span tính theo chỉ số trong `paragraph` cộng `offset`,
// đã bỏ khoảng trắng hai đầu.
[[nodiscard]] std::vector<Span> split_sentences(std::u32string_view paragraph, std::size_t offset = 0);

// Từ (không kèm dấu chấm, không phân biệt hoa/thường) có phải từ viết tắt thường gặp.
[[nodiscard]] bool is_abbreviation(std::u32string_view word);

}  // namespace pdfread::text
