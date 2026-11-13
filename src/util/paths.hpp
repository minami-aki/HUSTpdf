#pragma once
// Thư mục dữ liệu của người dùng cho app (glossary người dùng, sau này: cài đặt, cache).

#include <cstdlib>
#include <filesystem>
#include <string>

#include "util/utf8.hpp"

namespace pdfread {

// Windows: %APPDATA%\Pdfread; nơi khác: $XDG_CONFIG_HOME/pdfread hoặc ~/.config/pdfread.
[[nodiscard]] inline std::filesystem::path app_data_dir() {
#ifdef _WIN32
    wchar_t* buf = nullptr;
    std::size_t len = 0;
    std::filesystem::path base;
    if (_wdupenv_s(&buf, &len, L"APPDATA") == 0 && buf != nullptr) {
        base = buf;
        std::free(buf);
    } else {
        base = std::filesystem::temp_directory_path();
    }
    return base / "Pdfread";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && *xdg != '\0') return path_from_utf8(xdg) / "pdfread";
    const char* home = std::getenv("HOME");
    return path_from_utf8(home != nullptr ? home : ".") / ".config" / "pdfread";
#endif
}

}  // namespace pdfread
