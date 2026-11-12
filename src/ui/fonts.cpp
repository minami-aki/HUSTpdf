#include "ui/fonts.hpp"

#include <imgui.h>

#include "util/log.hpp"
#include "util/utf8.hpp"

namespace pdfread::ui {

bool load_fonts(const std::filesystem::path& font_dir) {
    const auto regular = font_dir / "NotoSans-Regular.ttf";
    const auto math = font_dir / "NotoSansMath-Regular.ttf";
    std::error_code ec;
    if (!std::filesystem::exists(regular, ec)) {
        log::warn("Không tìm thấy font {}", path_to_utf8(regular));
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.OversampleH = 1;  // FreeType tự xử lý; tiết kiệm bộ nhớ atlas
    ImFont* font = io.Fonts->AddFontFromFileTTF(path_to_utf8(regular).c_str(), kBaseFontSize, &cfg);
    if (font == nullptr) return false;

    if (std::filesystem::exists(math, ec)) {
        ImFontConfig merge;
        merge.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(path_to_utf8(math).c_str(), kBaseFontSize, &merge);
    } else {
        log::warn("Không tìm thấy font ký hiệu toán {}", path_to_utf8(math));
    }
    io.FontDefault = font;
    return true;
}

void apply_style(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsLight(&style);
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.ScrollbarSize = 14.0f;
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(6.0f, 4.0f);
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.96f, 0.96f, 0.97f, 1.0f);
    style.ScaleAllSizes(scale);
    style.FontSizeBase = kBaseFontSize;
    style.FontScaleDpi = scale;
}

}  // namespace pdfread::ui
