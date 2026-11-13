#include "text/sentence_splitter.hpp"

#include <algorithm>
#include <iterator>
#include <string>

#include "text/normalize.hpp"

namespace pdfread::text {
namespace {

// Viết thường, không có dấu chấm cuối. Cố ý KHÔNG có các từ hay đứng cuối câu trong tài
// liệu kỹ thuật: "etc", đơn vị "ms"/"min" (trùng danh xưng Ms., viết tắt min.), "max",
// biến một chữ "p", "app", "ex", "est".
constexpr std::u32string_view kAbbreviations[] = {
    U"fig",   U"figs",  U"eq",    U"eqs",   U"eqn",   U"eqns",  U"ref",   U"refs",  U"sec",   U"secs",
    U"sect",  U"tab",   U"tbl",   U"no",    U"nos",   U"vol",   U"vols",  U"vs",    U"cf",    U"e.g",
    U"i.e",   U"al",    U"approx", U"ca",   U"resp",  U"dr",    U"mr",    U"mrs",   U"prof",  U"st",
    U"ch",    U"chap",  U"pp",    U"inc",   U"ltd",   U"co",    U"corp",  U"jr",    U"sr",    U"viz",
    U"avg",   U"std",   U"dept",  U"univ",  U"assoc", U"jan",   U"feb",   U"mar",   U"apr",   U"jun",
    U"jul",   U"aug",   U"sep",   U"sept",  U"oct",   U"nov",   U"dec",   U"thm",   U"def",   U"prop",
    U"cor",   U"lem",   U"appx",  U"excl",  U"incl",  U"fn",    U"ed",    U"eds",   U"rev",   U"suppl"};

char32_t ascii_lower(char32_t c) { return (c >= U'A' && c <= U'Z') ? c - U'A' + U'a' : c; }

bool is_terminal(char32_t c) { return c == U'.' || c == U'!' || c == U'?' || c == 0x2026; }

bool is_closer(char32_t c) {
    return c == U')' || c == U']' || c == U'}' || c == U'"' || c == U'\'' || c == 0x201D || c == 0x2019 ||
           c == 0xBB;
}

bool is_opener(char32_t c) {
    return c == U'(' || c == U'[' || c == U'{' || c == U'"' || c == U'\'' || c == 0x201C || c == 0x2018 ||
           c == 0xAB;
}

// "1", "2.3", "a", "(a)", "IV", "iv)" — số thứ tự đầu mục.
bool is_enumerator(std::u32string_view s) {
    while (!s.empty() && (s.front() == U'(' || s.front() == U'[')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == U')' || s.back() == U']')) s.remove_suffix(1);
    if (s.empty() || s.size() > 8) return false;
    if (s.size() == 1 && is_letter(s[0])) return true;
    const bool numeric = std::all_of(s.begin(), s.end(), [](char32_t c) { return is_digit(c) || c == U'.'; });
    if (numeric && is_digit(s.front())) return true;
    const std::u32string_view roman = U"ivxlcdmIVXLCDM";
    return std::all_of(s.begin(), s.end(), [&](char32_t c) { return roman.find(c) != std::u32string_view::npos; });
}

// Từ ngay trước vị trí `dot` (bỏ dấu ngoặc mở đầu), ví dụ "e.g" trong "(e.g." .
std::u32string_view word_before(std::u32string_view s, std::size_t dot) {
    std::size_t b = dot;
    while (b > 0 && !is_space(s[b - 1]) && s[b - 1] != U'(' && s[b - 1] != U'[') --b;
    return s.substr(b, dot - b);
}

}  // namespace

bool is_abbreviation(std::u32string_view word) {
    if (word.empty() || word.size() > 8) return false;
    std::u32string lower;
    for (char32_t c : word) lower.push_back(ascii_lower(c));
    return std::find(std::begin(kAbbreviations), std::end(kAbbreviations), std::u32string_view(lower)) !=
           std::end(kAbbreviations);
}

std::vector<Span> split_sentences(std::u32string_view s, std::size_t offset) {
    std::vector<Span> out;
    const std::size_t n = s.size();
    std::size_t start = 0;
    while (start < n && is_space(s[start])) ++start;

    auto push = [&](std::size_t b, std::size_t e) {
        while (b < e && is_space(s[b])) ++b;
        while (e > b && is_space(s[e - 1])) --e;
        if (e > b) out.push_back({offset + b, offset + e});
    };

    for (std::size_t i = start; i < n; ++i) {
        const char32_t c = s[i];
        if (!is_terminal(c)) continue;

        std::size_t j = i + 1;
        while (j < n && is_terminal(s[j])) ++j;  // "?!", "..."
        while (j < n && is_closer(s[j])) ++j;    // ".)" ".”"
        if (j >= n) break;                        // hết đoạn: câu cuối xử lý sau vòng lặp
        if (!is_space(s[j])) continue;            // 3.14, v1.2, e.g.x

        std::size_t k = j;
        while (k < n && is_space(s[k])) ++k;
        if (k >= n) break;
        std::size_t m = k;
        while (m < n && is_opener(s[m])) ++m;
        const char32_t next = m < n ? s[m] : s[k];
        if (is_lower(next)) {
            // "i.e. all", "Vs. the": câu chưa kết thúc — trừ khi câu mới mở đầu bằng mục
            // liệt kê trong ngoặc như "(b) Second case".
            std::size_t e = k;
            while (e < n && !is_space(s[e])) ++e;
            const std::u32string_view token = s.substr(k, e - k);
            const bool enum_start = m > k && token.size() >= 3 && token.back() == U')' && is_enumerator(token);
            if (!enum_start) continue;
        }

        if (c == U'.') {
            const std::u32string_view word = word_before(s, i);
            if (is_abbreviation(word)) continue;
            if (word.find(U'.') != std::u32string_view::npos) continue;           // U.S., e.g. dạng lạ
            if (word.size() == 1 && is_upper(word[0])) continue;                  // tên viết tắt: J. Smith
            if (is_enumerator(s.substr(start, i - start))) continue;              // "1." "2.3." "(a)."
        }
        push(start, j);
        start = k;
        i = k - 1;
    }
    push(start, n);
    return out;
}

}  // namespace pdfread::text
