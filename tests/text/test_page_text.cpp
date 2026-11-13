#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "support/raw_text.hpp"
#include "text/normalize.hpp"
#include "text/page_text.hpp"
#include "text/reading_order.hpp"
#include "util/utf8.hpp"

using namespace pdfread;
using testing::make_block;

namespace {

std::vector<std::string> paragraphs(const text::PageText& pt) {
    std::vector<std::string> out;
    for (const auto& p : pt.paragraphs()) out.push_back(pt.utf8(p.span));
    return out;
}

std::vector<std::string> sentences(const text::PageText& pt) {
    std::vector<std::string> out;
    for (const auto& s : pt.sentences()) out.push_back(pt.utf8(s.span));
    return out;
}

using V = std::vector<std::string>;

}  // namespace

TEST_CASE("append_normalized expands ligatures and cleans spaces", "[text][normalize]") {
    std::u32string out;
    for (char32_t c : std::u32string(U"ﬁnite ﬂow eﬃcient x​y­z")) text::append_normalized(out, c);
    CHECK(to_utf8(out) == "finite flow efficient xyz");
}

TEST_CASE("reading_order reads two columns left then right", "[text][order]") {
    // Tiêu đề toàn trang, hai cột (khoảng cách đoạn hai cột thẳng hàng), chú thích toàn trang, hai cột tiếp.
    const std::vector<text::Box> blocks = {
        {300, 100, 540, 150},  // 0: cột phải, đoạn 1
        {50, 40, 540, 60},     // 1: tiêu đề
        {50, 100, 290, 150},   // 2: cột trái, đoạn 1
        {50, 160, 290, 210},   // 3: cột trái, đoạn 2
        {300, 160, 540, 210},  // 4: cột phải, đoạn 2
        {50, 230, 540, 250},   // 5: chú thích hình toàn trang
        {300, 270, 540, 300},  // 6: cột phải phần dưới
        {50, 270, 290, 300},   // 7: cột trái phần dưới
    };
    CHECK(text::reading_order(blocks) == std::vector<std::size_t>{1, 2, 3, 0, 4, 5, 7, 6});
}

TEST_CASE("reading_order keeps a single column top to bottom", "[text][order]") {
    const std::vector<text::Box> blocks = {{50, 300, 500, 350}, {50, 100, 500, 150}, {70, 200, 480, 220}};
    CHECK(text::reading_order(blocks) == std::vector<std::size_t>{1, 2, 0});
}

TEST_CASE("build_page_text joins lines, removes line-end hyphens and splits sentences", "[text][page]") {
    const auto pt = text::build_page_text({make_block(50, 100,
                                                      {"The controller uses an inter-",
                                                       "national standard. The Runge-",
                                                       "Kutta method is applied to the",
                                                       "plant model."})});
    CHECK(paragraphs(pt) == V{"The controller uses an international standard. The Runge-Kutta method is applied to the plant model."});
    CHECK(sentences(pt) == V{"The controller uses an international standard.",
                             "The Runge-Kutta method is applied to the plant model."});
}

TEST_CASE("build_page_text splits paragraphs by gaps, short last lines and indentation", "[text][page]") {
    // Đoạn 1 (dòng cuối ngắn) -> đoạn 2 thụt đầu dòng -> khoảng trống lớn -> đoạn 3.
    text::RawBlock block = make_block(50, 100,
                                      {"First paragraph line one goes to the end",
                                       "and stops here.",
                                       "Second paragraph starts with an indent and",
                                       "ends at the right margin of the block ok."},
                                      6.0f, 12.0f, 14.0f, {0, 0, 18, 0});
    text::RawBlock far = make_block(50, 200, {"Third paragraph after a large gap."});
    const auto pt = text::build_page_text({block, far});
    CHECK(paragraphs(pt) == V{"First paragraph line one goes to the end and stops here.",
                              "Second paragraph starts with an indent and ends at the right margin of the block ok.",
                              "Third paragraph after a large gap."});
    CHECK(pt.sentences().size() == 3);
    CHECK(pt.sentences()[1].paragraph == 1);
}

TEST_CASE("build_page_text merges a sentence continued in the next column", "[text][page]") {
    const auto pt = text::build_page_text({
        make_block(50, 100, {"The left column ends in the middle of a"}),
        make_block(300, 100, {"sentence that continues here. New one."}),
    });
    CHECK(paragraphs(pt) == V{"The left column ends in the middle of a sentence that continues here. New one."});
    CHECK(sentences(pt) == V{"The left column ends in the middle of a sentence that continues here.", "New one."});
}

TEST_CASE("PageText maps character boxes to sentences and line rectangles", "[text][page]") {
    const auto pt = text::build_page_text({make_block(50, 100, {"First sentence here. Second", "sentence follows."})});
    REQUIRE(pt.sentences().size() == 2);

    // "Second" là ký tự 21..26 của dòng 1: x từ 50 + 6*21 = 176 (dấu cách trước nó bị bỏ qua).
    const text::Span s = pt.span_where([](const text::Box& b) { return b.cx() > 170 && b.cy() < 112; });
    REQUIRE_FALSE(s.empty());
    CHECK(pt.utf8(s) == "Second");
    CHECK(pt.sentence_at(s.begin) == 1);
    CHECK(pt.sentence_at(0) == 0);

    const auto rects = pt.line_rects(pt.sentences()[1].span);
    REQUIRE(rects.size() == 2);  // câu 2 trải trên hai dòng
    CHECK(rects[0].y0 == 100.0f);
    CHECK(rects[1].y0 == 114.0f);
    CHECK(rects[1].x0 == 50.0f);
}
