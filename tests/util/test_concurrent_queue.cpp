// Tên TEST_CASE để ASCII: tên có dấu dễ hỏng khi ctest/catch_discover_tests
// truyền qua dòng lệnh Windows.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "util/concurrent_queue.hpp"

using pdfread::ConcurrentQueue;
using namespace std::chrono_literals;

TEST_CASE("ConcurrentQueue keeps FIFO order", "[util][queue]") {
    ConcurrentQueue<int> q;
    CHECK_FALSE(q.try_pop().has_value());

    for (int i = 0; i < 5; ++i) REQUIRE(q.push(i));
    CHECK(q.size() == 5);
    for (int i = 0; i < 5; ++i) CHECK(q.try_pop() == i);
    CHECK(q.empty());
}

TEST_CASE("ConcurrentQueue supports move-only types", "[util][queue]") {
    ConcurrentQueue<std::unique_ptr<std::string>> q;
    q.push(std::make_unique<std::string>("abc"));
    auto v = q.try_pop();
    REQUIRE(v.has_value());
    CHECK(**v == "abc");
}

TEST_CASE("ConcurrentQueue after close rejects push but drains remaining items", "[util][queue]") {
    ConcurrentQueue<int> q;
    q.push(1);
    q.push(2);
    q.close();

    CHECK(q.closed());
    CHECK_FALSE(q.push(3));
    CHECK(q.wait_pop() == 1);
    CHECK(q.wait_pop() == 2);
    CHECK_FALSE(q.wait_pop().has_value());  // không chặn
}

TEST_CASE("ConcurrentQueue wait_pop is woken by close", "[util][queue]") {
    ConcurrentQueue<int> q;
    std::optional<int> result{42};
    std::thread consumer([&] { result = q.wait_pop(); });
    std::this_thread::sleep_for(10ms);
    q.close();
    consumer.join();
    CHECK_FALSE(result.has_value());
}

TEST_CASE("ConcurrentQueue wait_pop_for returns nullopt on timeout", "[util][queue]") {
    ConcurrentQueue<int> q;
    CHECK_FALSE(q.wait_pop_for(5ms).has_value());
    q.push(7);
    CHECK(q.wait_pop_for(5ms) == 7);
}

TEST_CASE("ConcurrentQueue drain returns everything in order", "[util][queue]") {
    ConcurrentQueue<int> q;
    for (int i = 0; i < 4; ++i) q.push(i);
    const auto all = q.drain();
    CHECK(all == std::vector<int>{0, 1, 2, 3});
    CHECK(q.empty());
    CHECK(q.drain().empty());
}

TEST_CASE("ConcurrentQueue erase_if removes pending items", "[util][queue]") {
    ConcurrentQueue<int> q;
    for (int i = 0; i < 10; ++i) q.push(i);
    CHECK(q.erase_if([](int v) { return v % 2 == 0; }) == 5);
    CHECK(q.drain() == std::vector<int>{1, 3, 5, 7, 9});
}

TEST_CASE("ConcurrentQueue MPMC loses no items", "[util][queue]") {
    constexpr int kProducers = 4;
    constexpr int kConsumers = 4;
    constexpr int kPerProducer = 10000;

    ConcurrentQueue<int> q;
    std::atomic<std::int64_t> sum{0};
    std::atomic<int> count{0};

    std::vector<std::thread> consumers;
    for (int c = 0; c < kConsumers; ++c) {
        consumers.emplace_back([&] {
            while (auto v = q.wait_pop()) {
                sum += *v;
                ++count;
            }
        });
    }
    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&] {
            for (int i = 1; i <= kPerProducer; ++i) q.push(i);
        });
    }
    for (auto& t : producers) t.join();
    q.close();
    for (auto& t : consumers) t.join();

    constexpr std::int64_t kExpectedSum =
        static_cast<std::int64_t>(kProducers) * kPerProducer * (kPerProducer + 1) / 2;
    CHECK(count == kProducers * kPerProducer);
    CHECK(sum == kExpectedSum);
}
