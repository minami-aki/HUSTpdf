// Kiểm tra dữ liệu thật trong data/: domains.json và các glossary lĩnh vực.

#include <filesystem>
#include <set>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "terminology/classifier.hpp"
#include "terminology/domains.hpp"
#include "terminology/glossary.hpp"
#include "terminology/words.hpp"
#include "util/utf8.hpp"

#ifndef PDFREAD_DATA_DIR
#error "PDFREAD_DATA_DIR chưa được định nghĩa"
#endif

using namespace pdfread;

namespace {
std::filesystem::path data_dir() { return path_from_utf8(PDFREAD_DATA_DIR); }
}  // namespace

TEST_CASE("data/domains.json and every domain glossary load cleanly", "[terminology][data]") {
    const auto cat = terminology::DomainCatalog::load(data_dir() / "domains.json");
    CHECK(cat.groups().size() == 8);
    CHECK(cat.subdomains().size() == 37);

    std::size_t total = 0;
    for (const auto& sub : cat.subdomains()) {
        INFO(sub.id);
        CHECK(sub.keywords.size() >= 20);
        REQUIRE(std::filesystem::exists(sub.glossary));
        const auto entries = terminology::load_glossary_csv(sub.glossary);
        CHECK(entries.size() >= 25);
        std::set<std::u32string> seen;
        for (const auto& e : entries) {
            INFO(e.term_en);
            CHECK(e.confirmed());
            CHECK(seen.insert(terminology::lower(to_u32(e.term_en))).second);  // không trùng trong file
        }
        total += entries.size();
    }
    CHECK(total >= 1000);
}

TEST_CASE("Real domain keywords classify typical abstracts", "[terminology][data]") {
    const auto cat = terminology::DomainCatalog::load(data_dir() / "domains.json");
    auto top = [&](const std::u32string& text) {
        const auto s = terminology::classify({{text, 1.0}}, cat);
        return s.empty() ? std::string() : s.front().domain_id;
    };
    CHECK(top(U"A PID controller with feedback is tuned to reduce overshoot and settling time of the closed-loop "
              U"step response; the transfer function and stability margins are analysed.") == "electrical.control");
    CHECK(top(U"We propose a pipeline with a new branch predictor and cache hierarchy for a RISC-V processor; data "
              U"hazards are resolved by forwarding and the register file is enlarged.") == "it.computer_engineering");
    CHECK(top(U"The von Mises stress and strain in the beam are computed with the finite element method; fatigue "
              U"life and buckling are checked against the yield strength.") == "mechanical.engineering_mechanics");
    CHECK(top(U"Wastewater from the plant was treated by coagulation, flocculation and sedimentation; BOD and COD "
              U"removal and sludge production were measured.") == "chem_bio.environmental");
    CHECK(top(U"A convolutional neural network is trained on the dataset with gradient descent; accuracy, precision "
              U"and recall on the test set show less overfitting.") == "it.data_science_ai");
    CHECK(top(U"The effective multiplication factor and the delayed neutron fraction of the pressurized water reactor "
              U"core are computed; xenon poisoning and burnup reduce reactivity.") == "physics.nuclear");
    CHECK(top(U"Hyperfine structure and the Zeeman effect of the excited state are measured; the Rydberg constant "
              U"and spectral line positions agree with the Bohr model of the atomic nucleus.") == "physics.nuclear");
}

TEST_CASE("Nuclear glossary finds reactor and atomic physics terms", "[terminology][data]") {
    const auto cat = terminology::DomainCatalog::load(data_dir() / "domains.json");
    const auto* sub = cat.find("physics.nuclear");
    REQUIRE(sub != nullptr);
    const auto entries = terminology::load_glossary_csv(sub->glossary);
    CHECK(entries.size() >= 3000);  // từ điển chuyên ngành (source=dict) + mục nháp còn lại
    std::size_t with_explanation = 0;
    for (const auto& e : entries) with_explanation += e.explanation.empty() ? 0 : 1;
    CHECK(with_explanation >= 3000);
    terminology::GlossaryIndex index(entries);
    const std::u32string text =
        U"The binding energy per nucleon peaks near iron, while the delayed neutron fraction controls the reactor period.";
    std::vector<std::string> found;
    for (const auto& m : index.find_in(text)) found.push_back(to_utf8(terminology::lower(to_u32(m.entry->term_en))));
    CHECK(found == std::vector<std::string>{"binding energy per nucleon", "delayed neutron fraction", "reactor period"});

    // Tra viết tắt / ký hiệu đồng vị: trỏ về thuật ngữ gốc, có giải thích.
    const auto am = index.lookup(U"Am-241");
    REQUIRE(am);
    CHECK(am->entry->alias_of == "Americium-241");
    CHECK_FALSE(am->entry->explanation.empty());
    // Không còn LaTeX thô trong giải thích.
    std::vector<std::string> raw_latex;
    for (const auto& e : entries) {
        if (e.explanation.find('\\') != std::string::npos) raw_latex.push_back(e.term_en);
    }
    CHECK(raw_latex.empty());
}
