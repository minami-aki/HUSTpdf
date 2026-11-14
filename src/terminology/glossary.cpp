#include "terminology/glossary.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

#include "terminology/words.hpp"
#include "text/normalize.hpp"
#include "util/utf8.hpp"

namespace pdfread::terminology {
namespace {

std::string trim(std::string s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool parse_bool(std::string s) {
    s = trim(std::move(s));
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s == "1" || s == "true" || s == "yes" || s == "x" || s == "y";
}

std::vector<std::u32string> key_words(std::string_view term) {
    std::vector<std::u32string> out;
    for (Word& w : split_words(to_u32(term))) out.push_back(std::move(w.text));
    return out;
}

// So khớp hai dãy từ; từ cuối cho phép số nhiều.
bool words_match(const std::vector<Word>& text, std::size_t at, const std::vector<std::u32string>& key) {
    if (at + key.size() > text.size()) return false;
    for (std::size_t k = 0; k < key.size(); ++k) {
        const bool last = k + 1 == key.size();
        if (last ? !same_or_plural(text[at + k].text, key[k]) : text[at + k].text != key[k]) return false;
    }
    return true;
}

// Chuẩn hóa để so chuỗi tiếng Việt: viết thường (Unicode cơ bản), gộp khoảng trắng.
std::u32string fold(std::string_view s) {
    std::u32string out;
    bool space = false;
    for (char32_t c : to_u32(s)) {
        if (c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == 0xA0) {
            space = !out.empty();
            continue;
        }
        if (space) out.push_back(U' ');
        space = false;
        // Viết thường: ASCII, Latin-1, và các chữ có dấu tiếng Việt (Latin Extended Additional, cặp chẵn/lẻ).
        if (c >= 0x1EA0 && c <= 0x1EF9 && c % 2 == 0) c += 1;
        else if (c == 0x110) c = 0x111;  // Đ -> đ
        else if ((c == 0x102 || c == 0x1A0 || c == 0x1AF || c == 0x128 || c == 0x168)) c += 1;  // Ă Ơ Ư Ĩ Ũ
        else c = lower(c);
        out.push_back(c);
    }
    return out;
}

}  // namespace

// --- CSV ---------------------------------------------------------------------

std::vector<std::vector<std::string>> parse_csv(std::string_view data) {
    if (data.starts_with("\xEF\xBB\xBF")) data.remove_prefix(3);  // BOM (Excel)
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string cell;
    bool quoted = false;
    bool cell_started = false;
    for (std::size_t i = 0; i < data.size(); ++i) {
        const char c = data[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < data.size() && data[i + 1] == '"') {
                    cell.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cell.push_back(c);
            }
            continue;
        }
        if (c == '"' && !cell_started) {
            quoted = true;
            cell_started = true;
        } else if (c == ',') {
            row.push_back(std::move(cell));
            cell.clear();
            cell_started = false;
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < data.size() && data[i + 1] == '\n') ++i;
            row.push_back(std::move(cell));
            cell.clear();
            cell_started = false;
            const bool blank = row.size() == 1 && row[0].empty();
            if (!blank) rows.push_back(std::move(row));
            row.clear();
        } else {
            cell.push_back(c);
            cell_started = true;
        }
    }
    if (cell_started || !row.empty()) {
        row.push_back(std::move(cell));
        rows.push_back(std::move(row));
    }
    return rows;
}

std::string to_csv(const std::vector<std::vector<std::string>>& rows) {
    std::string out;
    for (const auto& row : rows) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i > 0) out.push_back(',');
            const std::string& v = row[i];
            if (v.find_first_of(",\"\r\n") != std::string::npos) {
                out.push_back('"');
                for (char c : v) {
                    if (c == '"') out.push_back('"');
                    out.push_back(c);
                }
                out.push_back('"');
            } else {
                out += v;
            }
        }
        out += "\r\n";
    }
    return out;
}

std::vector<GlossaryEntry> parse_glossary_csv(std::string_view data) {
    const auto rows = parse_csv(data);
    if (rows.empty()) return {};
    std::map<std::string, std::size_t> col;
    for (std::size_t i = 0; i < rows[0].size(); ++i) col[trim(rows[0][i])] = i;
    if (!col.contains("term_en") || !col.contains("term_vi")) {
        throw std::runtime_error("CSV glossary thiếu cột term_en hoặc term_vi");
    }
    auto cell = [&](const std::vector<std::string>& row, const char* name) -> std::string {
        auto it = col.find(name);
        return it != col.end() && it->second < row.size() ? trim(row[it->second]) : std::string();
    };
    std::vector<GlossaryEntry> out;
    for (std::size_t r = 1; r < rows.size(); ++r) {
        GlossaryEntry e;
        e.term_en = cell(rows[r], "term_en");
        if (e.term_en.empty() || e.term_en.starts_with("#")) continue;
        e.term_vi = cell(rows[r], "term_vi");
        e.keep_english = parse_bool(cell(rows[r], "keep_english"));
        e.notes = cell(rows[r], "notes");
        e.source = cell(rows[r], "source");
        e.explanation = cell(rows[r], "explanation");
        const std::string aliases = cell(rows[r], "aliases");
        for (std::size_t p = 0; p <= aliases.size();) {
            const std::size_t bar = std::min(aliases.find('|', p), aliases.size());
            std::string a = trim(aliases.substr(p, bar - p));
            if (!a.empty()) e.aliases.push_back(std::move(a));
            p = bar + 1;
        }
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<GlossaryEntry> load_glossary_csv(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error(std::format("Không mở được {}", path_to_utf8(path)));
    std::stringstream ss;
    ss << f.rdbuf();
    return parse_glossary_csv(ss.str());
}

void save_glossary_csv(const std::filesystem::path& path, const std::vector<GlossaryEntry>& entries) {
    std::vector<std::vector<std::string>> rows{
        {"term_en", "term_vi", "keep_english", "notes", "source", "aliases", "explanation"}};
    for (const auto& e : entries) {
        if (!e.alias_of.empty()) continue;  // mục ảo của chỉ mục
        std::string aliases;
        for (const auto& a : e.aliases) aliases += (aliases.empty() ? "" : "|") + a;
        rows.push_back({e.term_en, e.term_vi, e.keep_english ? "1" : "0", e.notes, e.source, aliases, e.explanation});
    }
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error(std::format("Không ghi được {}", path_to_utf8(path)));
    const std::string data = "\xEF\xBB\xBF" + to_csv(rows);  // BOM để Excel mở đúng UTF-8
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
}

// --- Chỉ mục -------------------------------------------------------------------

std::pair<std::string, std::string> split_alias(const std::string& alias) {
    const std::size_t eq = alias.find('=');
    if (eq == std::string::npos) return {trim(alias), {}};
    return {trim(alias.substr(0, eq)), trim(alias.substr(eq + 1))};
}

GlossaryIndex::GlossaryIndex(std::vector<GlossaryEntry> entries) : entries_(std::move(entries)) {
    // Tên gọi khác -> mục ảo: "Am-241" (giữ nguyên), "Clebsch–Gordan Coefficients=hệ số Clebsch–Gordan".
    const std::size_t base = entries_.size();
    for (std::size_t i = 0; i < base; ++i) {
        for (const std::string& alias : entries_[i].aliases) {
            auto [name, vi] = split_alias(alias);
            if (name.empty() || fold(name) == fold(entries_[i].term_en)) continue;
            GlossaryEntry a = entries_[i];
            a.aliases.clear();
            a.alias_of = entries_[i].term_en;
            a.term_en = std::move(name);
            // Viết tắt: giữ nguyên khi dịch câu (target() = tên), nhưng term_vi mang nghĩa của
            // thuật ngữ gốc để hiển thị khi tra ("Am-241" -> "americi-241").
            a.keep_english = vi.empty();
            a.term_vi = vi.empty() ? entries_[i].term_vi : std::move(vi);
            entries_.push_back(std::move(a));
        }
    }
    // Gom các entry cùng term_en (đã chuẩn hóa) vào một key, sắp theo ưu tiên.
    std::map<std::vector<std::u32string>, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        auto words = key_words(entries_[i].term_en);
        if (!words.empty()) groups[std::move(words)].push_back(i);
    }
    for (auto& [words, idx] : groups) {
        std::stable_sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) {
            const auto& ea = entries_[a];
            const auto& eb = entries_[b];
            if (ea.confirmed() != eb.confirmed()) return ea.confirmed();
            if (ea.layer != eb.layer) return ea.layer < eb.layer;
            // Cùng tầng: thuật ngữ gốc trước tên gọi khác của thuật ngữ khác.
            if (ea.alias_of.empty() != eb.alias_of.empty()) return ea.alias_of.empty();
            return ea.rank < eb.rank;
        });
        Key key{words, std::move(idx), false, {}};
        key.case_sensitive = std::all_of(key.entries.begin(), key.entries.end(), [&](std::size_t e) {
            const std::u32string t = to_u32(entries_[e].term_en);
            const auto letters = std::count_if(t.begin(), t.end(), [](char32_t ch) { return text::is_letter(ch); });
            const bool any_lower = std::any_of(t.begin(), t.end(), [](char32_t ch) { return text::is_lower(ch); });
            return letters >= 2 && !any_lower;
        });
        if (key.case_sensitive) key.exact = to_u32(entries_[key.entries.front()].term_en);
        keys_.push_back(std::move(key));
    }
    for (std::size_t k = 0; k < keys_.size(); ++k) by_first_word_[keys_[k].words.front()].push_back(k);
    for (auto& [_, list] : by_first_word_) {
        std::sort(list.begin(), list.end(),
                  [&](std::size_t a, std::size_t b) { return keys_[a].words.size() > keys_[b].words.size(); });
    }
}

TermMatch GlossaryIndex::make_match(const Key& key, text::Span span) const {
    TermMatch m;
    m.span = span;
    m.entry = &entries_[key.entries.front()];
    for (std::size_t k = 1; k < key.entries.size(); ++k) {
        const GlossaryEntry& e = entries_[key.entries[k]];
        if (!e.confirmed() || fold(e.target()) == fold(m.entry->target())) continue;
        const bool dup = std::any_of(m.alternatives.begin(), m.alternatives.end(),
                                     [&](const GlossaryEntry* a) { return fold(a->target()) == fold(e.target()); });
        if (!dup) m.alternatives.push_back(&e);
    }
    return m;
}

std::vector<TermMatch> GlossaryIndex::find_in(std::u32string_view text, const std::vector<text::Span>& excluded) const {
    std::vector<TermMatch> out;
    const std::vector<Word> words = split_words(text);
    auto blocked = [&](text::Span s) {
        return std::any_of(excluded.begin(), excluded.end(), [&](const text::Span& e) { return e.overlaps(s); });
    };
    std::size_t i = 0;
    while (i < words.size()) {
        bool matched = false;
        // Thử theo từ đầu; vì từ đầu có thể là số nhiều của key một từ, thử cả dạng gốc.
        std::vector<std::u32string> heads{words[i].text};
        const std::u32string& w = words[i].text;
        if (w.size() > 2 && w.back() == U's') heads.push_back(w.substr(0, w.size() - 1));
        if (w.size() > 3 && w.ends_with(U"es")) heads.push_back(w.substr(0, w.size() - 2));
        if (w.size() > 4 && w.ends_with(U"ies")) heads.push_back(w.substr(0, w.size() - 3) + U"y");
        std::size_t best_key = SIZE_MAX;
        for (const auto& h : heads) {
            auto it = by_first_word_.find(h);
            if (it == by_first_word_.end()) continue;
            for (std::size_t k : it->second) {
                if (best_key != SIZE_MAX && keys_[k].words.size() <= keys_[best_key].words.size()) continue;
                if (!words_match(words, i, keys_[k].words)) continue;
                if (keys_[k].case_sensitive) {
                    const std::size_t b = words[i].span.begin;
                    const std::size_t e = words[i + keys_[k].words.size() - 1].span.end;
                    const std::u32string_view surface = text.substr(b, e - b);
                    const std::u32string& x = keys_[k].exact;
                    if (surface != x && !(surface.size() == x.size() + 1 && surface.starts_with(x) && surface.back() == U's')) continue;
                }
                best_key = k;
            }
        }
        if (best_key != SIZE_MAX) {
            const Key& key = keys_[best_key];
            const text::Span span{words[i].span.begin, words[i + key.words.size() - 1].span.end};
            if (!blocked(span)) {
                out.push_back(make_match(key, span));
                i += key.words.size();
                matched = true;
            }
        }
        if (!matched) ++i;
    }
    return out;
}

std::optional<TermMatch> GlossaryIndex::lookup(std::u32string_view term) const {
    const auto words = split_words(term);
    if (words.empty()) return std::nullopt;
    for (const auto& m : find_in(term)) {
        if (m.span.begin <= words.front().span.begin && m.span.end >= words.back().span.end) return m;
    }
    return std::nullopt;
}

std::vector<std::string> GlossaryIndex::english_for(std::u32string_view vietnamese, std::size_t max_terms) const {
    const std::u32string q = fold(to_utf8(vietnamese));
    std::vector<std::string> out;
    if (q.empty()) return out;
    std::vector<const GlossaryEntry*> exact;
    std::vector<const GlossaryEntry*> partial;
    for (const GlossaryEntry& e : entries_) {
        if (e.term_vi.empty()) continue;
        const std::u32string vi = fold(e.term_vi);
        if (vi == q) {
            exact.push_back(&e);
        } else if (const std::size_t pos = vi.find(q); pos != std::u32string::npos) {
            const bool start_ok = pos == 0 || vi[pos - 1] == U' ';
            const std::size_t end = pos + q.size();
            const bool end_ok = end == vi.size() || vi[end] == U' ' || vi[end] == U',';
            if (start_ok && end_ok) partial.push_back(&e);
        }
    }
    auto by_priority = [](const GlossaryEntry* a, const GlossaryEntry* b) {
        return a->layer != b->layer ? a->layer < b->layer : a->rank < b->rank;
    };
    std::stable_sort(exact.begin(), exact.end(), by_priority);
    std::stable_sort(partial.begin(), partial.end(), by_priority);
    auto add = [&](const GlossaryEntry* e) {
        if (out.size() >= max_terms) return;
        const bool dup = std::any_of(out.begin(), out.end(), [&](const std::string& t) { return fold(t) == fold(e->term_en); });
        if (!dup) out.push_back(e->term_en);
    };
    for (const GlossaryEntry* e : exact) add(e);
    if (out.empty()) {
        for (const GlossaryEntry* e : partial) add(e);
    }
    return out;
}

std::vector<GlossaryEntry> check_terms(std::string_view translation, const std::vector<GlossaryEntry>& required) {
    const std::u32string out = fold(translation);
    std::vector<GlossaryEntry> missing;
    for (const GlossaryEntry& e : required) {
        if (!e.confirmed()) continue;
        if (out.find(fold(e.target())) == std::u32string::npos) missing.push_back(e);
    }
    return missing;
}

}  // namespace pdfread::terminology
