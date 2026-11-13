#include "text/page_text.hpp"

#include <algorithm>
#include <cmath>

#include "text/normalize.hpp"
#include "text/reading_order.hpp"
#include "text/sentence_splitter.hpp"
#include "util/utf8.hpp"

namespace pdfread::text {
namespace {

// Đoạn văn trong quá trình dựng: các dòng (trỏ vào RawBlock gốc).
struct ParaLines {
    std::vector<const RawLine*> lines;
};

bool ends_sentence(char32_t c) {
    return c == U'.' || c == U'!' || c == U'?' || c == U':' || c == 0x2026;
}

char32_t last_visible(const RawLine& l) {
    for (auto it = l.chars.rbegin(); it != l.chars.rend(); ++it) {
        if (!is_space(it->c)) return it->c;
    }
    return 0;
}

char32_t first_visible(const RawLine& l) {
    for (const RawChar& c : l.chars) {
        if (!is_space(c.c)) return c.c;
    }
    return 0;
}

float median(std::vector<float> v) {
    if (v.empty()) return 0;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

// Chia các dòng của một khối thành đoạn: khoảng cách dọc lớn bất thường, hoặc dòng trước
// kết thúc câu và ngắn hẳn so với khối (dòng cuối đoạn), hoặc dòng sau thụt đầu dòng.
void split_block(const RawBlock& block, std::vector<ParaLines>& out) {
    std::vector<const RawLine*> lines;
    for (const RawLine& l : block.lines) {
        if (first_visible(l) != 0) lines.push_back(&l);
    }
    if (lines.empty()) return;

    std::vector<float> heights;
    for (const RawLine* l : lines) heights.push_back(l->box.height());
    const float h = std::max(1.0f, median(heights));
    float block_x0 = lines[0]->box.x0;
    float block_x1 = lines[0]->box.x1;
    for (const RawLine* l : lines) {
        block_x0 = std::min(block_x0, l->box.x0);
        block_x1 = std::max(block_x1, l->box.x1);
    }
    const float width = std::max(1.0f, block_x1 - block_x0);

    out.emplace_back();
    out.back().lines.push_back(lines[0]);
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const RawLine& prev = *lines[i - 1];
        const RawLine& cur = *lines[i];
        const float gap = cur.box.y0 - prev.box.y1;
        const bool prev_ends = ends_sentence(last_visible(prev));
        const bool prev_short = prev.box.x1 < block_x1 - 0.15f * width;
        const bool indented = cur.box.x0 > block_x0 + 0.8f * h && cur.box.x0 > prev.box.x0 + 0.8f * h;
        const bool new_para = gap > 0.6f * h || (prev_ends && prev_short) || (prev_ends && indented);
        if (new_para) out.emplace_back();
        out.back().lines.push_back(&cur);
    }
}

void append_char(std::u32string& text, std::vector<Box>& boxes, std::vector<int>& lines, char32_t c, const Box& box,
                 int line) {
    // Gộp khoảng trắng liên tiếp.
    if (is_space(c) && (text.empty() || text.back() == U' ' || text.back() == U'\n')) return;
    const std::size_t n = append_normalized(text, c);
    for (std::size_t k = 0; k < n; ++k) {
        boxes.push_back(box);
        lines.push_back(line);
    }
}

}  // namespace

std::string PageText::utf8(Span span) const {
    span.end = std::min(span.end, text_.size());
    if (span.empty()) return {};
    return to_utf8(std::u32string_view(text_).substr(span.begin, span.size()));
}

int PageText::sentence_at(std::size_t i) const {
    if (sentences_.empty()) return -1;
    // Câu đầu tiên có end > i.
    auto it = std::upper_bound(sentences_.begin(), sentences_.end(), i,
                               [](std::size_t v, const Sentence& s) { return v < s.span.end; });
    if (it == sentences_.end()) return static_cast<int>(sentences_.size()) - 1;
    return static_cast<int>(it - sentences_.begin());
}

Span PageText::span_where(const std::function<bool(const Box&)>& pred) const {
    Span s{text_.size(), 0};
    for (std::size_t i = 0; i < text_.size(); ++i) {
        if (lines_[i] < 0 || is_space(text_[i])) continue;
        if (pred(boxes_[i])) {
            s.begin = std::min(s.begin, i);
            s.end = std::max(s.end, i + 1);
        }
    }
    return s.empty() ? Span{} : s;
}

std::vector<Box> PageText::line_rects(Span span) const {
    std::vector<Box> out;
    int current = -2;
    span.end = std::min(span.end, text_.size());
    for (std::size_t i = span.begin; i < span.end; ++i) {
        if (lines_[i] < 0 || !boxes_[i].valid()) continue;
        if (lines_[i] != current) {
            out.push_back(boxes_[i]);
            current = lines_[i];
        } else {
            out.back().include(boxes_[i]);
        }
    }
    return out;
}

PageText build_page_text(const std::vector<RawBlock>& blocks) {
    PageText pt;

    // 1. Thứ tự đọc.
    std::vector<Box> block_boxes;
    block_boxes.reserve(blocks.size());
    for (const RawBlock& b : blocks) block_boxes.push_back(b.box);
    const std::vector<std::size_t> order = reading_order(block_boxes);

    // 2. Đoạn trong từng khối.
    std::vector<ParaLines> paras;
    for (std::size_t bi : order) split_block(blocks[bi], paras);

    // 3. Nối đoạn bị ngắt giữa câu qua khối/cột: đoạn trước không kết thúc câu và đoạn
    //    sau bắt đầu bằng chữ thường.
    std::vector<ParaLines> merged;
    for (ParaLines& p : paras) {
        if (!merged.empty()) {
            const RawLine& last = *merged.back().lines.back();
            const RawLine& first = *p.lines.front();
            if (!ends_sentence(last_visible(last)) && is_lower(first_visible(first))) {
                merged.back().lines.insert(merged.back().lines.end(), p.lines.begin(), p.lines.end());
                continue;
            }
        }
        merged.push_back(std::move(p));
    }

    // 4. Ghép text: làm sạch ký tự, nối dòng (bỏ gạch nối cuối dòng), tách câu.
    std::u32string& text = pt.text_;
    int line_id = 0;
    for (const ParaLines& p : merged) {
        if (!text.empty()) {
            text.push_back(U'\n');
            pt.boxes_.push_back({});
            pt.lines_.push_back(-1);
        }
        const std::size_t para_begin = text.size();
        for (std::size_t li = 0; li < p.lines.size(); ++li) {
            const RawLine& line = *p.lines[li];
            if (li > 0) {
                // Nối với dòng trước.
                const char32_t next = first_visible(line);
                while (!text.empty() && text.back() == U' ') {
                    text.pop_back();
                    pt.boxes_.pop_back();
                    pt.lines_.pop_back();
                }
                const char32_t prev = text.empty() ? 0 : text.back();
                const bool word_before =
                    text.size() >= 2 && !is_space(text[text.size() - 2]) && text[text.size() - 2] != U'\n';
                if (last_visible(*p.lines[li - 1]) == 0xAD) {
                    // Soft hyphen cuối dòng (đã bị làm sạch bỏ đi): nối liền, không dấu cách.
                } else if (is_hyphen(prev) && word_before) {
                    // Gạch nối cuối dòng: dòng sau chữ thường -> từ bị ngắt, bỏ gạch nối;
                    // chữ hoa/số -> giữ gạch nối (Runge-Kutta, ISO-9001). Soft hyphen luôn bỏ.
                    if (prev == 0xAD || is_lower(next)) {
                        text.pop_back();
                        pt.boxes_.pop_back();
                        pt.lines_.pop_back();
                    }
                } else {
                    text.push_back(U' ');
                    pt.boxes_.push_back({});
                    pt.lines_.push_back(-1);
                }
            }
            for (const RawChar& c : line.chars) {
                append_char(text, pt.boxes_, pt.lines_, c.c, c.box, line_id);
            }
            ++line_id;
        }
        while (text.size() > para_begin && text.back() == U' ') {
            text.pop_back();
            pt.boxes_.pop_back();
            pt.lines_.pop_back();
        }
        if (text.size() == para_begin) continue;

        Paragraph para;
        para.span = {para_begin, text.size()};
        para.first_sentence = pt.sentences_.size();
        for (const Span& s :
             split_sentences(std::u32string_view(text).substr(para_begin, text.size() - para_begin), para_begin)) {
            pt.sentences_.push_back({s, pt.paragraphs_.size()});
        }
        para.sentence_count = pt.sentences_.size() - para.first_sentence;
        pt.paragraphs_.push_back(para);
    }
    // Bỏ '\n' thừa ở cuối nếu đoạn cuối rỗng.
    while (!text.empty() && text.back() == U'\n') {
        text.pop_back();
        pt.boxes_.pop_back();
        pt.lines_.pop_back();
    }
    return pt;
}

}  // namespace pdfread::text
