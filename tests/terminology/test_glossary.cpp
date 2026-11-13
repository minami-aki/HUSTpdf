#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "terminology/glossary.hpp"
#include "util/utf8.hpp"

using namespace pdfread;
using terminology::GlossaryEntry;
using terminology::Layer;

namespace {

GlossaryEntry entry(std::string en, std::string vi, Layer layer, int rank = 0, std::string domain = {}, bool keep = false) {
    GlossaryEntry e;
    e.term_en = std::move(en);
    e.term_vi = std::move(vi);
    e.layer = layer;
    e.rank = rank;
    e.domain_id = std::move(domain);
    e.keep_english = keep;
    return e;
}

std::vector<std::string> matched_terms(const terminology::GlossaryIndex& idx, const std::string& text) {
    std::vector<std::string> out;
    const std::u32string u = to_u32(text);
    for (const auto& m : idx.find_in(u)) {
        out.push_back(to_utf8(std::u32string_view(u).substr(m.span.begin, m.span.size())) + "=" + m.entry->target());
    }
    return out;
}

using V = std::vector<std::string>;

}  // namespace

TEST_CASE("parse_csv handles quotes, embedded commas, newlines and BOM", "[terminology][csv]") {
    const auto rows = terminology::parse_csv("\xEF\xBB\xBF" "a,b,c\r\n\"x, y\",\"he said \"\"hi\"\"\",\"line1\nline2\"\n\n,last,\n");
    REQUIRE(rows.size() == 3);
    CHECK(rows[0] == V{"a", "b", "c"});
    CHECK(rows[1] == V{"x, y", "he said \"hi\"", "line1\nline2"});
    CHECK(rows[2] == V{"", "last", ""});
    CHECK(terminology::parse_csv(terminology::to_csv(rows)) == rows);
}

TEST_CASE("parse_glossary_csv reads columns by name and skips comments", "[terminology][csv]") {
    const auto entries = terminology::parse_glossary_csv(
        "term_vi,term_en,keep_english,source\n"
        "ứng suất,stress,0,draft\n"
        ",firmware,1,draft\n"
        "# ghi chú,#comment,0,\n"
        "giới hạn chảy,yield strength,no,verified\n");
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].term_en == "stress");
    CHECK(entries[0].term_vi == "ứng suất");
    CHECK(entries[1].keep_english);
    CHECK(entries[1].target() == "firmware");
    CHECK(entries[2].source == "verified");
    CHECK_THROWS(terminology::parse_glossary_csv("en,vi\nx,y\n"));
}

TEST_CASE("GlossaryIndex prefers longest multi-word terms and plural forms", "[terminology][glossary]") {
    const terminology::GlossaryIndex idx({
        entry("stress", "ứng suất", Layer::PrimaryDomain),
        entry("von Mises stress", "ứng suất von Mises", Layer::PrimaryDomain),
        entry("data hazard", "xung đột dữ liệu", Layer::PrimaryDomain),
        entry("branch predictor", "bộ dự đoán rẽ nhánh", Layer::PrimaryDomain),
        entry("property", "tính chất", Layer::PrimaryDomain),
        entry("firmware", "", Layer::PrimaryDomain, 0, {}, true),
    });
    CHECK(matched_terms(idx, "The von Mises stress and the shear stress.") ==
          V{"von Mises stress=ứng suất von Mises", "stress=ứng suất"});
    CHECK(matched_terms(idx, "Data hazards, branch predictors and properties.") ==
          V{"Data hazards=xung đột dữ liệu", "branch predictors=bộ dự đoán rẽ nhánh", "properties=tính chất"});
    CHECK(matched_terms(idx, "The firmware-level fix.") == V{"firmware=firmware"});
    CHECK(matched_terms(idx, "Stressful days").empty());
}

TEST_CASE("GlossaryIndex applies layer priority and reports alternative senses", "[terminology][glossary]") {
    const terminology::GlossaryIndex idx({
        entry("kernel", "hạt nhân", Layer::OtherDomain, 1, "math.applied"),
        entry("kernel", "nhân", Layer::PrimaryDomain, 0, "it.computer_science"),
        entry("kernel", "nhân hệ điều hành", Layer::User),
        entry("bus", "bus", Layer::OtherDomain, 2, "electrical.power"),
        entry("bus", "", Layer::PrimaryDomain, 0, "it.computer_engineering"),  // chưa xác nhận
    });
    const auto k = idx.lookup(U"Kernels");
    REQUIRE(k);
    CHECK(k->entry->term_vi == "nhân hệ điều hành");  // người dùng thắng
    REQUIRE(k->alternatives.size() == 2);
    CHECK(k->alternatives[0]->term_vi == "nhân");
    CHECK(k->alternatives[1]->term_vi == "hạt nhân");

    const auto b = idx.lookup(U"bus");
    REQUIRE(b);
    CHECK(b->entry->term_vi == "bus");  // bản đã xác nhận được ưu tiên hơn bản chưa xác nhận

    CHECK_FALSE(idx.lookup(U"kernel trick"));  // phải khớp trọn chuỗi
}

TEST_CASE("GlossaryIndex skips excluded spans", "[terminology][glossary]") {
    const terminology::GlossaryIndex idx({entry("gain", "hệ số khuếch đại", Layer::PrimaryDomain)});
    const std::u32string t = U"The gain is K = gain + 1.";
    const std::size_t formula = t.find(U"K = gain + 1");
    const auto m = idx.find_in(t, {{formula, formula + 12}});
    REQUIRE(m.size() == 1);
    CHECK(m[0].span.begin == 4);
}

TEST_CASE("check_terms reports glossary violations case-insensitively", "[terminology][glossary]") {
    const std::vector<GlossaryEntry> req{entry("stress", "ứng suất", Layer::PrimaryDomain),
                                         entry("yield strength", "giới hạn chảy", Layer::PrimaryDomain),
                                         entry("firmware", "", Layer::PrimaryDomain, 0, {}, true)};
    const auto missing = terminology::check_terms("Ứng Suất đạt 145 MPa, thấp hơn sức bền. Firmware  mới.", req);
    REQUIRE(missing.size() == 1);
    CHECK(missing[0].term_en == "yield strength");
}
