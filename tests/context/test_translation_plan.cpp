#include <string>

#include <catch2/catch_test_macros.hpp>

#include "context/translation_plan.hpp"
#include "support/raw_text.hpp"
#include "util/utf8.hpp"

using namespace pdfread;
using context::SelectionKind;
using testing::make_block;

namespace {

// Ba câu trong một đoạn, một câu ở đoạn sau.
text::PageText sample() {
    return text::build_page_text({
        make_block(50, 100, {"Thermal stress depends on the mismatch. For a temperature rise of 80 K the",
                             "computed von Mises stress reaches 145 MPa. The alloy remains elastic."}),
        make_block(50, 200, {"A new paragraph starts here."}),
    });
}

// Khoảng ký tự của chuỗi con `needle` (lần xuất hiện đầu).
text::Span find(const text::PageText& pt, const std::string& needle) {
    const std::u32string n = to_u32(needle);
    const std::size_t pos = pt.text().find(n);
    REQUIRE(pos != std::u32string::npos);
    return {pos, pos + n.size()};
}

}  // namespace

TEST_CASE("classify distinguishes terms, partial and whole sentences", "[context][plan]") {
    const auto pt = sample();
    CHECK(context::classify(pt, find(pt, "stress")) == SelectionKind::Term);
    CHECK(context::classify(pt, find(pt, "von Mises stress")) == SelectionKind::Term);
    CHECK(context::classify(pt, find(pt, "the computed von Mises stress reaches")) == SelectionKind::Partial);
    CHECK(context::classify(pt, find(pt, "Thermal stress depends on the mismatch.")) == SelectionKind::Sentences);
    // Hụt vài ký tự ở hai đầu vẫn tính là trọn câu.
    CHECK(context::classify(pt, find(pt, "hermal stress depends on the mismatch")) == SelectionKind::Sentences);
    // Qua hai câu nhưng không trọn -> một phần.
    CHECK(context::classify(pt, find(pt, "mismatch. For a temperature")) == SelectionKind::Partial);
}

TEST_CASE("make_plan expands a partial selection to the whole sentence", "[context][plan]") {
    const auto pt = sample();
    const auto plan = context::make_plan(pt, find(pt, "von Mises stress reaches 145"));
    REQUIRE(plan);
    CHECK(plan->kind == SelectionKind::Partial);
    CHECK(plan->expanded);
    CHECK(plan->source == "For a temperature rise of 80 K the computed von Mises stress reaches 145 MPa.");
    CHECK(plan->request.context.empty());  // ngữ cảnh mặc định tắt
    CHECK(pt.utf8(plan->translated) == plan->source);

    context::PlanOptions only;
    only.expand_to_sentence = false;
    const auto narrow = context::make_plan(pt, find(pt, "von Mises stress reaches 145"), only);
    REQUIRE(narrow);
    CHECK_FALSE(narrow->expanded);
    CHECK(narrow->source == "von Mises stress reaches 145");
}

TEST_CASE("make_plan ignores a few characters spilling into the next sentence", "[context][plan]") {
    const auto pt = sample();
    // Kéo lố sang "The" của câu sau.
    const auto plan = context::make_plan(pt, find(pt, "von Mises stress reaches 145 MPa. The"));
    REQUIRE(plan);
    CHECK(plan->source == "For a temperature rise of 80 K the computed von Mises stress reaches 145 MPa.");
    // Lố sang câu trước vài ký tự cũng vậy.
    const auto before = context::make_plan(pt, find(pt, "ch. For a temperature rise"));
    REQUIRE(before);
    CHECK(before->source.starts_with("For a temperature rise"));
}

TEST_CASE("make_plan keeps terms as selected and spans multiple sentences", "[context][plan]") {
    const auto pt = sample();
    const auto term = context::make_plan(pt, find(pt, "stress"));
    REQUIRE(term);
    CHECK(term->kind == SelectionKind::Term);
    CHECK_FALSE(term->expanded);
    CHECK(term->source == "stress");

    const auto multi = context::make_plan(pt, find(pt, "mismatch. For a temperature"));
    REQUIRE(multi);
    CHECK(multi->source ==
          "Thermal stress depends on the mismatch. For a temperature rise of 80 K the computed von Mises stress "
          "reaches 145 MPa.");

    CHECK_FALSE(context::make_plan(pt, {}));
}

TEST_CASE("make_plan truncates long text and adds context only when enabled", "[context][plan]") {
    const auto pt = sample();
    context::PlanOptions opts;
    opts.include_context = true;
    const auto plan = context::make_plan(pt, find(pt, "computed von Mises stress reaches"), opts);
    REQUIRE(plan);
    // Một câu trước và một câu sau, cùng đoạn; không lấy câu của đoạn khác.
    CHECK(plan->request.context == "Thermal stress depends on the mismatch. The alloy remains elastic.");

    opts.max_context_chars = 45;  // chỉ đủ một câu
    const auto small = context::make_plan(pt, find(pt, "computed von Mises stress reaches"), opts);
    REQUIRE(small);
    CHECK(small->request.context == "Thermal stress depends on the mismatch.");

    opts.include_context = false;
    opts.max_chars = 20;
    const auto cut = context::make_plan(pt, find(pt, "computed von Mises stress reaches"), opts);
    REQUIRE(cut);
    CHECK(cut->truncated);
    CHECK(utf8_length(cut->source) == 20);
}

TEST_CASE("make_plan adds glossary terms, placeholders and glossary answers", "[context][plan][glossary]") {
    const auto pt = sample();
    auto entry = [](std::string en, std::string vi, terminology::Layer layer, std::string domain = {}) {
        terminology::GlossaryEntry e;
        e.term_en = std::move(en);
        e.term_vi = std::move(vi);
        e.layer = layer;
        e.domain_id = std::move(domain);
        return e;
    };
    const terminology::GlossaryIndex idx({
        entry("stress", "ứng suất", terminology::Layer::PrimaryDomain, "mechanical.engineering_mechanics"),
        entry("stress", "căng thẳng", terminology::Layer::Unrelated, "economics.business"),
        entry("von Mises stress", "ứng suất von Mises", terminology::Layer::PrimaryDomain),
        entry("temperature rise", "độ tăng nhiệt độ", terminology::Layer::Unrelated, "physics.engineering"),
        entry("alloy", "", terminology::Layer::PrimaryDomain),  // chưa xác nhận
    });
    context::PlanOptions opts;
    opts.glossary = &idx;
    opts.domains = {{"mechanical.engineering_mechanics", 1.0f}};

    const auto plan = context::make_plan(pt, find(pt, "von Mises stress reaches 145"), opts);
    REQUIRE(plan);
    // Mặc định đại lượng giữ nguyên trong câu; chỉ thuật ngữ của lĩnh vực tài liệu vào prompt.
    CHECK(plan->request.text == "For a temperature rise of 80 K the computed von Mises stress reaches 145 MPa.");
    CHECK(plan->placeholders.empty());
    REQUIRE(plan->request.terms.size() == 1);
    CHECK(plan->request.terms[0].term_en == "von Mises stress");
    CHECK(plan->request.domains.size() == 1);

    // Bật bảo vệ đại lượng: "80 K" và "145 MPa" thành placeholder.
    context::PlanOptions with_q = opts;
    with_q.protect_quantities = true;
    const auto plan_q = context::make_plan(pt, find(pt, "von Mises stress reaches 145"), with_q);
    REQUIRE(plan_q);
    CHECK(plan_q->request.text == "For a temperature rise of ⟦U1⟧ the computed von Mises stress reaches ⟦U2⟧.");
    REQUIRE(plan_q->placeholders.size() == 2);
    CHECK(plan_q->placeholders[1].original == "145 MPa");

    // Một từ có trong glossary: trả lời trực tiếp, kèm nghĩa ở lĩnh vực khác.
    const auto term = context::make_plan(pt, find(pt, "stress"), opts);
    REQUIRE(term);
    REQUIRE(term->glossary_answer);
    CHECK(term->glossary_answer->entry.term_vi == "ứng suất");
    REQUIRE(term->glossary_answer->alternatives.size() == 1);
    CHECK(term->glossary_answer->alternatives[0].term_vi == "căng thẳng");

    // Không có glossary: không lỗi, vẫn có placeholder.
    const auto plain = context::make_plan(pt, find(pt, "von Mises stress reaches 145"));
    REQUIRE(plain);
    CHECK(plain->request.terms.empty());
    CHECK_FALSE(plain->glossary_answer);
}

TEST_CASE("make_plan treats a long selection matching a glossary entry as a term", "[context][plan]") {
    const auto pt = text::build_page_text({
        make_block(50, 100, {"After the trip the emergency core cooling system injection valve opened quickly."}),
    });
    terminology::GlossaryEntry e;
    e.term_en = "Emergency Core Cooling System Injection Valve";
    e.term_vi = "van phun của hệ thống làm mát vùng hoạt khẩn cấp";
    e.explanation = "Van đưa nước pha bo vào vùng hoạt.";
    e.layer = terminology::Layer::PrimaryDomain;
    const terminology::GlossaryIndex idx({e});
    context::PlanOptions opts;
    opts.glossary = &idx;

    // 6 từ (> max_term_words) nhưng trùng đúng một mục: tra thuật ngữ, có giải thích, không mở rộng ra câu.
    const auto plan = context::make_plan(pt, find(pt, "emergency core cooling system injection valve"), opts);
    REQUIRE(plan);
    CHECK(plan->kind == SelectionKind::Term);
    REQUIRE(plan->glossary_answer);
    CHECK(plan->glossary_answer->entry.explanation == "Van đưa nước pha bo vào vùng hoạt.");
    CHECK_FALSE(plan->expanded);

    // Kéo chuột hụt chữ cái ở hai đầu: mở rộng ra trọn từ rồi tra.
    const auto sloppy = context::make_plan(pt, find(pt, "mergency core cooling system injection val"), opts);
    REQUIRE(sloppy);
    CHECK(sloppy->kind == SelectionKind::Term);
    CHECK(sloppy->glossary_answer);

    // Cả câu: dịch bằng mô hình, thuật ngữ vào prompt nhưng không có trả lời glossary.
    const auto sentence = context::make_plan(pt, find(pt, "After the trip the emergency core cooling system injection valve opened quickly."), opts);
    REQUIRE(sentence);
    CHECK(sentence->kind == SelectionKind::Sentences);
    CHECK_FALSE(sentence->glossary_answer);
    REQUIRE(sentence->request.terms.size() == 1);
    CHECK(sentence->request.terms[0].term_vi == "van phun của hệ thống làm mát vùng hoạt khẩn cấp");
}
