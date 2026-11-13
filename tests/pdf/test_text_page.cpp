// Trích text và bôi đen trên PDF dựng sẵn với vị trí chữ biết trước.
//
// Helvetica 12 pt, đường cơ sở dòng 1 ở y = 742 (PDF, gốc dưới) = 100 tính từ đỉnh trang.
// "The quick " rộng ~55.3 pt nên "brown" nằm khoảng x 127..160 pt.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "pdf/document.hpp"
#include "pdf/text_page.hpp"
#include "support/mupdf_fixture.hpp"
#include "support/sample_pdf.hpp"
#include "ui/selection.hpp"

using namespace pdfread;
using testing::MuPdfScope;
using testing::TempFile;

namespace {

std::vector<testing::SamplePage> text_pages() {
    testing::SamplePage p0;
    p0.content += testing::pdf_text(72, 742, 1, 12, "The quick brown fox");
    p0.content += testing::pdf_text(72, 722, 1, 12, "jumps over the lazy dog.");
    p0.content += testing::pdf_text(72, 700, 1, 12, "Proportional-integral control uses inter-");
    p0.content += testing::pdf_text(72, 686, 1, 12, "national standards.");

    testing::SamplePage p1;
    p1.content += testing::pdf_text(72, 742, 1, 12, "Second page first line");
    p1.content += testing::pdf_text(72, 722, 1, 12, "Second page second line");

    testing::SamplePage empty;  // không có chữ
    empty.content = testing::pdf_rect(100, 100, 50, 50, 0.5);

    testing::SamplePage two_col;  // hai cột, tiêu đề hai cột cùng độ cao
    two_col.content += testing::pdf_text(56, 780, 1, 12, "Left column header");
    two_col.content += testing::pdf_text(56, 760, 1, 12, "left line one");
    two_col.content += testing::pdf_text(56, 740, 1, 12, "left line two");
    two_col.content += testing::pdf_text(310, 780, 1, 12, "Right column header");
    two_col.content += testing::pdf_text(310, 760, 1, 12, "right line one");
    two_col.content += testing::pdf_text(310, 740, 1, 12, "right line last");
    return {p0, p1, empty, two_col};
}

struct Fixture {
    MuPdfScope scope;
    TempFile file{"pdfread_test_text.pdf"};
    std::shared_ptr<pdf::Document> doc;
    std::vector<pdf::TextPagePtr> pages;

    Fixture() {
        testing::write_pdf(file.path, text_pages());
        doc = pdf::Document::open(file.path);
        for (int i = 0; i < doc->page_count(); ++i) {
            pages.push_back(pdf::TextPage::build(*doc->display_list(i), doc->page(i)));
        }
    }
    ~Fixture() {
        pages.clear();  // giải phóng khi context của luồng còn gắn
        doc.reset();
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
};

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

}  // namespace

TEST_CASE("TextPage builds cleaned page text with sentences", "[pdf][text]") {
    Fixture f;
    const text::PageText& pt = f.pages[0]->page_text();
    std::vector<std::string> sents;
    for (const auto& s : pt.sentences()) sents.push_back(pt.utf8(s.span));
    INFO(pt.utf8({0, pt.text().size()}));
    REQUIRE(sents.size() == 2);
    CHECK(sents[0] == "The quick brown fox jumps over the lazy dog.");
    CHECK(sents[1] == "Proportional-integral control uses international standards.");
}

TEST_CASE("TextPage copies whole-page text with dehyphenation", "[pdf][text]") {
    Fixture f;
    const pdf::TextPage& t = *f.pages[0];
    CHECK(t.char_count() > 50);

    const std::string all = t.copy(t.page_start(), t.page_end());
    CHECK(contains(all, "The quick brown fox"));
    CHECK(contains(all, "jumps over the lazy dog."));
    CHECK(contains(all, "Proportional-integral"));  // gạch nối giữa dòng giữ nguyên
    CHECK(contains(all, "international standards."));  // gạch nối cuối dòng được nối lại
    CHECK_FALSE(contains(all, "inter-"));
}

TEST_CASE("join_hyphenated_lines joins words broken at line ends", "[pdf][text]") {
    using pdf::join_hyphenated_lines;
    CHECK(join_hyphenated_lines("uses inter-\nnational standards") == "uses international standards");
    CHECK(join_hyphenated_lines("the Runge-\nKutta method") == "the Runge-Kutta method");
    CHECK(join_hyphenated_lines("ISO-\n9001") == "ISO-9001");
    CHECK(join_hyphenated_lines("trans\xC2\xAD\nformer") == "transformer");
    CHECK(join_hyphenated_lines("trans\xE2\x80\x90\nformer") == "transformer");
    CHECK(join_hyphenated_lines("Proportional-integral\ncontrol") == "Proportional-integral\ncontrol");
    CHECK(join_hyphenated_lines("first item -\nsecond") == "first item -\nsecond");  // gạch ngang
    CHECK(join_hyphenated_lines("ends with-") == "ends with-");
    CHECK(join_hyphenated_lines("ends with-\n") == "ends with-\n");
    CHECK(join_hyphenated_lines("") == "");
}

TEST_CASE("TextPage snaps a point to the surrounding word and line", "[pdf][text]") {
    Fixture f;
    const pdf::TextPage& t = *f.pages[0];

    pdf::PointF a{140, 96};
    pdf::PointF b = a;
    t.snap(a, b, pdf::SelectUnit::Words);
    CHECK(t.copy(a, b) == "brown");

    a = b = {140, 96};
    t.snap(a, b, pdf::SelectUnit::Lines);
    CHECK(t.copy(a, b) == "The quick brown fox");

    a = b = {140, 96};
    t.snap(a, b, pdf::SelectUnit::Chars);  // không đổi
    CHECK(a.x == 140.0f);
}

TEST_CASE("TextPage highlight returns quads around the selected text", "[pdf][text]") {
    Fixture f;
    const pdf::TextPage& t = *f.pages[0];

    // Từ đầu dòng 1 tới giữa dòng 2.
    const std::vector<pdf::Quad> quads = t.highlight({72, 96}, {150, 116});
    REQUIRE(quads.size() == 2);  // một tứ giác cho mỗi dòng
    for (const pdf::Quad& q : quads) {
        CHECK(q.ul.x >= 71.0f);
        CHECK(q.ul.y < q.ll.y);  // trục y hướng xuống
        CHECK(q.ul.y > 80.0f);
        CHECK(q.ll.y < 125.0f);
    }
    CHECK(quads[0].ul.y < quads[1].ul.y);

    // Kéo ngược cho cùng kết quả.
    CHECK(t.copy({150, 116}, {72, 96}) == t.copy({72, 96}, {150, 116}));
}

TEST_CASE("TextPage on a page without text selects nothing", "[pdf][text]") {
    Fixture f;
    const pdf::TextPage& t = *f.pages[2];
    CHECK(t.char_count() == 0);
    CHECK(t.highlight(t.page_start(), t.page_end()).empty());
    CHECK(t.copy(t.page_start(), t.page_end()).empty());
}

TEST_CASE("Selection spans several pages in either drag direction", "[ui][selection]") {
    Fixture f;
    auto lookup = [&](int page) -> pdf::TextPagePtr {
        return page >= 0 && page < static_cast<int>(f.pages.size()) ? f.pages[static_cast<std::size_t>(page)] : nullptr;
    };

    // Từ đầu dòng 2 trang 1 tới sau chữ "page" ở dòng 1 trang 2 ("Second page" ~ x 72..143).
    const ui::SelectionPoint start{0, {72, 116}};
    const ui::SelectionPoint stop{1, {144, 96}};

    ui::Selection forward;
    forward.begin(start, pdf::SelectUnit::Chars);
    forward.extend(stop);
    forward.end_drag();
    forward.update(lookup);
    REQUIRE(forward.active());
    CHECK(forward.complete());
    CHECK(forward.page_range() == std::pair<int, int>{0, 1});
    CHECK(forward.quads(0) != nullptr);
    CHECK(forward.quads(1) != nullptr);
    CHECK(forward.quads(2) == nullptr);
    const std::string& text = forward.text();
    CHECK(text.starts_with("jumps over"));
    CHECK(contains(text, "international standards."));
    CHECK(contains(text, "Second page"));
    CHECK_FALSE(contains(text, "The quick"));
    CHECK_FALSE(contains(text, "first line"));

    ui::Selection backward;
    backward.begin(stop, pdf::SelectUnit::Chars);
    backward.extend(start);
    backward.update(lookup);
    CHECK(backward.text() == text);
}

TEST_CASE("Whole-page selection on a two-column page starts at the left column", "[pdf][text]") {
    Fixture f;
    const pdf::TextPage& t = *f.pages[3];
    const std::string all = t.copy(t.page_start(), t.page_end());
    CHECK(all.starts_with("Left column header"));
    CHECK(contains(all, "left line two"));
    CHECK(contains(all, "Right column header"));
    CHECK(all.ends_with("right line last"));

    // Tầng 2 cũng đọc hết cột trái rồi mới sang cột phải.
    const text::PageText& pt = t.page_text();
    std::vector<std::string> paras;
    for (const auto& p : pt.paragraphs()) paras.push_back(pt.utf8(p.span));
    REQUIRE_FALSE(paras.empty());
    const std::string joined = [&] {
        std::string s;
        for (const auto& p : paras) s += p + "|";
        return s;
    }();
    INFO(joined);
    CHECK(joined.find("left line two") < joined.find("Right column header"));

    // Kéo từ trang 1 sang cột trái trang hai cột (dòng "left line one", y đỉnh = 82).
    auto lookup = [&](int page) -> pdf::TextPagePtr { return f.pages[static_cast<std::size_t>(page)]; };
    ui::Selection s;
    s.begin({0, {72, 116}}, pdf::SelectUnit::Chars);
    s.extend({3, {100, 79}});
    s.update(lookup);
    const std::string& text = s.text();
    CHECK(contains(text, "Second page second line"));
    CHECK(contains(text, "Left column header\nleft"));
    CHECK_FALSE(contains(text, "Right column"));
}

TEST_CASE("Selection ending between two lines stays in the same column", "[pdf][text]") {
    Fixture f;
    const pdf::TextPage& t = *f.pages[3];
    // Dòng "left line one" có đường cơ sở y = 82, "left line two" y = 102 (tính từ đỉnh);
    // y = 89 nằm trong khe giữa hai dòng, cột phải có hai dòng cùng độ cao.
    const std::string text = t.copy({56, 58}, {100, 89});
    INFO(text);
    CHECK(text.starts_with("Left column header"));
    CHECK_FALSE(contains(text, "Right column"));
    CHECK_FALSE(contains(text, "right line"));

    // Điểm bên phải cuối dòng (dòng kết thúc ở x ~ 126, cột phải bắt đầu ở x = 310) vẫn lấy
    // trọn ký tự cuối của dòng.
    // (Dòng tiêu đề phía trên dài hơn: khoảng cách Euclid thuần sẽ dời điểm lên tiêu đề.)
    CHECK(t.copy({56, 79}, {200, 79}) == "left line one");
}

TEST_CASE("Selection waits for missing page text and word-snaps on double click", "[ui][selection]") {
    Fixture f;
    bool page1_ready = false;
    auto lookup = [&](int page) -> pdf::TextPagePtr {
        if (page == 1 && !page1_ready) return nullptr;
        return f.pages[static_cast<std::size_t>(page)];
    };

    ui::Selection s;
    s.begin({0, {72, 96}}, pdf::SelectUnit::Chars);
    s.extend({1, {300, 96}});
    s.update(lookup);
    CHECK_FALSE(s.complete());
    CHECK_FALSE(contains(s.text(), "Second"));

    page1_ready = true;
    s.update(lookup);
    CHECK(s.complete());
    CHECK(contains(s.text(), "Second page first line"));

    ui::Selection word;
    word.begin({0, {140, 96}}, pdf::SelectUnit::Words);  // nhấn đúp vào "brown"
    word.update(lookup);
    CHECK(word.text() == "brown");

    word.clear();
    CHECK_FALSE(word.active());
    CHECK(word.page_range() == std::pair<int, int>{-1, -1});
}
