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
    CHECK(plan->request.text == "For a temperature rise of 80 K the computed von Mises stress reaches 145 MPa.");
    CHECK(plan->request.context.empty());  // ngữ cảnh mặc định tắt
    CHECK(pt.utf8(plan->translated) == plan->request.text);

    context::PlanOptions only;
    only.expand_to_sentence = false;
    const auto narrow = context::make_plan(pt, find(pt, "von Mises stress reaches 145"), only);
    REQUIRE(narrow);
    CHECK_FALSE(narrow->expanded);
    CHECK(narrow->request.text == "von Mises stress reaches 145");
}

TEST_CASE("make_plan ignores a few characters spilling into the next sentence", "[context][plan]") {
    const auto pt = sample();
    // Kéo lố sang "The" của câu sau.
    const auto plan = context::make_plan(pt, find(pt, "von Mises stress reaches 145 MPa. The"));
    REQUIRE(plan);
    CHECK(plan->request.text == "For a temperature rise of 80 K the computed von Mises stress reaches 145 MPa.");
    // Lố sang câu trước vài ký tự cũng vậy.
    const auto before = context::make_plan(pt, find(pt, "ch. For a temperature rise"));
    REQUIRE(before);
    CHECK(before->request.text.starts_with("For a temperature rise"));
}

TEST_CASE("make_plan keeps terms as selected and spans multiple sentences", "[context][plan]") {
    const auto pt = sample();
    const auto term = context::make_plan(pt, find(pt, "stress"));
    REQUIRE(term);
    CHECK(term->kind == SelectionKind::Term);
    CHECK_FALSE(term->expanded);
    CHECK(term->request.text == "stress");

    const auto multi = context::make_plan(pt, find(pt, "mismatch. For a temperature"));
    REQUIRE(multi);
    CHECK(multi->request.text ==
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
    CHECK(utf8_length(cut->request.text) == 20);
}
