#pragma once
// Cửa sổ "Mô hình dịch": chọn file GGUF có sẵn hoặc tải mô hình mặc định từ Hugging Face
// (xem giấy phép, đồng ý, tiến độ, tải tiếp, kiểm tra SHA-256), số luồng suy luận, cache
// bản dịch. Mở tự động ở lần chạy đầu khi chưa có mô hình.

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "download/downloader.hpp"
#include "storage/store.hpp"
#include "translate/translation_service.hpp"

namespace pdfread::ui {

class ModelWindow {
public:
    enum class Action { None, ChooseFile, Downloaded, ApplyThreads, ClearCache };

    struct Info {
        std::filesystem::path model_path;  // mô hình đang dùng / đang nạp (rỗng: chưa có)
        std::string model_status;
        translate::ModelState state = translate::ModelState::None;
        std::filesystem::path download_dir;  // nơi lưu mô hình tải về
        int threads = 0;                     // 0 = mặc định
        int default_threads = 4;
        std::optional<storage::CacheStats> cache;  // nullopt: không có cơ sở dữ liệu
    };

    explicit ModelWindow(std::function<void()> wake);
    ~ModelWindow();
    ModelWindow(const ModelWindow&) = delete;
    ModelWindow& operator=(const ModelWindow&) = delete;

    bool open = false;

    Action draw(const Info& info, float ui_scale);

    // Sau Action::Downloaded: đường dẫn file mô hình vừa tải.
    [[nodiscard]] std::filesystem::path downloaded_path() const { return downloader_.dest(); }
    // Sau Action::ApplyThreads: số luồng đã chọn (0 = mặc định).
    [[nodiscard]] int chosen_threads() const noexcept { return threads_; }
    [[nodiscard]] bool downloading() const noexcept { return downloader_.running(); }

private:
    struct LicenseFetch {
        std::mutex mutex;
        bool done = false;
        std::optional<std::string> text;
        std::string error;
        std::atomic<bool> cancel{false};
    };

    void fetch_license();
    void draw_download(const Info& info, float ui_scale, Action& action);

    std::function<void()> wake_;
    download::Downloader downloader_;
    bool download_reported_ = true;  // đã trả Action::Downloaded cho lần tải gần nhất

    std::shared_ptr<LicenseFetch> license_;
    std::thread license_thread_;
    std::string license_text_;
    bool show_license_ = false;
    bool agreed_ = false;
    std::string message_;
    bool message_error_ = false;

    int threads_ = -1;  // -1: chưa khởi tạo từ Info
};

}  // namespace pdfread::ui
