#pragma once
// Popup bản dịch: hiện cạnh vùng chọn (ưu tiên phía dưới, không che vùng chọn), tự
// xuống dòng, hiển thị bản dịch dần theo token, có nút sao chép.

#include <cstdint>
#include <string>
#include <utility>

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

    enum class Action { None, Close, UseWholeSentence, UseSelectionOnly };

    // Bắt đầu hiển thị cho yêu cầu `request_id`.
    void show(std::uint64_t request_id, bool truncated, Scope scope = Scope::None);
    void hide() noexcept { state_ = State::Hidden; }
    void on_event(const translate::TranslationEvent& e);

    // Vẽ cạnh khung vùng chọn `anchor`, bên trong `bounds` (vùng nhìn).
    Action draw(const std::pair<ImVec2, ImVec2>& anchor, const std::pair<ImVec2, ImVec2>& bounds, float ui_scale,
                translate::ModelState model);

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] bool visible() const noexcept { return state_ != State::Hidden; }
    [[nodiscard]] bool busy() const noexcept { return state_ == State::Waiting || state_ == State::Streaming; }
    [[nodiscard]] std::uint64_t request_id() const noexcept { return request_id_; }
    [[nodiscard]] const std::string& text() const noexcept { return text_; }

private:
    State state_ = State::Hidden;
    std::uint64_t request_id_ = 0;
    std::string text_;
    std::string error_;
    bool truncated_ = false;
    Scope scope_ = Scope::None;
    translate::TranslationStats stats_;
    float last_height_ = 0;
};

}  // namespace pdfread::ui
