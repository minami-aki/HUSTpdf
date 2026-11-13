#pragma once
// Thứ tự đọc các khối text của một trang (bố cục nhiều cột).
//
// Thứ tự khối của MuPDF theo content stream, thường đúng nhưng sai với nhiều PDF
// hai cột. Dùng XY-cut có điều chỉnh:
//  1. Nếu có khe dọc (gutter) chạy suốt chiều cao nhóm khối -> tách trái/phải, đọc trái trước.
//  2. Không thì cắt ngang ở các khe ngang, nhưng GỘP các dải liền nhau chỉ chứa khối hẹp
//     (không khối nào rộng >= 60% nhóm) lại với nhau: tránh lỗi kinh điển của XY-cut là
//     cắt ngang hai cột tại chỗ khoảng cách đoạn của hai cột vô tình thẳng hàng, làm đọc
//     xen kẽ trái-phải. Khối rộng (tiêu đề, hình, chú thích toàn trang) vẫn được tách riêng.
//  3. Không cắt được nữa -> sắp theo (y, x).

#include <cstddef>
#include <vector>

#include "text/types.hpp"

namespace pdfread::text {

// Trả chỉ số các khối theo thứ tự đọc.
[[nodiscard]] std::vector<std::size_t> reading_order(const std::vector<Box>& blocks);

}  // namespace pdfread::text
