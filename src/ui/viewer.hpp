#pragma once
// Vùng xem PDF: cuộn liên tục, zoom, vẽ tile từ TextureCache và yêu cầu render
// tile còn thiếu (vùng nhìn trước, rồi tới vùng lân cận để cuộn mượt); trích text
// các trang đang nhìn và xử lý bôi đen.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "pdf/document.hpp"
#include "pdf/render_service.hpp"
#include "pdf/text_service.hpp"
#include "ui/page_layout.hpp"
#include "ui/selection.hpp"
#include "ui/texture_cache.hpp"

struct ImRect;

namespace pdfread::ui {

enum class ZoomMode { Custom, FitWidth, FitPage };

class Viewer {
public:
    static constexpr double kMinZoom = 0.1;
    static constexpr double kMaxZoom = 8.0;

    Viewer(pdf::RenderService& render, pdf::TextService& text, TextureCache& textures);

    void set_document(std::shared_ptr<pdf::Document> doc);
    [[nodiscard]] const std::shared_ptr<pdf::Document>& document() const noexcept { return doc_; }

    // Vẽ vào một child window kích thước `size`.
    // ui_scale: hệ số DPI của giao diện; fb_scale: pixel framebuffer trên một đơn vị ImGui.
    // Trả true nếu cần vẽ thêm frame ngay (bố cục/cuộn đang thay đổi).
    bool draw(ImVec2 size, float ui_scale, float fb_scale, std::uint64_t frame);

    void set_zoom(double zoom);  // chuyển sang ZoomMode::Custom, giữ tâm vùng nhìn
    void zoom_in();
    void zoom_out();
    void set_zoom_mode(ZoomMode mode);
    [[nodiscard]] double zoom() const noexcept { return zoom_; }
    [[nodiscard]] ZoomMode zoom_mode() const noexcept { return mode_; }

    void go_to_page(int index);
    [[nodiscard]] int current_page() const noexcept { return current_page_; }
    [[nodiscard]] int page_count() const noexcept { return doc_ ? doc_->page_count() : 0; }

    // Vùng bôi đen.
    [[nodiscard]] bool has_selection() const noexcept { return selection_.active(); }
    [[nodiscard]] const std::string& selection_text() const noexcept { return selection_.text(); }
    [[nodiscard]] bool selection_complete() const noexcept { return selection_.complete(); }
    [[nodiscard]] bool selection_dragging() const noexcept { return selection_.dragging(); }
    [[nodiscard]] std::uint64_t selection_version() const noexcept { return selection_.version(); }
    // Khung bao phần vùng chọn đang nhìn thấy (tọa độ màn hình), nếu có; tính trong draw().
    [[nodiscard]] const std::optional<std::pair<ImVec2, ImVec2>>& selection_screen_rect() const noexcept {
        return selection_rect_;
    }
    // Vùng nhìn (không gồm thanh cuộn), tọa độ màn hình.
    [[nodiscard]] std::pair<ImVec2, ImVec2> viewport_rect() const noexcept { return viewport_; }

    // Vùng chọn nằm gọn trong một trang và text của trang đó đã có.
    struct PageSelection {
        int page = -1;
        pdf::TextPagePtr text;
        std::vector<pdf::Quad> quads;
    };
    [[nodiscard]] std::optional<PageSelection> single_page_selection() const;

    // Tô nhạt phần đang được dịch (khi mở rộng vùng chọn ra trọn câu). Tọa độ point của trang.
    void set_translated_highlight(int page, std::vector<text::Box> rects);
    void clear_translated_highlight() { translated_rects_.clear(); }
    void clear_selection() { selection_.clear(); }
    // Đặt vùng chọn bằng tọa độ trang (dùng cho chế độ kiểm thử --select).
    void select(const SelectionPoint& a, const SelectionPoint& b, pdf::SelectUnit unit);

    // Mọi tile đang nhìn thấy đã có ở tỉ lệ hiện tại.
    [[nodiscard]] bool fully_rendered() const noexcept { return doc_ && layout_ready_ && !missing_visible_; }

private:
    struct Wanted {
        pdf::TileKey key;
        double priority;  // nhỏ = gấp
    };
    struct TextEntry {
        pdf::TextPagePtr text;
        std::uint64_t last_used = 0;
    };

    void receive_results(std::uint64_t frame);
    bool update_layout(float ui_scale, float fb_scale);
    void handle_input(const ImVec2& origin, float ui_scale);
    bool handle_selection_input(const ImRect& inner);
    void zoom_at(ImVec2 anchor, double zoom);
    void clamp_scroll();
    [[nodiscard]] double x_offset() const noexcept;
    [[nodiscard]] ViewTransform make_view(const ImVec2& origin) const noexcept;
    void draw_page(ImDrawList* dl, int page, const ImVec2& origin, float fb_scale, std::uint64_t frame);
    void draw_selection(ImDrawList* dl, int page, const ImVec2& page_origin);
    void collect_prefetch(float fb_scale);
    void request_text(int first_visible, int last_visible, std::uint64_t frame);

    pdf::RenderService& render_;
    pdf::TextService& text_service_;
    TextureCache& textures_;
    std::shared_ptr<pdf::Document> doc_;

    PageLayout layout_;
    bool layout_ready_ = false;
    int scale_q_ = 0;
    double zoom_ = 1.0;
    ZoomMode mode_ = ZoomMode::FitWidth;
    std::optional<ImVec2> zoom_anchor_;  // điểm trong vùng nhìn giữ cố định khi zoom

    double view_w_ = 0;
    double view_h_ = 0;
    double scroll_x_ = 0;
    double scroll_y_ = 0;
    bool pending_scroll_ = false;
    std::optional<int> pending_page_;
    bool panning_ = false;
    int current_page_ = 0;
    ViewTransform view_;

    bool missing_visible_ = false;
    std::vector<Wanted> wanted_;

    std::unordered_map<int, TextEntry> texts_;
    Selection selection_;
    std::optional<std::pair<ImVec2, ImVec2>> selection_rect_;
    std::pair<ImVec2, ImVec2> viewport_;
    int translated_page_ = -1;
    std::vector<text::Box> translated_rects_;
};

}  // namespace pdfread::ui
