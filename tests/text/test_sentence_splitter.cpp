#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "text/sentence_splitter.hpp"
#include "util/utf8.hpp"

using namespace pdfread;

namespace {

std::vector<std::string> split(const std::string& s) {
    const std::u32string u = to_u32(s);
    std::vector<std::string> out;
    for (const text::Span& sp : text::split_sentences(u)) out.push_back(to_utf8(std::u32string_view(u).substr(sp.begin, sp.size())));
    return out;
}

using V = std::vector<std::string>;

}  // namespace

TEST_CASE("split_sentences splits on terminal punctuation followed by a capital", "[text][sentence]") {
    CHECK(split("The plant is stable. The controller is tuned. Is it robust? Yes!") ==
          V{"The plant is stable.", "The controller is tuned.", "Is it robust?", "Yes!"});
    CHECK(split("  Leading and trailing spaces.   Second.  ") == V{"Leading and trailing spaces.", "Second."});
    CHECK(split("No terminal punctuation at the end") == V{"No terminal punctuation at the end"});
    CHECK(split("").empty());
}

TEST_CASE("split_sentences keeps technical abbreviations inside the sentence", "[text][sentence]") {
    CHECK(split("Fig. 3 shows the response. Eq. (2) gives the gain.") ==
          V{"Fig. 3 shows the response.", "Eq. (2) gives the gain."});
    CHECK(split("Small gains, e.g. Kp = 0.1, are safe. Large ones are not.") ==
          V{"Small gains, e.g. Kp = 0.1, are safe.", "Large ones are not."});
    CHECK(split("All poles, i.e. both roots, are stable.") == V{"All poles, i.e. both roots, are stable."});
    CHECK(split("Nguyen et al. reported similar results. We agree.") ==
          V{"Nguyen et al. reported similar results.", "We agree."});
    CHECK(split("Results by Smith et al. (2019) differ.") == V{"Results by Smith et al. (2019) differ."});
    CHECK(split("See No. 5 in the list. Done.") == V{"See No. 5 in the list.", "Done."});
    CHECK(split("Model A vs. Model B was tested.") == V{"Model A vs. Model B was tested."});
    CHECK(split("The stress is approx. 270 MPa here.") == V{"The stress is approx. 270 MPa here."});
    CHECK(split("Details are in Sec. 4.2 and Ref. 7. Next topic.") ==
          V{"Details are in Sec. 4.2 and Ref. 7.", "Next topic."});
    CHECK(split("Data from the U.S. Army were used.") == V{"Data from the U.S. Army were used."});
    CHECK(split("As shown by J. Smith in his thesis.") == V{"As shown by J. Smith in his thesis."});
}

TEST_CASE("split_sentences ignores dots inside numbers, versions and units", "[text][sentence]") {
    CHECK(split("The gain is 3.14 and the period is 1.5 ms. The firmware v1.2 is used.") ==
          V{"The gain is 3.14 and the period is 1.5 ms.", "The firmware v1.2 is used."});
    CHECK(split("Visit www.example.com for details.") == V{"Visit www.example.com for details."});
    CHECK(split("The stress reaches 145 MPa. The alloy yields at 270 MPa.") ==
          V{"The stress reaches 145 MPa.", "The alloy yields at 270 MPa."});
    CHECK(split("Heat it for 5 min. Then cool the vector p. It works.") ==
          V{"Heat it for 5 min.", "Then cool the vector p.", "It works."});
}

TEST_CASE("split_sentences handles quotes, brackets and numbered headings", "[text][sentence]") {
    CHECK(split("He said \"stop.\" Then it stopped.") == V{"He said \"stop.\"", "Then it stopped."});
    CHECK(split("The value is fixed (see below.) Another one.") == V{"The value is fixed (see below.)", "Another one."});
    CHECK(split("1. Introduction to control") == V{"1. Introduction to control"});
    CHECK(split("2.3. Model setup is described here. It is simple.") ==
          V{"2.3. Model setup is described here.", "It is simple."});
    CHECK(split("(a) First case. (b) Second case.") == V{"(a) First case.", "(b) Second case."});
    CHECK(split("Wait... there is more. Indeed.") == V{"Wait... there is more.", "Indeed."});
}

TEST_CASE("is_abbreviation is case-insensitive", "[text][sentence]") {
    CHECK(text::is_abbreviation(U"Fig"));
    CHECK(text::is_abbreviation(U"FIG"));
    CHECK(text::is_abbreviation(U"e.g"));
    CHECK_FALSE(text::is_abbreviation(U"etc"));
    CHECK_FALSE(text::is_abbreviation(U"controller"));
}
