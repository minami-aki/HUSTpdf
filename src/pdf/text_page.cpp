#include "pdf/text_page.hpp"

#include <format>

#include "pdf/context.hpp"
#include "pdf/mupdf.hpp"
#include "util/timer.hpp"

namespace pdfread::pdf {
namespace {

// Chuyển giữa tọa độ của PageLayout (gốc góc trên-trái khung trang) và tọa độ stext
// (không gian trang của MuPDF, khung có thể bắt đầu ở x0, y0 khác 0).
fz_point to_fz(const PageBox& box, PointF p) { return fz_make_point(p.x + box.x0, p.y + box.y0); }
PointF from_fz(const PageBox& box, fz_point p) { return {p.x - box.x0, p.y - box.y0}; }

fz_stext_page* stext(void* p) { return static_cast<fz_stext_page*>(p); }

// Điểm trong ký tự theo tỉ lệ t dọc chiều ngang (0 = cạnh đầu, 1 = cạnh cuối), giữa
// chiều cao ký tự.
fz_point inside_char(const fz_stext_char* ch, float t) {
    const fz_quad& q = ch->quad;
    const fz_point start{(q.ul.x + q.ll.x) / 2, (q.ul.y + q.ll.y) / 2};
    const fz_point end{(q.ur.x + q.lr.x) / 2, (q.ur.y + q.lr.y) / 2};
    return fz_make_point(start.x + (end.x - start.x) * t, start.y + (end.y - start.y) * t);
}

struct PageStats {
    int chars = 0;
    const fz_stext_char* first = nullptr;
    const fz_stext_char* last = nullptr;
};

PageStats scan(fz_stext_page* page) {
    PageStats s;
    for (fz_stext_block* b = page->first_block; b != nullptr; b = b->next) {
        if (b->type != FZ_STEXT_BLOCK_TEXT) continue;
        for (fz_stext_line* l = b->u.t.first_line; l != nullptr; l = l->next) {
            for (fz_stext_char* c = l->first_char; c != nullptr; c = c->next) {
                ++s.chars;
                if (s.first == nullptr) s.first = c;
                s.last = c;
            }
        }
    }
    return s;
}

}  // namespace

TextPagePtr TextPage::build(const DisplayList& list, const PageBox& box) {
    fz_context* c = require_ctx();
    ScopedTimer timer("stext");
    fz_stext_page* page = nullptr;
    std::string err;
    fz_var(page);
    fz_try(c) {
        fz_stext_options opts;
        fz_init_stext_options(c, &opts);
        opts.flags = FZ_STEXT_DEHYPHENATE | FZ_STEXT_MEDIABOX_CLIP;
        page = fz_new_stext_page_from_display_list(c, list.get(), &opts);
    }
    fz_catch(c) {
        err = fz_caught_message(c);
        fz_report_error(c);
        fz_drop_stext_page(c, page);
        page = nullptr;
    }
    if (page == nullptr) throw Error(std::format("Không trích được text của trang: {}", err));

    const PageStats stats = scan(page);
    PointF start{0, 0};
    PointF end{box.width, box.height};
    if (stats.first != nullptr) {
        // 1/4 và 3/4 bề ngang: MuPDF đặt biên vùng chọn ở cạnh ký tự gần điểm hơn.
        start = from_fz(box, inside_char(stats.first, 0.25f));
        end = from_fz(box, inside_char(stats.last, 0.75f));
    }
    return TextPagePtr(new TextPage(page, box, stats.chars, start, end));
}

TextPage::~TextPage() {
    if (page_ != nullptr) fz_drop_stext_page(thread_ctx(), stext(page_));
}

void TextPage::snap(PointF& a, PointF& b, SelectUnit unit) const {
    if (unit == SelectUnit::Chars) return;
    fz_context* c = require_ctx();
    fz_point fa = to_fz(box_, a);
    fz_point fb = to_fz(box_, b);
    const int mode = unit == SelectUnit::Words ? FZ_SELECT_WORDS : FZ_SELECT_LINES;
    bool ok = true;
    fz_try(c) { (void)fz_snap_selection(c, stext(page_), &fa, &fb, mode); }
    fz_catch(c) {
        fz_report_error(c);
        ok = false;
    }
    if (!ok) return;
    a = from_fz(box_, fa);
    b = from_fz(box_, fb);
}

std::vector<Quad> TextPage::highlight(PointF a, PointF b) const {
    fz_context* c = require_ctx();
    std::vector<fz_quad> buf(256);
    int n = 0;
    for (;;) {
        bool ok = true;
        fz_quad* data = buf.data();
        const int cap = static_cast<int>(buf.size());
        fz_try(c) { n = fz_highlight_selection(c, stext(page_), to_fz(box_, a), to_fz(box_, b), data, cap); }
        fz_catch(c) {
            fz_report_error(c);
            ok = false;
        }
        if (!ok) return {};
        if (n < cap) break;
        buf.resize(buf.size() * 4);  // có thể bị cắt bớt: thử lại với mảng lớn hơn
    }
    std::vector<Quad> out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const fz_quad& q = buf[static_cast<std::size_t>(i)];
        out.push_back({from_fz(box_, q.ul), from_fz(box_, q.ur), from_fz(box_, q.ll), from_fz(box_, q.lr)});
    }
    return out;
}

std::string TextPage::copy(PointF a, PointF b) const {
    fz_context* c = require_ctx();
    char* text = nullptr;
    fz_var(text);
    fz_try(c) { text = fz_copy_selection(c, stext(page_), to_fz(box_, a), to_fz(box_, b), 0); }
    fz_catch(c) {
        fz_report_error(c);
        text = nullptr;
    }
    if (text == nullptr) return {};
    std::string out = join_hyphenated_lines(text);
    fz_free(c, text);
    return out;
}

std::string join_hyphenated_lines(std::string_view text) {
    struct Hyphen {
        std::string_view bytes;
        bool soft;
    };
    static constexpr Hyphen kHyphens[] = {
        {"-", false},
        {"\xC2\xAD", true},       // U+00AD soft hyphen
        {"\xE2\x80\x90", false},  // U+2010 hyphen
        {"\xE2\x80\x91", false},  // U+2011 non-breaking hyphen
    };

    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const Hyphen* hit = nullptr;
        for (const Hyphen& h : kHyphens) {
            if (text.substr(i, h.bytes.size()) == h.bytes && i + h.bytes.size() < text.size() &&
                text[i + h.bytes.size()] == '\n') {
                hit = &h;
                break;
            }
        }
        // Gạch nối phải dính vào chữ phía trước (sau dấu cách thì là gạch ngang).
        if (hit != nullptr && i > 0 && text[i - 1] != ' ' && text[i - 1] != '\n') {
            const std::size_t after = i + hit->bytes.size() + 1;  // ký tự đầu dòng sau
            const char next = after < text.size() ? text[after] : '\0';
            if (next != '\0') {
                const bool lower = next >= 'a' && next <= 'z';
                if (!hit->soft && !lower) out.append(hit->bytes);
                i = after;
                continue;
            }
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

}  // namespace pdfread::pdf
