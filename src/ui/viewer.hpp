#pragma once
// Vùng xem PDF: cuộn liên tục, zoom, vẽ tile từ TextureCache và yêu cầu render
// tile còn thiếu (vùng nhìn trước, rồi tới vùng lân cận để cuộn mượt).

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <imgui.h>

#include "pdf/document.hpp"
#include "pdf/render_service.hpp"
#include "ui/page_layout.hpp"
#include "ui/texture_cache.hpp"

namespace pdfread::ui {

enum class ZoomMode { Custom, FitWidth, FitPage };

class Viewer {
public:
    static constexpr double kMinZoom = 0.1;
    static constexpr double kMaxZoom = 8.0;

    Viewer(pdf::RenderService& render, TextureCache& textures);

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

    // Mọi tile đang nhìn thấy đã có ở tỉ lệ hiện tại.
    [[nodiscard]] bool fully_rendered() const noexcept { return doc_ && layout_ready_ && !missing_visible_; }

private:
    struct Wanted {
        pdf::TileKey key;
        double priority;  // nhỏ = gấp
    };

    void receive_results(std::uint64_t frame);
    bool update_layout(float ui_scale, float fb_scale);
    void handle_input(const ImVec2& origin, float ui_scale);
    void zoom_at(ImVec2 anchor, double zoom);
    void clamp_scroll();
    [[nodiscard]] double x_offset() const noexcept;
    void draw_page(ImDrawList* dl, int page, const ImVec2& origin, float fb_scale, std::uint64_t frame);
    void collect_prefetch(float fb_scale);

    pdf::RenderService& render_;
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

    bool missing_visible_ = false;
    std::vector<Wanted> wanted_;
};

}  // namespace pdfread::ui
