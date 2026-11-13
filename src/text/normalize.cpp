#include "text/normalize.hpp"

namespace pdfread::text {

bool is_space(char32_t c) noexcept {
    return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == 0xA0 || (c >= 0x2000 && c <= 0x200A) ||
           c == 0x202F || c == 0x205F || c == 0x3000;
}

bool is_hyphen(char32_t c) noexcept { return c == U'-' || c == 0xAD || c == 0x2010 || c == 0x2011; }

// Chỉ phân loại ASCII, Latin-1 và Hy Lạp; ký tự khác coi là "không rõ" (cả hai hàm trả
// false). Đủ cho tài liệu tiếng Anh: chỉ dùng để đoán ranh giới câu / gạch nối.
bool is_lower(char32_t c) noexcept {
    if (c >= U'a' && c <= U'z') return true;
    if (c >= 0xDF && c <= 0xFF && c != 0xF7) return true;  // Latin-1
    if (c >= 0x3B1 && c <= 0x3C9) return true;              // Hy Lạp thường
    return false;
}

bool is_upper(char32_t c) noexcept {
    if (c >= U'A' && c <= U'Z') return true;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return true;
    if (c >= 0x391 && c <= 0x3A9) return true;
    return false;
}

bool is_digit(char32_t c) noexcept { return c >= U'0' && c <= U'9'; }

bool is_letter(char32_t c) noexcept { return is_lower(c) || is_upper(c); }

std::size_t append_normalized(std::u32string& out, char32_t c) {
    switch (c) {
        case 0xFB00: out += U"ff"; return 2;
        case 0xFB01: out += U"fi"; return 2;
        case 0xFB02: out += U"fl"; return 2;
        case 0xFB03: out += U"ffi"; return 3;
        case 0xFB04: out += U"ffl"; return 3;
        case 0xFB05:
        case 0xFB06: out += U"st"; return 2;
        case 0x200B:  // zero width space
        case 0x200C:
        case 0x200D:
        case 0x2060:
        case 0xFEFF:
        case 0xAD:  // soft hyphen giữa dòng: chỉ là gợi ý ngắt dòng
            return 0;
        default: break;
    }
    if (is_space(c)) {
        out.push_back(U' ');
        return 1;
    }
    out.push_back(c);
    return 1;
}

}  // namespace pdfread::text
