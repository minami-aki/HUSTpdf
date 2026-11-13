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
    std::string text = page.utf8(plan.translated);
    std::replace(text.begin(), text.end(), '\n', ' ');
    if (utf8_length(text) > options.max_chars) {
        text = utf8_truncate(text, options.max_chars);
        plan.truncated = true;
    }
    if (text.empty()) return std::nullopt;
    plan.request.text = std::move(text);

    if (options.include_context) {
        plan.request.context = surrounding_context(page, plan.translated, options.context_sentences_before,
                                                   options.context_sentences_after, options.max_context_chars);
    }
    return plan;
}

}  // namespace pdfread::context
