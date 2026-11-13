#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "text/protect.hpp"
#include "util/utf8.hpp"

using namespace pdfread;
using text::ProtectKind;

namespace {

struct Found {
    std::string text;
    ProtectKind kind;
    friend bool operator==(const Found&, const Found&) = default;
};

std::vector<Found> found(const std::string& s) {
    const std::u32string u = to_u32(s);
    std::vector<Found> out;
    for (const auto& p : text::find_protected(u)) out.push_back({to_utf8(std::u32string_view(u).substr(p.span.begin, p.span.size())), p.kind});
    return out;
}

using F = std::vector<Found>;

}  // namespace

TEST_CASE("find_protected detects formulas and stops at ordinary words", "[text][protect]") {
    CHECK(found("The transfer function of the plant is G(s) = 1 / (s^2 + 3s + 2), see Eq. (2).") ==
          F{{"G(s) = 1 / (s^2 + 3s + 2)", ProtectKind::Formula}});
    CHECK(found("The gains are Kp = 2.5, Ki = 0.8 and Kd = 0.1.") ==
          F{{"Kp = 2.5", ProtectKind::Formula}, {"Ki = 0.8", ProtectKind::Formula}, {"Kd = 0.1", ProtectKind::Formula}});
    CHECK(found("The angle α is small when x ≤ 0.") == F{{"α", ProtectKind::Formula}, {"x ≤ 0", ProtectKind::Formula}});
    CHECK(found("This sentence, e.g. Fig. 3, has no formula.").empty());
}

TEST_CASE("find_protected detects quantities, code and links", "[text][protect]") {
    CHECK(found("The stress reaches 145 MPa within 1.5 ms (approx. 270 MPa).") ==
          F{{"145 MPa", ProtectKind::Quantity}, {"1.5 ms", ProtectKind::Quantity}, {"270 MPa", ProtectKind::Quantity}});
    CHECK(found("A rise of 80 K and 25% load at 3000 rpm.") ==
          F{{"80 K", ProtectKind::Quantity}, {"25%", ProtectKind::Quantity}, {"3000 rpm", ProtectKind::Quantity}});
    CHECK(found("Speed is 12 m/s here.") == F{{"12 m/s", ProtectKind::Quantity}});
    CHECK(found("Call read_reg() and readValue at 0x4000 1000.") ==
          F{{"read_reg()", ProtectKind::Code}, {"readValue", ProtectKind::Code}, {"0x4000", ProtectKind::Code}});
    CHECK(found("See https://example.com/a or mail info@example.org.") ==
          F{{"https://example.com/a", ProtectKind::Link}, {"info@example.org", ProtectKind::Link}});
    CHECK(found("We counted 4096 counts per revolution.").empty());  // không có đơn vị
}

TEST_CASE("apply and restore placeholders round-trip", "[text][protect]") {
    const std::u32string src = to_u32("The plant is G(s) = 1 / (s+1), and it settles in 1.5 ms.");
    const auto spans = text::find_protected(src);
    const auto ph = text::apply_placeholders(src, spans);
    CHECK(ph.text == "The plant is ⟦F1⟧, and it settles in ⟦U1⟧.");
    REQUIRE(ph.placeholders.size() == 2);

    // Mô hình bỏ dấu cách trước placeholder: khôi phục lại.
    const auto r = text::restore_placeholders("Hệ thống là⟦F1⟧, ổn định trong ⟦U1⟧.", ph.placeholders);
    CHECK(r.ok());
    CHECK(r.text == "Hệ thống là G(s) = 1 / (s+1), ổn định trong 1.5 ms.");

    const auto bad = text::restore_placeholders("Mất ⟦U1⟧ và ⟦U1⟧ và ⟦X9⟧.", ph.placeholders);
    CHECK_FALSE(bad.ok());
    CHECK(bad.missing == std::vector<std::string>{"G(s) = 1 / (s+1)"});
    CHECK(bad.duplicated == std::vector<std::string>{"1.5 ms"});
    CHECK(bad.unknown == std::vector<std::string>{"⟦X9⟧"});
}

TEST_CASE("PlaceholderStream restores placeholders split across pieces", "[text][protect]") {
    const std::u32string src = to_u32("Gain Kp = 2 is used for 5 ms only.");
    const auto ph = text::apply_placeholders(src, text::find_protected(src));
    REQUIRE(ph.text == "Gain ⟦F1⟧ is used for ⟦U1⟧ only.");

    text::PlaceholderStream s(ph.placeholders);
    std::string out;
    for (const char* piece : {"Hệ số ", "⟦", "F", "1⟧", "được", " dùng trong ⟦U1", "⟧", " thôi."}) out += s.push(piece);
    out += s.flush();
    CHECK(out == "Hệ số Kp = 2 được dùng trong 5 ms thôi.");
}
