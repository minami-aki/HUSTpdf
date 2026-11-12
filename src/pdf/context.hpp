#pragma once
// Context MuPDF cho môi trường đa luồng.
//
// - RootContext: tạo một lần khi khởi động (có hàm lock), sống lâu hơn mọi luồng.
//   Không dùng trực tiếp để làm việc, chỉ để clone.
// - Mỗi luồng gọi MuPDF phải gắn một context clone: attach_thread() / detach_thread()
//   hoặc ThreadContextScope. Context đó lấy lại bằng thread_ctx().
// - Các lớp RAII trong module pdf giải phóng tài nguyên bằng context của luồng
//   đang chạy destructor, nên luồng đó cũng phải đã attach.

#include <cstddef>
#include <memory>
#include <stdexcept>

struct fz_context;

namespace pdfread::pdf {

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class RootContext {
public:
    static constexpr std::size_t kDefaultStoreBytes = std::size_t{256} << 20;

    explicit RootContext(std::size_t store_bytes = kDefaultStoreBytes);
    ~RootContext();

    RootContext(const RootContext&) = delete;
    RootContext& operator=(const RootContext&) = delete;

    [[nodiscard]] fz_context* get() const noexcept { return ctx_; }

private:
    struct Locks;
    std::unique_ptr<Locks> locks_;
    fz_context* ctx_ = nullptr;
};

// Gắn context clone vào luồng hiện tại (không làm gì nếu đã gắn). Ném Error nếu thất bại.
void attach_thread(const RootContext& root);
// Hủy context clone của luồng hiện tại (nếu có).
void detach_thread() noexcept;
// Context của luồng hiện tại, nullptr nếu chưa attach.
[[nodiscard]] fz_context* thread_ctx() noexcept;
// Như thread_ctx() nhưng ném Error nếu luồng chưa attach.
[[nodiscard]] fz_context* require_ctx();

class ThreadContextScope {
public:
    explicit ThreadContextScope(const RootContext& root) { attach_thread(root); }
    ~ThreadContextScope() { detach_thread(); }
    ThreadContextScope(const ThreadContextScope&) = delete;
    ThreadContextScope& operator=(const ThreadContextScope&) = delete;
};

}  // namespace pdfread::pdf
