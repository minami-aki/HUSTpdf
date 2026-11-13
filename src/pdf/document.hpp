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
    void set_display_list_capacity(std::size_t capacity);

private:
    Document(fz_document* doc, std::filesystem::path path, std::vector<PageBox> pages);
    DisplayListPtr build_display_list(int index);

    fz_document* doc_;
    std::filesystem::path path_;
    std::vector<PageBox> pages_;
    std::uint64_t id_;

    std::mutex doc_mutex_;  // mọi lời gọi MuPDF chạm tới doc_

    std::mutex cache_mutex_;
    std::size_t cache_capacity_ = 48;
    std::list<int> lru_;  // trước = mới dùng nhất
    std::unordered_map<int, std::pair<DisplayListPtr, std::list<int>::iterator>> cache_;
};

// Render vùng `rect` của trang ở tỉ lệ `scale` (pixel trên point). Tọa độ pixel có gốc
// ở góc trên-trái trang. Nền trắng. Ném pdf::Error khi lỗi.
[[nodiscard]] std::unique_ptr<Pixmap> render(const DisplayList& list, const PageBox& box, float scale,
                                             const IRect& rect);

// Ghi pixmap ra PNG. Ném pdf::Error khi lỗi.
void save_png(const Pixmap& pixmap, const std::filesystem::path& path);

}  // namespace pdfread::pdf
