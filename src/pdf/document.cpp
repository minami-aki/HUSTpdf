#include "pdf/document.hpp"

#include <atomic>
#include <format>
#include <utility>

#include "pdf/context.hpp"
#include "pdf/mupdf.hpp"
#include "util/log.hpp"
#include "util/timer.hpp"
#include "util/utf8.hpp"

namespace pdfread::pdf {
namespace {

std::atomic<std::uint64_t> g_next_document_id{1};

[[noreturn]] void fail(const std::string& what, const std::string& detail) {
    throw Error(detail.empty() ? what : std::format("{}: {}", what, detail));
}

}  // namespace

// --- DisplayList / Pixmap -----------------------------------------------------

DisplayList::~DisplayList() {
    if (list_ != nullptr) fz_drop_display_list(thread_ctx(), list_);
}

Pixmap::~Pixmap() {
    if (pix_ != nullptr) fz_drop_pixmap(thread_ctx(), pix_);
}

std::unique_ptr<Pixmap> Pixmap::create_rgb(int width, int height) {
    fz_context* c = require_ctx();
    fz_pixmap* pix = nullptr;
    std::string err;
    fz_var(pix);
    fz_try(c) {
        pix = fz_new_pixmap(c, fz_device_rgb(c), width, height, nullptr, 0);
        fz_clear_pixmap_with_value(c, pix, 0xff);
    }
    fz_catch(c) {
        err = fz_caught_message(c);
        fz_report_error(c);
        fz_drop_pixmap(c, pix);
        pix = nullptr;
    }
    if (pix == nullptr) fail("Không tạo được pixmap", err);
    return std::make_unique<Pixmap>(pix);
}

int Pixmap::width() const noexcept { return fz_pixmap_width(thread_ctx(), pix_); }
int Pixmap::height() const noexcept { return fz_pixmap_height(thread_ctx(), pix_); }
std::ptrdiff_t Pixmap::stride() const noexcept { return fz_pixmap_stride(thread_ctx(), pix_); }
int Pixmap::components() const noexcept { return fz_pixmap_components(thread_ctx(), pix_); }
const unsigned char* Pixmap::samples() const noexcept { return fz_pixmap_samples(thread_ctx(), pix_); }
unsigned char* Pixmap::samples() noexcept { return fz_pixmap_samples(thread_ctx(), pix_); }

// --- Document -----------------------------------------------------------------

Document::Document(fz_document* doc, std::filesystem::path path, std::vector<PageBox> pages)
    : doc_(doc), path_(std::move(path)), pages_(std::move(pages)), id_(g_next_document_id++) {}

Document::~Document() {
    {
        std::lock_guard lock(cache_mutex_);
        cache_.clear();
        lru_.clear();
    }
    if (doc_ != nullptr) fz_drop_document(thread_ctx(), doc_);
}

std::shared_ptr<Document> Document::open(const std::filesystem::path& path) {
    fz_context* c = require_ctx();
    ScopedTimer timer("open document");
    const std::string utf8 = path_to_utf8(path);

    fz_document* doc = nullptr;
    int needs_password = 0;
    int count = 0;
    std::string err;
    fz_var(doc);
    fz_try(c) {
        doc = fz_open_document(c, utf8.c_str());
        needs_password = fz_needs_password(c, doc);
        if (!needs_password) count = fz_count_pages(c, doc);
    }
    fz_catch(c) {
        err = fz_caught_message(c);
        fz_report_error(c);
        fz_drop_document(c, doc);
        doc = nullptr;
    }
    if (doc == nullptr) fail("Không mở được tài liệu", err);
    if (needs_password) {
        fz_drop_document(c, doc);
        fail("Tài liệu được bảo vệ bằng mật khẩu (phiên bản này chưa hỗ trợ)", {});
    }
    if (count <= 0) {
        fz_drop_document(c, doc);
        fail("Tài liệu không có trang nào", {});
    }

    // Khung trang: với PDF đọc thẳng từ page object (không cần nạp trang, nhanh với
    // tài liệu hàng nghìn trang); định dạng khác thì nạp từng trang.
    std::vector<PageBox> pages(static_cast<std::size_t>(count));
    PageBox* out = pages.data();  // ghi qua con trỏ: không gọi gì có thể ném exception C++ trong fz_try
    fz_page* page = nullptr;
    bool ok = true;
    fz_var(page);
    fz_try(c) {
        pdf_document* pdf = pdf_specifics(c, doc);
        for (int i = 0; i < count; ++i) {
            fz_rect r;
            if (pdf != nullptr) {
                pdf_obj* obj = pdf_lookup_page_obj(c, pdf, i);
                fz_rect box;
                fz_matrix ctm;
                pdf_page_obj_transform(c, obj, &box, &ctm);
                r = fz_transform_rect(box, ctm);
            } else {
                page = fz_load_page(c, doc, i);
                r = fz_bound_page(c, page);
                fz_drop_page(c, page);
                page = nullptr;
            }
            out[i] = PageBox{r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0};
        }
    }
    fz_catch(c) {
        err = fz_caught_message(c);
        fz_report_error(c);
        fz_drop_page(c, page);
        ok = false;
    }
    if (!ok) {
        fz_drop_document(c, doc);
        fail("Không đọc được kích thước trang", err);
    }

    log::info("Đã mở '{}': {} trang", utf8, count);
    return std::shared_ptr<Document>(new Document(doc, path, std::move(pages)));
}

void Document::set_display_list_capacity(std::size_t capacity) {
    std::vector<DisplayListPtr> evicted;  // hủy ngoài khóa
    std::lock_guard lock(cache_mutex_);
    cache_capacity_ = capacity == 0 ? 1 : capacity;
    while (cache_.size() > cache_capacity_) {
        auto it = cache_.find(lru_.back());
        evicted.push_back(std::move(it->second.first));
        cache_.erase(it);
        lru_.pop_back();
    }
}

DisplayListPtr Document::display_list(int index) {
    if (index < 0 || index >= page_count()) throw Error(std::format("Trang {} không tồn tại", index + 1));
    {
        std::lock_guard lock(cache_mutex_);
        if (auto it = cache_.find(index); it != cache_.end()) {
            lru_.splice(lru_.begin(), lru_, it->second.second);
            return it->second.first;
        }
    }

    DisplayListPtr built = build_display_list(index);

    std::vector<DisplayListPtr> evicted;
    std::lock_guard lock(cache_mutex_);
    if (auto it = cache_.find(index); it != cache_.end()) return it->second.first;  // luồng khác vừa tạo
    lru_.push_front(index);
    cache_.emplace(index, std::make_pair(built, lru_.begin()));
    while (cache_.size() > cache_capacity_) {
        auto old = cache_.find(lru_.back());
        evicted.push_back(std::move(old->second.first));
        cache_.erase(old);
        lru_.pop_back();
    }
    return built;
}

DisplayListPtr Document::build_display_list(int index) {
    fz_context* c = require_ctx();

    fz_page* page = nullptr;
    fz_display_list* list = nullptr;
    std::string err;
    {
        std::lock_guard lock(doc_mutex_);
        // Luồng khác có thể vừa dựng xong trang này trong lúc ta chờ khóa.
        {
            std::lock_guard cache_lock(cache_mutex_);
            if (auto it = cache_.find(index); it != cache_.end()) return it->second.first;
        }
        ScopedTimer timer(std::format("display list p{}", index + 1));
        fz_var(page);
        fz_var(list);
        fz_try(c) {
            page = fz_load_page(c, doc_, index);
            list = fz_new_display_list_from_page(c, page);
        }
        fz_always(c) { fz_drop_page(c, page); }
        fz_catch(c) {
            err = fz_caught_message(c);
            fz_report_error(c);
            fz_drop_display_list(c, list);
            list = nullptr;
        }
    }
    if (list == nullptr) fail(std::format("Không đọc được nội dung trang {}", index + 1), err);
    return std::make_shared<const DisplayList>(list);
}

// --- render / PNG -----------------------------------------------------------------

std::unique_ptr<Pixmap> render(const DisplayList& list, const PageBox& box, float scale, const IRect& rect) {
    fz_context* c = require_ctx();
    const fz_irect bbox{rect.x0, rect.y0, rect.x1, rect.y1};
    const fz_matrix ctm = fz_concat(fz_translate(-box.x0, -box.y0), fz_scale(scale, scale));

    fz_pixmap* pix = nullptr;
    fz_device* dev = nullptr;
    std::string err;
    fz_var(pix);
    fz_var(dev);
    fz_try(c) {
        pix = fz_new_pixmap_with_bbox(c, fz_device_rgb(c), bbox, nullptr, 0);
        fz_clear_pixmap_with_value(c, pix, 0xff);
        dev = fz_new_draw_device(c, fz_identity, pix);
        fz_run_display_list(c, list.get(), dev, ctm, fz_rect_from_irect(bbox), nullptr);
        fz_close_device(c, dev);
    }
    fz_always(c) { fz_drop_device(c, dev); }
    fz_catch(c) {
        err = fz_caught_message(c);
        fz_report_error(c);
        fz_drop_pixmap(c, pix);
        pix = nullptr;
    }
    if (pix == nullptr) fail("Không render được trang", err);
    return std::make_unique<Pixmap>(pix);
}

void save_png(const Pixmap& pixmap, const std::filesystem::path& path) {
    fz_context* c = require_ctx();
    const std::string utf8 = path_to_utf8(path);
    std::string err;
    bool ok = true;
    fz_try(c) { fz_save_pixmap_as_png(c, pixmap.get(), utf8.c_str()); }
    fz_catch(c) {
        err = fz_caught_message(c);
        fz_report_error(c);
        ok = false;
    }
    if (!ok) fail("Không ghi được PNG", err);
}

}  // namespace pdfread::pdf
