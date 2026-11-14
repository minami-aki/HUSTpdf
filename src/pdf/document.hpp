#pragma once
// Tài liệu PDF, display list, pixmap và hàm render.
//
// Luồng dữ liệu: Document::display_list(trang) (truy cập fz_document, tuần tự hóa
// bằng mutex) -> render() từ display list (chạy song song trên nhiều luồng) -> Pixmap.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct fz_document;
struct fz_display_list;
struct fz_pixmap;

namespace pdfread::pdf {

// Khung trang theo point (1/72 inch), đã tính /Rotate.
struct PageBox {
    float x0 = 0;
    float y0 = 0;
    float width = 0;
    float height = 0;
};

// Hình chữ nhật nguyên, nửa mở [x0, x1) x [y0, y1).
struct IRect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    [[nodiscard]] int width() const noexcept { return x1 - x0; }
    [[nodiscard]] int height() const noexcept { return y1 - y0; }
};

// Một mục mục lục (outline), danh sách phẳng theo thứ tự duyệt trước; con có level lớn hơn
// cha 1 và nằm ngay sau cha.
struct OutlineItem {
    std::string title;
    int level = 0;
    int page = -1;     // -1: không trỏ tới trang nào trong tài liệu
    float y = -1;      // point từ mép trên trang (giống tọa độ text); < 0: đầu trang
    std::string uri;   // liên kết ngoài (http…), nếu có
};

// Liên kết trên trang. Khung theo point, gốc góc trên-trái trang (cùng hệ với text/vùng chọn).
struct Link {
    float x0 = 0;
    float y0 = 0;
    float x1 = 0;
    float y1 = 0;
    int page = -1;     // đích trong tài liệu (-1 nếu là liên kết ngoài / không xác định)
    float y = -1;      // vị trí đích trên trang (point từ mép trên); < 0: đầu trang
    std::string uri;   // liên kết ngoài (http, https, mailto…)
    [[nodiscard]] bool contains(float px, float py) const noexcept { return px >= x0 && px <= x1 && py >= y0 && py <= y1; }
};

class DisplayList {
public:
    explicit DisplayList(fz_display_list* list) noexcept : list_(list) {}
    ~DisplayList();
    DisplayList(const DisplayList&) = delete;
    DisplayList& operator=(const DisplayList&) = delete;

    [[nodiscard]] fz_display_list* get() const noexcept { return list_; }

private:
    fz_display_list* list_;
};
using DisplayListPtr = std::shared_ptr<const DisplayList>;

// Pixmap RGB không alpha (3 byte/pixel).
class Pixmap {
public:
    explicit Pixmap(fz_pixmap* pix) noexcept : pix_(pix) {}
    ~Pixmap();
    Pixmap(const Pixmap&) = delete;
    Pixmap& operator=(const Pixmap&) = delete;

    // Tạo pixmap RGB trắng kích thước w x h.
    [[nodiscard]] static std::unique_ptr<Pixmap> create_rgb(int width, int height);

    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;
    [[nodiscard]] std::ptrdiff_t stride() const noexcept;
    [[nodiscard]] int components() const noexcept;
    [[nodiscard]] const unsigned char* samples() const noexcept;
    [[nodiscard]] unsigned char* samples() noexcept;
    [[nodiscard]] fz_pixmap* get() const noexcept { return pix_; }

private:
    fz_pixmap* pix_;
};

class Document {
public:
    // Mở tài liệu. Ném pdf::Error (thông điệp tiếng Việt) khi không mở được.
    [[nodiscard]] static std::shared_ptr<Document> open(const std::filesystem::path& path);
    ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    // Định danh duy nhất của lần mở này (khác nhau kể cả khi mở lại cùng file).
    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] int page_count() const noexcept { return static_cast<int>(pages_.size()); }
    [[nodiscard]] const PageBox& page(int index) const { return pages_.at(static_cast<std::size_t>(index)); }
    [[nodiscard]] const std::vector<PageBox>& pages() const noexcept { return pages_; }

    // Display list của trang, có cache LRU. Thread-safe. Ném pdf::Error khi lỗi.
    [[nodiscard]] DisplayListPtr display_list(int index);
    // Như display_list() nhưng không đưa vào cache (cho tác vụ duyệt cả tài liệu ở nền,
    // tránh đẩy các trang đang xem ra khỏi cache). Vẫn dùng bản có sẵn trong cache.
    [[nodiscard]] DisplayListPtr display_list_uncached(int index);

    // Tiêu đề các mục trong mục lục (outline) theo thứ tự, tối đa `max_items`. Rỗng nếu
    // tài liệu không có mục lục. Thread-safe.
    [[nodiscard]] std::vector<std::string> outline_titles(std::size_t max_items = 300);
    // Mục lục đầy đủ (tối đa `max_items` mục). Thread-safe.
    [[nodiscard]] std::vector<OutlineItem> outline(std::size_t max_items = 20000);
    // Liên kết của trang (có cache). Thread-safe. Lỗi -> danh sách rỗng.
    [[nodiscard]] std::vector<Link> links(int index);
    void set_display_list_capacity(std::size_t capacity);

private:
    Document(fz_document* doc, std::filesystem::path path, std::vector<PageBox> pages);
    DisplayListPtr build_display_list(int index);

    fz_document* doc_;
    std::filesystem::path path_;
    std::vector<PageBox> pages_;
    std::uint64_t id_;

    std::mutex doc_mutex_;  // mọi lời gọi MuPDF chạm tới doc_
    std::unordered_map<int, std::vector<Link>> links_;  // cache, bảo vệ bởi doc_mutex_

    std::mutex cache_mutex_;
    std::size_t cache_capacity_ = 48;
    std::list<int> lru_;  // trước = mới dùng nhất
    std::unordered_map<int, std::pair<DisplayListPtr, std::list<int>::iterator>> cache_;
};

// Render vùng `rect` của trang ở tỉ lệ `scale` (pixel trên point). Tọa độ pixel có gốc
// ở góc trên-trái trang. Nền trắng. Ném pdf::Error khi lỗi.
[[nodiscard]] std::unique_ptr<Pixmap> render(const DisplayList& list, const PageBox& box, float scale,
                                             const IRect& rect);

// Đổi màu pixmap theo chế độ đọc: đen -> ink, trắng -> paper (0xRRGGBB), nội suy tuyến tính
// từng kênh (giữ khử răng cưa của chữ). Ảnh màu cũng bị ánh xạ theo (chế độ Tối đảo màu ảnh).
void tint(Pixmap& pixmap, std::uint32_t ink, std::uint32_t paper);

// Ghi pixmap ra PNG. Ném pdf::Error khi lỗi.
void save_png(const Pixmap& pixmap, const std::filesystem::path& path);

}  // namespace pdfread::pdf
