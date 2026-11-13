#pragma once
// Phân loại lĩnh vực của tài liệu (đa nhãn, tối đa 3) bằng so khớp từ khóa.
//
// Điểm của một lĩnh vực = Σ_k w_k · log2(1 + n_k), n_k = số lần từ khóa k xuất hiện
// (cụm nhiều từ được tính trọng số cao hơn: w = 1 + 0.5·(số từ − 1)); log làm một từ khóa
// lặp nhiều lần không lấn át độ phủ nhiều từ khóa khác nhau. Mỗi đoạn text đầu vào có
// trọng số riêng (tiêu đề, mục lục quan trọng hơn thân bài).
// Kết quả: các lĩnh vực có điểm >= 35% lĩnh vực cao nhất và >= ngưỡng tối thiểu; độ tin
// cậy = điểm / tổng điểm các lĩnh vực được trả về.

#include <string>
#include <vector>

#include "terminology/domains.hpp"
#include "translate/translator.hpp"

namespace pdfread::terminology {

struct WeightedText {
    std::u32string text;
    double weight = 1.0;
};

struct ClassifyOptions {
    std::size_t max_results = 3;
    double relative_threshold = 0.35;
    double min_score = 3.0;
};

[[nodiscard]] std::vector<translate::DomainScore> classify(const std::vector<WeightedText>& inputs,
                                                           const DomainCatalog& catalog,
                                                           const ClassifyOptions& options = {});

}  // namespace pdfread::terminology
