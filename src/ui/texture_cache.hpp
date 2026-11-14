#pragma once
// Cache texture OpenGL của các tile, giới hạn theo bộ nhớ, loại bỏ theo LRU.
// Chỉ dùng trên UI thread (thread có OpenGL context).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "pdf/render_service.hpp"

namespace pdfread::ui {

struct TileTexture {
    unsigned int texture = 0;  // GLuint; 0 nếu tile render lỗi
    int width = 0;
    int height = 0;
    std::uint64_t last_used = 0;  // số frame dùng gần nhất
};

class TextureCache {
public:
    explicit TextureCache(std::size_t budget_bytes = kDefaultBudget);
    ~TextureCache();

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    static constexpr std::size_t kDefaultBudget = std::size_t{160} << 20;

    // Tìm tile; nếu có thì đánh dấu đã dùng ở frame `frame`.
    [[nodiscard]] const TileTexture* use(const pdf::TileKey& key, std::uint64_t frame);
    [[nodiscard]] bool contains(const pdf::TileKey& key) const { return entries_.contains(key); }

    // Upload pixmap (nullptr = tile lỗi, lưu để không render lại liên tục).
    void insert(const pdf::TileKey& key, const pdf::Pixmap* pixmap, std::uint64_t frame);

    // Duyệt mọi tile của (tài liệu, trang).
    void for_each_of_page(std::uint64_t doc, int page,
                          const std::function<void(const pdf::TileKey&, TileTexture&)>& fn);

    // Loại tile dùng lâu nhất cho tới khi dưới ngân sách; không loại tile dùng ở `frame`.
    void evict(std::uint64_t frame);
    void clear();

    // Xóa thật các texture đã bỏ. Texture bị bỏ giữa frame (vd. đổi chế độ màu từ cửa sổ Cài đặt,
    // vẽ SAU trang) có thể vẫn nằm trong draw list của frame đó: glDeleteTextures ngay thì lúc
    // render tên texture không còn hợp lệ, GL vẽ bằng texture đang bind (atlas chữ) -> trang
    // chớp thành rác. App gọi hàm này sau khi đã render draw list.
    void flush_deleted();

    [[nodiscard]] std::size_t bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    void destroy(TileTexture& t);

    std::size_t budget_;
    std::size_t bytes_ = 0;
    std::unordered_map<pdf::TileKey, TileTexture, pdf::TileKeyHash> entries_;
    std::vector<unsigned int> deleted_;  // chờ flush_deleted()
};

}  // namespace pdfread::ui
