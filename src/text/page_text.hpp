#pragma once
// Mô hình text của một trang (tầng 2): text đã làm sạch theo thứ tự đọc, chia đoạn và
// câu, kèm khung tọa độ của từng ký tự để ánh xạ vùng bôi đen <-> câu <-> đoạn.
// Dựng trên luồng nền (một lần mỗi trang), sau đó chỉ đọc.

#include <functional>
#include <string>
#include <vector>

#include "text/types.hpp"

namespace pdfread::text {

struct Paragraph {
    Span span;
    std::size_t first_sentence = 0;
    std::size_t sentence_count = 0;
};

struct Sentence {
    Span span;
    std::size_t paragraph = 0;
};

class PageText {
public:
    // Toàn bộ text của trang; các đoạn cách nhau bằng '\n' (không thuộc đoạn nào).
    [[nodiscard]] const std::u32string& text() const noexcept { return text_; }
    [[nodiscard]] const std::vector<Paragraph>& paragraphs() const noexcept { return paragraphs_; }
    [[nodiscard]] const std::vector<Sentence>& sentences() const noexcept { return sentences_; }

    // Khung của ký tự i (không hợp lệ với dấu cách tự chèn khi nối dòng) và dòng chứa nó.
    [[nodiscard]] const Box& box(std::size_t i) const { return boxes_.at(i); }
    [[nodiscard]] int line_of(std::size_t i) const { return lines_.at(i); }

    [[nodiscard]] std::string utf8(Span span) const;

    // Câu chứa ký tự i; ký tự nằm giữa hai câu (dấu cách, '\n') thuộc câu sau gần nhất.
    // -1 nếu trang không có câu nào.
    [[nodiscard]] int sentence_at(std::size_t i) const;

    // Khoảng nhỏ nhất bao mọi ký tự có khung thỏa `pred` (rỗng nếu không có).
    [[nodiscard]] Span span_where(const std::function<bool(const Box&)>& pred) const;

    // Một hình chữ nhật cho mỗi dòng mà `span` đi qua (để tô sáng).
    [[nodiscard]] std::vector<Box> line_rects(Span span) const;

private:
    friend PageText build_page_text(const std::vector<RawBlock>& blocks);

    std::u32string text_;
    std::vector<Box> boxes_;  // song song với text_
    std::vector<int> lines_;  // song song với text_; -1 với ký tự chèn thêm
    std::vector<Paragraph> paragraphs_;
    std::vector<Sentence> sentences_;
};

// Dựng PageText từ các khối text thô (thứ tự bất kỳ; tự sắp theo thứ tự đọc).
[[nodiscard]] PageText build_page_text(const std::vector<RawBlock>& blocks);

}  // namespace pdfread::text
