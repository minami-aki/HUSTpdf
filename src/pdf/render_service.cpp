#include "pdf/render_service.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <thread>

#include "pdf/context.hpp"
#include "util/log.hpp"
#include "util/timer.hpp"

namespace pdfread::pdf {

RenderService::RenderService(const RootContext& root, std::size_t threads, std::function<void()> on_result)
    : on_result_(std::move(on_result)),
      pool_(ThreadPool::Options{
          .threads = threads == 0 ? default_thread_count() : threads,
          .name = "render",
          .on_start = [&root](std::size_t) { attach_thread(root); },
          .on_stop = [](std::size_t) { detach_thread(); },
      }) {}

RenderService::~RenderService() {
    {
        std::lock_guard lock(mutex_);
        pending_.clear();
    }
    pool_.shutdown();
    // Tài liệu và pixmap còn lại được hủy trên luồng gọi destructor (UI thread, đã attach).
}

std::size_t RenderService::default_thread_count() noexcept {
    const unsigned hw = std::thread::hardware_concurrency();
    return std::clamp<std::size_t>(hw / 2, 1, 3);
}

void RenderService::set_document(std::shared_ptr<Document> doc) {
    std::shared_ptr<Document> old;
    {
        std::lock_guard lock(mutex_);
        old = std::move(doc_);
        doc_ = std::move(doc);
        pending_.clear();
    }
    (void)results_.drain();  // kết quả của tài liệu cũ
}

void RenderService::request(const std::vector<TileKey>& wanted) {
    std::size_t to_post = 0;
    {
        std::lock_guard lock(mutex_);
        pending_.clear();
        for (const TileKey& k : wanted) {
            if (!in_flight_.contains(k)) pending_.push_back(k);
        }
        if (pending_.size() > queued_jobs_) {
            to_post = pending_.size() - queued_jobs_;
            queued_jobs_ += to_post;
        }
    }
    for (std::size_t i = 0; i < to_post; ++i) pool_.post([this] { run_one(); });
}

bool RenderService::busy() const {
    std::lock_guard lock(mutex_);
    return !pending_.empty() || !in_flight_.empty();
}

void RenderService::run_one() {
    TileKey key;
    std::shared_ptr<Document> doc;
    {
        std::lock_guard lock(mutex_);
        --queued_jobs_;
        if (pending_.empty() || !doc_) return;
        key = pending_.front();
        pending_.pop_front();
        if (key.doc != doc_->id()) return;
        in_flight_.insert(key);
        doc = doc_;
    }

    TileResult result{.key = key, .pixmap = nullptr, .error = {}};
    try {
        const PageBox& box = doc->page(key.page);
        const float scale = static_cast<float>(scale_from_q(key.scale_q));
        const TileGrid grid = tile_grid(box.width, box.height, key.scale_q);
        const IRect rect = tile_rect(grid, key.col, key.row);
        DisplayListPtr list = doc->display_list(key.page);
        ScopedTimer timer(std::format("render p{} tile {},{} @{:.3f}", key.page + 1, key.col, key.row, scale));
        result.pixmap = render(*list, box, scale, rect);
        if (key.ink != 0x000000 || key.paper != 0xFFFFFF) tint(*result.pixmap, key.ink, key.paper);
    } catch (const std::exception& e) {
        result.error = e.what();
        log::warn("render p{} tile {},{} lỗi: {}", key.page + 1, key.col, key.row, e.what());
    }

    {
        std::lock_guard lock(mutex_);
        in_flight_.erase(key);
    }
    results_.push(std::move(result));
    if (on_result_) on_result_();
}

}  // namespace pdfread::pdf
