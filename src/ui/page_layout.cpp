#include "ui/page_layout.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace pdfread::ui {

void PageLayout::set_pages(std::vector<PageSize> pages) {
    pages_ = std::move(pages);
    max_w_ = 0;
    max_h_ = 0;
    // Đếm khổ trang (làm tròn 1 pt); hòa thì chọn khổ xuất hiện trước.
    std::map<std::pair<long, long>, std::pair<int, std::size_t>> counts;  // khổ -> (số trang, trang đầu)
    for (std::size_t i = 0; i < pages_.size(); ++i) {
        const PageSize& p = pages_[i];
        max_w_ = std::max(max_w_, p.w);
        max_h_ = std::max(max_h_, p.h);
        auto [it, inserted] = counts.try_emplace({std::lround(p.w), std::lround(p.h)}, 0, i);
        ++it->second.first;
    }
    typical_ = {};
    int best = 0;
    std::size_t best_first = 0;
    for (const auto& [size, info] : counts) {
        if (info.first > best || (info.first == best && info.second < best_first)) {
            best = info.first;
            best_first = info.second;
            typical_ = pages_[info.second];
        }
    }
    rebuild();
}

void PageLayout::set_metrics(double units_per_pt, double gap, double margin) {
    upp_ = units_per_pt;
    gap_ = gap;
    margin_ = margin;
    rebuild();
}

void PageLayout::rebuild() {
    tops_.resize(pages_.size());
    double y = margin_;
    for (std::size_t i = 0; i < pages_.size(); ++i) {
        tops_[i] = y;
        y += pages_[i].h * upp_;
        if (i + 1 < pages_.size()) y += gap_;
    }
    content_h_ = pages_.empty() ? 0 : y + margin_;
    content_w_ = pages_.empty() ? 0 : max_w_ * upp_ + 2 * margin_;
}

Rect PageLayout::page_rect(int i) const {
    const PageSize& p = page_size(i);
    const double w = p.w * upp_;
    return Rect{(content_w_ - w) / 2, tops_[static_cast<std::size_t>(i)], w, p.h * upp_};
}

std::pair<int, int> PageLayout::pages_in(double y0, double y1) const {
    if (pages_.empty() || y1 <= y0) return {0, -1};
    // Trang đầu tiên có đáy > y0.
    int first = static_cast<int>(std::upper_bound(tops_.begin(), tops_.end(), y0) - tops_.begin()) - 1;
    first = std::max(first, 0);
    if (page_rect(first).bottom() <= y0) ++first;
    // Trang cuối cùng có đỉnh < y1.
    const int last = static_cast<int>(std::lower_bound(tops_.begin(), tops_.end(), y1) - tops_.begin()) - 1;
    if (first > last || first >= page_count()) return {0, -1};
    return {first, last};
}

int PageLayout::page_near(double y) const {
    if (pages_.empty()) return -1;
    // Ranh giới giữa trang i-1 và i: giữa khoảng trống phía trên trang i.
    int lo = 0;
    int hi = page_count() - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        const double boundary = tops_[static_cast<std::size_t>(mid)] - gap_ / 2;
        if (y >= boundary) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

PagePoint PageLayout::to_page(double cx, double cy) const {
    const int i = page_near(cy);
    if (i < 0 || upp_ <= 0) return {};
    const Rect r = page_rect(i);
    return PagePoint{i, (cx - r.x) / upp_, (cy - r.y) / upp_};
}

std::pair<double, double> PageLayout::to_content(const PagePoint& p) const {
    if (p.page < 0 || p.page >= page_count()) return {0, 0};
    const Rect r = page_rect(p.page);
    return {r.x + p.x * upp_, r.y + p.y * upp_};
}

PagePoint screen_to_page(const PageLayout& layout, const ViewTransform& view, double sx, double sy) {
    const auto [cx, cy] = view.to_content(sx, sy);
    return layout.to_page(cx, cy);
}

std::pair<double, double> page_to_screen(const PageLayout& layout, const ViewTransform& view, const PagePoint& p) {
    const auto [cx, cy] = layout.to_content(p);
    return view.to_screen(cx, cy);
}

}  // namespace pdfread::ui
