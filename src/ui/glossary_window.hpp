#pragma once
// Cửa sổ quản lý glossary: xem, tìm, sửa (ghi vào glossary người dùng hoặc của tài liệu),
// nhập/xuất CSV, và danh sách thuật ngữ tự trích từ tài liệu (chưa xác nhận).
//
// Gõ tiếng Việt: ô nhập liệu của ImGui nhận ký tự qua GLFW (WM_CHAR); bộ gõ kiểu Unikey/
// EVKey (gửi phím xóa + ký tự) thường dùng được, bộ gõ tiếng Việt có sẵn của Windows (TSF,
// có ô soạn thảo) có thể không hiện chữ đang soạn. Luôn có lựa chọn sửa file CSV bằng
// Excel/trình soạn thảo ngoài rồi bấm "Nạp lại".

#include <string>
#include <vector>

#include "terminology/glossary_store.hpp"
#include "terminology/term_extractor.hpp"

namespace pdfread::ui {

class GlossaryWindow {
public:
    bool open = false;

    // Vẽ (nếu đang mở). Trả true nếu glossary đã thay đổi (cần dựng lại chỉ mục).
    bool draw(terminology::GlossaryStore& store, const std::vector<terminology::TermCandidate>& extracted,
              bool extracting, float ui_scale);

    // Mở cửa sổ và nạp sẵn thuật ngữ vào ô sửa.
    void edit(const terminology::GlossaryStore& store, const std::string& term_en);

private:
    bool draw_terms_tab(terminology::GlossaryStore& store, float ui_scale);
    void draw_extracted_tab(terminology::GlossaryStore& store, const std::vector<terminology::TermCandidate>& extracted,
                            bool extracting);
    bool draw_form(terminology::GlossaryStore& store);
    void load_form(const terminology::GlossaryEntry& e);

    std::string search_;
    int filter_ = 0;
    bool select_terms_tab_ = false;

    // Ô sửa
    std::string form_en_;
    std::string form_vi_;
    std::string form_notes_;
    bool form_keep_ = false;
    std::vector<std::string> form_aliases_;  // giữ nguyên khi lưu (sửa bằng CSV)
    std::string form_explanation_;
    int form_target_ = 0;  // 0: glossary của tôi, 1: glossary tài liệu
    std::string message_;
    bool message_error_ = false;
};

}  // namespace pdfread::ui
