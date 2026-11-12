#include "util/thread_pool.hpp"

#include <algorithm>
#include <exception>
#include <format>

#include "util/log.hpp"
#include "util/thread_name.hpp"

namespace pdfread {

ThreadPool::ThreadPool(Options options)
    : name_(std::move(options.name)),
      on_start_(std::move(options.on_start)),
      on_stop_(std::move(options.on_stop)),
      thread_count_(options.threads == 0 ? default_thread_count() : options.threads) {
    threads_.reserve(thread_count_);
    try {
        for (std::size_t i = 0; i < thread_count_; ++i) {
            threads_.emplace_back([this, i] { worker_main(i); });
        }
    } catch (...) {
        shutdown();
        throw;
    }
}

ThreadPool::ThreadPool(std::size_t threads, std::string name)
    : ThreadPool(Options{.threads = threads, .name = std::move(name), .on_start = {}, .on_stop = {}}) {}

ThreadPool::~ThreadPool() { shutdown(); }

bool ThreadPool::post(std::function<void()> task) { return queue_.push(std::move(task)); }

void ThreadPool::shutdown() {
    std::lock_guard lock(shutdown_mutex_);
    queue_.close();
    queue_.clear();
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
}

std::size_t ThreadPool::default_thread_count() noexcept {
    const unsigned hw = std::thread::hardware_concurrency();
    return hw > 1 ? hw - 1 : 1;
}

void ThreadPool::worker_main(std::size_t index) {
    name_current_thread(std::format("{}-{}", name_, index));

    if (on_start_) {
        try {
            on_start_(index);
        } catch (const std::exception& e) {
            log::error("thread pool '{}': on_start threw: {}", name_, e.what());
        } catch (...) {
            log::error("thread pool '{}': on_start threw an unknown exception", name_);
        }
    }

    while (auto task = queue_.wait_pop()) {
        try {
            (*task)();
        } catch (const std::exception& e) {
            log::error("thread pool '{}': task threw: {}", name_, e.what());
        } catch (...) {
            log::error("thread pool '{}': task threw an unknown exception", name_);
        }
    }

    if (on_stop_) {
        try {
            on_stop_(index);
        } catch (...) {
            log::error("thread pool '{}': on_stop threw", name_);
        }
    }
}

}  // namespace pdfread
