#pragma once
// Tải file lớn bằng libcurl (lần chạy đầu: tải mô hình dịch).
//
//  - Ghi vào "<đích>.part"; đứt mạng/thoát app thì lần sau tải tiếp bằng HTTP Range.
//  - Tự thử lại vài lần khi lỗi mạng (mỗi lần tiếp tục từ chỗ đã có).
//  - Xong thì kiểm tra kích thước + SHA-256, đúng mới đổi tên thành file đích.
//  - Chạy trên luồng riêng; UI đọc tiến độ mỗi frame, được đánh thức bằng `wake`.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace pdfread::download {

// Mô hình dịch mặc định (Tencent HY-MT1.5-1.8B, GGUF Q4_K_M) trên Hugging Face.
struct ModelSource {
    const char* file_name;
    const char* url;
    std::uint64_t size;
    const char* sha256;
    const char* license_url;
    const char* license_file_name;  // lưu cạnh file mô hình
    const char* page_url;           // trang mô hình (hiển thị cho người dùng)
};

inline constexpr ModelSource kDefaultModel{
    .file_name = "HY-MT1.5-1.8B-Q4_K_M.gguf",
    .url = "https://huggingface.co/tencent/HY-MT1.5-1.8B-GGUF/resolve/main/HY-MT1.5-1.8B-Q4_K_M.gguf",
    .size = 1133080512,
    .sha256 = "4383ac0c3c8e476de98ff979c2a3f069f8c4fb385e7860cf2d28da896cc477c7",
    .license_url = "https://huggingface.co/tencent/HY-MT1.5-1.8B-GGUF/resolve/main/License.txt",
    .license_file_name = "HY-MT1.5-License.txt",
    .page_url = "https://huggingface.co/tencent/HY-MT1.5-1.8B-GGUF",
};

struct DownloadSpec {
    std::string url;
    std::filesystem::path dest;
    std::uint64_t expected_size = 0;  // 0 = không biết trước
    std::string sha256;               // hex thường; rỗng = không kiểm tra
    int max_retries = 5;
};

enum class DownloadState { Idle, Connecting, Downloading, Retrying, Verifying, Done, Failed, Cancelled };

struct DownloadProgress {
    DownloadState state = DownloadState::Idle;
    std::uint64_t done = 0;   // byte đã có (gồm phần tải từ lần trước)
    std::uint64_t total = 0;  // 0 = chưa biết
    double bytes_per_second = 0;
    int attempt = 0;
    std::string error;
};

// Tải đồng bộ trên luồng gọi. `report` được gọi khi tiến độ thay đổi (tối đa ~10 lần/giây).
// Trả trạng thái cuối: Done, Failed (kèm error) hoặc Cancelled.
DownloadProgress download_file(const DownloadSpec& spec, const std::function<void(const DownloadProgress&)>& report,
                               const std::atomic<bool>& cancel);

// GET một tài nguyên nhỏ (văn bản giấy phép…) vào bộ nhớ. nullopt nếu lỗi.
std::optional<std::string> http_get(const std::string& url, std::string* error = nullptr,
                                    std::size_t max_bytes = 4u << 20, const std::atomic<bool>* cancel = nullptr);

// Chạy download_file trên luồng nền.
class Downloader {
public:
    explicit Downloader(std::function<void()> wake) : wake_(std::move(wake)) {}
    ~Downloader();
    Downloader(const Downloader&) = delete;
    Downloader& operator=(const Downloader&) = delete;

    // Bỏ qua nếu đang chạy.
    void start(DownloadSpec spec);
    void cancel() { cancel_ = true; }
    [[nodiscard]] bool running() const noexcept { return running_.load(); }
    [[nodiscard]] DownloadProgress progress() const;
    [[nodiscard]] std::filesystem::path dest() const;

private:
    void join();

    std::function<void()> wake_;
    mutable std::mutex mutex_;
    DownloadProgress progress_;
    std::filesystem::path dest_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> running_{false};
    std::thread thread_;
};

[[nodiscard]] std::filesystem::path part_path(const std::filesystem::path& dest);

}  // namespace pdfread::download
