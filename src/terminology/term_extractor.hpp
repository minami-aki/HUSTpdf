#pragma once
// Trích thuật ngữ tự động từ tài liệu (tầng 1), chưa có bản dịch -> "chưa xác nhận",
// KHÔNG đưa vào prompt; người dùng xác nhận trong cửa sổ glossary.
//
//  - Từ viết tắt có định nghĩa: "long form (ABBR)" hoặc "ABBR (long form)", chữ cái đầu
//    các từ của long form khớp ABBR (gạch nối tách từ: "proportional-integral-derivative" -> P I D).
//  - Từ viết tắt đứng riêng (chữ hoa, 2–8 ký tự, xuất hiện >= 2 lần).
//  - Cụm danh từ lặp lại: 2–4 từ, đầu/cuối không phải từ chức năng (bên trong cho phép "of"),
//    xuất hiện >= 2 lần; cụm ngắn nằm trong cụm dài cùng tần suất thì bỏ.
// Xếp hạng: tần suất × độ dài cụm × độ dài từ trung bình (từ dài thường đặc thù hơn).

#include <string>
#include <vector>

namespace pdfread::terminology {

struct TermCandidate {
    enum class Kind { Abbreviation, Phrase };
    Kind kind = Kind::Phrase;
    std::string term;        // cụm (viết thường) hoặc từ viết tắt
    std::string expansion;   // dạng đầy đủ của từ viết tắt nếu tài liệu có định nghĩa
    int count = 0;
    double score = 0;
};

struct ExtractOptions {
    int min_count = 2;
    std::size_t max_results = 200;
};

[[nodiscard]] std::vector<TermCandidate> extract_terms(const std::vector<std::u32string>& sentences,
                                                       const ExtractOptions& options = {});

}  // namespace pdfread::terminology
