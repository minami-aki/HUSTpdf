// TranslationService với backend giả: thứ tự sự kiện, streaming, hủy yêu cầu cũ.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "translate/llama_translator.hpp"
#include "translate/translation_service.hpp"
#include "util/log.hpp"
#include "util/utf8.hpp"

using namespace pdfread::translate;
using namespace std::chrono_literals;
using Kind = TranslationEvent::Kind;

namespace {

// Trả lại "[vi] <text>" từng từ một, mỗi từ cách nhau `delay`; tôn trọng cờ hủy.
class FakeTranslator final : public Translator {
public:
    explicit FakeTranslator(std::chrono::milliseconds delay) : delay_(delay) {}
    bool supports_glossary() const override { return false; }
    bool supports_context() const override { return false; }
    std::string name() const override { return "fake"; }
    bool translate(const TranslateRequest& r, const std::function<void(std::string_view)>& on_token,
                   const std::atomic<bool>& cancel, TranslationStats* stats) override {
        if (r.text == "boom") throw TranslateError("hỏng");
        std::string words = "[vi] " + r.text;
        std::size_t pos = 0;
        int n = 0;
        while (pos < words.size()) {
            if (cancel.load()) return false;
            std::this_thread::sleep_for(delay_);
            const std::size_t space = words.find(' ', pos);
            const std::size_t end = space == std::string::npos ? words.size() : space + 1;
            on_token(std::string_view(words).substr(pos, end - pos));
            pos = end;
            ++n;
        }
        if (stats != nullptr) stats->output_tokens = n;
        return true;
    }

private:
    std::chrono::milliseconds delay_;
};

// Gom sự kiện và chờ điều kiện.
struct Collector {
    std::mutex m;
    std::condition_variable cv;
    std::vector<TranslationEvent> events;

    void wake() { cv.notify_all(); }
    template <class Pred>
    bool wait(TranslationService& s, Pred pred, std::chrono::milliseconds timeout = 5s) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::unique_lock lock(m);
        for (;;) {
            for (auto& e : s.take_events()) events.push_back(std::move(e));
            if (pred()) return true;
            if (cv.wait_until(lock, deadline) == std::cv_status::timeout) {
                for (auto& e : s.take_events()) events.push_back(std::move(e));
                return pred();
            }
        }
    }
    bool has(Kind k, std::uint64_t id) {
        for (const auto& e : events) {
            if (e.kind == k && e.request_id == id) return true;
        }
        return false;
    }
    std::string text_of(std::uint64_t id) {
        std::string out;
        for (const auto& e : events) {
            if (e.kind == Kind::Token && e.request_id == id) out += e.text;
        }
        return out;
    }
};

std::string env(const char* name) {
#ifdef _MSC_VER
    char* buf = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&buf, &len, name) != 0 || buf == nullptr) return {};
    std::string value(buf);
    std::free(buf);
    return value;
#else
    const char* v = std::getenv(name);
    return v != nullptr ? v : "";
#endif
}

TranslatorFactory fake_factory(std::chrono::milliseconds delay) {
    return [delay](const ModelConfig& c) -> std::unique_ptr<Translator> {
        if (c.model_path == "missing.gguf") throw TranslateError("không có file");
        return std::make_unique<FakeTranslator>(delay);
    };
}

}  // namespace

TEST_CASE("TranslationService loads a model then streams a translation", "[translate][service]") {
    Collector c;
    TranslationService s(fake_factory(1ms), [&] { c.wake(); });
    CHECK(s.model_state() == ModelState::None);

    s.load_model({.model_path = "fake.gguf"});
    const auto id = s.submit({.text = "hello world", .context = {}, .terms = {}, .domains = {}});
    REQUIRE(c.wait(s, [&] { return c.has(Kind::Done, id); }));
    CHECK(s.model_state() == ModelState::Ready);
    CHECK(s.model_name() == "fake");
    CHECK(c.text_of(id) == "[vi] hello world");

    // Thứ tự: ModelLoading, ModelReady, Started, Token..., Done.
    std::vector<Kind> kinds;
    for (const auto& e : c.events) {
        if (kinds.empty() || kinds.back() != e.kind) kinds.push_back(e.kind);
    }
    CHECK(kinds == std::vector<Kind>{Kind::ModelLoading, Kind::ModelReady, Kind::Started, Kind::Token, Kind::Done});
}

TEST_CASE("TranslationService cancels the running request when a new one arrives", "[translate][service]") {
    Collector c;
    TranslationService s(fake_factory(30ms), [&] { c.wake(); });
    s.load_model({.model_path = "fake.gguf"});

    const auto first = s.submit({.text = "one two three four five six seven eight nine ten", .context = {}, .terms = {}, .domains = {}});
    REQUIRE(c.wait(s, [&] { return c.has(Kind::Token, first); }));
    const auto second = s.submit({.text = "second", .context = {}, .terms = {}, .domains = {}});
    REQUIRE(c.wait(s, [&] { return c.has(Kind::Done, second); }));

    CHECK(second > first);
    CHECK(c.has(Kind::Cancelled, first));
    CHECK_FALSE(c.has(Kind::Done, first));
    CHECK(c.text_of(second) == "[vi] second");
}

TEST_CASE("TranslationService replaces a pending request that never started", "[translate][service]") {
    Collector c;
    TranslationService s(fake_factory(20ms), [&] { c.wake(); });
    s.load_model({.model_path = "fake.gguf"});
    const auto a = s.submit({.text = "a b c d e f", .context = {}, .terms = {}, .domains = {}});
    const auto b = s.submit({.text = "b", .context = {}, .terms = {}, .domains = {}});
    const auto d = s.submit({.text = "d", .context = {}, .terms = {}, .domains = {}});
    REQUIRE(c.wait(s, [&] { return c.has(Kind::Done, d); }));
    CHECK_FALSE(c.has(Kind::Done, a));
    CHECK_FALSE(c.has(Kind::Done, b));
    CHECK(c.has(Kind::Cancelled, b));
}

TEST_CASE("TranslationService reports model and translation failures", "[translate][service]") {
    pdfread::log::set_stderr_enabled(false);
    Collector c;
    TranslationService s(fake_factory(1ms), [&] { c.wake(); });

    const auto no_model = s.submit({.text = "x", .context = {}, .terms = {}, .domains = {}});
    REQUIRE(c.wait(s, [&] { return c.has(Kind::Failed, no_model); }));

    s.load_model({.model_path = "missing.gguf"});
    REQUIRE(c.wait(s, [&] { return s.model_state() == ModelState::Failed; }));

    s.load_model({.model_path = "fake.gguf"});
    const auto boom = s.submit({.text = "boom", .context = {}, .terms = {}, .domains = {}});
    REQUIRE(c.wait(s, [&] { return c.has(Kind::Failed, boom); }));
    CHECK(s.model_state() == ModelState::Ready);

    s.cancel();  // không có gì để hủy: không lỗi
    pdfread::log::set_stderr_enabled(true);
}

TEST_CASE("TranslationService destructor stops a long translation promptly", "[translate][service]") {
    const auto start = std::chrono::steady_clock::now();
    {
        Collector c;
        TranslationService s(fake_factory(50ms), [&] { c.wake(); });
        s.load_model({.model_path = "fake.gguf"});
        const auto id = s.submit({.text = "w w w w w w w w w w w w w w w w w w w w w w w w w w w w w w", .context = {}, .terms = {}, .domains = {}});
        REQUIRE(c.wait(s, [&] { return c.has(Kind::Token, id); }));
    }
    CHECK(std::chrono::steady_clock::now() - start < 1s);
}

// Chạy với mô hình thật: đặt biến môi trường PDFREAD_TEST_MODEL=đường dẫn file GGUF rồi
// chạy: pdfread_tests "[model]"
TEST_CASE("LlamaTranslator translates a technical sentence", "[.model]") {
    const std::string path = env("PDFREAD_TEST_MODEL");
    if (path.empty()) SKIP("PDFREAD_TEST_MODEL chưa đặt");
    LlamaTranslator t({.model_path = pdfread::path_from_utf8(path), .threads = 0, .context = 2048, .add_generation_prompt = false});
    const std::atomic<bool> cancel{false};
    std::string out;
    TranslationStats stats;
    REQUIRE(t.translate({.text = "The controller reduces the steady-state error.", .context = {}, .terms = {}, .domains = {}},
                        [&](std::string_view p) { out += p; }, cancel, &stats));
    INFO(out);
    CHECK_FALSE(out.empty());
    CHECK(stats.output_tokens > 3);
    CHECK(pdfread::utf8_length(out) < 400);  // dừng đúng lúc, không lan man
}
