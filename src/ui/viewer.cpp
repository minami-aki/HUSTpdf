#include "ui/viewer.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <imgui_internal.h>  // ImGuiWindow::InnerRect: vùng nhìn thật, không gồm thanh cuộn

#include "pdf/tiles.hpp"
#include "util/log.hpp"

namespace pdfread::ui {
namespace {

constexpr double kPointsToUnits = 96.0 / 72.0;  // zoom 100% = 96 dpi
constexpr std::array kZoomSteps{0.1, 0.25, 0.33, 0.5, 0.67, 0.75, 0.8, 0.9, 1.0, 1.1, 1.25,
                                1.5, 1.75, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0};

constexpr ImU32 kBackground = IM_COL32(0xD5, 0xD8, 0xDC, 0xFF);
constexpr ImU32 kPageColor = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
constexpr ImU32 kShadowColor = IM_COL32(0x00, 0x00, 0x00, 0x30);
constexpr ImU32 kSelectionColor = IM_COL32(0x2F, 0x7D, 0xF6, 0x55);

constexpr std::size_t kTextCacheCapacity = 64;  // số trang giữ text
constexpr int kMaxSelectionTextPages = 50;      // trích text tối đa cho vùng chọn nhiều trang

float snap(float v, float fb_scale) { return std::floor(v * fb_scale + 0.5f) / fb_scale; }

ImTextureID to_texture_id(unsigned int tex) { return static_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)); }

}  // namespace

Viewer::Viewer(pdf::RenderService& render, pdf::TextService& text, TextureCache& textures)
    : render_(render), text_service_(text), textures_(textures) {}

void Viewer::set_document(std::shared_ptr<pdf::Document> doc) {
    doc_ = std::move(doc);
    textures_.clear();
    texts_.clear();
    selection_.clear();
    render_.set_document(doc_);
    text_service_.set_document(doc_);
    layout_ready_ = false;
    scale_q_ = 0;
    scroll_x_ = 0;
    scroll_y_ = 0;
    pending_scroll_ = true;
    pending_page_.reset();
    zoom_anchor_.reset();
    current_page_ = 0;
    missing_visible_ = false;
    std::vector<PageSize> sizes;
    if (doc_) {
        sizes.reserve(static_cast<std::size_t>(doc_->page_count()));
        for (const pdf::PageBox& b : doc_->pages()) sizes.push_back({b.width, b.height});
    }
    layout_.set_pages(std::move(sizes));
}

// --- zoom / điều hướng ---------------------------------------------------------

void Viewer::set_zoom(double zoom) {
    mode_ = ZoomMode::Custom;
    zoom_ = std::clamp(zoom, kMinZoom, kMaxZoom);
}

void Viewer::zoom_in() {
    for (double z : kZoomSteps) {
        if (z > zoom_ * 1.001) return set_zoom(z);
    }
    set_zoom(kMaxZoom);
}

void Viewer::zoom_out() {
    for (auto it = kZoomSteps.rbegin(); it != kZoomSteps.rend(); ++it) {
        if (*it < zoom_ * 0.999) return set_zoom(*it);
    }
    set_zoom(kMinZoom);
}

void Viewer::set_zoom_mode(ZoomMode mode) { mode_ = mode; }

void Viewer::zoom_at(ImVec2 anchor, double zoom) {
    zoom_anchor_ = anchor;
    set_zoom(zoom);
}

void Viewer::go_to_page(int index) {
    if (!doc_) return;
    pending_page_ = std::clamp(index, 0, doc_->page_count() - 1);
}

void Viewer::select(const SelectionPoint& a, const SelectionPoint& b, pdf::SelectUnit unit) {
    selection_.begin(a, unit);
    selection_.extend(b);
    selection_.end_drag();
}

double Viewer::x_offset() const noexcept {
    return std::max(0.0, (view_w_ - layout_.content_width()) / 2);
}

ViewTransform Viewer::make_view(const ImVec2& origin) const noexcept {
    return ViewTransform{origin.x, origin.y, scroll_x_, scroll_y_, x_offset()};
}

void Viewer::clamp_scroll() {
    const double max_x = std::max(0.0, layout_.content_width() - view_w_);
    const double max_y = std::max(0.0, layout_.content_height() - view_h_);
    scroll_x_ = std::clamp(scroll_x_, 0.0, max_x);
    scroll_y_ = std::clamp(scroll_y_, 0.0, max_y);
}

// --- frame ---------------------------------------------------------------------

void Viewer::receive_results(std::uint64_t frame) {
    for (pdf::TileResult& r : render_.take_results()) {
        if (!doc_ || r.key.doc != doc_->id()) continue;  // tài liệu cũ: pixmap tự hủy
        textures_.insert(r.key, r.pixmap.get(), frame);
    }
    for (pdf::TextResult& r : text_service_.take_results()) {
        if (!doc_ || r.doc != doc_->id() || !r.text) continue;
        texts_[r.page] = TextEntry{std::move(r.text), frame};
    }
}

bool Viewer::update_layout(float ui_scale, float fb_scale) {
    if (!doc_ || doc_->page_count() == 0) return false;
    // Chưa biết kích thước vùng nhìn (frame đầu): chưa dựng bố cục để khỏi render ở zoom sai.
    if (view_w_ <= 0 || view_h_ <= 0) return false;

    const double base = kPointsToUnits * ui_scale;
    const double margin = std::round(12.0 * ui_scale);
    const double gap = std::round(10.0 * ui_scale);

    const PageSize& typical = layout_.typical_page();
    if (mode_ != ZoomMode::Custom && typical.w > 0 && typical.h > 0) {
        double z = (view_w_ - 2 * margin - 1) / (typical.w * base);
        if (mode_ == ZoomMode::FitPage) z = std::min(z, (view_h_ - 2 * margin - 1) / (typical.h * base));
        zoom_ = std::clamp(z, kMinZoom, kMaxZoom);
    }

    const int scale_q = std::max(1, static_cast<int>(std::lround(zoom_ * base * fb_scale * pdf::kScaleDenominator)));
    const double upp = pdf::scale_from_q(scale_q) / fb_scale;

    bool changed = false;
    if (scale_q != scale_q_ || gap != layout_.gap() || margin != layout_.margin() || !layout_ready_) {
        const ImVec2 anchor = zoom_anchor_.value_or(ImVec2(static_cast<float>(view_w_ / 2), static_cast<float>(view_h_ / 2)));
        PagePoint pp;
        if (layout_ready_) pp = layout_.to_page(scroll_x_ + anchor.x - x_offset(), scroll_y_ + anchor.y);

        layout_.set_metrics(upp, gap, margin);
        scale_q_ = scale_q;

        if (pp.page >= 0) {
            const auto [cx, cy] = layout_.to_content(pp);
            scroll_x_ = cx + x_offset() - anchor.x;
            scroll_y_ = cy - anchor.y;
            pending_scroll_ = true;
        } else if (!layout_ready_) {
            // Lần đầu: đầu tài liệu, căn giữa theo chiều ngang (khi có trang rộng hơn vùng nhìn).
            scroll_x_ = std::max(0.0, (layout_.content_width() - view_w_) / 2);
            scroll_y_ = 0;
            pending_scroll_ = true;
        }
        layout_ready_ = true;
        changed = true;
    }
    zoom_anchor_.reset();

    if (pending_page_) {
        scroll_y_ = layout_.page_rect(*pending_page_).y - layout_.margin();
        pending_page_.reset();
        pending_scroll_ = true;
    }
    return changed;
}

void Viewer::handle_input(const ImVec2& origin, float ui_scale) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsWindowHovered();
    const double line = 48.0 * ui_scale;

    if (hovered && io.MouseWheel != 0.0f) {
        if (io.KeyCtrl) {
            const ImVec2 anchor(io.MousePos.x - origin.x, io.MousePos.y - origin.y);
            zoom_at(anchor, zoom_ * std::pow(1.15, static_cast<double>(io.MouseWheel)));
        } else if (io.KeyShift) {
            scroll_x_ -= io.MouseWheel * line * 1.5;
            pending_scroll_ = true;
        } else {
            scroll_y_ -= io.MouseWheel * line * 1.5;
            pending_scroll_ = true;
        }
    }
    if (hovered && io.MouseWheelH != 0.0f) {
        scroll_x_ -= io.MouseWheelH * line * 1.5;
        pending_scroll_ = true;
    }

    // Kéo bằng chuột giữa để di chuyển trang.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) panning_ = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) panning_ = false;
    if (panning_ && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
        scroll_x_ -= io.MouseDelta.x;
        scroll_y_ -= io.MouseDelta.y;
        pending_scroll_ = true;
    }

    // Bàn phím: chỉ khi không gõ chữ và không có popup.
    if (io.WantTextInput || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) || io.KeyCtrl) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) selection_.clear();
    const double page_step = std::max(line, view_h_ - line);
    const bool can_scroll_x = layout_.content_width() > view_w_ + 0.5;
    struct Binding {
        ImGuiKey key;
        double dx;
        double dy;
    };
    const Binding bindings[] = {
        {ImGuiKey_DownArrow, 0, line},       {ImGuiKey_UpArrow, 0, -line},
        {ImGuiKey_PageDown, 0, page_step},   {ImGuiKey_PageUp, 0, -page_step},
        {ImGuiKey_Space, 0, io.KeyShift ? -page_step : page_step},
    };
    for (const Binding& b : bindings) {
        if (ImGui::IsKeyPressed(b.key)) {
            scroll_x_ += b.dx;
            scroll_y_ += b.dy;
            pending_scroll_ = true;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
        scroll_y_ = 0;
        pending_scroll_ = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_End)) {
        scroll_y_ = layout_.content_height();
        pending_scroll_ = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        if (can_scroll_x) {
            scroll_x_ += line;
            pending_scroll_ = true;
        } else {
            go_to_page(current_page_ + 1);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        if (can_scroll_x) {
            scroll_x_ -= line;
            pending_scroll_ = true;
        } else {
            go_to_page(current_page_ - 1);
        }
    }
}

bool Viewer::handle_selection_input(const ImRect& inner) {
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const bool in_view = inner.Contains(mouse);  // không tính thanh cuộn
    const bool hovered = ImGui::IsWindowHovered() && in_view;
    bool more = false;

    auto point_at = [&](ImVec2 s) {
        const PagePoint p = screen_to_page(layout_, view_, s.x, s.y);
        return SelectionPoint{p.page, {static_cast<float>(p.x), static_cast<float>(p.y)}};
    };

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const SelectionPoint p = point_at(mouse);
        if (io.KeyShift && selection_.has_anchor()) {
            selection_.extend(p);
        } else {
            // Một lần nhấn: theo ký tự; nhấn đúp: theo từ; nhấn ba lần: theo dòng.
            const int clicks = io.MouseClickedCount[ImGuiMouseButton_Left];
            const pdf::SelectUnit unit = clicks >= 3   ? pdf::SelectUnit::Lines
                                         : clicks == 2 ? pdf::SelectUnit::Words
                                                       : pdf::SelectUnit::Chars;
            selection_.begin(p, unit);
        }
    }

    if (selection_.dragging()) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            selection_.end_drag();
            if (selection_.active()) log::debug("Vùng chọn: {}", selection_.text());
        } else {
            // Kéo ra ngoài vùng nhìn: tự cuộn, tốc độ theo khoảng cách tới mép.
            auto overshoot = [](float v, float lo, float hi) { return v < lo ? v - lo : (v > hi ? v - hi : 0.0f); };
            const float dx = overshoot(mouse.x, inner.Min.x, inner.Max.x);
            const float dy = overshoot(mouse.y, inner.Min.y, inner.Max.y);
            if (dx != 0.0f || dy != 0.0f) {
                scroll_x_ += dx * 0.25;
                scroll_y_ += dy * 0.25;
                pending_scroll_ = true;
                more = true;
            }
            const ImVec2 clamped(std::clamp(mouse.x, inner.Min.x, inner.Max.x - 1.0f),
                                 std::clamp(mouse.y, inner.Min.y, inner.Max.y - 1.0f));
            selection_.extend(point_at(clamped));
        }
    }

    // Con trỏ dạng chữ I khi rê trên trang.
    if (hovered && !panning_) {
        const PagePoint p = screen_to_page(layout_, view_, mouse.x, mouse.y);
        if (p.page >= 0) {
            const PageSize& s = layout_.page_size(p.page);
            if (p.x >= 0 && p.y >= 0 && p.x <= s.w && p.y <= s.h) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
        }
    }
    return more;
}

void Viewer::request_text(int first_visible, int last_visible, std::uint64_t frame) {
    std::vector<int> wanted;
    auto want = [&](int page) {
        if (page < 0 || page >= page_count()) return;
        if (auto it = texts_.find(page); it != texts_.end()) {
            it->second.last_used = frame;
            return;
        }
        if (std::find(wanted.begin(), wanted.end(), page) == wanted.end()) wanted.push_back(page);
    };
    for (int p = first_visible; p <= last_visible; ++p) want(p);

    // Trang thuộc vùng chọn (giới hạn số trang để vùng chọn rất dài không làm nghẽn).
    const auto [s0, s1] = selection_.page_range();
    if (s0 >= 0) {
        for (int p = s0; p <= std::min(s1, s0 + kMaxSelectionTextPages - 1); ++p) want(p);
    }
    text_service_.request(wanted);

    // Giới hạn cache: bỏ trang dùng lâu nhất, trừ trang dùng ở frame này.
    if (texts_.size() > kTextCacheCapacity) {
        std::vector<std::pair<std::uint64_t, int>> old;
        for (const auto& [page, e] : texts_) {
            if (e.last_used != frame) old.emplace_back(e.last_used, page);
        }
        std::sort(old.begin(), old.end());
        for (const auto& [_, page] : old) {
            if (texts_.size() <= kTextCacheCapacity) break;
            texts_.erase(page);
        }
    }
}

bool Viewer::draw(ImVec2 size, float ui_scale, float fb_scale, std::uint64_t frame) {
    bool more = false;
    receive_results(frame);
    more |= update_layout(ui_scale, fb_scale);

    if (layout_ready_) {
        ImGui::SetNextWindowContentSize(ImVec2(static_cast<float>(std::max(layout_.content_width(), view_w_)),
                                               static_cast<float>(layout_.content_height())));
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kBackground);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##viewer", size, ImGuiChildFlags_None,
                      // Luôn giữ chỗ thanh cuộn dọc: nếu nó hiện/ẩn, chiều rộng vùng nhìn đổi
                      // và chế độ "vừa chiều rộng" phải zoom lại (render thừa).
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs);
    ImGui::PopStyleVar();

    const ImRect inner = ImGui::GetCurrentWindow()->InnerRect;
    const ImVec2 origin = inner.Min;
    viewport_ = {inner.Min, inner.Max};
    selection_rect_.reset();
    if (std::abs(inner.GetWidth() - view_w_) > 0.5 || std::abs(inner.GetHeight() - view_h_) > 0.5) {
        view_w_ = inner.GetWidth();
        view_h_ = inner.GetHeight();
        more = true;
    }

    if (!doc_ || !layout_ready_) {
        missing_visible_ = true;
        wanted_.clear();
        render_.request({});
        text_service_.request({});
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return more;
    }

    if (!pending_scroll_) {
        scroll_x_ = ImGui::GetScrollX();
        scroll_y_ = ImGui::GetScrollY();
    }
    handle_input(origin, ui_scale);
    clamp_scroll();
    view_ = make_view(origin);
    more |= handle_selection_input(inner);
    if (pending_scroll_) {
        clamp_scroll();
        ImGui::SetScrollX(static_cast<float>(scroll_x_));
        ImGui::SetScrollY(static_cast<float>(scroll_y_));
        pending_scroll_ = false;
        more = true;  // ImGui áp dụng cuộn ở frame sau
        view_ = make_view(origin);
    }

    current_page_ = std::max(0, layout_.page_near(scroll_y_ + view_h_ * 0.4));

    const auto [first, last] = layout_.pages_in(scroll_y_, scroll_y_ + view_h_);
    request_text(first, last, frame);
    selection_.update([this](int page) -> pdf::TextPagePtr {
        auto it = texts_.find(page);
        return it == texts_.end() ? nullptr : it->second.text;
    });

    // Vẽ các trang trong vùng nhìn và ghi lại tile còn thiếu.
    wanted_.clear();
    missing_visible_ = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = first; i <= last; ++i) draw_page(dl, i, origin, fb_scale, frame);
    collect_prefetch(fb_scale);

    std::stable_sort(wanted_.begin(), wanted_.end(),
                     [](const Wanted& a, const Wanted& b) { return a.priority < b.priority; });
    std::vector<pdf::TileKey> keys;
    keys.reserve(wanted_.size());
    for (const Wanted& w : wanted_) keys.push_back(w.key);
    render_.request(keys);
    textures_.evict(frame);

    ImGui::EndChild();
    ImGui::PopStyleColor();
    return more;
}

void Viewer::draw_page(ImDrawList* dl, int page, const ImVec2& origin, float fb_scale, std::uint64_t frame) {
    const Rect r = layout_.page_rect(page);
    const double xoff = x_offset();
    const ImVec2 p0(snap(origin.x + static_cast<float>(r.x + xoff - scroll_x_), fb_scale),
                    snap(origin.y + static_cast<float>(r.y - scroll_y_), fb_scale));
    const ImVec2 p1(p0.x + static_cast<float>(r.w), p0.y + static_cast<float>(r.h));
    const float shadow = 2.0f;
    dl->AddRectFilled(ImVec2(p0.x + shadow, p0.y + shadow), ImVec2(p1.x + shadow, p1.y + shadow), kShadowColor);
    dl->AddRectFilled(p0, p1, kPageColor);

    const pdf::PageBox& box = doc_->page(page);
    const pdf::TileGrid grid = pdf::tile_grid(box.width, box.height, scale_q_);

    // Phần trang đang nhìn thấy, theo pixel của trang ở tỉ lệ hiện tại.
    const double vx0 = (scroll_x_ - xoff - r.x) * fb_scale;
    const double vy0 = (scroll_y_ - r.y) * fb_scale;
    const pdf::TileSpan span = pdf::tiles_in(grid, vx0, vy0, vx0 + view_w_ * fb_scale, vy0 + view_h_ * fb_scale);
    if (span.empty()) return;

    const std::uint64_t doc_id = doc_->id();
    bool complete = true;
    for (int row = span.row0; row <= span.row1 && complete; ++row) {
        for (int col = span.col0; col <= span.col1; ++col) {
            if (!textures_.contains({doc_id, page, scale_q_, col, row})) {
                complete = false;
                break;
            }
        }
    }

    // Tile của tỉ lệ khác (ví dụ trước khi zoom) phóng to/thu nhỏ tạm trong lúc chờ.
    if (!complete) {
        std::vector<std::pair<pdf::TileKey, const TileTexture*>> fallback;
        textures_.for_each_of_page(doc_id, page, [&](const pdf::TileKey& key, TileTexture& tex) {
            if (key.scale_q == scale_q_ || tex.texture == 0) return;
            tex.last_used = frame;
            fallback.emplace_back(key, &tex);
        });
        std::sort(fallback.begin(), fallback.end(),
                  [](const auto& a, const auto& b) { return a.first.scale_q < b.first.scale_q; });
        for (const auto& [key, tex] : fallback) {
            const double units_per_px = layout_.units_per_pt() / pdf::scale_from_q(key.scale_q);
            const ImVec2 a(p0.x + static_cast<float>(key.col * pdf::kTileSize * units_per_px),
                           p0.y + static_cast<float>(key.row * pdf::kTileSize * units_per_px));
            const ImVec2 b(a.x + static_cast<float>(tex->width * units_per_px),
                           a.y + static_cast<float>(tex->height * units_per_px));
            dl->AddImage(to_texture_id(tex->texture), a, b);
        }
    }

    const double cx = view_w_ * fb_scale / 2;
    const double cy = view_h_ * fb_scale / 2;
    for (int row = span.row0; row <= span.row1; ++row) {
        for (int col = span.col0; col <= span.col1; ++col) {
            const pdf::TileKey key{doc_id, page, scale_q_, col, row};
            const pdf::IRect tr = pdf::tile_rect(grid, col, row);
            if (const TileTexture* tex = textures_.use(key, frame)) {
                if (tex->texture == 0) continue;  // tile lỗi: để trắng
                const ImVec2 a(p0.x + tr.x0 / fb_scale, p0.y + tr.y0 / fb_scale);
                const ImVec2 b(p0.x + tr.x1 / fb_scale, p0.y + tr.y1 / fb_scale);
                dl->AddImage(to_texture_id(tex->texture), a, b);
            } else {
                missing_visible_ = true;
                // Ưu tiên tile gần tâm vùng nhìn.
                const double tx = (tr.x0 + tr.x1) / 2.0 - vx0;
                const double ty = (tr.y0 + tr.y1) / 2.0 - vy0;
                wanted_.push_back({key, std::hypot(tx - cx, ty - cy)});
            }
        }
    }

    draw_selection(dl, page, p0);
}

void Viewer::draw_selection(ImDrawList* dl, int page, const ImVec2& page_origin) {
    const std::vector<pdf::Quad>* quads = selection_.quads(page);
    if (quads == nullptr) return;
    const float upp = static_cast<float>(layout_.units_per_pt());
    auto at = [&](const pdf::PointF& p) { return ImVec2(page_origin.x + p.x * upp, page_origin.y + p.y * upp); };
    for (const pdf::Quad& q : *quads) {
        const ImVec2 pts[4] = {at(q.ul), at(q.ur), at(q.lr), at(q.ll)};
        dl->AddQuadFilled(pts[0], pts[1], pts[2], pts[3], kSelectionColor);
        // Khung bao trên màn hình (để đặt popup dịch), chỉ phần nằm trong vùng nhìn.
        for (const ImVec2& p : pts) {
            const ImVec2 c(std::clamp(p.x, viewport_.first.x, viewport_.second.x),
                           std::clamp(p.y, viewport_.first.y, viewport_.second.y));
            if (!selection_rect_) {
                selection_rect_ = std::make_pair(c, c);
            } else {
                selection_rect_->first = ImVec2(std::min(selection_rect_->first.x, c.x), std::min(selection_rect_->first.y, c.y));
                selection_rect_->second = ImVec2(std::max(selection_rect_->second.x, c.x), std::max(selection_rect_->second.y, c.y));
            }
        }
    }
}

void Viewer::collect_prefetch(float fb_scale) {
    // Vùng lân cận: nửa màn hình phía trên và một màn hình phía dưới.
    const double y0 = scroll_y_ - view_h_ * 0.5;
    const double y1 = scroll_y_ + view_h_ * 2.0;
    const double xoff = x_offset();
    const auto [first, last] = layout_.pages_in(y0, y1);
    const std::uint64_t doc_id = doc_->id();
    constexpr double kPrefetchBase = 1e12;  // luôn sau tile đang nhìn thấy

    for (int page = first; page <= last; ++page) {
        const Rect r = layout_.page_rect(page);
        const pdf::PageBox& box = doc_->page(page);
        const pdf::TileGrid grid = pdf::tile_grid(box.width, box.height, scale_q_);
        const double px0 = (scroll_x_ - xoff - r.x) * fb_scale;
        const double py0 = (y0 - r.y) * fb_scale;
        const pdf::TileSpan span =
            pdf::tiles_in(grid, px0, py0, px0 + view_w_ * fb_scale, py0 + (y1 - y0) * fb_scale);
        for (int row = span.row0; row <= span.row1; ++row) {
            for (int col = span.col0; col <= span.col1; ++col) {
                const pdf::TileKey key{doc_id, page, scale_q_, col, row};
                if (textures_.contains(key)) continue;
                const bool already = std::any_of(wanted_.begin(), wanted_.end(),
                                                 [&](const Wanted& w) { return w.key == key; });
                if (already) continue;
                const double tile_y = r.y + (row * pdf::kTileSize) / fb_scale;
                wanted_.push_back({key, kPrefetchBase + std::abs(tile_y - (scroll_y_ + view_h_ / 2))});
            }
        }
    }
}

}  // namespace pdfread::ui
