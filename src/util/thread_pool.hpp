#pragma once
// Thread pool kích thước cố định.
//
// - on_start / on_stop chạy trên từng luồng worker, dùng để tạo/hủy tài nguyên
//   riêng mỗi luồng (ví dụ fz_clone_context / fz_drop_context của MuPDF).
// - shutdown() BỎ các task chưa chạy (app đang đóng thì không cần render nữa),
//   chờ các task đang chạy xong rồi join. Future của task bị bỏ sẽ ném
//   std::future_error (broken_promise) khi get().
// - Không gọi shutdown() / hủy pool từ bên trong một task của chính pool đó.

#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "util/concurrent_queue.hpp"

namespace pdfread {

class ThreadPool {
public:
    using ThreadHook = std::function<void(std::size_t index)>;

    struct Options {
        std::size_t threads = 0;  // 0 = default_thread_count()
        std::string name = "worker";
        ThreadHook on_start;
        ThreadHook on_stop;
    };

    explicit ThreadPool(Options options);
    explicit ThreadPool(std::size_t threads, std::string name = "worker");
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Trả false nếu pool đã dừng. Exception thoát khỏi task được log và bỏ qua.
    bool post(std::function<void()> task);

    // Như post() nhưng trả về future cho kết quả (hoặc exception) của task.
    template <class F>
    [[nodiscard]] auto submit(F&& f) -> std::future<std::invoke_result_t<std::decay_t<F>&>> {
        using R = std::invoke_result_t<std::decay_t<F>&>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        auto future = task->get_future();
        post([task] { (*task)(); });  // pool đã dừng -> task bị hủy -> broken_promise
        return future;
    }

    // Gọi nhiều lần an toàn.
    void shutdown();

    [[nodiscard]] std::size_t size() const noexcept { return thread_count_; }
    [[nodiscard]] std::size_t pending() const { return queue_.size(); }

    // max(1, số luồng phần cứng - 1)
    [[nodiscard]] static std::size_t default_thread_count() noexcept;

private:
    void worker_main(std::size_t index);

    std::string name_;
    ThreadHook on_start_;
    ThreadHook on_stop_;
    std::size_t thread_count_ = 0;
    ConcurrentQueue<std::function<void()>> queue_;
    std::vector<std::thread> threads_;
    std::mutex shutdown_mutex_;
};

}  // namespace pdfread
