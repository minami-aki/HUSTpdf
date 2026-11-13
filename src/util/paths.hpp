#pragma once
// Thư mục dữ liệu của người dùng cho app.
//  - app_data_dir()      : dữ liệu người dùng tự sửa, đi theo tài khoản (glossary người dùng).
//  - local_data_dir()    : dữ liệu lớn/cục bộ của máy (cơ sở dữ liệu cache + cài đặt, mô hình tải về).

#include <cstdlib>
#include <filesystem>
#include <string>

#include "util/utf8.hpp"

namespace pdfread {

namespace detail {

#ifdef _WIN32
inline std::filesystem::path env_dir(const wchar_t* name) {
    wchar_t* buf = nullptr;
    std::size_t len = 0;
    std::filesystem::path out;
    if (_wdupenv_s(&buf, &len, name) == 0 && buf != nullptr) {
        out = buf;
        std::free(buf);
    }
    return out;
}
#else
inline std::filesystem::path xdg_dir(const char* var, const char* fallback) {
    const char* v = std::getenv(var);
    if (v != nullptr && *v != '\0') return path_from_utf8(v) / "pdfread";
    const char* home = std::getenv("HOME");
    return path_from_utf8(home != nullptr ? home : ".") / fallback / "pdfread";
}
#endif

}  // namespace detail

// Windows: %APPDATA%\Pdfread; nơi khác: $XDG_CONFIG_HOME/pdfread hoặc ~/.config/pdfread.
[[nodiscard]] inline std::filesystem::path app_data_dir() {
#ifdef _WIN32
    std::filesystem::path base = detail::env_dir(L"APPDATA");
    if (base.empty()) base = std::filesystem::temp_directory_path();
    return base / "Pdfread";
#else
    return detail::xdg_dir("XDG_CONFIG_HOME", ".config");
#endif
}

// Windows: %LOCALAPPDATA%\Pdfread; nơi khác: $XDG_DATA_HOME/pdfread hoặc ~/.local/share/pdfread.
[[nodiscard]] inline std::filesystem::path local_data_dir() {
#ifdef _WIN32
    std::filesystem::path base = detail::env_dir(L"LOCALAPPDATA");
    if (base.empty()) return app_data_dir();
    return base / "Pdfread";
#else
    return detail::xdg_dir("XDG_DATA_HOME", ".local/share");
#endif
}

}  // namespace pdfread
