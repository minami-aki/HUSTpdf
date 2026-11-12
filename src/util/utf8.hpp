#pragma once
// Chuyển đổi đường dẫn <-> UTF-8. Toàn bộ chuỗi trong app là UTF-8; chỉ đổi
// sang dạng gốc của hệ điều hành (UTF-16 trên Windows) ở biên.

#include <filesystem>
#include <string>
#include <string_view>

namespace pdfread {

[[nodiscard]] inline std::string path_to_utf8(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}

[[nodiscard]] inline std::filesystem::path path_from_utf8(std::string_view s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

}  // namespace pdfread
