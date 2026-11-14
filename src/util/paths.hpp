#pragma once
// Thư mục dữ liệu của người dùng cho app (HUSTpdf; trước đây là "Pdfread").
//  - app_data_dir()   : dữ liệu người dùng tự sửa, đi theo tài khoản (glossary người dùng).
//  - local_data_dir() : dữ liệu lớn/cục bộ của máy (cơ sở dữ liệu cache + cài đặt, mô hình tải về).
//  - migrate_legacy_data_dirs(): lần chạy đầu sau khi đổi tên, chuyển thư mục cũ sang tên mới.

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "util/utf8.hpp"

namespace pdfread {

inline constexpr const wchar_t* kAppDirName = L"HUSTpdf";
inline constexpr const wchar_t* kLegacyAppDirName = L"Pdfread";

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

inline std::filesystem::path roaming_base() {
    std::filesystem::path base = env_dir(L"APPDATA");
    return base.empty() ? std::filesystem::temp_directory_path() : base;
}

inline std::filesystem::path local_base() {
    std::filesystem::path base = env_dir(L"LOCALAPPDATA");
    return base.empty() ? roaming_base() : base;
}
#else
inline std::filesystem::path xdg_dir(const char* var, const char* fallback, const char* name) {
    const char* v = std::getenv(var);
    if (v != nullptr && *v != '\0') return path_from_utf8(v) / name;
    const char* home = std::getenv("HOME");
    return path_from_utf8(home != nullptr ? home : ".") / fallback / name;
}
#endif

}  // namespace detail

// Windows: %APPDATA%\HUSTpdf; nơi khác: $XDG_CONFIG_HOME/hustpdf hoặc ~/.config/hustpdf.
[[nodiscard]] inline std::filesystem::path app_data_dir() {
#ifdef _WIN32
    return detail::roaming_base() / kAppDirName;
#else
    return detail::xdg_dir("XDG_CONFIG_HOME", ".config", "hustpdf");
#endif
}

// Windows: %LOCALAPPDATA%\HUSTpdf; nơi khác: $XDG_DATA_HOME/hustpdf hoặc ~/.local/share/hustpdf.
[[nodiscard]] inline std::filesystem::path local_data_dir() {
#ifdef _WIN32
    return detail::local_base() / kAppDirName;
#else
    return detail::xdg_dir("XDG_DATA_HOME", ".local/share", "hustpdf");
#endif
}

// Thư mục của tên cũ (Pdfread).
[[nodiscard]] inline std::filesystem::path legacy_app_data_dir() {
#ifdef _WIN32
    return detail::roaming_base() / kLegacyAppDirName;
#else
    return detail::xdg_dir("XDG_CONFIG_HOME", ".config", "pdfread");
#endif
}

[[nodiscard]] inline std::filesystem::path legacy_local_data_dir() {
#ifdef _WIN32
    return detail::local_base() / kLegacyAppDirName;
#else
    return detail::xdg_dir("XDG_DATA_HOME", ".local/share", "pdfread");
#endif
}

// Đổi tên thư mục cũ sang tên mới nếu thư mục mới chưa có (cùng ổ đĩa nên chỉ là đổi tên,
// không chép). Trả mô tả các việc đã làm / lỗi để ghi log.
inline std::vector<std::string> migrate_legacy_data_dirs() {
    std::vector<std::string> out;
    const std::pair<std::filesystem::path, std::filesystem::path> pairs[] = {
        {legacy_app_data_dir(), app_data_dir()},
        {legacy_local_data_dir(), local_data_dir()},
    };
    for (const auto& [from, to] : pairs) {
        std::error_code ec;
        if (!std::filesystem::is_directory(from, ec) || std::filesystem::exists(to, ec)) continue;
        std::filesystem::rename(from, to, ec);
        out.push_back(ec ? "Không chuyển được " + path_to_utf8(from) + " -> " + path_to_utf8(to) + ": " + ec.message()
                         : "Đã chuyển dữ liệu " + path_to_utf8(from) + " -> " + path_to_utf8(to));
    }
    return out;
}

}  // namespace pdfread
