#pragma once
// Tìm kiếm toàn tài liệu trên một luồng nền (có fz_context riêng).
//
// - start() hủy lần tìm đang chạy, quét các trang theo thứ tự, gửi kết quả dần (take_updates()
//   mỗi frame; callback `wake` đánh thức UI).
// - Văn bản đã chuẩn hóa (fold_for_search) của từng trang được giữ lại (~12 KB/trang) nên tìm
//   lần sau gần như tức thì; khung tô kết quả chỉ tính lại cho các trang có kết quả.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pdf/document.hpp"
#include "text/types.hpp"
#include "util/concurrent_queue.hpp"
#include "util/thread_pool.hpp"

namespace pdfread::pdf {

class RootContext;

struct SearchHit {
    int page = 0;
    text::Span span;               // vị trí trong văn bản trang (PageText)
    std::vector<text::Box> rects;  // khung tô (point, gốc trên-trái trang), mỗi dòng một khung
    std::string snippet;           // đoạn trích UTF-8 quanh kết quả
};

struct SearchUpdate {
    std::uint64_t ticket = 0;
    std::vector<SearchHit> hits;  // kết quả mới (theo thứ tự trang)
    int pages_done = 0;
    int pages_total = 0;
    bool finished = false;
};

class SearchService {
public:
    SearchService(const RootContext& root, std::function<void()> wake);
    ~SearchService();
    SearchService(const SearchService&) = delete;
    SearchService& operator=(const SearchService&) = delete;

    void set_document(std::shared_ptr<Document> doc);
    // `needles`: các cụm đã text::normalize_query (OR). Trả mã lần tìm.
    std::uint64_t start(std::vector<std::u32string> needles);
    void cancel() { ++ticket_; }
    [[nodiscard]] std::vector<SearchUpdate> take_updates() { return updates_.drain(); }

private:
    void run(std::uint64_t ticket, const std::shared_ptr<Document>& doc, const std::vector<std::u32string>& needles);

    std::function<void()> wake_;
    std::shared_ptr<Document> doc_;  // chỉ UI thread
    std::atomic<std::uint64_t> ticket_{0};
    ConcurrentQueue<SearchUpdate> updates_;

    // Chỉ luồng tìm kiếm chạm tới.
    std::uint64_t cache_doc_ = 0;
    std::vector<std::optional<std::u32string>> folded_;

    ThreadPool pool_;  // khai báo cuối: join trước khi các thành viên khác bị hủy
};

}  // namespace pdfread::pdf
