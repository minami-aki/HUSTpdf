#include "ui/selection.hpp"

#include <algorithm>
#include <utility>

namespace pdfread::ui {

void Selection::clear() {
    anchor_ = {};
    focus_ = {};
    dragging_ = false;
    dirty_ = false;
    quads_.clear();
    if (!text_.empty()) ++version_;
    text_.clear();
}

void Selection::begin(const SelectionPoint& p, pdf::SelectUnit unit) {
    anchor_ = p;
    focus_ = p;
    unit_ = unit;
    dragging_ = true;
    dirty_ = true;
}

void Selection::extend(const SelectionPoint& p) {
    if (!has_anchor()) return begin(p, pdf::SelectUnit::Chars);
    if (p.page == focus_.page && p.pt.x == focus_.pt.x && p.pt.y == focus_.pt.y) return;
    focus_ = p;
    dragging_ = true;
    dirty_ = true;
}

std::pair<int, int> Selection::page_range() const noexcept {
    if (!has_anchor()) return {-1, -1};
    return {std::min(anchor_.page, focus_.page), std::max(anchor_.page, focus_.page)};
}

const std::vector<pdf::Quad>* Selection::quads(int page) const {
    auto it = quads_.find(page);
    return it == quads_.end() ? nullptr : &it->second;
}

void Selection::update(const TextLookup& text_of) {
    if (!dirty_ || !has_anchor()) return;

    // Đầu vùng chọn = điểm thuộc trang nhỏ hơn. Trong cùng một trang MuPDF tự sắp
    // hai điểm theo thứ tự đọc.
    const bool forward = anchor_.page <= focus_.page;
    const SelectionPoint& first = forward ? anchor_ : focus_;
    const SelectionPoint& last = forward ? focus_ : anchor_;

    quads_.clear();
    std::string previous = std::move(text_);
    text_.clear();
    bool complete = true;
    for (int page = first.page; page <= last.page; ++page) {
        const pdf::TextPagePtr text = text_of(page);
        if (!text) {
            complete = false;
            continue;
        }
        pdf::PointF a = page == first.page ? first.pt : text->page_start();
        pdf::PointF b = page == last.page ? last.pt : text->page_end();
        text->snap(a, b, unit_);

        std::vector<pdf::Quad> q = text->highlight(a, b);
        if (q.empty()) continue;
        quads_.emplace(page, std::move(q));

        std::string part = text->copy(a, b);
        if (!text_.empty() && !part.empty() && text_.back() != '\n') text_.push_back('\n');
        text_ += part;
    }
    dirty_ = !complete;
    if (text_ != previous) ++version_;
}

}  // namespace pdfread::ui
