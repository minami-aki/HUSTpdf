// Tạo file PDF mẫu để chạy thử viewer:
//   make_sample_pdf <out.pdf> [số trang thêm]          tài liệu kỹ thuật mẫu nhiều trang
//   make_sample_pdf <out.pdf> --text <file.txt>        dàn trang một file văn bản ASCII
//     (dòng đầu = tiêu đề, các đoạn cách nhau bằng dòng trống)

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "support/sample_pdf.hpp"

namespace {

using pdfread::testing::SamplePage;

std::vector<SamplePage> pages_from_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path);
    std::vector<std::string> paragraphs;
    std::string line;
    std::string cur;
    std::string title;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (title.empty() && !line.empty()) {
            title = line;
            continue;
        }
        if (line.empty()) {
            if (!cur.empty()) paragraphs.push_back(cur);
            cur.clear();
        } else {
            cur += (cur.empty() ? "" : " ") + line;
        }
    }
    if (!cur.empty()) paragraphs.push_back(cur);

    std::vector<SamplePage> pages(1);
    double y = 842 - 72;
    pages.back().content += pdfread::testing::pdf_text(72, y, 3, 16, title);
    y -= 32;
    for (const std::string& p : paragraphs) {
        if (y < 140) {
            pages.emplace_back();
            y = 842 - 72;
        }
        pages.back().content += pdfread::testing::pdf_paragraph(72, y, 451, 2, 11, 14.5, p);
        y -= 10;
    }
    return pages;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: make_sample_pdf <out.pdf> [extra_pages | --text file.txt]\n");
        return 1;
    }
    try {
        if (argc > 3 && std::string(argv[2]) == "--text") {
            pdfread::testing::write_pdf(argv[1], pages_from_text(argv[3]));
        } else {
            const int extra = argc > 2 ? std::atoi(argv[2]) : 8;
            pdfread::testing::write_pdf(argv[1], pdfread::testing::technical_sample(extra));
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
