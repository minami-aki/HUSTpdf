#pragma once
// Ô chứa đúng một giá trị: put() ghi đè giá trị chưa được lấy.
//
// Dùng khi chỉ yêu cầu mới nhất có ý nghĩa, ví dụ yêu cầu dịch: người dùng bôi đen
// vùng mới thì yêu cầu cũ chưa xử lý bị thay thế (yêu cầu đang chạy thì hủy riêng
// bằng cờ cancel).
//
// Sau close(): put() bị từ chối; giá trị còn trong ô vẫn lấy được, sau đó
// wait_take() trả về nullopt ngay.

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>

namespace pdfread {

template <class T>
class LatestSlot {
public:
    LatestSlot() = default;
    LatestSlot(const LatestSlot&) = delete;
    LatestSlot& operator=(const LatestSlot&) = delete;

    // Trả false (và bỏ value) nếu đã đóng.
    bool put(T value) {
        {
            std::lock_guard lock(mutex_);
            if (closed_) return false;
            value_ = std::move(value);
        }
        cv_.notify_one();
        return true;
    }

    [[nodiscard]] std::optional<T> take() {
        std::lock_guard lock(mutex_);
        return take_locked();
    }

    [[nodiscard]] std::optional<T> wait_take() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return closed_ || value_.has_value(); });
        return take_locked();
    }

    template <class Rep, class Period>
    [[nodiscard]] std::optional<T> wait_take_for(const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, timeout, [this] { return closed_ || value_.has_value(); });
        return take_locked();
    }

    [[nodiscard]] bool has_value() const {
        std::lock_guard lock(mutex_);
        return value_.has_value();
    }

    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    [[nodiscard]] bool closed() const {
        std::lock_guard lock(mutex_);
        return closed_;
    }

private:
    std::optional<T> take_locked() {
        std::optional<T> out = std::move(value_);
        value_.reset();
        return out;
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<T> value_;
    bool closed_ = false;
};

}  // namespace pdfread
