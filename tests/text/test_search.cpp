#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "text/normalize.hpp"
#include "text/search.hpp"
#include "util/utf8.hpp"

using namespace pdfread;
using text::Span;

namespace {

std::vector<std::string> found(const std::string& hay_utf8, const std::vector<std::string>& queries) {
    const std::u32string hay = to_u32(hay_utf8);
    const std::u32string folded = text::fold_for_search(hay);
    std::vector<std::u32string> needles;
    for (const auto& q : queries) needles.push_back(text::normalize_query(to_u32(q)));
    std::vector<std::string> out;
    for (const Span& s : text::find_matches(folded, needles)) out.push_back(to_utf8(hay.substr(s.begin, s.size())));
    return out;
}

}  // namespace

TEST_CASE("to_lower handles ASCII, Vietnamese and Greek letters", "[text][search]") {
    CHECK(text::to_lower(U'A') == U'a');
    CHECK(text::to_lower(U'Đ') == U'đ');
    CHECK(text::to_lower(U'Ứ') == U'ứ');
    CHECK(text::to_lower(U'Ơ') == U'ơ');
    CHECK(text::to_lower(U'Ạ') == U'ạ');
    CHECK(text::to_lower(U'É') == U'é');
    CHECK(text::to_lower(U'Σ') == U'σ');
    CHECK(text::to_lower(U'ạ') == U'ạ');
    CHECK(text::to_lower(U'7') == U'7');
    CHECK(text::fold_for_search(U"Đỉnh\nBragg") == U"đỉnh bragg");
    CHECK(text::fold_for_search(U"AB\tC").size() == 4);  // giữ nguyên độ dài
}

TEST_CASE("normalize_query trims and collapses spaces", "[text][search]") {
    CHECK(text::normalize_query(U"  Delayed   Neutron\tFraction ") == U"delayed neutron fraction");
    CHECK(text::normalize_query(U"   ").empty());
}

TEST_CASE("find_matches is case-insensitive and spans line breaks", "[text][search]") {
    CHECK(found("The Reactor period is short. reactor PERIOD again.", {"reactor period"}) ==
          std::vector<std::string>{"Reactor period", "reactor PERIOD"});
    // Cụm bị ngắt dòng / nhiều khoảng trắng vẫn khớp.
    CHECK(found("the delayed\nneutron  fraction", {"delayed neutron fraction"}) ==
          std::vector<std::string>{"delayed\nneutron  fraction"});
    CHECK(found("nothing here", {"reactor"}).empty());
    CHECK(found("anything", {""}).empty());
}

TEST_CASE("find_matches with several needles prefers the longest, no overlaps", "[text][search]") {
    CHECK(found("von Mises stress and stress", {"stress", "von mises stress"}) ==
          std::vector<std::string>{"von Mises stress", "stress"});
    CHECK(found("aaaa", {"aa"}) == std::vector<std::string>{"aa", "aa"});
}

TEST_CASE("snippet cuts at word boundaries with ellipses", "[text][search]") {
    const std::u32string t = U"The effective multiplication factor of the core is kept close to unity during operation.";
    const std::size_t b = t.find(U"factor");
    const std::string s = text::snippet(t, {b, b + 6}, 20);
    CHECK(s.find("factor") != std::string::npos);
    CHECK(s.starts_with("…"));
    CHECK(s.ends_with("…"));
    CHECK(text::snippet(U"short text", {0, 5}, 40) == "short text");
}
