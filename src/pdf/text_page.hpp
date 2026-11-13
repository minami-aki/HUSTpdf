#pragma once
// Text có cấu trúc của một trang (fz_stext_page, đã bỏ gạch nối cuối dòng) và các
// thao tác bôi đen. Tọa độ: point, gốc ở góc trên-trái khung trang (giống PageLayout).
//
// Dựng trên luồng nền (TextService), sau đó chỉ dùng ĐỌC trên UI thread.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "pdf/document.hpp"
#include "text/page_text.hpp"

namespace pdfread::pdf {

struct PointF {
    float x = 0;
    float y = 0;
};

// Tứ giác tô sáng (thường là hình chữ nhật, có thể nghiêng nếu chữ xoay).
struct Quad {
    PointF ul, ur, ll, lr;
};

enum class SelectUnit { Chars, Words, Lines };

class TextPage {
public:
    // Dựng từ display list của trang. Ném pdf::Error khi lỗi.
    [[nodiscard]] static std::shared_ptr<const TextPage> build(const DisplayList& list, const PageBox& box);
    ~TextPage();

    TextPage(const TextPage&) = delete;
    TextPage& operator=(const TextPage&) = delete;

    // Nới hai đầu vùng chọn [a, b] ra biên từ / dòng. Với Chars không đổi gì.
    void snap(PointF& a, PointF& b, SelectUnit unit) const;
    // Các tứ giác cần tô cho vùng chọn từ a tới b (theo thứ tự đọc).
    [[nodiscard]] std::vector<Quad> highlight(PointF a, PointF b) const;
    // Text UTF-8 của vùng chọn (xuống dòng bằng "\n").
    [[nodiscard]] std::string copy(PointF a, PointF b) const;

    // Điểm nằm trong ký tự đầu tiên / cuối cùng của trang theo thứ tự đọc: chọn từ
    // page_start() tới page_end() là chọn toàn bộ text của trang. (Không dùng góc trang:
    // với điểm ngoài mọi dòng, MuPDF chọn dòng gần nhất và khi hòa lấy dòng sau, nên
    // ở trang hai cột góc trên-trái bị hiểu thành đầu cột phải.)
    [[nodiscard]] PointF page_start() const noexcept { return start_; }
    [[nodiscard]] PointF page_end() const noexcept { return end_; }

    [[nodiscard]] int char_count() const noexcept { return char_count_; }

    // Text đã làm sạch, chia đoạn, tách câu (tầng 2).
    [[nodiscard]] const text::PageText& page_text() const noexcept { return page_text_; }

    // Dời điểm vào trong ký tự gần nhất (khoảng cách 2D), giữ phía gần điểm gốc. Mọi điểm
    // đưa cho MuPDF đều qua bước này: với điểm nằm ngoài mọi dòng (khe giữa hai dòng, lề),
    // MuPDF chọn dòng gần nhất theo chiều dọc và khi hòa lấy dòng sau — ở trang hai cột,
    // dòng cùng độ cao của cột phải luôn thắng dù chuột đang ở cột trái.
    [[nodiscard]] PointF snap_to_text(PointF p) const;

private:
    TextPage(void* page, const PageBox& box, int char_count, PointF start, PointF end, text::PageText page_text) noexcept
        : page_(page), box_(box), char_count_(char_count), start_(start), end_(end), page_text_(std::move(page_text)) {}

    // fz_stext_page* (MuPDF khai báo là typedef của struct ẩn danh nên không
    // forward-declare được; ép kiểu trong text_page.cpp).
    void* page_;
    PageBox box_;
    int char_count_;
    PointF start_;
    PointF end_;
    text::PageText page_text_;
};

using TextPagePtr = std::shared_ptr<const TextPage>;

// Nối các từ bị ngắt bằng gạch nối ở cuối dòng. MuPDF 1.28 với FZ_STEXT_DEHYPHENATE chỉ
// đánh dấu dòng "joined" nhưng fz_copy_selection vẫn giữ nguyên gạch nối, nên xử lý ở đây:
//  - gạch nối (-, U+2010, U+2011) + "\n" + chữ thường  -> bỏ cả hai ("inter-\nnational" -> "international")
//  - gạch nối + "\n" + chữ hoa/số/khác                 -> giữ gạch nối, bỏ "\n" ("Runge-\nKutta")
//  - soft hyphen (U+00AD) + "\n"                       -> luôn bỏ cả hai
//  - gạch nối đứng sau dấu cách là dấu gạch ngang      -> giữ nguyên
// Hạn chế đã biết: từ ghép thật bị ngắt đúng chỗ gạch nối ("self-\ncontained") bị nối
// thành "selfcontained"; tầng làm sạch text ở M4 có thể sửa bằng danh sách từ.
[[nodiscard]] std::string join_hyphenated_lines(std::string_view text);

}  // namespace pdfread::pdf
