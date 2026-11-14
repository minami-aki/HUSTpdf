#include "text/search.hpp"

#include <algorithm>

#include "text/normalize.hpp"
#include "util/utf8.hpp"

namespace pdfread::text {

std::u32string fold_for_search(std::u32string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (char32_t c : s) out.push_back(is_space(c) ? U' ' : to_lower(c));
    return out;
}

std::u32string normalize_query(std::u32string_view s) {
    const std::u32string f = fold_for_search(s);
    std::u32string out;
    bool space = false;
    for (char32_t c : f) {
        if (c == U' ') {
            space = !out.empty();
            continue;
        }
        if (space) out.push_back(U' ');
        space = false;
        out.push_back(c);
    }
    return out;
}

namespace {

// Khớp `needle` tại `at`; khoảng trắng trong needle khớp >= 1 khoảng trắng. Trả vị trí kết thúc
// (độ dài thật trong hay) hoặc 0 nếu không khớp.
std::size_t match_at(std::u32string_view hay, std::size_t at, std::u32string_view needle) {
    std::size_t i = at;
    for (std::size_t k = 0; k < needle.size(); ++k) {
        if (i >= hay.size()) return 0;
        if (needle[k] == U' ') {
            if (hay[i] != U' ') return 0;
            while (i < hay.size() && hay[i] == U' ') ++i;
            continue;
        }
        if (hay[i] != needle[k]) return 0;
        ++i;
    }
    return i;
}

}  // namespace

std::vector<Span> find_matches(std::u32string_view hay, const std::vector<std::u32string>& needles) {
    std::vector<Span> out;
    if (needles.empty()) return out;
    std::size_t i = 0;
    while (i < hay.size()) {
        std::size_t best_end = 0;
        for (const std::u32string& n : needles) {
            if (n.empty() || hay[i] != n[0]) continue;
            const std::size_t e = match_at(hay, i, n);
            if (e > best_end) best_end = e;
        }
        if (best_end > i) {
            out.push_back({i, best_end});
            i = best_end;
        } else {
            ++i;
        }
    }
    return out;
}

std::string snippet(std::u32string_view text, Span span, std::size_t radius) {
    std::size_t b = span.begin > radius ? span.begin - radius : 0;
    std::size_t e = std::min(text.size(), span.end + radius);
    // Cắt ở biên từ.
    if (b > 0) {
        while (b < span.begin && !is_space(text[b - 1])) ++b;
    }
    if (e < text.size()) {
        while (e > span.end && !is_space(text[e])) --e;
    }
    std::u32string out;
    if (b > 0) out += U"…";
    for (std::size_t k = b; k < e; ++k) out.push_back(is_space(text[k]) ? U' ' : text[k]);
    if (e < text.size()) out += U"…";
    return to_utf8(out);
}

}  // namespace pdfread::text
