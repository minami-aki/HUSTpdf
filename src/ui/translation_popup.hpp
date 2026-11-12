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

    // Bắt đầu hiển thị cho yêu cầu `request_id`.
    void show(std::uint64_t request_id, bool truncated);
    void hide() noexcept { state_ = State::Hidden; }
    void on_event(const translate::TranslationEvent& e);

    // Vẽ cạnh khung vùng chọn `anchor`, bên trong `bounds` (vùng nhìn). Trả false nếu
    // người dùng bấm đóng.
    bool draw(const std::pair<ImVec2, ImVec2>& anchor, const std::pair<ImVec2, ImVec2>& bounds, float ui_scale,
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
    translate::TranslationStats stats_;
    float last_height_ = 0;
};

}  // namespace pdfread::ui
