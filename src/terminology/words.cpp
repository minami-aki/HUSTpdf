#include "terminology/words.hpp"

#include <algorithm>
#include <array>

namespace pdfread::terminology {
namespace {

bool word_char(char32_t c) {
    return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= U'0' && c <= U'9') ||
           (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) || (c >= 0x370 && c <= 0x3FF);
}

constexpr std::array<std::u32string_view, 72> kStopwords{
    U"a",     U"an",    U"the",   U"and",   U"or",    U"of",    U"in",    U"on",    U"at",    U"to",
    U"for",   U"from",  U"by",    U"with",  U"without", U"as",  U"is",    U"are",   U"was",   U"were",
    U"be",    U"been",  U"being", U"this",  U"that",  U"these", U"those", U"it",    U"its",   U"we",
    U"our",   U"they",  U"their", U"he",    U"she",   U"which", U"who",   U"whom",  U"whose", U"what",
    U"when",  U"where", U"while", U"than",  U"then",  U"there", U"here",  U"such",  U"can",   U"may",
    U"must",  U"should", U"will", U"would", U"could", U"has",   U"have",  U"had",   U"not",   U"no",
    U"all",   U"any",   U"each",  U"both",  U"more",  U"most",  U"other", U"into",  U"over",  U"under",
    U"also",  U"via"};

}  // namespace

char32_t lower(char32_t c) noexcept {
    if (c >= U'A' && c <= U'Z') return c - U'A' + U'a';
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 0x20;
    return c;
}

std::u32string lower(std::u32string_view s) {
    std::u32string out(s);
    for (char32_t& c : out) c = lower(c);
    return out;
}

std::vector<Word> split_words(std::u32string_view text) {
    std::vector<Word> out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (!word_char(text[i])) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < text.size() && word_char(text[j])) ++j;
        out.push_back({lower(text.substr(i, j - i)), {i, j}});
        i = j;
    }
    return out;
}

bool same_or_plural(std::u32string_view word, std::u32string_view base) noexcept {
    if (word == base) return true;
    if (word.size() == base.size() + 1 && word.back() == U's' && word.substr(0, base.size()) == base) return true;
    if (word.size() == base.size() + 2 && word.ends_with(U"es") && word.substr(0, base.size()) == base) return true;
    if (!base.empty() && base.back() == U'y' && word.size() == base.size() + 2 && word.ends_with(U"ies") &&
        word.substr(0, base.size() - 1) == base.substr(0, base.size() - 1)) {
        return true;
    }
    return false;
}

bool is_stopword(std::u32string_view w) noexcept {
    return std::find(kStopwords.begin(), kStopwords.end(), w) != kStopwords.end();
}

}  // namespace pdfread::terminology
