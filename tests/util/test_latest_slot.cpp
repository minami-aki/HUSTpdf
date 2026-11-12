#include <chrono>
#include <optional>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "util/latest_slot.hpp"

using pdfread::LatestSlot;
using namespace std::chrono_literals;

TEST_CASE("LatestSlot take on empty slot returns nullopt", "[util][slot]") {
    LatestSlot<int> slot;
    CHECK_FALSE(slot.has_value());
    CHECK_FALSE(slot.take().has_value());
}

TEST_CASE("LatestSlot put overwrites, only the newest value is kept", "[util][slot]") {
    LatestSlot<std::string> slot;
    REQUIRE(slot.put("vùng chọn 1"));
    REQUIRE(slot.put("vùng chọn 2"));
    REQUIRE(slot.put("vùng chọn 3"));
    CHECK(slot.take() == "vùng chọn 3");
    CHECK_FALSE(slot.take().has_value());
}

TEST_CASE("LatestSlot wait_take blocks until a value arrives", "[util][slot]") {
    LatestSlot<int> slot;
    std::optional<int> got;
    std::thread consumer([&] { got = slot.wait_take(); });
    std::this_thread::sleep_for(10ms);
    slot.put(5);
    consumer.join();
    CHECK(got == 5);
}

TEST_CASE("LatestSlot wait_take_for returns nullopt on timeout", "[util][slot]") {
    LatestSlot<int> slot;
    CHECK_FALSE(slot.wait_take_for(5ms).has_value());
}

TEST_CASE("LatestSlot after close rejects put but keeps pending value", "[util][slot]") {
    LatestSlot<int> slot;
    slot.put(1);
    slot.close();
    CHECK(slot.closed());
    CHECK_FALSE(slot.put(2));
    CHECK(slot.wait_take() == 1);
    CHECK_FALSE(slot.wait_take().has_value());  // không chặn
}

TEST_CASE("LatestSlot close wakes a waiting thread", "[util][slot]") {
    LatestSlot<int> slot;
    std::optional<int> got{42};
    std::thread consumer([&] { got = slot.wait_take(); });
    std::this_thread::sleep_for(10ms);
    slot.close();
    consumer.join();
    CHECK_FALSE(got.has_value());
}
