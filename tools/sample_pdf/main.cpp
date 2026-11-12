// Tạo file PDF mẫu để chạy thử viewer: make_sample_pdf <out.pdf> [số trang thêm]

#include <cstdio>
#include <cstdlib>
#include <exception>

#include "support/sample_pdf.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: make_sample_pdf <out.pdf> [extra_pages]\n");
        return 1;
    }
    try {
        const int extra = argc > 2 ? std::atoi(argv[2]) : 8;
        pdfread::testing::write_pdf(argv[1], pdfread::testing::technical_sample(extra));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
