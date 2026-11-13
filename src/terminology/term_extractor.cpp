#include "terminology/term_extractor.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

#include "terminology/words.hpp"
#include "util/utf8.hpp"

namespace pdfread::terminology {
namespace {

bool is_abbreviation_token(std::u32string_view raw) {
    if (raw.size() < 2 || raw.size() > 8) return false;
    int upper = 0;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const char32_t c = raw[i];
        if (c >= U'A' && c <= U'Z') ++upper;
        else if (c >= U'0' && c <= U'9') continue;
        else if (c == U's' && i + 1 == raw.size() && upper >= 2) continue;  // "PIDs"
        else return false;
    }
    if (upper < 2) return false;
    // Số La Mã thuần (II, IV, XII…) không phải từ viết tắt.
    return !std::all_of(raw.begin(), raw.end(), [](char32_t c) { return std::u32string_view(U"IVXLCDM").find(c) != std::u32string_view::npos; });
}

std::u32string strip_plural(std::u32string_view abbr) {
    if (abbr.size() > 2 && abbr.back() == U's') abbr.remove_suffix(1);
    return std::u32string(abbr);
}

// Khớp chữ cái của `abbr` (chữ hoa) với chữ cái đầu các từ trong words[from, to) theo thứ tự,
// cho phép bỏ qua từ chức năng. Trả chỉ số từ đầu tiên của long form, hoặc -1.
int match_long_form_before(const std::vector<Word>& words, std::size_t end, std::u32string_view abbr) {
    std::u32string letters;
    for (char32_t c : abbr) {
        if (c >= U'A' && c <= U'Z') letters.push_back(c - U'A' + U'a');
    }
    if (letters.empty()) return -1;
    int li = static_cast<int>(letters.size()) - 1;
    const std::size_t limit = letters.size() + 3;
    for (std::size_t n = 0; n < limit && n < end; ++n) {
        const std::size_t wi = end - 1 - n;
        const std::u32string& w = words[wi].text;
        if (w.empty()) continue;
        if (w.front() == letters[static_cast<std::size_t>(li)]) {
            if (--li < 0) return static_cast<int>(wi);
        } else if (!is_stopword(w)) {
            return -1;
        }
    }
    return -1;
}

}  // namespace

std::vector<TermCandidate> extract_terms(const std::vector<std::u32string>& sentences, const ExtractOptions& options) {
    std::map<std::u32string, TermCandidate> abbrs;  // khóa: từ viết tắt (bỏ 's' số nhiều)
    std::unordered_map<std::u32string, int> phrase_counts;

    for (const std::u32string& s : sentences) {
        const std::vector<Word> words = split_words(s);

        // Từ viết tắt (dựa trên dạng gốc, có hoa/thường).
        for (std::size_t i = 0; i < words.size(); ++i) {
            const std::u32string_view raw = std::u32string_view(s).substr(words[i].span.begin, words[i].span.size());
            if (!is_abbreviation_token(raw)) continue;
            const std::u32string key = strip_plural(raw);
            TermCandidate& c = abbrs[key];
            c.kind = TermCandidate::Kind::Abbreviation;
            c.term = to_utf8(key);
            ++c.count;
            if (!c.expansion.empty()) continue;

            // "long form (ABBR)": ngay trước ABBR là '('.
            const std::size_t b = words[i].span.begin;
            if (b > 0 && s[b - 1] == U'(' && i > 0) {
                const int first = match_long_form_before(words, i, key);
                if (first >= 0) {
                    const std::size_t from = words[static_cast<std::size_t>(first)].span.begin;
                    const std::size_t to = words[i - 1].span.end;
                    c.expansion = to_utf8(std::u32string_view(s).substr(from, to - from));
                }
            }
            // "ABBR (long form)": ngay sau ABBR là " (" và long form khớp trong ngoặc.
            const std::size_t e = words[i].span.end;
            if (c.expansion.empty() && e + 1 < s.size() && s[e] == U' ' && s[e + 1] == U'(') {
                const std::size_t close = s.find(U')', e + 2);
                if (close != std::u32string::npos) {
                    const std::u32string_view inner = std::u32string_view(s).substr(e + 2, close - e - 2);
                    const std::vector<Word> iw = split_words(inner);
                    if (!iw.empty() && match_long_form_before(iw, iw.size(), key) == 0) c.expansion = to_utf8(inner);
                }
            }
        }

        // Cụm danh từ 2–4 từ.
        for (std::size_t i = 0; i < words.size(); ++i) {
            for (std::size_t n = 2; n <= 4 && i + n <= words.size(); ++n) {
                const Word& first = words[i];
                const Word& last = words[i + n - 1];
                if (is_stopword(first.text) || is_stopword(last.text)) continue;
                if (first.text.size() < 3 || last.text.size() < 3) continue;
                bool ok = true;
                for (std::size_t k = i; k < i + n && ok; ++k) {
                    const std::u32string& w = words[k].text;
                    const bool alpha = std::all_of(w.begin(), w.end(), [](char32_t c) { return c >= U'a' && c <= U'z'; });
                    ok = alpha && (!is_stopword(w) || w == U"of");
                    // Cụm không vượt qua dấu câu: giữa hai từ chỉ được là khoảng trắng hoặc gạch nối.
                    if (ok && k > i) {
                        for (std::size_t p = words[k - 1].span.end; p < words[k].span.begin && ok; ++p) {
                            ok = s[p] == U' ' || s[p] == U'-';
                        }
                    }
                }
                if (!ok) continue;
                std::u32string key;
                for (std::size_t k = i; k < i + n; ++k) {
                    if (!key.empty()) key.push_back(U' ');
                    key += words[k].text;
                }
                ++phrase_counts[key];
            }
        }
    }

    std::vector<TermCandidate> out;
    for (auto& [_, c] : abbrs) {
        if (c.count >= options.min_count || !c.expansion.empty()) {
            c.score = c.count * (c.expansion.empty() ? 1.5 : 3.0);
            out.push_back(c);
        }
    }

    // Bỏ cụm ngắn nằm trong cụm dài có cùng tần suất ("closed loop" trong "closed loop step
    // response"): với mỗi cụm, đánh dấu các cụm con liên tiếp của nó (tuyến tính theo số cụm).
    std::unordered_map<std::u32string, bool> subsumed_set;
    for (const auto& [k, n] : phrase_counts) {
        if (n < options.min_count) continue;
        const auto ws = split_words(k);
        for (std::size_t len = 2; len < ws.size(); ++len) {
            for (std::size_t start = 0; start + len <= ws.size(); ++start) {
                std::u32string sub;
                for (std::size_t t = start; t < start + len; ++t) {
                    if (!sub.empty()) sub.push_back(U' ');
                    sub += ws[t].text;
                }
                auto it = phrase_counts.find(sub);
                if (it != phrase_counts.end() && it->second == n) subsumed_set[sub] = true;
            }
        }
    }
    for (const auto& [k, n] : phrase_counts) {
        if (n < options.min_count || subsumed_set.contains(k)) continue;
        TermCandidate c;
        c.kind = TermCandidate::Kind::Phrase;
        c.term = to_utf8(k);
        c.count = n;
        const auto ws = split_words(k);
        double avg = 0;
        for (const auto& w : ws) avg += static_cast<double>(w.text.size());
        avg /= static_cast<double>(ws.size());
        c.score = n * (1.0 + 0.5 * (static_cast<double>(ws.size()) - 1.0)) * (avg / 6.0);
        out.push_back(std::move(c));
    }

    std::sort(out.begin(), out.end(), [](const TermCandidate& a, const TermCandidate& b) {
        return a.score != b.score ? a.score > b.score : a.term < b.term;
    });
    if (out.size() > options.max_results) out.resize(options.max_results);
    return out;
}

}  // namespace pdfread::terminology
