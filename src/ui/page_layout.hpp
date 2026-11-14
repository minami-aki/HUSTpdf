#pragma once
// Bố cục cuộn liên tục: các trang xếp dọc, căn giữa theo chiều ngang.
// Thuần toán (không phụ thuộc ImGui/GL) để unit test được.
//
// Hệ tọa độ "content": đơn vị ImGui, gốc ở góc trên-trái toàn bộ tài liệu.
// Hệ tọa độ "trang": point (1/72 inch), gốc ở góc trên-trái trang.
// Dùng double vì tài liệu dài ở zoom lớn có thể cao hàng chục triệu đơn vị.

#include <utility>
#include <vector>

namespace pdfread::ui {

struct PageSize {
    double w = 0;
    double h = 0;
};

struct Rect {
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
    [[nodiscard]] double right() const noexcept { return x + w; }
    [[nodiscard]] double bottom() const noexcept { return y + h; }
};

struct PagePoint {
    int page = -1;
    double x = 0;  // point
    double y = 0;  // point
};

// Ánh xạ tọa độ màn hình (đơn vị ImGui, tuyệt đối trong cửa sổ) <-> content.
// DPI đã nằm trong units_per_pt của PageLayout, nên ở đây chỉ còn gốc vùng nhìn,
// vị trí cuộn và độ dịch căn giữa.
struct ViewTransform {
    double origin_x = 0;  // góc trên-trái vùng nhìn trên màn hình
    double origin_y = 0;
    double scroll_x = 0;
    double scroll_y = 0;
    double offset_x = 0;  // dịch ngang khi tài liệu hẹp hơn vùng nhìn (căn giữa)

    [[nodiscard]] std::pair<double, double> to_content(double sx, double sy) const noexcept {
        return {sx - origin_x + scroll_x - offset_x, sy - origin_y + scroll_y};
    }
    [[nodiscard]] std::pair<double, double> to_screen(double cx, double cy) const noexcept {
        return {cx + offset_x - scroll_x + origin_x, cy - scroll_y + origin_y};
    }
};

class PageLayout;

// Màn hình -> điểm trên trang gần nhất (point), và ngược lại.
[[nodiscard]] PagePoint screen_to_page(const PageLayout& layout, const ViewTransform& view, double sx, double sy);
[[nodiscard]] std::pair<double, double> page_to_screen(const PageLayout& layout, const ViewTransform& view,
                                                       const PagePoint& p);

class PageLayout {
public:
    void set_pages(std::vector<PageSize> pages);
    // units_per_pt: số đơn vị content trên 1 point; gap: khoảng cách giữa hai trang;
    // margin: lề quanh toàn bộ tài liệu (đơn vị content).
    void set_metrics(double units_per_pt, double gap, double margin);

    [[nodiscard]] int page_count() const noexcept { return static_cast<int>(pages_.size()); }
    [[nodiscard]] const PageSize& page_size(int i) const { return pages_.at(static_cast<std::size_t>(i)); }
    [[nodiscard]] double units_per_pt() const noexcept { return upp_; }
    [[nodiscard]] double gap() const noexcept { return gap_; }
    [[nodiscard]] double margin() const noexcept { return margin_; }
    [[nodiscard]] double max_page_width() const noexcept { return max_w_; }
    [[nodiscard]] double max_page_height() const noexcept { return max_h_; }
    // Khổ trang xuất hiện nhiều nhất (point), hoặc trung vị khi không khổ nào chiếm >= 30% số
    // trang (sách scan). Dùng cho "vừa chiều rộng"/"vừa trang" để vài trang khổ ngang (bảng,
    // hình) không làm các trang dọc bị thu nhỏ.
    [[nodiscard]] const PageSize& typical_page() const noexcept { return typical_; }
    [[nodiscard]] double content_width() const noexcept { return content_w_; }
    [[nodiscard]] double content_height() const noexcept { return content_h_; }

    // Khung trang i trong hệ content.
    [[nodiscard]] Rect page_rect(int i) const;

    // Các trang giao với dải dọc [y0, y1): {đầu, cuối} (bao gồm), {0, -1} nếu không có.
    [[nodiscard]] std::pair<int, int> pages_in(double y0, double y1) const;

    // Trang gần y nhất (khoảng trống giữa hai trang chia đôi cho hai trang).
    [[nodiscard]] int page_near(double y) const;

    // Điểm content -> điểm trên trang gần nhất (có thể nằm ngoài khung trang).
    [[nodiscard]] PagePoint to_page(double cx, double cy) const;
    // Điểm trên trang -> điểm content.
    [[nodiscard]] std::pair<double, double> to_content(const PagePoint& p) const;

private:
    void rebuild();

    std::vector<PageSize> pages_;
    std::vector<double> tops_;
    double upp_ = 1;
    double gap_ = 0;
    double margin_ = 0;
    double max_w_ = 0;
    double max_h_ = 0;
    PageSize typical_;
    double content_w_ = 0;
    double content_h_ = 0;
};

}  // namespace pdfread::ui
