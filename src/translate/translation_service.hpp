#pragma once
// Luồng suy luận duy nhất: nạp mô hình một lần, xử lý yêu cầu dịch, stream token về UI.
//
// - submit() HỦY yêu cầu đang chạy và thay yêu cầu đang chờ: người dùng chỉ quan tâm
//   vùng chọn mới nhất.
// - Kết quả trả về UI dạng sự kiện (take_events() mỗi frame); mỗi khi có sự kiện mới,
//   callback `wake` được gọi (glfwPostEmptyEvent).
// - Translator được tạo qua factory để test thay bằng backend giả.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "translate/translator.hpp"
#include "util/concurrent_queue.hpp"

namespace pdfread::translate {

enum class ModelState { None, Loading, Ready, Failed };

struct TranslationEvent {
    enum class Kind {
        ModelLoading,  // text: tên file mô hình
        ModelReady,    // text: tên mô hình
        ModelFailed,   // text: thông báo lỗi
        Started,       // bắt đầu xử lý yêu cầu `request_id`
        Token,         // text: đoạn bản dịch mới
        Done,          // stats
        Cancelled,
        Failed,        // text: thông báo lỗi
    };
    Kind kind = Kind::Token;
    std::uint64_t request_id = 0;
    std::string text;
    TranslationStats stats;
};

using TranslatorFactory = std::function<std::unique_ptr<Translator>(const ModelConfig&)>;

class TranslationService {
public:
    TranslationService(TranslatorFactory factory, std::function<void()> wake);
    ~TranslationService();

    TranslationService(const TranslationService&) = delete;
    TranslationService& operator=(const TranslationService&) = delete;

    // Nạp (hoặc đổi) mô hình trên luồng suy luận.
    void load_model(ModelConfig config);
    // Gửi yêu cầu mới; hủy yêu cầu cũ. Trả mã yêu cầu (tăng dần).
    std::uint64_t submit(TranslateRequest request);
    // Hủy yêu cầu đang chạy và đang chờ.
    void cancel();

    [[nodiscard]] std::vector<TranslationEvent> take_events() { return events_.drain(); }
    [[nodiscard]] ModelState model_state() const noexcept { return state_.load(); }
    [[nodiscard]] std::string model_name() const;

private:
    struct Job {
        std::uint64_t id = 0;
        TranslateRequest request;
        std::shared_ptr<std::atomic<bool>> cancel;
    };

    void run();
    void emit(TranslationEvent e);
    void do_load(const ModelConfig& config);
    void do_translate(Job& job);

    TranslatorFactory factory_;
    std::function<void()> wake_;
    ConcurrentQueue<TranslationEvent> events_;
    std::atomic<ModelState> state_{ModelState::None};

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<ModelConfig> pending_load_;
    std::optional<Job> pending_job_;
    std::shared_ptr<std::atomic<bool>> running_cancel_;  // cờ hủy của yêu cầu đang chạy
    std::uint64_t next_id_ = 1;
    std::string model_name_;
    bool stop_ = false;

    std::unique_ptr<Translator> translator_;  // chỉ luồng suy luận chạm vào
    std::thread thread_;                      // khai báo cuối: khởi động sau cùng
};

}  // namespace pdfread::translate
