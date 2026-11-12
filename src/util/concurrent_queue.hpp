#pragma once
// Hàng đợi FIFO thread-safe, nhiều producer / nhiều consumer.
//
// Sau close(): push() bị từ chối; consumer vẫn lấy hết phần còn lại,
// sau đó wait_pop() trả về nullopt ngay (để luồng worker thoát vòng lặp).
//
// UI thread dùng drain() mỗi frame để nhận toàn bộ kết quả từ luồng nền.

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace pdfread {

template <class T>
class ConcurrentQueue {
public:
    ConcurrentQueue() = default;
    ConcurrentQueue(const ConcurrentQueue&) = delete;
    ConcurrentQueue& operator=(const ConcurrentQueue&) = delete;

    // Trả false (và bỏ value) nếu hàng đợi đã đóng.
    bool push(T value) {
        {
            std::lock_guard lock(mutex_);
            if (closed_) return false;
            items_.push_back(std::move(value));
        }
        cv_.notify_one();
        return true;
    }

    [[nodiscard]] std::optional<T> try_pop() {
        std::lock_guard lock(mutex_);
        return pop_locked();
    }

    // Chặn tới khi có phần tử hoặc hàng đợi đóng và rỗng (khi đó trả nullopt).
    [[nodiscard]] std::optional<T> wait_pop() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return closed_ || !items_.empty(); });
        return pop_locked();
    }

    template <class Rep, class Period>
    [[nodiscard]] std::optional<T> wait_pop_for(const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, timeout, [this] { return closed_ || !items_.empty(); });
        return pop_locked();
    }

    // Lấy toàn bộ phần tử hiện có theo thứ tự FIFO.
    [[nodiscard]] std::vector<T> drain() {
        std::lock_guard lock(mutex_);
        std::vector<T> out;
        out.reserve(items_.size());
        for (auto& item : items_) out.push_back(std::move(item));
        items_.clear();
        return out;
    }

    // Xóa các phần tử chưa được lấy thỏa pred (ví dụ: task render của trang đã cuộn qua).
    template <class Pred>
    std::size_t erase_if(Pred pred) {
        std::lock_guard lock(mutex_);
        return std::erase_if(items_, pred);
    }

    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    void clear() {
        std::lock_guard lock(mutex_);
        items_.clear();
    }

    [[nodiscard]] bool closed() const {
        std::lock_guard lock(mutex_);
        return closed_;
    }
    [[nodiscard]] std::size_t size() const {
        std::lock_guard lock(mutex_);
        return items_.size();
    }
    [[nodiscard]] bool empty() const { return size() == 0; }

private:
    std::optional<T> pop_locked() {
        if (items_.empty()) return std::nullopt;
        std::optional<T> value(std::move(items_.front()));
        items_.pop_front();
        return value;
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> items_;
    bool closed_ = false;
};

}  // namespace pdfread
