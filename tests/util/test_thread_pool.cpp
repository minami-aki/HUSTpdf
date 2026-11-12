#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "util/log.hpp"
#include "util/thread_pool.hpp"

using pdfread::ThreadPool;
using namespace std::chrono_literals;

TEST_CASE("ThreadPool runs every submitted task", "[util][pool]") {
    ThreadPool pool(4, "test");
    CHECK(pool.size() == 4);

    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 1000; ++i) futures.push_back(pool.submit([&] { ++counter; }));
    for (auto& f : futures) f.get();
    CHECK(counter == 1000);
}

TEST_CASE("ThreadPool submit returns results and propagates exceptions", "[util][pool]") {
    ThreadPool pool(2, "test");
    auto ok = pool.submit([] { return 6 * 7; });
    auto bad = pool.submit([]() -> int { throw std::runtime_error("lỗi"); });
    CHECK(ok.get() == 42);
    CHECK_THROWS_AS(bad.get(), std::runtime_error);
}

TEST_CASE("ThreadPool throwing posted task does not kill the worker", "[util][pool]") {
    pdfread::log::set_stderr_enabled(false);  // exception này là cố ý, không in ra
    ThreadPool pool(1, "test");
    REQUIRE(pool.post([] { throw std::runtime_error("cố ý"); }));
    auto after = pool.submit([] { return 1; });
    CHECK(after.get() == 1);
    pool.shutdown();
    pdfread::log::set_stderr_enabled(true);
}

TEST_CASE("ThreadPool calls on_start and on_stop once per thread", "[util][pool]") {
    std::mutex m;
    std::set<std::size_t> started;
    std::set<std::size_t> stopped;
    {
        ThreadPool pool(ThreadPool::Options{
            .threads = 3,
            .name = "hook",
            .on_start = [&](std::size_t i) { std::lock_guard l(m); started.insert(i); },
            .on_stop = [&](std::size_t i) { std::lock_guard l(m); stopped.insert(i); },
        });
        std::vector<std::future<void>> fs;
        for (int i = 0; i < 30; ++i) fs.push_back(pool.submit([] { std::this_thread::sleep_for(1ms); }));
        for (auto& f : fs) f.get();
    }  // hủy pool: mọi luồng đều chạy on_start (trước task đầu) và on_stop (khi thoát)
    CHECK(started == std::set<std::size_t>{0, 1, 2});
    CHECK(stopped == std::set<std::size_t>{0, 1, 2});
}

TEST_CASE("ThreadPool names its threads for the log", "[util][pool]") {
    ThreadPool pool(1, "render");
    auto name = pool.submit([] { return pdfread::log::thread_name(); });
    CHECK(name.get() == "render-0");
}

TEST_CASE("ThreadPool rejects tasks after shutdown", "[util][pool]") {
    ThreadPool pool(2, "test");
    pool.shutdown();
    pool.shutdown();  // gọi lại an toàn
    CHECK_FALSE(pool.post([] {}));
    auto f = pool.submit([] { return 1; });
    CHECK_THROWS_AS(f.get(), std::future_error);
}

TEST_CASE("ThreadPool shutdown drops pending tasks and waits for running ones", "[util][pool]") {
    ThreadPool pool(1, "test");

    std::promise<void> gate;
    std::shared_future<void> gate_open = gate.get_future().share();
    std::atomic<bool> first_started{false};
    std::atomic<bool> first_finished{false};
    std::atomic<int> dropped_ran{0};

    pool.post([&] {
        first_started = true;
        gate_open.wait();
        first_finished = true;
    });
    while (!first_started) std::this_thread::yield();

    for (int i = 0; i < 10; ++i) pool.post([&] { ++dropped_ran; });
    CHECK(pool.pending() == 10);

    std::thread stopper([&] { pool.shutdown(); });
    while (pool.pending() != 0) std::this_thread::yield();  // shutdown đã bỏ hàng đợi
    gate.set_value();
    stopper.join();

    CHECK(first_finished);
    CHECK(dropped_ran == 0);
}
