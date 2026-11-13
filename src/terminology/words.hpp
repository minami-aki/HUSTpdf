#pragma once
// Tách từ đơn giản cho tiếng Anh kỹ thuật (dùng chung cho phân loại lĩnh vực, khớp
// glossary và trích thuật ngữ): chữ cái/chữ số liền nhau là một từ, '-' và khoảng trắng
// ngăn từ; hạ chữ thường ASCII.

#include <string>
#include <string_view>
#include <vector>

#include "text/types.hpp"

namespace pdfread::terminology {

struct Word {
    std::u32string text;  // đã viết thường
    text::Span span;      // vị trí trong chuỗi gốc
};

[[nodiscard]] std::vector<Word> split_words(std::u32string_view text);

// Viết thường ASCII + Latin-1.
[[nodiscard]] char32_t lower(char32_t c) noexcept;
[[nodiscard]] std::u32string lower(std::u32string_view s);

// `word` là `base` hoặc dạng số nhiều thông thường của nó (s, es, y -> ies).
[[nodiscard]] bool same_or_plural(std::u32string_view word, std::u32string_view base) noexcept;

// Danh sách từ chức năng tiếng Anh (the, of, and…).
[[nodiscard]] bool is_stopword(std::u32string_view lower_word) noexcept;

}  // namespace pdfread::terminology
