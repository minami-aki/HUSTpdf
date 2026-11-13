#pragma once
// Tham số dòng lệnh dạng UTF-8. Trên Windows argv của main() theo bảng mã ANSI nên mất
// dấu tiếng Việt; lấy lại từ GetCommandLineW().

#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include "util/utf8.hpp"

namespace pdfread {

[[nodiscard]] inline std::vector<std::string> utf8_args(int argc, char** argv) {
    std::vector<std::string> out;
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int n = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &n);
    if (wargv != nullptr) {
        for (int i = 0; i < n; ++i) out.push_back(path_to_utf8(std::filesystem::path(wargv[i])));
        LocalFree(wargv);
    }
#else
    for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
#endif
    return out;
}

}  // namespace pdfread
