#include <filesystem>
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

TEST_CASE("parse_glossary_csv reads aliases and explanation", "[terminology][csv]") {
    const auto entries = terminology::parse_glossary_csv(
        "term_en,term_vi,keep_english,notes,source,aliases,explanation\n"
        "Americium-241,americi-241,0,,dict,Am-241,\"T₁/₂ = 432.6 năm, alpha 5.49 MeV\"\n"
        "Angular Momentum Coupling,cộng mômen động lượng,0,,dict,Clebsch–Gordan Coefficients=hệ số Clebsch–Gordan|CG,\n");
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].aliases == std::vector<std::string>{"Am-241"});
    CHECK(entries[0].explanation == "T₁/₂ = 432.6 năm, alpha 5.49 MeV");
    CHECK(entries[1].aliases.size() == 2);
    CHECK(terminology::split_alias(entries[1].aliases[0]).second == "hệ số Clebsch–Gordan");
    CHECK(terminology::split_alias("CG").second.empty());
}

TEST_CASE("GlossaryIndex matches aliases: abbreviations kept, named aliases translated", "[terminology][glossary]") {
    GlossaryEntry am = entry("Americium-241", "americi-241", Layer::PrimaryDomain);
    am.aliases = {"Am-241"};
    am.explanation = "T₁/₂ = 432.6 năm";
    GlossaryEntry cg = entry("Angular Momentum Coupling", "cộng mômen động lượng", Layer::PrimaryDomain);
    cg.aliases = {"Clebsch–Gordan Coefficients=hệ số Clebsch–Gordan"};
    const terminology::GlossaryIndex idx({am, cg});

    const auto m = idx.lookup(U"Am-241");
    REQUIRE(m);
    CHECK(m->entry->alias_of == "Americium-241");
    CHECK(m->entry->keep_english);
    CHECK(m->entry->target() == "Am-241");
    CHECK(m->entry->explanation == "T₁/₂ = 432.6 năm");
    CHECK(m->entry->term_vi == "americi-241");  // nghĩa của thuật ngữ gốc, để hiển thị

    const auto found = idx.find_in(U"The Clebsch–Gordan coefficients couple the momenta of Am-241 nuclei.");
    REQUIRE(found.size() == 2);
    CHECK(found[0].entry->target() == "hệ số Clebsch–Gordan");
    CHECK(found[1].entry->target() == "Am-241");

    // Ghi ra CSV từ chỉ mục (có cả mục ảo): mục ảo không ghi, cột aliases/explanation được giữ.
    const auto path = std::filesystem::temp_directory_path() / "pdfread_alias_roundtrip.csv";
    terminology::save_glossary_csv(path, idx.entries());
    const auto back = terminology::load_glossary_csv(path);
    std::filesystem::remove(path);
    REQUIRE(back.size() == 2);
    CHECK(back[0].aliases == std::vector<std::string>{"Am-241"});
    CHECK(back[0].explanation == "T₁/₂ = 432.6 năm");
    CHECK(back[1].aliases == cg.aliases);
}

TEST_CASE("GlossaryIndex matches all-caps abbreviations case-sensitively", "[terminology][glossary]") {
    GlossaryEntry it = entry("IT", "chuyển dời đồng phân", Layer::PrimaryDomain, 0, {}, true);
    GlossaryEntry loca = entry("LOCA", "sự cố mất chất tải nhiệt", Layer::PrimaryDomain, 0, {}, true);
    GlossaryEntry am = entry("Americium-241", "americi-241", Layer::PrimaryDomain);
    const terminology::GlossaryIndex idx({it, loca, am});
    CHECK(idx.find_in(U"It decays because it is unstable.").empty());
    CHECK(idx.find_in(U"The IT branch dominates.").size() == 1);
    CHECK(idx.find_in(U"Two LOCAs were analysed.").size() == 1);
    CHECK(idx.find_in(U"A loca is not an acronym here.").empty());
    CHECK(idx.lookup(U"americium-241"));  // thuật ngữ thường: không phân biệt hoa/thường
}

TEST_CASE("english_for finds English terms from a Vietnamese meaning", "[terminology][glossary]") {
    GlossaryEntry s1 = entry("stress", "ứng suất", Layer::PrimaryDomain);
    GlossaryEntry s2 = entry("von Mises stress", "ứng suất von Mises", Layer::PrimaryDomain);
    GlossaryEntry s3 = entry("stress", "căng thẳng", Layer::Unrelated, 100, "economics.business");
    GlossaryEntry r = entry("Reactivity", "độ phản ứng", Layer::PrimaryDomain);
    r.aliases = {"Reactivity Worth=giá trị độ phản ứng"};
    const terminology::GlossaryIndex idx({s1, s2, s3, r});
    // Khớp cả cụm (không phân biệt hoa thường).
    CHECK(idx.english_for(U"Ứng suất") == std::vector<std::string>{"stress"});
    CHECK(idx.english_for(U"căng thẳng") == std::vector<std::string>{"stress"});
    // Không có mục trùng hoàn toàn: các mục chứa trọn cụm (cả tên gọi khác).
    const auto partial = idx.english_for(U"phản ứng");
    CHECK(partial.size() == 2);
    CHECK(idx.english_for(U"uất").empty());  // chỉ khớp trọn tiếng (không khớp giữa chừng)
    CHECK(idx.english_for(U"").empty());
}
