// Ánh xạ màn hình <-> trang có tính zoom, DPI, cuộn và căn giữa.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ui/page_layout.hpp"

using Catch::Approx;
using namespace pdfread::ui;

namespace {

// Hai trang A4 (595x842 pt). zoom 150%, DPI 1.25 -> 1.5 * 96/72 * 1.25 = 2.5 đơn vị/point.
PageLayout a4_layout(double units_per_pt) {
    PageLayout l;
    l.set_pages({{595, 842}, {595, 842}});
    l.set_metrics(units_per_pt, 10, 12);
    return l;
}

}  // namespace

TEST_CASE("ViewTransform maps screen and content both ways", "[ui][coords]") {
    const ViewTransform v{.origin_x = 0, .origin_y = 40, .scroll_x = 30, .scroll_y = 500, .offset_x = 0};
    const auto [cx, cy] = v.to_content(100, 140);
    CHECK(cx == Approx(130));
    CHECK(cy == Approx(600));
    const auto [sx, sy] = v.to_screen(cx, cy);
    CHECK(sx == Approx(100));
    CHECK(sy == Approx(140));

    const ViewTransform centered{.origin_x = 5, .origin_y = 0, .scroll_x = 0, .scroll_y = 0, .offset_x = 200};
    CHECK(centered.to_content(205, 0).first == Approx(0));
}

TEST_CASE("screen_to_page accounts for zoom, DPI and scrolling", "[ui][coords]") {
    const double upp = 1.5 * 96.0 / 72.0 * 1.25;  // 2.5
    REQUIRE(upp == Approx(2.5));
    const PageLayout l = a4_layout(upp);
    // Trang 2 bắt đầu ở y = 12 + 842*2.5 + 10 = 2127; trang rộng hơn mọi vùng nhìn nên x = 12.
    REQUIRE(l.page_rect(1).y == Approx(2127));
    REQUIRE(l.page_rect(1).x == Approx(12));

    const ViewTransform v{.origin_x = 0, .origin_y = 48, .scroll_x = 100, .scroll_y = 2000, .offset_x = 0};
    // Điểm (100 pt, 50 pt) trên trang 2 nằm ở content (12 + 250, 2127 + 125) = (262, 2252)
    // -> màn hình (262 - 100, 2252 - 2000 + 48) = (162, 300).
    const PagePoint p = screen_to_page(l, v, 162, 300);
    CHECK(p.page == 1);
    CHECK(p.x == Approx(100));
    CHECK(p.y == Approx(50));

    const auto [sx, sy] = page_to_screen(l, v, p);
    CHECK(sx == Approx(162));
    CHECK(sy == Approx(300));
}

TEST_CASE("screen_to_page round-trips across zoom levels", "[ui][coords]") {
    for (double upp : {0.5, 1.0, 1.3333, 2.0, 4.75, 10.0}) {
        const PageLayout l = a4_layout(upp);
        const ViewTransform v{.origin_x = 7, .origin_y = 33, .scroll_x = 13, .scroll_y = 900 * upp, .offset_x = 4};
        for (const PagePoint& in : {PagePoint{0, 10, 20}, PagePoint{1, 300.5, 421}, PagePoint{1, 594, 841}}) {
            const auto [sx, sy] = page_to_screen(l, v, in);
            const PagePoint out = screen_to_page(l, v, sx, sy);
            CHECK(out.page == in.page);
            CHECK(out.x == Approx(in.x).margin(1e-6));
            CHECK(out.y == Approx(in.y).margin(1e-6));
        }
    }
}

TEST_CASE("screen_to_page outside pages returns the nearest page", "[ui][coords]") {
    const PageLayout l = a4_layout(1.0);
    const ViewTransform v{};
    // Trong khoảng trống giữa hai trang (y content 854..864): nửa trên thuộc trang 1.
    CHECK(screen_to_page(l, v, 100, 856).page == 0);
    CHECK(screen_to_page(l, v, 100, 862).page == 1);
    // Bên trái trang: x âm.
    const PagePoint left = screen_to_page(l, v, 2, 100);
    CHECK(left.page == 0);
    CHECK(left.x < 0);
}
