// Mục lục, liên kết và tìm kiếm toàn tài liệu trên PDF mẫu sinh tại chỗ.

#include <chrono>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "pdf/document.hpp"
#include "pdf/search_service.hpp"
#include "support/mupdf_fixture.hpp"
#include "support/sample_pdf.hpp"
#include "text/search.hpp"
#include "util/utf8.hpp"

using namespace pdfread;
using namespace pdfread::testing;

namespace {

// 3 trang A4: trang 1 có liên kết tới trang 3 và một liên kết web; mục lục 2 cấp.
std::filesystem::path make_doc(TempFile& f) {
    std::vector<SamplePage> pages(3);
    double y = 770;
    pages[0].content = pdf_text(72, 770, 3, 16, "Chapter 1 Reactor Physics");
    pages[0].content += pdf_paragraph(72, y = 740, 451, 2, 11, 14,
                                      "The delayed neutron fraction controls the reactor period. See Section 3.");
    pages[0].links.push_back({.x0 = 72, .y0 = 700, .x1 = 200, .y1 = 716, .dest_page = 2, .dest_y = 600, .uri = {}});
    pages[0].links.push_back({.x0 = 72, .y0 = 600, .x1 = 200, .y1 = 616, .dest_page = -1, .dest_y = 0,
                              .uri = "https://example.org/reactor"});
    pages[1].content = pdf_paragraph(72, y = 770, 451, 2, 11, 14, "Nothing about the topic on this page.");
    pages[2].content = pdf_text(72, 620, 3, 14, "Section 3 Reactor Period");
    pages[2].content += pdf_paragraph(72, y = 590, 451, 2, 11, 14,
                                      "The reactor period becomes short when reactivity approaches one dollar.");
    const std::vector<SampleOutline> outline = {
        {"Chapter 1", 0, 0, 780},
        {"Section 1.1", 1, 0, 740},
        {"Section 3", 0, 2, 620},
    };
    write_pdf(f.path, pages, outline);
    return f.path;
}

}  // namespace

TEST_CASE("Document reads the outline tree with pages and positions", "[pdf][navigation]") {
    MuPdfScope scope;
    TempFile f("pdfread_nav_outline.pdf");
    const auto doc = pdf::Document::open(make_doc(f));
    const auto outline = doc->outline();
    REQUIRE(outline.size() == 3);
    CHECK(outline[0].title == "Chapter 1");
    CHECK(outline[0].level == 0);
    CHECK(outline[1].title == "Section 1.1");
    CHECK(outline[1].level == 1);
    CHECK(outline[2].title == "Section 3");
    CHECK(outline[2].page == 2);
    // y = 620 (tọa độ PDF, gốc dưới) -> 842 - 620 = 222 point từ mép trên.
    CHECK(outline[2].y > 215.0f);
    CHECK(outline[2].y < 229.0f);
    CHECK(doc->outline_titles().size() == 3);
}

TEST_CASE("Document reads internal and external links in top-left page coordinates", "[pdf][navigation]") {
    MuPdfScope scope;
    TempFile f("pdfread_nav_links.pdf");
    const auto doc = pdf::Document::open(make_doc(f));
    const auto links = doc->links(0);
    REQUIRE(links.size() == 2);
    const pdf::Link* internal = links[0].page >= 0 ? &links[0] : &links[1];
    const pdf::Link* external = links[0].page >= 0 ? &links[1] : &links[0];
    CHECK(internal->page == 2);
    CHECK(internal->y > 235.0f);  // 842 - 600 = 242
    CHECK(internal->y < 249.0f);
    // Khung: PDF y 700..716 -> từ mép trên 126..142.
    CHECK(internal->y0 > 120.0f);
    CHECK(internal->y1 < 148.0f);
    CHECK(internal->contains(100.0f, 134.0f));
    CHECK_FALSE(internal->contains(100.0f, 300.0f));
    CHECK(external->page == -1);
    CHECK(external->uri == "https://example.org/reactor");
    CHECK(doc->links(1).empty());
    CHECK(doc->links(99).empty());
}

TEST_CASE("SearchService finds phrases across pages with highlight rects", "[pdf][navigation]") {
    MuPdfScope scope;
    TempFile f("pdfread_nav_search.pdf");
    const auto doc = pdf::Document::open(make_doc(f));
    pdf::SearchService search(mupdf_root(), {});
    search.set_document(doc);
    auto run = [&](std::vector<std::u32string> needles) {
        const std::uint64_t ticket = search.start(std::move(needles));
        std::vector<pdf::SearchHit> hits;
        for (int i = 0; i < 500; ++i) {
            bool done = false;
            for (auto& u : search.take_updates()) {
                if (u.ticket != ticket) continue;
                for (auto& h : u.hits) hits.push_back(std::move(h));
                done = done || u.finished;
            }
            if (done) return hits;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        FAIL("search did not finish");
        return hits;
    };

    const auto hits = run({text::normalize_query(U"Reactor Period")});
    REQUIRE(hits.size() == 3);  // trang 1: "reactor period"; trang 3: tiêu đề + đoạn văn
    CHECK(hits[0].page == 0);
    CHECK(hits[1].page == 2);
    CHECK(hits[2].page == 2);
    for (const auto& h : hits) {
        REQUIRE_FALSE(h.rects.empty());
        CHECK(h.rects.front().x1 > h.rects.front().x0);
        CHECK(h.snippet.find("eactor") != std::string::npos);
    }
    // Lần hai dùng cache văn bản; nhiều cụm (OR).
    const auto again = run({text::normalize_query(U"dollar"), text::normalize_query(U"delayed neutron fraction")});
    REQUIRE(again.size() == 2);
    CHECK(again[0].page == 0);
    CHECK(again[1].page == 2);
    CHECK(run({text::normalize_query(U"zzz")}).empty());
}
