#pragma once
// Prompt cho Tencent HY-MT1.5 theo model card chính thức (chiều EN -> VI).
//
//  - Dịch thường (nhóm "XX<=>XX", lệnh tiếng Anh, tên ngôn ngữ tiếng Anh "Vietnamese").
//  - Chỉ định thuật ngữ và dịch có ngữ cảnh: model card chỉ có bản lệnh tiếng Trung,
//    dùng nguyên văn với tên ngôn ngữ tiếng Trung "越南语".
//  - Có cả thuật ngữ lẫn ngữ cảnh: model card không có mẫu gộp; ghép khối thuật ngữ
//    ("参考下面的翻译：…") lên trước mẫu ngữ cảnh. Cần đánh giá bằng tools/eval (M6).

#include <string>
#include <string_view>

#include "translate/translator.hpp"

namespace pdfread::translate {

// Nội dung tin nhắn "user" theo mẫu phù hợp với request.
[[nodiscard]] std::string build_user_prompt(const TranslateRequest& request);

// Bọc tin nhắn user bằng chat template của mô hình (lấy từ GGUF, metadata
// tokenizer.chat_template; mô hình không có system prompt mặc định).
//   add_generation_prompt = false: "...<｜hy_place▁holder▁no▁8｜>" (như ví dụ trên model card)
//   add_generation_prompt = true : "...<｜hy_Assistant｜>"
[[nodiscard]] std::string format_chat(std::string_view user_message, bool add_generation_prompt);

}  // namespace pdfread::translate
