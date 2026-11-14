#pragma once
// Cửa sổ Cài đặt chung: Hiển thị (chế độ đọc), Dịch (tự động, phạm vi, ngữ cảnh), Mô hình dịch
// (chọn / tải mô hình, số luồng, cache). Các thay đổi trả về cho App để áp dụng và lưu.

#include "ui/model_window.hpp"
#include "ui/theme.hpp"

namespace pdfread::ui {

class SettingsWindow {
public:
    enum class Tab { Display, Translation, Model };

    struct State {
        ReadingMode reading_mode = ReadingMode::Normal;
        bool auto_translate = true;
        bool whole_sentence = true;
        bool include_context = false;
    };

    struct Result {
        bool reading_mode_changed = false;
        bool translation_changed = false;  // auto_translate / whole_sentence / include_context
        ModelWindow::Action model = ModelWindow::Action::None;
    };

    bool open = false;
    void open_at(Tab tab) {
        open = true;
        select_ = tab;
    }

    Result draw(State& state, ModelWindow& model, const ModelWindow::Info& info, float ui_scale);

private:
    std::optional<Tab> select_;
};

}  // namespace pdfread::ui
