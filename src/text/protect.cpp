#include "text/protect.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>

#include "text/normalize.hpp"
#include "util/utf8.hpp"

namespace pdfread::text {
namespace {

struct Token {
    Span span;           // cả token (không gồm khoảng trắng)
    Span core;           // đã bỏ dấu câu cuối (,.;:!?) và ngoặc đóng thừa
    bool trailing_punct;  // có dấu câu ngăn cách sau core (",", ";"…)
};

constexpr std::u32string_view kMathChars =
    U"=<>≤≥≈≠±×÷∑∏∫∂∇√∞∈∉⊂⊆∀∃→⇒⇔^∝′·∘≡≅∼";

constexpr auto kUnits = std::to_array<std::u32string_view>({
    U"s",   U"ms",  U"us",  U"µs",  U"μs",  U"ns",  U"ps",  U"min", U"h",   U"Hz",  U"kHz", U"MHz", U"GHz", U"THz",
    U"V",   U"mV",  U"µV",  U"kV",  U"MV",  U"A",   U"mA",  U"µA",  U"μA",  U"kA",  U"W",   U"mW",  U"kW",  U"MW",
    U"GW",  U"Ω",   U"kΩ",  U"MΩ",  U"mΩ",  U"F",   U"mF",  U"µF",  U"μF",  U"nF",  U"pF",  U"H",   U"mH",  U"µH",
    U"K",   U"°C",  U"°F",  U"Pa",  U"kPa", U"MPa", U"GPa", U"bar", U"N",   U"kN",  U"Nm",  U"J",   U"kJ",  U"MJ",
    U"Wh",  U"kWh", U"kg",  U"g",   U"mg",  U"m",   U"mm",  U"cm",  U"µm",  U"μm",  U"nm",  U"km",  U"L",   U"mL",
    U"mol", U"dB",  U"dBm", U"rpm", U"bit", U"bits", U"kbps", U"Mbps", U"Gbps", U"ppm",
    // Hạt nhân – nguyên tử: năng lượng, hoạt độ, liều, tiết diện, khối lượng nguyên tử, công suất nhà máy.
    U"eV",  U"meV", U"keV", U"MeV", U"GeV", U"TeV", U"Bq",  U"kBq", U"MBq", U"GBq", U"TBq", U"Ci",  U"mCi", U"µCi",
    U"μCi", U"Gy",  U"cGy", U"mGy", U"µGy", U"μGy", U"Sv",  U"mSv", U"µSv", U"μSv", U"rem", U"mrem", U"b",  U"mb",
    U"barn", U"barns", U"u", U"MWe", U"MWt", U"MWth", U"GWe", U"GWd", U"MWd", U"t", U"tU", U"tHM", U"µg",  U"μg",  U"ng",  U"Torr", U"atm"});

bool is_unit(std::u32string_view t) {
    if (std::find(kUnits.begin(), kUnits.end(), t) != kUnits.end()) return true;
    // Đơn vị ghép: m/s, km/h, N/mm2, W/(m·K), m/s2…
    if (t.find(U'/') != std::u32string_view::npos) {
        const std::size_t slash = t.find(U'/');
        std::u32string_view a = t.substr(0, slash);
        std::u32string_view b = t.substr(slash + 1);
        while (!b.empty() && (b.back() == U'2' || b.back() == U'3' || b.back() == U'²' || b.back() == U'³')) b.remove_suffix(1);
        while (!a.empty() && (a.back() == U'2' || a.back() == U'3' || a.back() == U'²' || a.back() == U'³')) a.remove_suffix(1);
        return !a.empty() && !b.empty() && std::find(kUnits.begin(), kUnits.end(), a) != kUnits.end() &&
               (std::find(kUnits.begin(), kUnits.end(), b) != kUnits.end() || b.starts_with(U"("));
    }
    return false;
}

bool is_number(std::u32string_view t) {
    if (t.empty()) return false;
    std::size_t i = 0;
    if (t[0] == U'-' || t[0] == U'+' || t[0] == 0x2212) i = 1;
    bool digit = false;
    for (; i < t.size(); ++i) {
        const char32_t c = t[i];
        if (is_digit(c)) digit = true;
        else if (c != U'.' && c != U',' && c != 0x2013 && c != U'-') return false;
    }
    return digit && is_digit(t.back());
}

bool has_math_char(std::u32string_view t) {
    for (char32_t c : t) {
        if (kMathChars.find(c) != std::u32string_view::npos) return true;
        if ((c >= 0x3B1 && c <= 0x3C9) || (c >= 0x391 && c <= 0x3A9)) return true;  // chữ Hy Lạp
    }
    return false;
}

bool is_operator(std::u32string_view t) {
    return t.size() == 1 && std::u32string_view(U"+-−*/=<>≤≥≈≠±×÷·|").find(t[0]) != std::u32string_view::npos;
}

bool is_alnum(char32_t c) { return is_letter(c) || is_digit(c); }

// Từ tiếng Anh thường gặp ngắn: chặn việc mở rộng công thức sang chữ.
bool is_short_word(std::u32string_view t) {
    static constexpr std::array<std::u32string_view, 24> words{
        U"a",  U"an", U"and", U"or",  U"the", U"is", U"are", U"of", U"to", U"in", U"for", U"on",
        U"at", U"by", U"we",  U"it",  U"as",  U"be", U"if",  U"so", U"with", U"see", U"when", U"then"};
    std::u32string lower;
    for (char32_t c : t) lower.push_back((c >= U'A' && c <= U'Z') ? c + 32 : c);
    return std::find(words.begin(), words.end(), std::u32string_view(lower)) != words.end();
}

// Token "kiểu toán": toán tử, số, tên biến ngắn (<= 3 chữ/số), biểu thức trong ngoặc.
bool is_mathy(std::u32string_view t) {
    if (t.empty()) return false;
    if (has_math_char(t) || is_operator(t) || is_number(t)) return true;
    if (is_short_word(t)) return false;
    std::size_t letters = 0;
    std::size_t run = 0;
    std::size_t max_run = 0;
    for (char32_t c : t) {
        if (is_letter(c)) {
            ++letters;
            max_run = std::max(max_run, ++run);
        } else {
            run = 0;
            if (!is_digit(c) && std::u32string_view(U"()[]_,.'+-−*/").find(c) == std::u32string_view::npos) return false;
        }
    }
    return max_run <= 3 && (letters > 0 || t.find_first_of(U"()") != std::u32string_view::npos);
}

bool is_link(std::u32string_view t) {
    if (t.starts_with(U"http://") || t.starts_with(U"https://") || t.starts_with(U"www.")) return true;
    const std::size_t at = t.find(U'@');
    return at != std::u32string_view::npos && at > 0 && t.find(U'.', at) != std::u32string_view::npos;
}

bool is_code(std::u32string_view t) {
    if (t.size() < 2) return false;
    if (t.find(U"::") != std::u32string_view::npos || t.find(U"->") != std::u32string_view::npos ||
        t.find(U"==") != std::u32string_view::npos || t.find(U'`') != std::u32string_view::npos) {
        return true;
    }
    if ((t.starts_with(U"0x") || t.starts_with(U"0X")) && t.size() > 2 &&
        std::all_of(t.begin() + 2, t.end(), [](char32_t c) {
            return is_digit(c) || (c >= U'a' && c <= U'f') || (c >= U'A' && c <= U'F') || c == U'_';
        })) {
        return true;
    }
    // ident( … ) với tên >= 3 ký tự: read_reg(), printf(x)
    const std::size_t paren = t.find(U'(');
    if (paren != std::u32string_view::npos && paren >= 3 && t.back() == U')' &&
        std::all_of(t.begin(), t.begin() + static_cast<std::ptrdiff_t>(paren), [](char32_t c) { return is_alnum(c) || c == U'_' || c == U'.'; })) {
        return true;
    }
    // snake_case: '_' giữa hai chữ/số
    for (std::size_t i = 1; i + 1 < t.size(); ++i) {
        if (t[i] == U'_' && is_alnum(t[i - 1]) && is_alnum(t[i + 1])) return true;
    }
    // camelCase: bắt đầu chữ thường, có chữ hoa đứng sau chữ thường, toàn chữ/số, dài >= 4
    if (t.size() >= 4 && is_lower(t[0]) && std::all_of(t.begin(), t.end(), is_alnum)) {
        for (std::size_t i = 1; i < t.size(); ++i) {
            if (is_upper(t[i]) && is_lower(t[i - 1])) return true;
        }
    }
    return false;
}

std::vector<Token> tokenize(std::u32string_view s) {
    std::vector<Token> out;
    std::size_t i = 0;
    while (i < s.size()) {
        if (is_space(s[i])) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < s.size() && !is_space(s[j])) ++j;
        Token t{{i, j}, {i, j}, false};
        // Bỏ dấu câu cuối; ngoặc đóng chỉ bỏ khi trong token không có ngoặc mở tương ứng.
        std::size_t e = j;
        for (;;) {
            if (e <= i) break;
            const char32_t c = s[e - 1];
            if (c == U',' || c == U';' || c == U':' || c == U'!' || c == U'?' || c == U'.' || c == U'"' || c == 0x201D) {
                --e;
                t.trailing_punct = t.trailing_punct || c == U',' || c == U';' || c == U':';
                continue;
            }
            if (c == U')' || c == U']') {
                const std::u32string_view body = s.substr(i, e - i);
                const auto opens = std::count(body.begin(), body.end(), c == U')' ? U'(' : U'[');
                const auto closes = std::count(body.begin(), body.end(), c);
                if (closes > opens) {
                    --e;
                    continue;
                }
            }
            break;
        }
        // Bỏ ngoặc mở thừa đầu token: "(see" -> "see".
        std::size_t b = i;
        while (b < e && (s[b] == U'(' || s[b] == U'[' || s[b] == U'"' || s[b] == 0x201C)) {
            const std::u32string_view body = s.substr(b, e - b);
            const char32_t close = s[b] == U'(' ? U')' : s[b] == U'[' ? U']' : U'"';
            if (std::count(body.begin(), body.end(), s[b]) <= std::count(body.begin(), body.end(), close) && s[b] != U'"' && s[b] != 0x201C) break;
            ++b;
        }
        t.core = {b, std::max(b, e)};
        out.push_back(t);
        i = j;
    }
    return out;
}

std::u32string_view view(std::u32string_view s, Span sp) { return s.substr(sp.begin, sp.size()); }

}  // namespace

std::vector<ProtectedSpan> find_protected(std::u32string_view s, const ProtectOptions& options) {
    const std::vector<Token> tokens = tokenize(s);
    std::vector<ProtectedSpan> out;
    std::vector<bool> used(tokens.size(), false);

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (used[i]) continue;
        const std::u32string_view t = view(s, tokens[i].core);
        if (t.empty()) continue;

        if (is_link(t)) {
            out.push_back({tokens[i].core, ProtectKind::Link});
            used[i] = true;
            continue;
        }
        if (is_code(t)) {
            out.push_back({tokens[i].core, ProtectKind::Code});
            used[i] = true;
            continue;
        }
        // Đại lượng: số + đơn vị ("145 MPa"), hoặc liền nhau ("25%", "10V").
        if (options.quantities && is_number(t) && i + 1 < tokens.size() && !tokens[i].trailing_punct && is_unit(view(s, tokens[i + 1].core))) {
            out.push_back({{tokens[i].core.begin, tokens[i + 1].core.end}, ProtectKind::Quantity});
            used[i] = used[i + 1] = true;
            ++i;
            continue;
        }
        if (options.quantities && t.size() >= 2 && t.back() == U'%' && is_number(t.substr(0, t.size() - 1))) {
            out.push_back({tokens[i].core, ProtectKind::Quantity});
            used[i] = true;
            continue;
        }
        // Công thức: token có ký hiệu toán làm nhân, mở rộng qua các token kiểu toán lân cận.
        if (has_math_char(t)) {
            std::size_t first = i;
            std::size_t last = i;
            while (first > 0 && !used[first - 1] && !tokens[first - 1].trailing_punct &&
                   is_mathy(view(s, tokens[first - 1].core))) {
                --first;
            }
            while (!tokens[last].trailing_punct && last + 1 < tokens.size() && !used[last + 1] &&
                   is_mathy(view(s, tokens[last + 1].core))) {
                ++last;
            }
            Span span{tokens[first].core.begin, tokens[last].core.end};
            // Ngoặc mở chưa đóng: dấu đóng có thể nằm ở đuôi token cuối đã bị cắt như dấu câu
            // ("(s^2 + 3s + 2)," -> core "2", đuôi "),"). Chỉ đóng trong đuôi đó, không lan sang từ khác.
            int bal = 0;
            for (std::size_t p = span.begin; p < span.end; ++p) {
                if (s[p] == U'(') ++bal;
                if (s[p] == U')') --bal;
            }
            for (std::size_t p = span.end; bal > 0 && p < tokens[last].span.end; ++p) {
                if (s[p] == U')' && --bal == 0) span.end = p + 1;
            }
            for (std::size_t k = first; k <= last; ++k) used[k] = true;
            // Không bảo vệ đại lượng: "17 × 17" (chỉ số và dấu nhân, như kích thước mảng) cũng
            // để nguyên — mô hình chép đúng, còn placeholder thì hay bị bỏ mất.
            const std::u32string_view body = s.substr(span.begin, span.size());
            const bool numeric_product = std::all_of(body.begin(), body.end(), [](char32_t c) {
                return is_digit(c) || is_space(c) || c == U'.' || c == U',' || c == U'×';
            });
            if (!options.quantities && numeric_product) {
                i = last;
                continue;
            }
            out.push_back({span, ProtectKind::Formula});
            i = last;
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.span.begin < b.span.begin; });
    return out;
}

Placeholdered apply_placeholders(std::u32string_view text, const std::vector<ProtectedSpan>& spans) {
    Placeholdered out;
    int counts[4] = {0, 0, 0, 0};
    std::size_t pos = 0;
    for (const ProtectedSpan& p : spans) {
        if (p.span.begin < pos || p.span.end > text.size()) continue;
        out.text += to_utf8(text.substr(pos, p.span.begin - pos));
        const int k = static_cast<int>(p.kind);
        const char letter = "FCUL"[k];
        Placeholder ph;
        ph.token = std::format("⟦{}{}⟧", letter, ++counts[k]);
        ph.original = to_utf8(text.substr(p.span.begin, p.span.size()));
        ph.space_before = p.span.begin > 0 && is_space(text[p.span.begin - 1]);
        ph.space_after = p.span.end < text.size() && is_space(text[p.span.end]);
        out.text += ph.token;
        out.placeholders.push_back(std::move(ph));
        pos = p.span.end;
    }
    out.text += to_utf8(text.substr(pos));
    return out;
}

namespace {

constexpr std::string_view kOpen = "⟦";
constexpr std::string_view kClose = "⟧";

bool alnum_byte(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || u >= 0x80;  // chữ có dấu (UTF-8 nhiều byte) cũng tính là chữ
}

// Tên trong ⟦…⟧, bỏ khoảng trắng mô hình có thể chèn vào.
std::string token_id(std::string_view inner) {
    std::string id;
    for (char c : inner) {
        if (c != ' ') id.push_back(c);
    }
    return id;
}

const Placeholder* find_ph(const std::vector<Placeholder>& phs, const std::string& id) {
    for (const auto& p : phs) {
        if (p.token.size() > kOpen.size() + kClose.size() &&
            p.token.substr(kOpen.size(), p.token.size() - kOpen.size() - kClose.size()) == id) {
            return &p;
        }
    }
    return nullptr;
}

}  // namespace

RestoreResult restore_placeholders(std::string_view translated, const std::vector<Placeholder>& placeholders) {
    RestoreResult r;
    std::vector<int> seen(placeholders.size(), 0);
    bool need_space_after = false;
    std::size_t i = 0;
    while (i < translated.size()) {
        const std::size_t open = translated.find(kOpen, i);
        const std::size_t close = open == std::string_view::npos ? open : translated.find(kClose, open + kOpen.size());
        if (open == std::string_view::npos || close == std::string_view::npos) {
            std::string_view rest = translated.substr(i);
            if (need_space_after && !rest.empty() && alnum_byte(rest.front())) r.text.push_back(' ');
            r.text += rest;
            break;
        }
        std::string_view before = translated.substr(i, open - i);
        if (need_space_after && !before.empty() && alnum_byte(before.front())) r.text.push_back(' ');
        r.text += before;
        need_space_after = false;

        const std::string id = token_id(translated.substr(open + kOpen.size(), close - open - kOpen.size()));
        const Placeholder* ph = find_ph(placeholders, id);
        if (ph == nullptr) {
            r.unknown.push_back(std::string(translated.substr(open, close + kClose.size() - open)));
            r.text += translated.substr(open, close + kClose.size() - open);
        } else {
            ++seen[static_cast<std::size_t>(ph - placeholders.data())];
            if (ph->space_before && !r.text.empty() && alnum_byte(r.text.back())) r.text.push_back(' ');
            r.text += ph->original;
            need_space_after = ph->space_after;
        }
        i = close + kClose.size();
    }
    for (std::size_t k = 0; k < placeholders.size(); ++k) {
        if (seen[k] == 0) r.missing.push_back(placeholders[k].original);
        if (seen[k] > 1) r.duplicated.push_back(placeholders[k].original);
    }
    return r;
}

std::string PlaceholderStream::push(std::string_view piece) {
    pending_ += piece;
    std::string out;
    for (;;) {
        const std::size_t open = pending_.find(kOpen);
        if (open == std::string::npos) {
            out += pending_;
            pending_.clear();
            break;
        }
        const std::size_t close = pending_.find(kClose, open);
        if (close == std::string::npos) {
            out += pending_.substr(0, open);
            pending_.erase(0, open);
            if (pending_.size() > 24) {  // không phải placeholder: trả nguyên văn
                out += pending_;
                pending_.clear();
            }
            break;
        }
        out += pending_.substr(0, close + kClose.size());
        pending_.erase(0, close + kClose.size());
    }
    // Khôi phục phần đã trọn (giữ trạng thái dấu cách qua last_out_).
    if (out.empty()) return out;
    RestoreResult r = restore_placeholders(out, placeholders_);
    std::string text = std::move(r.text);
    if (!text.empty() && last_out_ != ' ' && last_out_ != '\0') {
        // Placeholder đầu đoạn mới cần dấu cách nếu câu gốc có và ký tự trước là chữ.
        const std::size_t open = out.find(kOpen);
        if (open == 0) {
            const std::size_t close = out.find(kClose);
            const Placeholder* ph = find_ph(placeholders_, token_id(out.substr(kOpen.size(), close - kOpen.size())));
            if (ph != nullptr && ph->space_before && alnum_byte(last_out_)) text.insert(text.begin(), ' ');
        }
    }
    if (space_after_pending_ && !text.empty() && alnum_byte(text.front())) text.insert(text.begin(), ' ');
    // Mảnh này kết thúc bằng placeholder -> nhớ để chèn dấu cách vào đầu mảnh sau.
    space_after_pending_ = false;
    if (out.size() >= kClose.size() && std::string_view(out).ends_with(kClose)) {
        const std::size_t open = out.rfind(kOpen);
        if (open != std::string::npos) {
            const Placeholder* ph = find_ph(placeholders_, token_id(std::string_view(out).substr(
                                                                open + kOpen.size(), out.size() - kClose.size() - open - kOpen.size())));
            space_after_pending_ = ph != nullptr && ph->space_after;
        }
    }
    if (!text.empty()) last_out_ = text.back();
    return text;
}

std::string PlaceholderStream::flush() {
    std::string out;
    out.swap(pending_);
    if (space_after_pending_ && !out.empty() && alnum_byte(out.front())) out.insert(out.begin(), ' ');
    space_after_pending_ = false;
    if (!out.empty()) last_out_ = out.back();
    return out;
}

namespace {

std::vector<std::string> numbers_in(std::string_view s) {
    std::vector<std::string> out;
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    for (std::size_t i = 0; i < s.size();) {
        if (!digit(s[i])) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < s.size() && (digit(s[j]) || ((s[j] == '.' || s[j] == ',') && j + 1 < s.size() && digit(s[j + 1])))) ++j;
        out.emplace_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

std::string unify_decimal(std::string_view s) {
    std::string out(s);
    std::replace(out.begin(), out.end(), ',', '.');
    return out;
}

}  // namespace

std::vector<std::string> missing_numbers(std::string_view source, std::string_view translation) {
    const std::string target = unify_decimal(translation);
    std::vector<std::string> out;
    for (const std::string& n : numbers_in(source)) {
        if (target.find(unify_decimal(n)) == std::string::npos &&
            std::find(out.begin(), out.end(), n) == out.end()) {
            out.push_back(n);
        }
    }
    return out;
}

}  // namespace pdfread::text
