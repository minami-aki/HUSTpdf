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

// Thêm một code point vào chuỗi UTF-8.
inline void append_utf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (c >> 18)));
        out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

[[nodiscard]] inline std::string to_utf8(std::u32string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char32_t c : s) append_utf8(out, c);
    return out;
}

// UTF-8 -> UTF-32; byte lỗi được thay bằng U+FFFD.
[[nodiscard]] inline std::u32string to_u32(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + len > s.size()) {
            out.push_back(U'�');
            ++i;
            continue;
        }
        char32_t cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        for (std::size_t k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += len;
    }
    return out;
}

// Số ký tự (code point) của chuỗi UTF-8.
[[nodiscard]] inline std::size_t utf8_length(std::string_view s) noexcept {
    std::size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

// Cắt chuỗi UTF-8 còn tối đa `max_chars` ký tự (không cắt giữa ký tự).
[[nodiscard]] inline std::string utf8_truncate(std::string_view s, std::size_t max_chars) {
    std::size_t chars = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (chars == max_chars) return std::string(s.substr(0, i));
            ++chars;
        }
    }
    return std::string(s);
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
