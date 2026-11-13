#include "context/translation_plan.hpp"

#include <algorithm>

#include "text/normalize.hpp"
#include "util/utf8.hpp"

namespace pdfread::context {
namespace {

using text::PageText;
using text::Span;

// Bỏ khoảng trắng / dấu xuống dòng hai đầu.
Span trim(const PageText& page, Span s) {
    const std::u32string& t = page.text();
    s.end = std::min(s.end, t.size());
    while (s.begin < s.end && (text::is_space(t[s.begin]) || t[s.begin] == U'\n')) ++s.begin;
    while (s.end > s.begin && (text::is_space(t[s.end - 1]) || t[s.end - 1] == U'\n')) --s.end;
    return s;
}

// Mở rộng hai đầu vùng chọn ra trọn từ (chữ, số, gạch nối, nháy đơn).
Span whole_words(const PageText& page, Span s) {
    const std::u32string& t = page.text();
    auto word_char = [](char32_t c) { return text::is_letter(c) || text::is_digit(c) || c == U'-' || c == U'\''; };
    while (s.begin > 0 && s.begin < t.size() && word_char(t[s.begin - 1]) && word_char(t[s.begin])) --s.begin;
    while (s.end > 0 && s.end < t.size() && word_char(t[s.end - 1]) && word_char(t[s.end])) ++s.end;
    return s;
}

std::size_t count_words(const PageText& page, Span s) {
    const std::u32string& t = page.text();
    std::size_t words = 0;
    bool in_word = false;
    for (std::size_t i = s.begin; i < s.end; ++i) {
        const bool space = text::is_space(t[i]) || t[i] == U'\n';
        if (!space && !in_word) ++words;
        in_word = !space;
    }
    return words;
}

bool has_terminal(const PageText& page, Span s) {
    const std::u32string& t = page.text();
    for (std::size_t i = s.begin; i < s.end; ++i) {
        if ((t[i] == U'.' || t[i] == U'!' || t[i] == U'?') && i + 1 < s.end && text::is_space(t[i + 1])) return true;
    }
    return false;
}

constexpr std::size_t kMinEdgeOverlap = 4;  // ký tự
constexpr std::size_t kMaxLookupWords = 12;  // vùng chọn dài hơn không tra glossary như một thuật ngữ

std::size_t overlap_len(Span a, Span b) {
    const std::size_t lo = std::max(a.begin, b.begin);
    const std::size_t hi = std::min(a.end, b.end);
    return hi > lo ? hi - lo : 0;
}

// Các câu giao với `s`: [first, last], hoặc {-1,-1}. Câu ở hai đầu chỉ bị dính vài ký tự
// (kéo chuột lố sang "The" của câu sau) thì bỏ qua.
std::pair<int, int> sentences_over(const PageText& page, Span s) {
    const auto& sents = page.sentences();
    int first = -1;
    int last = -1;
    for (std::size_t i = 0; i < sents.size(); ++i) {
        if (sents[i].span.overlaps(s)) {
            if (first < 0) first = static_cast<int>(i);
            last = static_cast<int>(i);
        }
    }
    auto marginal = [&](int idx) {
        const Span sp = sents[static_cast<std::size_t>(idx)].span;
        return overlap_len(sp, s) < std::min(kMinEdgeOverlap, sp.size());
    };
    while (first >= 0 && first < last && marginal(first)) ++first;
    while (last > first && marginal(last)) --last;
    return {first, last};
}

// Phần chọn "phủ trọn" câu nếu chỉ hụt tối đa vài ký tự (do bôi đen không chính xác).
bool covers(Span selected, Span sentence) {
    const std::size_t slack = std::max<std::size_t>(3, sentence.size() / 20);
    return selected.begin <= sentence.begin + slack && selected.end + slack >= sentence.end;
}

}  // namespace

SelectionKind classify(const PageText& page, Span selected, std::size_t max_term_words) {
    selected = trim(page, selected);
    const auto [first, last] = sentences_over(page, selected);
    if (first < 0) return SelectionKind::Term;
    const auto& sents = page.sentences();
    if (first == last) {
        if (covers(selected, sents[static_cast<std::size_t>(first)].span)) return SelectionKind::Sentences;
        if (count_words(page, selected) <= max_term_words && !has_terminal(page, selected)) return SelectionKind::Term;
        return SelectionKind::Partial;
    }
    const bool full = covers(selected, {sents[static_cast<std::size_t>(first)].span.begin,
                                        sents[static_cast<std::size_t>(last)].span.end});
    return full ? SelectionKind::Sentences : SelectionKind::Partial;
}

std::string surrounding_context(const PageText& page, Span span, int before, int after, std::size_t max_chars) {
    const auto& sents = page.sentences();
    const auto [first, last] = sentences_over(page, span);
    if (first < 0) return {};
    const std::size_t para = sents[static_cast<std::size_t>(first)].paragraph;

    std::vector<int> picked;
    std::size_t chars = 0;
    // Xen kẽ câu trước / câu sau, gần trước xa sau, tới khi đủ số câu hoặc chạm giới hạn ký tự.
    for (int d = 1; d <= std::max(before, after); ++d) {
        for (int idx : {first - d, last + d}) {
            const bool is_before = idx < first;
            if ((is_before && d > before) || (!is_before && d > after)) continue;
            if (idx < 0 || idx >= static_cast<int>(sents.size())) continue;
            const text::Sentence& s = sents[static_cast<std::size_t>(idx)];
            if (s.paragraph != para) continue;
            if (chars + s.span.size() > max_chars) continue;
            chars += s.span.size();
            picked.push_back(idx);
        }
    }
    std::sort(picked.begin(), picked.end());
    std::string out;
    for (int idx : picked) {
        if (!out.empty()) out.push_back(' ');
        out += page.utf8(sents[static_cast<std::size_t>(idx)].span);
    }
    return out;
}

std::optional<TranslationPlan> make_plan(const PageText& page, Span selected, const PlanOptions& options) {
    selected = trim(page, selected);
    if (selected.empty()) return std::nullopt;

    TranslationPlan plan;
    plan.selected = selected;
    plan.kind = classify(page, selected, options.max_term_words);
    plan.translated = selected;
    // Tra glossary/từ điển: vùng chọn mở rộng ra trọn từ (kéo chuột hụt vài chữ cái: "mergency core
    // cooling syste"). Trùng khớp đúng một mục — kể cả thuật ngữ dài hơn max_term_words như
    // "emergency core cooling system" — thì coi là tra một thuật ngữ.
    if (options.glossary != nullptr && count_words(page, selected) <= kMaxLookupWords) {
        const Span ws = whole_words(page, selected);
        const std::u32string sel(std::u32string_view(page.text()).substr(ws.begin, ws.size()));
        if (auto m = options.glossary->lookup(sel); m && m->entry->confirmed()) {
            plan.kind = SelectionKind::Term;
            selected = plan.selected = plan.translated = ws;
        }
    }

    const auto [first, last] = sentences_over(page, selected);
    if (first >= 0 && options.expand_to_sentence && plan.kind != SelectionKind::Term) {
        // Mở rộng ra trọn các câu liên quan (phần lố sang câu bên cạnh bị bỏ).
        const auto& sents = page.sentences();
        plan.translated = {sents[static_cast<std::size_t>(first)].span.begin,
                           sents[static_cast<std::size_t>(last)].span.end};
    }
    plan.translated = trim(page, plan.translated);
    plan.expanded = plan.translated != selected;

    // Text gửi đi: thay '\n' (ranh giới đoạn) bằng dấu cách; cắt bớt nếu quá dài.
    std::u32string u(std::u32string_view(page.text()).substr(plan.translated.begin, plan.translated.size()));
    std::replace(u.begin(), u.end(), U'\n', U' ');
    if (u.size() > options.max_chars) {
        u.resize(options.max_chars);
        plan.truncated = true;
    }
    if (u.empty()) return std::nullopt;
    plan.source = to_utf8(u);

    // Vùng không dịch (công thức, mã, đơn vị…) -> placeholder.
    const std::vector<text::ProtectedSpan> protected_spans =
        options.protect ? text::find_protected(u, {.quantities = options.protect_quantities})
                        : std::vector<text::ProtectedSpan>{};

    // Thuật ngữ glossary thực sự có trong text (ngoài vùng không dịch). Chỉ thuật ngữ đã xác
    // nhận và thuộc người dùng / tài liệu / lĩnh vực của tài liệu mới vào prompt.
    if (options.glossary != nullptr) {
        std::vector<text::Span> excluded;
        for (const auto& p : protected_spans) excluded.push_back(p.span);
        for (const terminology::TermMatch& m : options.glossary->find_in(u, excluded)) {
            const terminology::GlossaryEntry& e = *m.entry;
            if (!e.confirmed() || e.layer == terminology::Layer::Unrelated) continue;
            const bool dup = std::any_of(plan.terms.begin(), plan.terms.end(),
                                         [&](const auto& t) { return t.term_en == e.term_en; });
            if (dup) continue;
            plan.terms.push_back(e);
            plan.request.terms.push_back({e.term_en, e.term_vi, e.keep_english});
        }
    }
    plan.request.domains = options.domains;

    text::Placeholdered ph = text::apply_placeholders(u, protected_spans);
    plan.request.text = std::move(ph.text);
    plan.placeholders = std::move(ph.placeholders);

    // Từ / cụm ngắn: tra glossary trước (kể cả lĩnh vực ngoài tài liệu, để hiện nghĩa khác).
    if (plan.kind == SelectionKind::Term && options.glossary != nullptr) {
        const std::u32string sel(std::u32string_view(page.text()).substr(selected.begin, selected.size()));
        if (auto m = options.glossary->lookup(sel); m && m->entry->confirmed()) {
            GlossaryAnswer a;
            a.entry = *m->entry;
            for (const auto* alt : m->alternatives) a.alternatives.push_back(*alt);
            plan.glossary_answer = std::move(a);
        }
    }

    if (options.include_context) {
        plan.request.context = surrounding_context(page, plan.translated, options.context_sentences_before,
                                                   options.context_sentences_after, options.max_context_chars);
    }
    return plan;
}

}  // namespace pdfread::context
