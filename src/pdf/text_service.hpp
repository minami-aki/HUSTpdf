#pragma once
// Trích text có cấu trúc (fz_stext_page) của các trang trên một luồng nền riêng.
//
// Giống RenderService: UI gọi request() với danh sách trang cần (theo độ ưu tiên),
// danh sách chờ cũ bị thay thế (trang đã cuộn qua thì bỏ). Kết quả lấy bằng
// take_results(); callback on_result đánh thức UI.

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
#include "pdf/text_page.hpp"
#include "util/concurrent_queue.hpp"
#include "util/thread_pool.hpp"

namespace pdfread::pdf {

class RootContext;

struct TextResult {
    std::uint64_t doc = 0;
    int page = 0;
    TextPagePtr text;  // nullptr nếu lỗi
    std::string error;
};

class TextService {
public:
    TextService(const RootContext& root, std::function<void()> on_result);
    ~TextService();

    TextService(const TextService&) = delete;
    TextService& operator=(const TextService&) = delete;

    void set_document(std::shared_ptr<Document> doc);
    // Thay danh sách chờ bằng các trang `pages` (thứ tự = độ ưu tiên).
    void request(const std::vector<int>& pages);
    [[nodiscard]] std::vector<TextResult> take_results() { return results_.drain(); }
    [[nodiscard]] bool busy() const;

private:
    void run_one();

    std::function<void()> on_result_;
    ConcurrentQueue<TextResult> results_;

    mutable std::mutex mutex_;
    std::shared_ptr<Document> doc_;
    std::deque<int> pending_;
    std::unordered_set<int> in_flight_;
    std::size_t queued_jobs_ = 0;

    ThreadPool pool_;  // khai báo cuối: join trước khi các thành viên khác bị hủy
};

}  // namespace pdfread::pdf
