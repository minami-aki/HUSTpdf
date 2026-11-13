#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "text/normalize.hpp"
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

TEST_CASE("find_protected handles nuclear units", "[text][protect]") {
    CHECK(found("Each fission releases about 200 MeV and 2.43 prompt neutrons.") ==
          F{{"200 MeV", ProtectKind::Quantity}});
    CHECK(found("The limit is 20 mSv per year and the dose rate was 2.5 µSv/h near the source.") ==
          F{{"20 mSv", ProtectKind::Quantity}, {"2.5 µSv/h", ProtectKind::Quantity}});
    CHECK(found("The cross section is 680 barns at 0.0253 eV.") ==
          F{{"680 barns", ProtectKind::Quantity}, {"0.0253 eV", ProtectKind::Quantity}});
    CHECK(found("A 370 MBq source and a burnup of 45 GWd/tU were used.") ==
          F{{"370 MBq", ProtectKind::Quantity}, {"45 GWd/tU", ProtectKind::Quantity}});
}

TEST_CASE("match_initial_case capitalizes Vietnamese first letters", "[text][protect]") {
    CHECK(text::match_initial_case("dãy Balmer tương ứng", "The Balmer series") == "Dãy Balmer tương ứng");
    CHECK(text::match_initial_case("đỉnh Bragg", "Bragg peak") == "Đỉnh Bragg");
    CHECK(text::match_initial_case("ứng suất", "Stress") == "Ứng suất");
    CHECK(text::match_initial_case("ống đếm", "Geiger") == "Ống đếm");
    CHECK(text::match_initial_case("ạ", "A") == "Ạ");
    CHECK(text::match_initial_case("\"ơ", "\"O") == "\"Ơ");
    CHECK(text::match_initial_case("dãy Balmer", "the Balmer series") == "dãy Balmer");  // nguồn viết thường
    CHECK(text::match_initial_case("137Cs có", "137Cs has") == "137Cs có");
    CHECK(text::match_initial_case("", "The") == "");
}

TEST_CASE("missing_numbers reports numbers dropped by the translation", "[text][protect]") {
    CHECK(text::missing_numbers("about 8.8 MeV near 56Fe", "khoảng 8,8 MeV gần 56Fe").empty());
    CHECK(text::missing_numbers("enriched to 4.95% 235U in a 17 × 17 array", "làm giàu đến 235U") ==
          std::vector<std::string>{"4.95", "17"});
    CHECK(text::missing_numbers("after 500 h", "sau một thời gian").size() == 1);
    CHECK(text::missing_numbers("no numbers here", "không có số").empty());
    CHECK(text::missing_numbers("25,000 elements", "25.000 phần tử").empty());
}

TEST_CASE("find_protected can leave quantities in the text", "[text][protect]") {
    const std::u32string u = to_u32("It releases 200 MeV, about 5% of E = mc^2.");
    const auto spans = text::find_protected(u, {.quantities = false});
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].kind == ProtectKind::Formula);
}

TEST_CASE("numeric products stay in the text unless quantities are protected", "[text][protect]") {
    const std::u32string u = to_u32("A 17 × 17 array and a flux of 5 × 10^12 n cm^-2 s^-1.");
    const auto off = text::find_protected(u, {.quantities = false});
    REQUIRE(off.size() == 1);
    CHECK(to_utf8(std::u32string_view(u).substr(off[0].span.begin, off[0].span.size())).starts_with("5 × 10^12"));
    CHECK(text::find_protected(u, {.quantities = true}).size() == 2);
}
