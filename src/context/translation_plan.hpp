#pragma once
// Tầng 3: từ vùng bôi đen (trên một trang) lập kế hoạch dịch.
//
//  - Term     : một từ / cụm ngắn (<= 4 từ, không có dấu kết câu) -> dịch đúng phần chọn
//               (M5: tra glossary trước).
//  - Partial  : một phần của câu -> mặc định mở rộng ra trọn câu (tùy chọn chỉ dịch phần chọn).
//  - Sentences: trọn một hay nhiều câu (cho phép hụt vài ký tự ở hai đầu).
//
// Ngữ cảnh (1–2 câu trước/sau trong cùng đoạn) chỉ thêm khi bật include_context: với
// HY-MT1.5-1.8B Q4, mẫu dịch có ngữ cảnh làm hỏng khoảng một nửa số câu thử (dịch luôn
// câu ngữ cảnh hoặc bỏ mất câu cần dịch) — xem docs/DECISIONS.md.

#include <cstddef>
#include <optional>
#include <string>

#include <vector>

#include "terminology/glossary.hpp"
#include "text/page_text.hpp"
#include "text/protect.hpp"
#include "translate/translator.hpp"

namespace pdfread::context {

enum class SelectionKind { Term, Partial, Sentences };

struct PlanOptions {
    bool expand_to_sentence = true;      // nửa câu -> dịch cả câu
    bool include_context = false;        // thêm câu xung quanh vào prompt
    int context_sentences_before = 1;
    int context_sentences_after = 1;
    std::size_t max_context_chars = 600;  // ~150 token
    std::size_t max_chars = 2000;         // độ dài tối đa của đoạn cần dịch
    std::size_t max_term_words = 4;
    bool protect = true;                  // thay công thức/mã/đơn vị bằng placeholder
    const terminology::GlossaryIndex* glossary = nullptr;
    std::vector<translate::DomainScore> domains;
};

// Kết quả tra glossary cho vùng chọn là một từ/cụm ngắn (không cần gọi mô hình).
struct GlossaryAnswer {
    terminology::GlossaryEntry entry;
    std::vector<terminology::GlossaryEntry> alternatives;  // nghĩa khác ở lĩnh vực khác
};

struct TranslationPlan {
    SelectionKind kind = SelectionKind::Sentences;
    translate::TranslateRequest request;  // request.text đã thay placeholder
    text::Span selected;    // phần người dùng bôi đen (chỉ số ký tự trong PageText)
    text::Span translated;  // phần thực sự đem dịch (>= selected khi đã mở rộng)
    bool expanded = false;  // translated rộng hơn selected
    bool truncated = false; // cắt bớt do quá max_chars
    std::string source;     // text gốc đem dịch (chưa thay placeholder)
    std::vector<text::Placeholder> placeholders;
    std::vector<terminology::GlossaryEntry> terms;  // thuật ngữ đưa vào prompt (để hậu kiểm)
    std::optional<GlossaryAnswer> glossary_answer;  // chỉ với SelectionKind::Term
};

// Phân loại vùng chọn.
[[nodiscard]] SelectionKind classify(const text::PageText& page, text::Span selected, std::size_t max_term_words = 4);

// nullopt nếu vùng chọn rỗng hoặc không có chữ.
[[nodiscard]] std::optional<TranslationPlan> make_plan(const text::PageText& page, text::Span selected,
                                                       const PlanOptions& options = {});

// Ngữ cảnh: tối đa `before` câu trước và `after` câu sau `span` trong cùng đoạn, không
// vượt `max_chars` (bỏ câu xa hơn trước).
[[nodiscard]] std::string surrounding_context(const text::PageText& page, text::Span span, int before, int after,
                                              std::size_t max_chars);

}  // namespace pdfread::context
