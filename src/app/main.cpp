// M0: chương trình khung, chỉ kiểm tra build và liên kết thư viện util.
// M1 sẽ thay bằng vòng lặp GLFW + ImGui.

#include <cstdio>
#include <string_view>

#include "util/log.hpp"
#include "util/thread_name.hpp"

#ifndef PDFREAD_VERSION
#define PDFREAD_VERSION "0.0.0"
#endif

int main(int argc, char** argv) {
    using namespace pdfread;
    name_current_thread("main");

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--version") {
            std::printf("pdfread %s\n", PDFREAD_VERSION);
            return 0;
        }
        if (arg == "--verbose") log::set_level(log::Level::Debug);
        if (arg == "--timing") log::set_timing_enabled(true);
    }

    log::info("pdfread {} — khung M0", PDFREAD_VERSION);
    return 0;
}
