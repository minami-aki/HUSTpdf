#include "text/normalize.hpp"

#include <algorithm>
#include <cctype>

#include "util/utf8.hpp"

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

char32_t to_upper(char32_t c) noexcept {
    if (c >= U'a' && c <= U'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 0x20;               // Latin-1: à…þ
    if (c >= 0x100 && c <= 0x17F && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F) {
        // Latin Extended-A: cặp hoa/thường liền nhau (đ U+0111 -> Đ U+0110).
        const bool odd_lower = (c < 0x138) || (c > 0x149 && c < 0x179);
        if (odd_lower) return (c % 2 == 1) ? c - 1 : c;
        return (c % 2 == 0) ? c - 1 : c;                                  // 0x139..0x148, 0x179..0x17E
    }
    if (c == 0x1A1) return 0x1A0;  // ơ -> Ơ
    if (c == 0x1B0) return 0x1AF;  // ư -> Ư
    if (c >= 0x1EA0 && c <= 0x1EFF) return (c % 2 == 1) ? c - 1 : c;    // Latin Extended Additional: ạ ả ấ…
    return c;
}

char32_t to_lower(char32_t c) noexcept {
    if (c >= U'A' && c <= U'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 0x20;               // Latin-1: À…Þ
    if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x138 && c != 0x149 && c != 0x17F) {
        const bool even_upper = (c < 0x138) || (c > 0x149 && c < 0x179);
        if (even_upper) return (c % 2 == 0) ? c + 1 : c;
        return (c % 2 == 1) ? c + 1 : c;                                  // 0x139..0x148, 0x179..0x17E
    }
    if (c == 0x1A0) return 0x1A1;  // Ơ -> ơ
    if (c == 0x1AF) return 0x1B0;  // Ư -> ư
    if (c >= 0x1EA0 && c <= 0x1EFF) return (c % 2 == 0) ? c + 1 : c;    // Latin Extended Additional
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 0x20;         // Hy Lạp
    return c;
}

std::string match_initial_case(std::string_view translation, std::string_view source) {
    std::string out(translation);
    const std::u32string src = to_u32(source.substr(0, std::min<std::size_t>(source.size(), 16)));
    std::size_t si = 0;
    while (si < src.size() && !is_letter(src[si]) && !is_digit(src[si])) ++si;
    if (si >= src.size() || !is_upper(src[si])) return out;
    // Chữ cái đầu tiên của bản dịch (bỏ qua khoảng trắng, ngoặc, dấu nháy).
    std::size_t pos = 0;
    while (pos < out.size()) {
        const auto b = static_cast<unsigned char>(out[pos]);
        if (b < 0x80 && !std::isalnum(b)) {
            ++pos;
            continue;
        }
        break;
    }
    if (pos >= out.size()) return out;
    std::size_t len = 1;
    const auto b0 = static_cast<unsigned char>(out[pos]);
    if (b0 >= 0xF0) len = 4;
    else if (b0 >= 0xE0) len = 3;
    else if (b0 >= 0xC0) len = 2;
    const std::u32string first = to_u32(std::string_view(out).substr(pos, len));
    if (first.size() != 1 || to_upper(first[0]) == first[0]) return out;  // không phải chữ thường
    std::string upper;
    append_utf8(upper, to_upper(first[0]));
    out.replace(pos, len, upper);
    return out;
}

}  // namespace pdfread::text
