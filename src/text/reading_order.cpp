#include "text/reading_order.hpp"

#include <algorithm>
#include <numeric>

namespace pdfread::text {
namespace {

constexpr float kMinGutter = 6.0f;     // khe dọc tối thiểu giữa hai cột (point)
constexpr float kMinRowGap = 0.5f;     // khe ngang tối thiểu
constexpr float kWideFraction = 0.6f;  // khối "rộng" so với bề rộng nhóm

using Group = std::vector<std::size_t>;

void order_group(const std::vector<Box>& b, Group group, std::vector<std::size_t>& out, int depth);

bool try_vertical_cut(const std::vector<Box>& b, Group& g, std::vector<std::size_t>& out, int depth) {
    std::sort(g.begin(), g.end(), [&](std::size_t l, std::size_t r) { return b[l].x0 < b[r].x0; });
    float max_x1 = b[g[0]].x1;
    for (std::size_t i = 1; i < g.size(); ++i) {
        if (b[g[i]].x0 - max_x1 >= kMinGutter) {
            order_group(b, Group(g.begin(), g.begin() + static_cast<std::ptrdiff_t>(i)), out, depth + 1);
            order_group(b, Group(g.begin() + static_cast<std::ptrdiff_t>(i), g.end()), out, depth + 1);
            return true;
        }
        max_x1 = std::max(max_x1, b[g[i]].x1);
    }
    return false;
}

bool try_horizontal_cut(const std::vector<Box>& b, Group& g, std::vector<std::size_t>& out, int depth) {
    std::sort(g.begin(), g.end(), [&](std::size_t l, std::size_t r) { return b[l].y0 < b[r].y0; });
    float gx0 = b[g[0]].x0;
    float gx1 = b[g[0]].x1;
    for (std::size_t i : g) {
        gx0 = std::min(gx0, b[i].x0);
        gx1 = std::max(gx1, b[i].x1);
    }
    const float wide = (gx1 - gx0) * kWideFraction;

    // Chia thành các dải theo khe ngang.
    std::vector<Group> bands(1);
    float max_y1 = b[g[0]].y1;
    bands.back().push_back(g[0]);
    for (std::size_t i = 1; i < g.size(); ++i) {
        if (b[g[i]].y0 - max_y1 >= kMinRowGap) bands.emplace_back();
        bands.back().push_back(g[i]);
        max_y1 = std::max(max_y1, b[g[i]].y1);
    }
    if (bands.size() < 2) return false;

    // Gộp các dải hẹp liền nhau (vùng nhiều cột) thành một.
    auto is_wide = [&](const Group& band) {
        return std::any_of(band.begin(), band.end(), [&](std::size_t i) { return b[i].width() >= wide; });
    };
    std::vector<Group> merged;
    for (Group& band : bands) {
        if (!merged.empty() && !is_wide(band) && !is_wide(merged.back())) {
            merged.back().insert(merged.back().end(), band.begin(), band.end());
        } else {
            merged.push_back(std::move(band));
        }
    }
    if (merged.size() < 2) return false;
    for (Group& band : merged) order_group(b, std::move(band), out, depth + 1);
    return true;
}

void order_group(const std::vector<Box>& b, Group group, std::vector<std::size_t>& out, int depth) {
    if (group.size() <= 1 || depth > 64) {
        std::sort(group.begin(), group.end(), [&](std::size_t l, std::size_t r) {
            return b[l].y0 != b[r].y0 ? b[l].y0 < b[r].y0 : b[l].x0 < b[r].x0;
        });
        out.insert(out.end(), group.begin(), group.end());
        return;
    }
    if (try_vertical_cut(b, group, out, depth)) return;
    if (try_horizontal_cut(b, group, out, depth)) return;
    std::sort(group.begin(), group.end(), [&](std::size_t l, std::size_t r) {
        return b[l].y0 != b[r].y0 ? b[l].y0 < b[r].y0 : b[l].x0 < b[r].x0;
    });
    out.insert(out.end(), group.begin(), group.end());
}

}  // namespace

std::vector<std::size_t> reading_order(const std::vector<Box>& blocks) {
    Group all(blocks.size());
    std::iota(all.begin(), all.end(), std::size_t{0});
    std::vector<std::size_t> out;
    out.reserve(blocks.size());
    order_group(blocks, std::move(all), out, 0);
    return out;
}

}  // namespace pdfread::text
