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

// Số ký tự (code point) của chuỗi UTF-8.
[[nodiscard]] inline std::size_t utf8_length(std::string_view s) noexcept {
    std::size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

// Bản xem trước một dòng: gộp khoảng trắng/xuống dòng thành một dấu cách, cắt ở
// biên ký tự sau tối đa `max_chars` ký tự và thêm "…" nếu bị cắt.
[[nodiscard]] inline std::string utf8_preview(std::string_view s, std::size_t max_chars) {
    std::string out;
    std::size_t chars = 0;
    bool space = false;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            space = !out.empty();
            ++i;
            continue;
        }
        const std::size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        if (chars + (space ? 1 : 0) >= max_chars) {
            out += "…";
            return out;
        }
        if (space) {
            out.push_back(' ');
            ++chars;
            space = false;
        }
        out.append(s.substr(i, len));
        ++chars;
        i += len;
    }
    return out;
}

}  // namespace pdfread
