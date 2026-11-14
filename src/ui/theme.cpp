#include "ui/theme.hpp"

namespace pdfread::ui {

namespace {
bool g_dark_ui = false;
}

bool dark_ui() noexcept { return g_dark_ui; }
void set_dark_ui(bool dark) noexcept { g_dark_ui = dark; }

PageColors page_colors(ReadingMode mode) {
    switch (mode) {
        case ReadingMode::Sepia:
            return {.ink = 0x4A3B2A, .paper = 0xF4ECD8, .background = IM_COL32(0xC9, 0xBF, 0xAA, 0xFF)};
        case ReadingMode::Dark:
            return {.ink = 0xD6D6D6, .paper = 0x1E1F22, .background = IM_COL32(0x2B, 0x2D, 0x31, 0xFF)};
        case ReadingMode::Warm:
            return {.ink = 0x1C1814, .paper = 0xFFF2DD, .background = IM_COL32(0xCF, 0xC6, 0xB6, 0xFF)};
        case ReadingMode::Normal:
        default:
            return {};
    }
}

bool is_dark(ReadingMode mode) noexcept { return mode == ReadingMode::Dark; }

const char* reading_mode_name(ReadingMode mode) noexcept {
    switch (mode) {
        case ReadingMode::Sepia: return "Sepia (giấy ngà)";
        case ReadingMode::Dark: return "Tối";
        case ReadingMode::Warm: return "Ấm (giảm ánh xanh)";
        case ReadingMode::Normal:
        default: return "Bình thường";
    }
}

const char* reading_mode_key(ReadingMode mode) noexcept {
    switch (mode) {
        case ReadingMode::Sepia: return "sepia";
        case ReadingMode::Dark: return "dark";
        case ReadingMode::Warm: return "warm";
        case ReadingMode::Normal:
        default: return "normal";
    }
}

std::optional<ReadingMode> reading_mode_from_key(std::string_view key) noexcept {
    for (int i = 0; i < kReadingModeCount; ++i) {
        const auto m = static_cast<ReadingMode>(i);
        if (key == reading_mode_key(m)) return m;
    }
    return std::nullopt;
}

}  // namespace pdfread::ui
