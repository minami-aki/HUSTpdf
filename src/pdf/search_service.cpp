#include "pdf/search_service.hpp"

#include <chrono>

#include "pdf/context.hpp"
#include "pdf/text_page.hpp"
#include "text/search.hpp"
#include "util/log.hpp"
#include "util/timer.hpp"

namespace pdfread::pdf {

SearchService::SearchService(const RootContext& root, std::function<void()> wake)
    : wake_(std::move(wake)),
      pool_(ThreadPool::Options{
          .threads = 1,
          .name = "search",
          .on_start = [&root](std::size_t) { attach_thread(root); },
          .on_stop = [](std::size_t) { detach_thread(); },
      }) {}

SearchService::~SearchService() {
    ++ticket_;
    pool_.shutdown();
}

void SearchService::set_document(std::shared_ptr<Document> doc) {
    ++ticket_;
    doc_ = std::move(doc);
    (void)updates_.drain();
}

std::uint64_t SearchService::start(std::vector<std::u32string> needles) {
    const std::uint64_t ticket = ++ticket_;
    (void)updates_.drain();
    if (!doc_ || needles.empty()) return ticket;
    pool_.post([this, ticket, doc = doc_, needles = std::move(needles)] { run(ticket, doc, needles); });
    return ticket;
}

void SearchService::run(std::uint64_t ticket, const std::shared_ptr<Document>& doc,
                        const std::vector<std::u32string>& needles) {
    using Clock = std::chrono::steady_clock;
    ScopedTimer timer("search");
    if (cache_doc_ != doc->id()) {
        cache_doc_ = doc->id();
        folded_.assign(static_cast<std::size_t>(doc->page_count()), std::nullopt);
    }
    SearchUpdate pending{.ticket = ticket, .hits = {}, .pages_done = 0, .pages_total = doc->page_count(), .finished = false};
    auto last_push = Clock::now();
    auto push = [&](bool finished) {
        pending.finished = finished;
        updates_.push(pending);
        pending.hits.clear();
        last_push = Clock::now();
        if (wake_) wake_();
    };

    for (int p = 0; p < doc->page_count(); ++p) {
        if (ticket_.load() != ticket) return;  // lần tìm mới đã thay thế
        auto& cached = folded_[static_cast<std::size_t>(p)];
        TextPagePtr page_text;
        try {
            if (!cached) {
                page_text = TextPage::build(*doc->display_list_uncached(p), doc->page(p));
                cached = text::fold_for_search(page_text->page_text().text());
            }
            const std::vector<text::Span> spans = text::find_matches(*cached, needles);
            if (!spans.empty()) {
                if (!page_text) page_text = TextPage::build(*doc->display_list_uncached(p), doc->page(p));
                const text::PageText& pt = page_text->page_text();
                for (const text::Span& s : spans) {
                    pending.hits.push_back({p, s, pt.line_rects(s), text::snippet(pt.text(), s)});
                }
            }
        } catch (const std::exception& e) {
            log::debug("Tìm kiếm: bỏ qua trang {}: {}", p + 1, e.what());
            cached = std::u32string();
        }
        pending.pages_done = p + 1;
        if (!pending.hits.empty() || Clock::now() - last_push > std::chrono::milliseconds(150)) push(false);
    }
    push(true);
}

}  // namespace pdfread::pdf
