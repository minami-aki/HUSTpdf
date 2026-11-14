#include "support/sample_pdf.hpp"

#include <format>
#include <fstream>
#include <stdexcept>

namespace pdfread::testing {
namespace {

std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '(' || c == ')' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

// Văn bản mẫu (tự soạn) mang nhiều trường hợp khó cho tách câu ở M4.
constexpr std::string_view kParagraphs[] = {
    "A proportional-integral-derivative (PID) controller computes a control signal from the error between "
    "a desired setpoint and a measured process variable. The derivative term reacts to the rate of change of "
    "the error, e.g. when a sudden disturbance acts on the plant, while the integral term removes the "
    "steady-state offset. Fig. 3 shows the closed-loop step response for Kp = 2.5, Ki = 0.8 and Kd = 0.1.",
    "The transfer function of the plant is G(s) = 1 / (s^2 + 3s + 2), see Eq. (2). Discretization with a "
    "sampling period of 1.5 ms preserves stability for the gains listed in Table 1, i.e. all closed-loop "
    "poles remain inside the unit circle. Similar results were reported by Nguyen et al. for a DC motor "
    "drive with an encoder resolution of 4096 counts per revolution.",
    "In a pipelined processor, the instruction fetch, decode, execute, memory access and write-back stages "
    "operate concurrently on different instructions. Data hazards are resolved by forwarding, and the branch "
    "predictor reduces the penalty of control hazards. The firmware version v1.2 exposes the cache "
    "configuration through a memory-mapped register at address 0x4000 1000.",
    "Thermal stress in the composite layer depends on the mismatch of the coefficients of thermal expansion. "
    "For a temperature rise of 80 K the computed von Mises stress reaches 145 MPa, which is below the yield "
    "strength of the aluminium alloy (approx. 270 MPa). Vs. the uncoated sample, fatigue life increased by "
    "a factor of 2.3.",
};

}  // namespace

std::string pdf_text(double x, double y, int font, double size, std::string_view text) {
    return std::format("BT /F{} {:.2f} Tf {:.2f} {:.2f} Td ({}) Tj ET\n", font, size, x, y, escape(text));
}

std::string pdf_paragraph(double x, double& y, double width, int font, double size, double leading,
                          std::string_view text) {
    // Ước lượng độ rộng ký tự trung bình ~0.5 em (đủ cho tài liệu mẫu).
    const std::size_t max_chars = static_cast<std::size_t>(width / (size * 0.5));
    std::string out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = std::min(text.size(), pos + max_chars);
        if (end < text.size()) {
            const std::size_t space = text.rfind(' ', end);
            if (space != std::string_view::npos && space > pos) end = space;
        }
        out += pdf_text(x, y, font, size, text.substr(pos, end - pos));
        y -= leading;
        pos = end;
        while (pos < text.size() && text[pos] == ' ') ++pos;
    }
    return out;
}

std::string pdf_rect(double x, double y, double w, double h, double gray) {
    return std::format("q {:.3f} g {:.2f} {:.2f} {:.2f} {:.2f} re f Q\n", gray, x, y, w, h);
}

std::string pdf_stroke_rect(double x, double y, double w, double h, double line_width) {
    return std::format("q {:.2f} w {:.2f} {:.2f} {:.2f} {:.2f} re S Q\n", line_width, x, y, w, h);
}

std::string build_pdf(const std::vector<SamplePage>& pages, const std::vector<SampleOutline>& outline) {
    std::string out = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;
    auto add_object = [&](const std::string& body) {
        offsets.push_back(out.size());
        out += std::format("{} 0 obj\n{}\nendobj\n", offsets.size(), body);
    };

    const int n = static_cast<int>(pages.size());
    const int first_page_obj = 7;  // 1 catalog, 2 pages, 3-6 font
    std::string kids;
    for (int i = 0; i < n; ++i) kids += std::format("{} 0 R ", first_page_obj + 2 * i);
    auto page_ref = [&](int i) { return first_page_obj + 2 * i; };

    // Số hiệu đối tượng: liên kết sau các trang, mục lục sau liên kết.
    int next_obj = first_page_obj + 2 * n;
    std::vector<std::vector<int>> link_objs(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < pages[static_cast<std::size_t>(i)].links.size(); ++k) {
            link_objs[static_cast<std::size_t>(i)].push_back(next_obj++);
        }
    }
    const int outline_root = outline.empty() ? 0 : next_obj++;
    std::vector<int> item_obj;
    for (std::size_t k = 0; k < outline.size(); ++k) item_obj.push_back(next_obj++);

    add_object(outline_root != 0 ? std::format("<< /Type /Catalog /Pages 2 0 R /Outlines {} 0 R >>", outline_root)
                                 : std::string("<< /Type /Catalog /Pages 2 0 R >>"));
    add_object(std::format("<< /Type /Pages /Kids [{}] /Count {} >>", kids, n));
    for (const char* base : {"Helvetica", "Times-Roman", "Helvetica-Bold", "Courier"}) {
        add_object(std::format("<< /Type /Font /Subtype /Type1 /BaseFont /{} /Encoding /WinAnsiEncoding >>", base));
    }
    for (int i = 0; i < n; ++i) {
        const SamplePage& p = pages[static_cast<std::size_t>(i)];
        std::string annots;
        for (int obj : link_objs[static_cast<std::size_t>(i)]) annots += std::format("{} 0 R ", obj);
        add_object(std::format(
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {:.2f} {:.2f}] /Rotate {} "
            "/Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R /F4 6 0 R >> >> /Contents {} 0 R{} >>",
            p.width, p.height, p.rotate, first_page_obj + 2 * i + 1,
            annots.empty() ? std::string() : std::format(" /Annots [{}]", annots)));
        add_object(std::format("<< /Length {} >>\nstream\n{}\nendstream", p.content.size(), p.content));
    }
    for (int i = 0; i < n; ++i) {
        for (const SampleLink& l : pages[static_cast<std::size_t>(i)].links) {
            const std::string action =
                l.dest_page >= 0 ? std::format("/Dest [{} 0 R /XYZ 0 {:.2f} 0]", page_ref(l.dest_page), l.dest_y)
                                 : std::format("/A << /S /URI /URI ({}) >>", l.uri);
            add_object(std::format("<< /Type /Annot /Subtype /Link /Rect [{:.2f} {:.2f} {:.2f} {:.2f}] /Border [0 0 0] {} >>",
                                   l.x0, l.y0, l.x1, l.y1, action));
        }
    }
    if (outline_root != 0) {
        // Cha của từng mục; anh em kề trước/sau; con đầu/cuối.
        const std::size_t m = outline.size();
        std::vector<int> parent(m, -1);
        std::vector<int> prev(m, -1);
        std::vector<int> next(m, -1);
        std::vector<int> first(m, -1);
        std::vector<int> last(m, -1);
        std::vector<int> stack;  // chỉ số mục theo level
        std::vector<int> last_at_level(m + 1, -1);
        int root_first = -1;
        int root_last = -1;
        for (std::size_t k = 0; k < m; ++k) {
            const int lv = outline[k].level;
            const int par = lv == 0 ? -1 : last_at_level[static_cast<std::size_t>(lv - 1)];
            parent[k] = par;
            const int sib = last_at_level[static_cast<std::size_t>(lv)];
            const bool same_parent = sib >= 0 && parent[static_cast<std::size_t>(sib)] == par;
            if (same_parent) {
                prev[k] = sib;
                next[static_cast<std::size_t>(sib)] = static_cast<int>(k);
            }
            if (par >= 0) {
                if (first[static_cast<std::size_t>(par)] < 0) first[static_cast<std::size_t>(par)] = static_cast<int>(k);
                last[static_cast<std::size_t>(par)] = static_cast<int>(k);
            } else {
                if (root_first < 0) root_first = static_cast<int>(k);
                root_last = static_cast<int>(k);
            }
            last_at_level[static_cast<std::size_t>(lv)] = static_cast<int>(k);
            for (std::size_t d = static_cast<std::size_t>(lv) + 1; d < last_at_level.size(); ++d) last_at_level[d] = -1;
        }
        auto ref = [&](int k) { return item_obj[static_cast<std::size_t>(k)]; };
        add_object(std::format("<< /Type /Outlines /First {} 0 R /Last {} 0 R /Count {} >>", ref(root_first),
                               ref(root_last), m));
        for (std::size_t k = 0; k < m; ++k) {
            const SampleOutline& o = outline[k];
            std::string body = std::format("<< /Title ({}) /Parent {} 0 R /Dest [{} 0 R /XYZ 0 {:.2f} 0]", o.title,
                                           parent[k] >= 0 ? ref(parent[k]) : outline_root, page_ref(o.page), o.y);
            if (prev[k] >= 0) body += std::format(" /Prev {} 0 R", ref(prev[k]));
            if (next[k] >= 0) body += std::format(" /Next {} 0 R", ref(next[k]));
            if (first[k] >= 0) body += std::format(" /First {} 0 R /Last {} 0 R /Count 1", ref(first[k]), ref(last[k]));
            add_object(body + " >>");
        }
    }

    const std::size_t xref = out.size();
    out += std::format("xref\n0 {}\n0000000000 65535 f \n", offsets.size() + 1);
    for (std::size_t off : offsets) out += std::format("{:010} 00000 n \n", off);
    out += std::format("trailer\n<< /Size {} /Root 1 0 R >>\nstartxref\n{}\n%%EOF\n", offsets.size() + 1, xref);
    return out;
}

void write_pdf(const std::filesystem::path& path, const std::vector<SamplePage>& pages,
               const std::vector<SampleOutline>& outline) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path.string());
    const std::string data = build_pdf(pages, outline);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::vector<SamplePage> technical_sample(int extra_pages) {
    std::vector<SamplePage> pages;

    {  // Trang 1: tiêu đề + tóm tắt, một cột
        SamplePage p;
        double y = 770;
        p.content += pdf_text(72, y, 3, 20, "Closed-Loop Control of a DC Motor Drive");
        y -= 28;
        p.content += pdf_text(72, y, 1, 11, "Sample document generated for Pdfread testing");
        y -= 36;
        p.content += pdf_text(72, y, 3, 12, "Abstract");
        y -= 18;
        for (std::string_view para : kParagraphs) {
            p.content += pdf_paragraph(72, y, 451, 2, 11, 14, para);
            y -= 10;
        }
        p.content += pdf_stroke_rect(72, 120, 451, 160, 0.8);
        p.content += pdf_text(250, 195, 1, 10, "Fig. 3  Step response");
        pages.push_back(std::move(p));
    }
    {  // Trang 2: hai cột
        SamplePage p;
        for (int col = 0; col < 2; ++col) {
            double y = 780;
            const double x = col == 0 ? 56 : 310;
            p.content += pdf_text(x, y, 3, 12, col == 0 ? "1. Introduction" : "2. System Model");
            y -= 18;
            for (int rep = 0; rep < 3; ++rep) {
                for (std::string_view para : kParagraphs) {
                    p.content += pdf_paragraph(x, y, 229, 2, 9.5, 11.5, para);
                    y -= 6;
                    if (y < 80) break;
                }
                if (y < 80) break;
            }
        }
        pages.push_back(std::move(p));
    }
    {  // Trang 3: khổ ngang, bảng
        SamplePage p{.width = 842, .height = 595, .rotate = 0, .content = {}};
        p.content += pdf_text(72, 530, 3, 14, "Table 1  Controller gains");
        const char* rows[][4] = {{"Case", "Kp", "Ki", "Kd"}, {"A", "2.5", "0.8", "0.10"},
                                 {"B", "3.0", "1.2", "0.05"}, {"C", "1.8", "0.4", "0.20"}};
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                p.content += pdf_text(72 + c * 120, 490 - r * 24, r == 0 ? 3 : 4, 11, rows[r][c]);
            }
        }
        p.content += pdf_rect(72, 300, 698, 2, 0.0);
        pages.push_back(std::move(p));
    }
    {  // Trang 4: /Rotate 90
        SamplePage p{.width = 595, .height = 842, .rotate = 90, .content = {}};
        p.content += pdf_text(72, 770, 3, 14, "Rotated page (/Rotate 90)");
        double y = 740;
        p.content += pdf_paragraph(72, y, 451, 2, 11, 14, kParagraphs[0]);
        pages.push_back(std::move(p));
    }
    for (int i = 0; i < extra_pages; ++i) {
        SamplePage p;
        double y = 780;
        p.content += pdf_text(72, y, 3, 13, std::format("{}. Section {}", i + 3, i + 3));
        y -= 20;
        while (y > 90) {
            for (std::string_view para : kParagraphs) {
                p.content += pdf_paragraph(72, y, 451, 2, 11, 14, para);
                y -= 8;
                if (y < 90) break;
            }
        }
        p.content += pdf_text(290, 40, 1, 9, std::format("{}", i + 5));
        pages.push_back(std::move(p));
    }
    return pages;
}

}  // namespace pdfread::testing
