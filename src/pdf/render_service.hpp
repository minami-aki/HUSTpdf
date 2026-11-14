#pragma once
// Dịch vụ render tile chạy trên các luồng nền.
//
// UI thread mỗi frame gọi request() với danh sách tile cần (đã sắp theo độ ưu tiên,
// đã bỏ tile có sẵn trong cache texture). Danh sách chờ cũ bị THAY THẾ hoàn toàn:
// tile không còn trong vùng nhìn sẽ không bao giờ được render. Tile đang render dở
// vẫn chạy nốt. Kết quả lấy bằng take_results(); mỗi khi có kết quả, callback
// on_result được gọi (dùng để đánh thức UI bằng glfwPostEmptyEvent).

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "pdf/document.hpp"
#include "pdf/tiles.hpp"
#include "util/concurrent_queue.hpp"
#include "util/thread_pool.hpp"

namespace pdfread::pdf {

class RootContext;

struct TileKey {
    std::uint64_t doc = 0;
    int page = 0;
    int scale_q = 0;
    int col = 0;
    int row = 0;
    // Màu trang (chế độ đọc): nét đen -> ink, nền trắng -> paper (0xRRGGBB). Mặc định: không đổi.
    std::uint32_t ink = 0x000000;
    std::uint32_t paper = 0xFFFFFF;

    friend bool operator==(const TileKey&, const TileKey&) = default;
};

struct TileKeyHash {
    std::size_t operator()(const TileKey& k) const noexcept {
        std::uint64_t h = k.doc * 0x9E3779B97F4A7C15ull;
        for (int v : {k.page, k.scale_q, k.col, k.row, static_cast<int>(k.ink), static_cast<int>(k.paper)}) {
            h ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(v)) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        }
        return static_cast<std::size_t>(h);
    }
};

struct TileResult {
    TileKey key;
    std::unique_ptr<Pixmap> pixmap;  // nullptr nếu lỗi
    std::string error;
};

class RenderService {
public:
    RenderService(const RootContext& root, std::size_t threads, std::function<void()> on_result);
    ~RenderService();

    RenderService(const RenderService&) = delete;
    RenderService& operator=(const RenderService&) = delete;

    // Đổi tài liệu (nullptr = đóng). Xóa danh sách chờ.
    void set_document(std::shared_ptr<Document> doc);

    // Thay danh sách chờ bằng `wanted` (thứ tự = độ ưu tiên). Tile đang render bị bỏ qua.
    void request(const std::vector<TileKey>& wanted);

    [[nodiscard]] std::vector<TileResult> take_results() { return results_.drain(); }

    // Còn tile đang chờ hoặc đang render.
    [[nodiscard]] bool busy() const;

    [[nodiscard]] static std::size_t default_thread_count() noexcept;

private:
    void run_one();

    std::function<void()> on_result_;
    ConcurrentQueue<TileResult> results_;

    mutable std::mutex mutex_;
    std::shared_ptr<Document> doc_;
    std::deque<TileKey> pending_;
    std::unordered_set<TileKey, TileKeyHash> in_flight_;
    std::size_t queued_jobs_ = 0;  // job đã post vào pool nhưng chưa bắt đầu

    ThreadPool pool_;  // khai báo cuối: hủy (join) trước các thành viên khác
};

}  // namespace pdfread::pdf
