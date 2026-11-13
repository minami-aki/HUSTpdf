#pragma once
// Popup bản dịch: hiện cạnh vùng chọn (ưu tiên phía dưới, không che vùng chọn), tự
// xuống dòng, hiển thị bản dịch dần theo token, có nút sao chép.
//
// Hai chế độ:
//  - bản dịch của mô hình (text do App đưa vào sau khi khôi phục placeholder), kèm các
//    thuật ngữ glossary đã chỉ định và cảnh báo hậu kiểm (thuật ngữ sai, ký hiệu mất);
//  - kết quả tra glossary cho một từ/cụm ngắn (không gọi mô hình), kèm nghĩa khác.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "translate/translation_service.hpp"

namespace pdfread::ui {

class TranslationPopup {
public:
    enum class State { Hidden, Waiting, Streaming, Done, Cancelled, Error };

    // Lựa chọn phạm vi dịch khi người dùng bôi đen một phần câu.
    enum class Scope {
        None,           // không áp dụng (đã chọn trọn câu, hoặc là từ/cụm ngắn)
        WholeSentence,  // đang dịch cả câu chứa vùng chọn
        SelectionOnly,  // đang dịch đúng phần bôi đen
    };

    enum class Action { None, Close, UseWholeSentence, UseSelectionOnly, TranslateWithModel, EditTerm };

    struct TermHint {
        std::string en;
        std::string vi;
    };

    struct GlossaryAnswer {
        std::string term_en;
        std::string term_vi;
        std::string domain;  // tên lĩnh vực / nguồn
        std::string notes;
        std::vector<std::pair<std::string, std::string>> alternatives;  // (nghĩa, lĩnh vực)
    };

    // Bắt đầu hiển thị bản dịch của mô hình cho yêu cầu `request_id`.
    void show(std::uint64_t request_id, bool truncated, Scope scope = Scope::None, std::vector<TermHint> terms = {});
    // Hiển thị kết quả tra glossary (không gọi mô hình).
    void show_glossary(GlossaryAnswer answer);
    void hide() noexcept { state_ = State::Hidden; }

    // Cập nhật trạng thái theo sự kiện của luồng suy luận (text bản dịch do App đưa vào).
    void on_event(const translate::TranslationEvent& e);
    void append_text(std::string_view s) { text_ += s; }
    void set_text(std::string s) { text_ = std::move(s); }
    void set_issues(std::vector<std::string> issues) { issues_ = std::move(issues); }

    // Vẽ cạnh khung vùng chọn `anchor`, bên trong `bounds` (vùng nhìn).
    Action draw(const std::pair<ImVec2, ImVec2>& anchor, const std::pair<ImVec2, ImVec2>& bounds, float ui_scale,
                translate::ModelState model);

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] bool visible() const noexcept { return state_ != State::Hidden; }
    [[nodiscard]] bool busy() const noexcept { return state_ == State::Waiting || state_ == State::Streaming; }
    [[nodiscard]] std::uint64_t request_id() const noexcept { return request_id_; }
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] const std::vector<std::string>& issues() const noexcept { return issues_; }
    // Thuật ngữ đang hiển thị trong chế độ tra glossary (để mở cửa sổ sửa).
    [[nodiscard]] const std::string& glossary_term() const noexcept { return answer_.term_en; }

private:
    void draw_glossary_answer();

    State state_ = State::Hidden;
    bool glossary_mode_ = false;
    std::uint64_t request_id_ = 0;
    std::string text_;
    std::string error_;
    bool truncated_ = false;
    Scope scope_ = Scope::None;
    std::vector<TermHint> terms_;
    std::vector<std::string> issues_;
    GlossaryAnswer answer_;
    translate::TranslationStats stats_;
    float last_height_ = 0;
};

}  // namespace pdfread::ui
