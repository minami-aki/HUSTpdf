#pragma once
// Tạo file PDF nhỏ bằng tay (không cần thư viện) cho test và chạy thử.
// Font: base-14 /F1 Helvetica, /F2 Times-Roman, /F3 Helvetica-Bold, /F4 Courier.
// Tọa độ trong content stream theo chuẩn PDF: gốc ở góc DƯỚI-trái, đơn vị point.

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace pdfread::testing {

struct SamplePage {
    double width = 595;   // A4
    double height = 842;
    int rotate = 0;       // /Rotate
    std::string content;  // content stream
};

// Lệnh vẽ một dòng chữ (ASCII).
std::string pdf_text(double x, double y, int font, double size, std::string_view text);
// Đoạn văn tự xuống dòng trong độ rộng `width`; trả về y của dòng tiếp theo qua `y`.
std::string pdf_paragraph(double x, double& y, double width, int font, double size, double leading,
                          std::string_view text);
// Hình chữ nhật tô màu xám `gray` (0 = đen, 1 = trắng).
std::string pdf_rect(double x, double y, double w, double h, double gray);
// Khung chữ nhật viền.
std::string pdf_stroke_rect(double x, double y, double w, double h, double line_width);

[[nodiscard]] std::string build_pdf(const std::vector<SamplePage>& pages);
void write_pdf(const std::filesystem::path& path, const std::vector<SamplePage>& pages);

// Tài liệu mẫu nhiều trang kiểu bài báo kỹ thuật (một cột, hai cột, trang ngang, trang xoay).
[[nodiscard]] std::vector<SamplePage> technical_sample(int extra_pages = 8);

}  // namespace pdfread::testing
