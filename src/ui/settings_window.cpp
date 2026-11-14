#include "ui/settings_window.hpp"

#include <algorithm>

#include <imgui.h>

namespace pdfread::ui {

namespace {

ImVec4 rgb(std::uint32_t c) {
    return ImVec4(static_cast<float>((c >> 16) & 0xFF) / 255.0f, static_cast<float>((c >> 8) & 0xFF) / 255.0f,
                  static_cast<float>(c & 0xFF) / 255.0f, 1.0f);
}

// Ô mẫu màu chế độ đọc: nền giấy + dòng chữ mẫu.
void swatch(ReadingMode mode, float ui_scale) {
    const PageColors c = page_colors(mode);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size(64.0f * ui_scale, ImGui::GetFrameHeight());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(rgb(c.paper)), 3.0f * ui_scale);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(ImGuiCol_Border), 3.0f * ui_scale);
    dl->AddText(ImVec2(p.x + 8.0f * ui_scale, p.y + (size.y - ImGui::GetTextLineHeight()) / 2),
                ImGui::GetColorU32(rgb(c.ink)), "Aa Bb");
    ImGui::Dummy(size);
}

}  // namespace

SettingsWindow::Result SettingsWindow::draw(State& state, ModelWindow& model, const ModelWindow::Info& info,
                                            float ui_scale) {
    Result result;
    if (!open) return result;
    // Chiều cao cố định (tối đa 85% cửa sổ): tab "Mô hình dịch" dài, chiều cao tự co theo tab
    // đầu tiên thì phần thiết bị suy luận bị khuất.
    const float max_h = ImGui::GetMainViewport()->WorkSize.y * 0.85f;
    ImGui::SetNextWindowSize(ImVec2(660 * ui_scale, std::min(600 * ui_scale, max_h)), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Cài đặt", &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return result;
    }

    if (ImGui::BeginTabBar("##settings_tabs")) {
        auto flags = [&](Tab t) {
            const bool sel = select_ && *select_ == t;
            return sel ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        };

        if (ImGui::BeginTabItem("Hiển thị", nullptr, flags(Tab::Display))) {
            ImGui::SeparatorText("Chế độ đọc");
            ImGui::TextDisabled("Đổi màu trang: dễ chịu hơn khi đọc lâu hoặc buổi tối. Ảnh trong trang cũng đổi màu theo.");
            for (int i = 0; i < kReadingModeCount; ++i) {
                const auto m = static_cast<ReadingMode>(i);
                ImGui::PushID(i);
                swatch(m, ui_scale);
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                if (ImGui::RadioButton(reading_mode_name(m), state.reading_mode == m) && state.reading_mode != m) {
                    state.reading_mode = m;
                    result.reading_mode_changed = true;
                }
                ImGui::PopID();
            }
            ImGui::TextDisabled("Phím tắt: F9 chuyển lần lượt các chế độ.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Dịch", nullptr, flags(Tab::Translation))) {
            ImGui::SeparatorText("Khi bôi đen");
            result.translation_changed |= ImGui::Checkbox("Dịch tự động sau khi thả chuột", &state.auto_translate);
            ImGui::SetItemTooltip("Tắt thì dịch bằng Ctrl+T");
            result.translation_changed |= ImGui::Checkbox("Bôi đen nửa câu thì dịch cả câu", &state.whole_sentence);
            ImGui::SetItemTooltip("Tắt: chỉ dịch đúng phần bôi đen. Đổi được ngay trong popup dịch.");
            result.translation_changed |=
                ImGui::Checkbox("Đưa thêm 1 câu trước/sau vào prompt (thử nghiệm)", &state.include_context);
            ImGui::SetItemTooltip("Mô hình 1.8B đôi khi dịch nhầm câu ngữ cảnh — mặc định tắt (xem docs/DECISIONS.md).");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Mô hình dịch", nullptr, flags(Tab::Model))) {
            result.model = model.draw_contents(info, ui_scale);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    select_.reset();
    ImGui::End();
    return result;
}

}  // namespace pdfread::ui
