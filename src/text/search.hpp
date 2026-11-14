#pragma once
// Tìm kiếm trong văn bản trang (tầng 2): không phân biệt hoa/thường, xuống dòng coi như
// khoảng trắng. Chuẩn hóa giữ nguyên độ dài để vị trí khớp dùng thẳng cho PageText (khung
// ký tự, tô kết quả).

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "text/types.hpp"

namespace pdfread::text {

// Viết thường từng ký tự (gồm chữ có dấu tiếng Việt, Hy Lạp), mọi khoảng trắng -> ' '.
// Cùng độ dài với đầu vào.
[[nodiscard]] std::u32string fold_for_search(std::u32string_view s);

// Câu truy vấn: fold, bỏ khoảng trắng hai đầu, gộp khoảng trắng liên tiếp.
[[nodiscard]] std::u32string normalize_query(std::u32string_view s);

// Mọi vị trí khớp của các cụm `needles` (đã normalize_query) trong `hay` (đã fold), theo thứ
// tự, không chồng lấn (ưu tiên cụm dài hơn tại cùng vị trí). Khoảng trắng trong cụm khớp với
// một hoặc nhiều khoảng trắng trong văn bản.
[[nodiscard]] std::vector<Span> find_matches(std::u32string_view hay, const std::vector<std::u32string>& needles);

// Đoạn trích UTF-8 quanh `span` (tối đa `radius` ký tự mỗi bên, cắt ở biên từ, "…" hai đầu).
[[nodiscard]] std::string snippet(std::u32string_view text, Span span, std::size_t radius = 40);

}  // namespace pdfread::text
