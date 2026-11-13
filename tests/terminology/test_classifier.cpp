#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "terminology/classifier.hpp"
#include "terminology/term_extractor.hpp"
#include "util/utf8.hpp"

using namespace pdfread;

namespace {

const char* kCatalog = R"({
  "groups": [
    { "id": "mechanical", "name_vi": "Cơ khí", "name_en": "Mechanical",
      "subdomains": [
        { "id": "mechanical.engineering_mechanics", "name_vi": "Cơ học kỹ thuật", "name_en": "Engineering mechanics",
          "keywords": ["stress", "strain", "von Mises", "elasticity", "beam", "yield strength", "fatigue"],
          "glossary": "glossary/mechanical.engineering_mechanics.csv" } ] },
    { "id": "electrical", "name_vi": "Điện", "name_en": "Electrical",
      "subdomains": [
        { "id": "electrical.control", "name_vi": "Điều khiển", "name_en": "Control",
          "keywords": ["controller", "PID", "transfer function", "closed-loop", "stability", "setpoint"] } ] },
    { "id": "it", "name_vi": "CNTT", "name_en": "IT",
      "subdomains": [
        { "id": "it.computer_engineering", "name_vi": "Kỹ thuật máy tính", "name_en": "Computer engineering",
          "keywords": ["pipeline", "cache", "branch predictor", "instruction", "register"] } ] }
  ] })";

}  // namespace

TEST_CASE("DomainCatalog parses groups, subdomains and glossary paths", "[terminology][domains]") {
    const auto cat = terminology::DomainCatalog::parse(kCatalog, "C:/data");
    REQUIRE(cat.groups().size() == 3);
    REQUIRE(cat.subdomains().size() == 3);
    const auto* mech = cat.find("mechanical.engineering_mechanics");
    REQUIRE(mech);
    CHECK(mech->keywords[2] == "von mises");  // viết thường
    CHECK(mech->glossary.filename() == "mechanical.engineering_mechanics.csv");
    CHECK(cat.group_of(*mech)->name_vi == "Cơ khí");
    CHECK(cat.find("nope") == nullptr);
    CHECK_THROWS_AS(terminology::DomainCatalog::parse("{\"groups\": [{\"id\": 1}]}", "."), terminology::DomainError);
}

TEST_CASE("classify ranks domains by keyword evidence (multi-label)", "[terminology][domains]") {
    const auto cat = terminology::DomainCatalog::parse(kCatalog, ".");
    const std::vector<terminology::WeightedText> mechatronic = {
        {U"Closed-loop PID control of a servo with fatigue analysis", 3.0},
        {U"The PID controller keeps the setpoint. The transfer function shows stability. "
         U"The von Mises stress in the beam and the strain remain below the yield strength.", 1.0},
    };
    const auto scores = terminology::classify(mechatronic, cat);
    REQUIRE(scores.size() == 2);
    CHECK(scores[0].domain_id == "electrical.control");
    CHECK(scores[1].domain_id == "mechanical.engineering_mechanics");
    CHECK(scores[0].score + scores[1].score > 0.99f);

    CHECK(terminology::classify({{U"A recipe for cooking pasta with tomatoes.", 1.0}}, cat).empty());
}

TEST_CASE("extract_terms finds defined abbreviations and repeated noun phrases", "[terminology][extract]") {
    const std::vector<std::u32string> sentences = {
        U"A proportional-integral-derivative (PID) controller is used.",
        U"The PID gains are tuned for the closed loop step response.",
        U"The closed loop step response is fast.",
        U"Thermal stress in the composite layer depends on the coefficient of thermal expansion.",
        U"The coefficient of thermal expansion of the composite layer is small.",
        U"Use the CAN (Controller Area Network) bus here.",
    };
    const auto terms = terminology::extract_terms(sentences, {.min_count = 2, .max_results = 50});
    auto find = [&](const std::string& t) -> const terminology::TermCandidate* {
        for (const auto& c : terms) {
            if (c.term == t) return &c;
        }
        return nullptr;
    };
    const auto* pid = find("PID");
    REQUIRE(pid);
    CHECK(pid->expansion == "proportional-integral-derivative");
    CHECK(pid->count == 2);
    const auto* can = find("CAN");
    REQUIRE(can);
    CHECK(can->expansion == "Controller Area Network");

    CHECK(find("coefficient of thermal expansion"));
    CHECK(find("composite layer"));
    CHECK(find("closed loop step response"));
    CHECK_FALSE(find("closed loop"));  // nằm trong cụm dài hơn cùng tần suất
}
