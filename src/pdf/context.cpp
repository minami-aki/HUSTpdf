#include "pdf/context.hpp"

#include <array>
#include <mutex>
#include <string>

#include "pdf/mupdf.hpp"
#include "util/log.hpp"

namespace pdfread::pdf {

struct RootContext::Locks {
    std::array<std::mutex, FZ_LOCK_MAX> mutexes;
    fz_locks_context fz{};

    static void lock(void* user, int index) { static_cast<Locks*>(user)->mutexes[static_cast<std::size_t>(index)].lock(); }
    static void unlock(void* user, int index) { static_cast<Locks*>(user)->mutexes[static_cast<std::size_t>(index)].unlock(); }
};

namespace {

thread_local fz_context* t_ctx = nullptr;
std::mutex g_clone_mutex;  // clone từ root một luồng một lúc cho chắc chắn

void on_mupdf_warning(void*, const char* message) { log::debug("mupdf warning: {}", message); }
void on_mupdf_error(void*, const char* message) { log::debug("mupdf error: {}", message); }

}  // namespace

RootContext::RootContext(std::size_t store_bytes) : locks_(std::make_unique<Locks>()) {
    locks_->fz.user = locks_.get();
    locks_->fz.lock = &Locks::lock;
    locks_->fz.unlock = &Locks::unlock;

    ctx_ = fz_new_context(nullptr, &locks_->fz, store_bytes);
    if (ctx_ == nullptr) throw Error("Không tạo được MuPDF context");

    fz_set_warning_callback(ctx_, on_mupdf_warning, nullptr);
    fz_set_error_callback(ctx_, on_mupdf_error, nullptr);

    bool ok = true;
    fz_try(ctx_) { fz_register_document_handlers(ctx_); }
    fz_catch(ctx_) {
        fz_report_error(ctx_);
        ok = false;
    }
    if (!ok) {
        fz_drop_context(ctx_);
        ctx_ = nullptr;
        throw Error("Không đăng ký được trình đọc tài liệu của MuPDF");
    }
}

RootContext::~RootContext() {
    if (ctx_ != nullptr) fz_drop_context(ctx_);
}

void attach_thread(const RootContext& root) {
    if (t_ctx != nullptr) return;
    std::lock_guard lock(g_clone_mutex);
    t_ctx = fz_clone_context(root.get());
    if (t_ctx == nullptr) throw Error("Không clone được MuPDF context");
}

void detach_thread() noexcept {
    if (t_ctx == nullptr) return;
    fz_drop_context(t_ctx);
    t_ctx = nullptr;
}

fz_context* thread_ctx() noexcept { return t_ctx; }

fz_context* require_ctx() {
    if (t_ctx == nullptr) throw Error("Luồng hiện tại chưa gắn MuPDF context (thiếu attach_thread)");
    return t_ctx;
}

}  // namespace pdfread::pdf
