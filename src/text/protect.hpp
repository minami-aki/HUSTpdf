#pragma once
// Vùng KHÔNG được dịch (công thức, mã, đại lượng có đơn vị, URL/email) và thay chúng bằng
// placeholder trước khi gửi cho mô hình, khôi phục sau khi dịch.
//
// Placeholder dạng ⟦F1⟧ ⟦C1⟧ ⟦U1⟧ ⟦L1⟧ (F = công thức, C = mã, U = đại lượng/đơn vị,
// L = liên kết). Thử với HY-MT1.5-1.8B: các dạng ⟦X1⟧ {X1} [X1] @@X1@@ <X1> đều được giữ
// nguyên 8/8; chọn ⟦⟧ vì gần như không xuất hiện trong văn bản gốc (không nhầm với chữ
// thật). Mô hình đôi khi bỏ dấu cách trước placeholder -> khôi phục lại theo câu gốc.

#include <string>
#include <string_view>
#include <vector>

#include "text/types.hpp"

namespace pdfread::text {

enum class ProtectKind { Formula, Code, Quantity, Link };

struct ProtectedSpan {
    Span span;
    ProtectKind kind = ProtectKind::Formula;
};

// Tìm các vùng không dịch trong một câu/đoạn (không chồng lấn, theo thứ tự).
[[nodiscard]] std::vector<ProtectedSpan> find_protected(std::u32string_view text);

struct Placeholder {
    std::string token;     // "⟦F1⟧"
    std::string original;  // text gốc
    bool space_before = false;  // trong câu gốc có khoảng trắng ngay trước / sau
    bool space_after = false;
};

struct Placeholdered {
    std::string text;  // UTF-8, đã thay placeholder
    std::vector<Placeholder> placeholders;
};

[[nodiscard]] Placeholdered apply_placeholders(std::u32string_view text, const std::vector<ProtectedSpan>& spans);

struct RestoreResult {
    std::string text;
    std::vector<std::string> missing;     // placeholder không có trong bản dịch (gốc của chúng)
    std::vector<std::string> duplicated;  // xuất hiện hơn một lần
    std::vector<std::string> unknown;     // placeholder lạ do mô hình tự sinh
    [[nodiscard]] bool ok() const noexcept { return missing.empty() && duplicated.empty() && unknown.empty(); }
};

[[nodiscard]] RestoreResult restore_placeholders(std::string_view translated, const std::vector<Placeholder>& placeholders);

// Khôi phục khi đang stream: giữ lại phần có thể là placeholder chưa trọn ("⟦F" …) tới khi đủ.
class PlaceholderStream {
public:
    explicit PlaceholderStream(std::vector<Placeholder> placeholders) : placeholders_(std::move(placeholders)) {}
    [[nodiscard]] std::string push(std::string_view piece);
    [[nodiscard]] std::string flush();

private:
    std::vector<Placeholder> placeholders_;
    std::string pending_;
    char last_out_ = ' ';              // ký tự cuối đã phát (để chèn lại dấu cách)
    bool space_after_pending_ = false;  // mảnh trước kết thúc bằng placeholder cần dấu cách sau
};

}  // namespace pdfread::text
