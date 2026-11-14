#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ui/page_layout.hpp"

using Catch::Approx;
using pdfread::ui::PageLayout;
using pdfread::ui::PagePoint;

namespace {

// 3 trang: 100x200, 300x100, 100x100 (point); 2 đơn vị/point; gap 10; margin 5.
// Đỉnh trang: 5, 5+400+10 = 415, 415+200+10 = 625. Cao: 625+200+5 = 830. Rộng: 300*2+2*5 = 610.
PageLayout make_layout() {
    PageLayout l;
    l.set_pages({{100, 200}, {300, 100}, {100, 100}});
    l.set_metrics(2.0, 10.0, 5.0);
    return l;
}

}  // namespace

TEST_CASE("PageLayout computes page rects and content size", "[ui][layout]") {
    const PageLayout l = make_layout();
    CHECK(l.page_count() == 3);
    CHECK(l.content_width() == Approx(610));
    CHECK(l.content_height() == Approx(830));

    const auto r0 = l.page_rect(0);
    CHECK(r0.x == Approx(205));  // căn giữa: (610 - 200) / 2
    CHECK(r0.y == Approx(5));
    CHECK(r0.w == Approx(200));
    CHECK(r0.h == Approx(400));

    const auto r1 = l.page_rect(1);
    CHECK(r1.x == Approx(5));
    CHECK(r1.y == Approx(415));
    CHECK(l.page_rect(2).y == Approx(625));
}

TEST_CASE("PageLayout pages_in finds pages intersecting a vertical band", "[ui][layout]") {
    const PageLayout l = make_layout();
    using P = std::pair<int, int>;
    CHECK(l.pages_in(0, 5) == P{0, -1});      // chỉ có lề trên
    CHECK(l.pages_in(0, 6) == P{0, 0});
    CHECK(l.pages_in(405, 415) == P{0, -1});  // nằm gọn trong khoảng trống giữa trang 0 và 1
    CHECK(l.pages_in(400, 416) == P{0, 1});
    CHECK(l.pages_in(400, 700) == P{0, 2});
    CHECK(l.pages_in(700, 2000) == P{2, 2});
    CHECK(l.pages_in(826, 2000) == P{0, -1});  // dưới trang cuối
    CHECK(l.pages_in(10, 10) == P{0, -1});
}

TEST_CASE("PageLayout page_near splits gaps between neighbouring pages", "[ui][layout]") {
    const PageLayout l = make_layout();
    CHECK(l.page_near(-100) == 0);
    CHECK(l.page_near(409.9) == 0);
    CHECK(l.page_near(410) == 1);  // giữa khoảng trống 405..415
    CHECK(l.page_near(619.9) == 1);
    CHECK(l.page_near(620) == 2);
    CHECK(l.page_near(1e9) == 2);
}

TEST_CASE("PageLayout converts between content and page coordinates", "[ui][layout]") {
    PageLayout l = make_layout();
    const PagePoint p = l.to_page(5 + 20, 415 + 30);
    CHECK(p.page == 1);
    CHECK(p.x == Approx(10));
    CHECK(p.y == Approx(15));

    const auto [cx, cy] = l.to_content(p);
    CHECK(cx == Approx(25));
    CHECK(cy == Approx(445));

    // Đổi zoom: cùng điểm trên trang phải ra vị trí content mới tương ứng.
    l.set_metrics(3.0, 10.0, 5.0);
    const auto [cx3, cy3] = l.to_content(p);
    CHECK(cx3 == Approx(l.page_rect(1).x + 30));
    CHECK(cy3 == Approx(l.page_rect(1).y + 45));
    const PagePoint back = l.to_page(cx3, cy3);
    CHECK(back.page == 1);
    CHECK(back.x == Approx(10));
    CHECK(back.y == Approx(15));
}

TEST_CASE("PageLayout typical_page picks the most common page size", "[ui][layout]") {
    PageLayout l;
    l.set_pages({{595, 842}, {595, 842}, {842, 595}, {595.3, 841.9}, {300, 200}});
    CHECK(l.typical_page().w == Approx(595));
    CHECK(l.typical_page().h == Approx(842));
    CHECK(l.max_page_width() == Approx(842));

    l.set_pages({{100, 100}, {200, 200}});  // hòa: lấy khổ xuất hiện trước
    CHECK(l.typical_page().w == Approx(100));
}

TEST_CASE("PageLayout typical_page uses the median for scanned books with varying sizes", "[ui][layout]") {
    PageLayout l;
    // 3 trang đầu cùng khổ nhỏ, 10 trang scan mỗi trang một khổ ~700 pt.
    std::vector<pdfread::ui::PageSize> pages{{466, 682}, {466, 682}, {466, 682}};
    for (int i = 0; i < 10; ++i) pages.push_back({695.0 + i, 840.0 + i});
    l.set_pages(pages);
    CHECK(l.typical_page().w > 690);
    CHECK(l.typical_page().h > 835);
}

TEST_CASE("PageLayout handles an empty document", "[ui][layout]") {
    PageLayout l;
    l.set_pages({});
    l.set_metrics(1.0, 10, 10);
    CHECK(l.page_count() == 0);
    CHECK(l.content_height() == 0);
    CHECK(l.pages_in(0, 100) == std::pair<int, int>{0, -1});
    CHECK(l.page_near(0) == -1);
    CHECK(l.to_page(0, 0).page == -1);
}
