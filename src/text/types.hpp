#pragma once
// Kiểu dữ liệu chung của tầng 2 (xử lý text của trang). Không phụ thuộc MuPDF:
// module pdf chuyển fz_stext_page sang RawBlock rồi giao cho text::build_page_text.
// Tọa độ: point, gốc góc trên-trái khung trang (giống PageLayout / pdf::TextPage).

#include <algorithm>
#include <cstddef>
#include <vector>

namespace pdfread::text {

struct Box {
    float x0 = 0;
    float y0 = 0;
    float x1 = 0;
    float y1 = 0;

    [[nodiscard]] float width() const noexcept { return x1 - x0; }
    [[nodiscard]] float height() const noexcept { return y1 - y0; }
    [[nodiscard]] float cx() const noexcept { return (x0 + x1) / 2; }
    [[nodiscard]] float cy() const noexcept { return (y0 + y1) / 2; }
    [[nodiscard]] bool valid() const noexcept { return x1 > x0 || y1 > y0; }
    void include(const Box& b) noexcept {
        if (!b.valid()) return;
        if (!valid()) {
            *this = b;
            return;
        }
        x0 = std::min(x0, b.x0);
        y0 = std::min(y0, b.y0);
        x1 = std::max(x1, b.x1);
        y1 = std::max(y1, b.y1);
    }
};

struct RawChar {
    char32_t c = 0;
    Box box;
};

struct RawLine {
    std::vector<RawChar> chars;
    Box box;
};

struct RawBlock {
    std::vector<RawLine> lines;
    Box box;
};

// Khoảng nửa mở [begin, end) trên chỉ số ký tự (code point) của PageText::text().
struct Span {
    std::size_t begin = 0;
    std::size_t end = 0;

    [[nodiscard]] bool empty() const noexcept { return end <= begin; }
    [[nodiscard]] std::size_t size() const noexcept { return empty() ? 0 : end - begin; }
    [[nodiscard]] bool overlaps(const Span& o) const noexcept { return begin < o.end && o.begin < end; }
    [[nodiscard]] bool contains(const Span& o) const noexcept { return begin <= o.begin && o.end <= end; }
    friend bool operator==(const Span&, const Span&) = default;
};

}  // namespace pdfread::text
