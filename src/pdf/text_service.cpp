#include "pdf/text_service.hpp"

#include <exception>

#include "pdf/context.hpp"
#include "util/log.hpp"

namespace pdfread::pdf {

TextService::TextService(const RootContext& root, std::function<void()> on_result)
    : on_result_(std::move(on_result)),
      pool_(ThreadPool::Options{
          .threads = 1,
          .name = "text",
          .on_start = [&root](std::size_t) { attach_thread(root); },
          .on_stop = [](std::size_t) { detach_thread(); },
      }) {}

TextService::~TextService() {
    {
        std::lock_guard lock(mutex_);
        pending_.clear();
    }
    pool_.shutdown();
}

void TextService::set_document(std::shared_ptr<Document> doc) {
    std::shared_ptr<Document> old;
    {
        std::lock_guard lock(mutex_);
        old = std::move(doc_);
        doc_ = std::move(doc);
        pending_.clear();
    }
    (void)results_.drain();
}

void TextService::request(const std::vector<int>& pages) {
    std::size_t to_post = 0;
    {
        std::lock_guard lock(mutex_);
        pending_.clear();
        for (int p : pages) {
            if (!in_flight_.contains(p)) pending_.push_back(p);
        }
        if (pending_.size() > queued_jobs_) {
            to_post = pending_.size() - queued_jobs_;
            queued_jobs_ += to_post;
        }
    }
    for (std::size_t i = 0; i < to_post; ++i) pool_.post([this] { run_one(); });
}

bool TextService::busy() const {
    std::lock_guard lock(mutex_);
    return !pending_.empty() || !in_flight_.empty();
}

void TextService::run_one() {
    int page = 0;
    std::shared_ptr<Document> doc;
    {
        std::lock_guard lock(mutex_);
        --queued_jobs_;
        if (pending_.empty() || !doc_) return;
        page = pending_.front();
        pending_.pop_front();
        in_flight_.insert(page);
        doc = doc_;
    }

    TextResult result{.doc = doc->id(), .page = page, .text = nullptr, .error = {}};
    try {
        DisplayListPtr list = doc->display_list(page);
        result.text = TextPage::build(*list, doc->page(page));
    } catch (const std::exception& e) {
        result.error = e.what();
        log::warn("trích text trang {} lỗi: {}", page + 1, e.what());
    }

    {
        std::lock_guard lock(mutex_);
        in_flight_.erase(page);
    }
    results_.push(std::move(result));
    if (on_result_) on_result_();
}

}  // namespace pdfread::pdf
