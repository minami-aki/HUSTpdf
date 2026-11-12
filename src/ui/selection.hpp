#pragma once
// Vùng bôi đen: điểm neo (nơi nhấn chuột) và điểm cuối (nơi chuột đang ở), có thể
// nằm trên hai trang khác nhau. Từ đó tính tứ giác tô sáng và text của từng trang.
// Thuần logic (không ImGui/GL); text của trang lấy qua callback để test được.

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "pdf/text_page.hpp"

namespace pdfread::ui {

struct SelectionPoint {
    int page = -1;
    pdf::PointF pt;  // point, gốc góc trên-trái trang
};

class Selection {
public:
    using TextLookup = std::function<pdf::TextPagePtr(int page)>;

    void clear();

    // Nhấn chuột: bắt đầu vùng chọn mới theo đơn vị ký tự / từ / dòng.
    void begin(const SelectionPoint& p, pdf::SelectUnit unit);
    // Kéo chuột (hoặc Shift+click): dời điểm cuối.
    void extend(const SelectionPoint& p);
    void end_drag() noexcept { dragging_ = false; }

    [[nodiscard]] bool dragging() const noexcept { return dragging_; }
    [[nodiscard]] bool has_anchor() const noexcept { return anchor_.page >= 0; }
    // Có text được chọn (sau update()).
    [[nodiscard]] bool active() const noexcept { return !text_.empty(); }

    // Tính lại tứ giác và text nếu vùng chọn đổi hoặc trước đó còn thiếu text của trang.
    void update(const TextLookup& text_of);

    // Trang đầu/cuối của vùng chọn (theo thứ tự trang), {-1,-1} nếu chưa có.
    [[nodiscard]] std::pair<int, int> page_range() const noexcept;
    [[nodiscard]] const std::vector<pdf::Quad>* quads(int page) const;
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    // Đã có text của mọi trang trong vùng chọn.
    [[nodiscard]] bool complete() const noexcept { return !dirty_; }

private:
    SelectionPoint anchor_;
    SelectionPoint focus_;
    pdf::SelectUnit unit_ = pdf::SelectUnit::Chars;
    bool dragging_ = false;
    bool dirty_ = false;

    std::map<int, std::vector<pdf::Quad>> quads_;
    std::string text_;
};

}  // namespace pdfread::ui
