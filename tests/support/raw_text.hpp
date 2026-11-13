#pragma once
// Dựng khối text thô giả (như từ fz_stext_page) cho test tầng 2: mỗi ký tự rộng
// `char_w` point, dòng cao `line_h`, cách nhau `leading`.

#include <string>
#include <vector>

#include "text/types.hpp"
#include "util/utf8.hpp"

namespace pdfread::testing {

inline text::RawBlock make_block(float x, float y, const std::vector<std::string>& lines, float char_w = 6.0f,
                                 float line_h = 12.0f, float leading = 14.0f, std::vector<float> indents = {}) {
    text::RawBlock block;
    for (std::size_t li = 0; li < lines.size(); ++li) {
        text::RawLine line;
        const float indent = li < indents.size() ? indents[li] : 0.0f;
        const float top = y + static_cast<float>(li) * leading;
        float cx = x + indent;
        for (char32_t c : to_u32(lines[li])) {
            line.chars.push_back({c, {cx, top, cx + char_w, top + line_h}});
            cx += char_w;
        }
        line.box = {x + indent, top, cx, top + line_h};
        block.box.include(line.box);
        block.lines.push_back(std::move(line));
    }
    return block;
}

}  // namespace pdfread::testing
