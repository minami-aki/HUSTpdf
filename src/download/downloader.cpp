#include "download/downloader.hpp"

#include <chrono>
#include <format>
#include <fstream>
#include <mutex>

#include <curl/curl.h>

#include "util/log.hpp"
#include "util/sha256.hpp"
#include "util/thread_name.hpp"
#include "util/utf8.hpp"

namespace pdfread::download {

namespace {

using Clock = std::chrono::steady_clock;

constexpr const char* kUserAgent = "pdfread/0.1 (libcurl)";
constexpr std::uint64_t kSpaceMargin = 64ull << 20;  // chừa thêm khi kiểm tra dung lượng đĩa

void global_init() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct CurlHandle {
    CURL* h = curl_easy_init();
    ~CurlHandle() {
        if (h != nullptr) curl_easy_cleanup(h);
    }
    CurlHandle() = default;
    CurlHandle(const CurlHandle&) = delete;
    CurlHandle& operator=(const CurlHandle&) = delete;
};

void common_options(CURL* h, const std::string& url) {
    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(h, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(h, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
#ifdef _WIN32
    // Schannel: mạng chặn máy chủ thu hồi chứng chỉ (OCSP/CRL) thì vẫn kết nối được.
    curl_easy_setopt(h, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_REVOKE_BEST_EFFORT));
#endif
}

std::uint64_t file_size_or_zero(const std::filesystem::path& p) {
    std::error_code ec;
    const auto n = std::filesystem::file_size(p, ec);
    return ec ? 0 : static_cast<std::uint64_t>(n);
}

struct Transfer {
    const std::filesystem::path* part = nullptr;
    CURL* h = nullptr;
    std::uint64_t existing = 0;  // byte đã có trước khi gửi yêu cầu
    std::uint64_t base = 0;      // byte đã có tính từ đầu file khi nhận dữ liệu (0 nếu tải lại từ đầu)
    std::uint64_t received = 0;
    std::ofstream out;
    bool opened = false;
    bool write_failed = false;
    const std::atomic<bool>* cancel = nullptr;
    DownloadProgress* progress = nullptr;
    const std::function<void(const DownloadProgress&)>* report = nullptr;
    Clock::time_point last_report{};
};

std::size_t on_write(char* data, std::size_t size, std::size_t n, void* user) {
    auto* t = static_cast<Transfer*>(user);
    const std::size_t bytes = size * n;
    if (!t->opened) {
        long code = 0;
        curl_easy_getinfo(t->h, CURLINFO_RESPONSE_CODE, &code);
        // 206: máy chủ nhận Range -> nối tiếp; 200: gửi lại cả file -> ghi từ đầu.
        // (Giao thức không phải HTTP, như file:// trong test: mã 0, curl tự bỏ qua phần đã có.)
        const bool append = t->existing > 0 && code != 200;
        t->base = append ? t->existing : 0;
        t->out.open(*t->part, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
        t->opened = true;
        if (!t->out) {
            t->write_failed = true;
            return 0;
        }
        if (!append && t->existing > 0) log::info("Máy chủ không hỗ trợ tải tiếp, tải lại từ đầu");
    }
    t->out.write(data, static_cast<std::streamsize>(bytes));
    if (!t->out) {
        t->write_failed = true;
        return 0;
    }
    t->received += bytes;
    return bytes;
}

int on_progress(void* user, curl_off_t dltotal, curl_off_t /*dlnow*/, curl_off_t, curl_off_t) {
    auto* t = static_cast<Transfer*>(user);
    if (t->cancel->load()) return 1;
    const auto now = Clock::now();
    if (now - t->last_report < std::chrono::milliseconds(100)) return 0;
    t->last_report = now;
    DownloadProgress& p = *t->progress;
    if (t->received > 0) p.state = DownloadState::Downloading;
    p.done = (t->opened ? t->base : t->existing) + t->received;
    if (dltotal > 0 && p.total == 0) p.total = (t->opened ? t->base : t->existing) + static_cast<std::uint64_t>(dltotal);
    curl_off_t speed = 0;
    curl_easy_getinfo(t->h, CURLINFO_SPEED_DOWNLOAD_T, &speed);
    p.bytes_per_second = static_cast<double>(speed);
    (*t->report)(p);
    return 0;
}

// Chờ `seconds` giây, thoát sớm nếu bị hủy.
bool wait_or_cancel(double seconds, const std::atomic<bool>& cancel) {
    const auto until = Clock::now() + std::chrono::milliseconds(static_cast<int>(seconds * 1000));
    while (Clock::now() < until) {
        if (cancel.load()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return !cancel.load();
}

std::string format_bytes(std::uint64_t n) {
    if (n >= (1ull << 30)) return std::format("{:.2f} GB", static_cast<double>(n) / (1ull << 30));
    return std::format("{:.0f} MB", static_cast<double>(n) / (1ull << 20));
}

}  // namespace

std::filesystem::path part_path(const std::filesystem::path& dest) {
    std::filesystem::path p = dest;
    p += ".part";
    return p;
}

DownloadProgress download_file(const DownloadSpec& spec, const std::function<void(const DownloadProgress&)>& report,
                               const std::atomic<bool>& cancel) {
    global_init();
    DownloadProgress p;
    p.total = spec.expected_size;
    auto fail = [&](std::string msg) {
        p.state = DownloadState::Failed;
        p.error = std::move(msg);
        log::warn("Tải {} thất bại: {}", spec.url, p.error);
        report(p);
        return p;
    };
    auto cancelled = [&] {
        p.state = DownloadState::Cancelled;
        report(p);
        return p;
    };

    const std::filesystem::path part = part_path(spec.dest);
    std::error_code ec;
    std::filesystem::create_directories(spec.dest.parent_path(), ec);
    if (ec) return fail(std::format("Không tạo được thư mục {}: {}", path_to_utf8(spec.dest.parent_path()), ec.message()));

    for (int attempt = 0;; ++attempt) {
        if (cancel.load()) return cancelled();
        p.attempt = attempt;
        std::uint64_t existing = file_size_or_zero(part);
        if (spec.expected_size > 0 && existing > spec.expected_size) {
            std::filesystem::remove(part, ec);
            existing = 0;
        }
        if (spec.expected_size > 0 && existing == spec.expected_size) break;  // đã đủ, sang kiểm tra

        if (spec.expected_size > 0) {
            const auto space = std::filesystem::space(spec.dest.parent_path(), ec);
            const std::uint64_t need = spec.expected_size - existing + kSpaceMargin;
            if (!ec && space.available < need) {
                return fail(std::format("Không đủ dung lượng đĩa: cần {}, còn {}", format_bytes(need),
                                        format_bytes(space.available)));
            }
        }

        p.state = DownloadState::Connecting;
        p.done = existing;
        p.error.clear();
        report(p);

        CurlHandle curl;
        if (curl.h == nullptr) return fail("Không khởi tạo được libcurl");
        Transfer t;
        t.part = &part;
        t.h = curl.h;
        t.existing = existing;
        t.cancel = &cancel;
        t.progress = &p;
        t.report = &report;
        char errbuf[CURL_ERROR_SIZE] = {};
        common_options(curl.h, spec.url);
        curl_easy_setopt(curl.h, CURLOPT_ERRORBUFFER, errbuf);
        curl_easy_setopt(curl.h, CURLOPT_WRITEFUNCTION, &on_write);
        curl_easy_setopt(curl.h, CURLOPT_WRITEDATA, &t);
        curl_easy_setopt(curl.h, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl.h, CURLOPT_XFERINFOFUNCTION, &on_progress);
        curl_easy_setopt(curl.h, CURLOPT_XFERINFODATA, &t);
        // Mạng treo (dưới 1 KB/s trong 60 s) -> lỗi -> thử lại từ chỗ đã có.
        curl_easy_setopt(curl.h, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(curl.h, CURLOPT_LOW_SPEED_TIME, 60L);
        if (existing > 0) curl_easy_setopt(curl.h, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(existing));
        log::info("Tải {} (thử lần {}, đã có {} byte)", spec.url, attempt + 1, existing);

        const CURLcode rc = curl_easy_perform(curl.h);
        long code = 0;
        curl_easy_getinfo(curl.h, CURLINFO_RESPONSE_CODE, &code);
        t.out.close();
        if (cancel.load()) return cancelled();  // giữ .part để lần sau tải tiếp
        if (t.write_failed) return fail(std::format("Không ghi được file {}", path_to_utf8(part)));

        const std::uint64_t have = file_size_or_zero(part);
        p.done = have;
        if (rc == CURLE_OK) {
            if (spec.expected_size == 0 || have == spec.expected_size) break;
            if (have > spec.expected_size) {
                std::filesystem::remove(part, ec);
                return fail("Kích thước file tải về lớn hơn dự kiến");
            }
            // Kết nối đóng sớm: thử lại, tải tiếp phần còn thiếu.
        } else if (rc == CURLE_RANGE_ERROR) {
            // Máy chủ không nhận Range: tải lại từ đầu.
            std::filesystem::remove(part, ec);
        } else if (code == 416) {
            // Range vượt cuối file: phần đã có không khớp với file trên máy chủ.
            std::filesystem::remove(part, ec);
        } else if (code >= 400 && code < 500 && code != 408 && code != 429) {
            return fail(std::format("Máy chủ trả lỗi HTTP {}", code));
        }
        const std::string why = rc != CURLE_OK ? (errbuf[0] != '\0' ? std::string(errbuf) : curl_easy_strerror(rc))
                                               : std::string("kết nối đóng trước khi tải xong");
        if (attempt >= spec.max_retries) return fail(std::format("Lỗi mạng: {}", why));
        p.state = DownloadState::Retrying;
        p.error = why;
        report(p);
        log::info("Lỗi tải ({}), thử lại sau {} s", why, 2 * (attempt + 1));
        if (!wait_or_cancel(2.0 * (attempt + 1), cancel)) return cancelled();
    }

    // Kiểm tra SHA-256 rồi mới đổi tên.
    if (!spec.sha256.empty()) {
        p.state = DownloadState::Verifying;
        p.done = 0;
        p.total = file_size_or_zero(part);
        p.error.clear();
        report(p);
        auto last = Clock::now();
        const auto hex = sha256_file(part, [&](std::uint64_t done) {
            if (cancel.load()) return false;
            if (Clock::now() - last >= std::chrono::milliseconds(100)) {
                last = Clock::now();
                p.done = done;
                report(p);
            }
            return true;
        });
        if (cancel.load()) return cancelled();
        if (!hex) return fail(std::format("Không đọc được {}", path_to_utf8(part)));
        if (*hex != spec.sha256) {
            std::filesystem::remove(part, ec);
            return fail(std::format("SHA-256 không khớp (nhận {}, cần {}). Đã xóa file tải về.", *hex, spec.sha256));
        }
    }
    std::filesystem::remove(spec.dest, ec);
    std::filesystem::rename(part, spec.dest, ec);
    if (ec) return fail(std::format("Không đổi tên được thành {}: {}", path_to_utf8(spec.dest), ec.message()));
    p.state = DownloadState::Done;
    p.done = p.total = file_size_or_zero(spec.dest);
    log::info("Đã tải xong {}", path_to_utf8(spec.dest));
    report(p);
    return p;
}

std::optional<std::string> http_get(const std::string& url, std::string* error, std::size_t max_bytes,
                                    const std::atomic<bool>* cancel) {
    global_init();
    CurlHandle curl;
    if (curl.h == nullptr) {
        if (error) *error = "Không khởi tạo được libcurl";
        return std::nullopt;
    }
    struct Sink {
        std::string data;
        std::size_t max = 0;
    } sink{{}, max_bytes};
    char errbuf[CURL_ERROR_SIZE] = {};
    common_options(curl.h, url);
    curl_easy_setopt(curl.h, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl.h, CURLOPT_TIMEOUT, 60L);
    if (cancel != nullptr) {
        curl_easy_setopt(curl.h, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl.h, CURLOPT_XFERINFODATA, const_cast<std::atomic<bool>*>(cancel));
        curl_easy_setopt(curl.h, CURLOPT_XFERINFOFUNCTION,
                         +[](void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                             return static_cast<std::atomic<bool>*>(user)->load() ? 1 : 0;
                         });
    }
    curl_easy_setopt(curl.h, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl.h, CURLOPT_WRITEFUNCTION,
                     +[](char* data, std::size_t size, std::size_t n, void* user) -> std::size_t {
                         auto* s = static_cast<Sink*>(user);
                         if (s->data.size() + size * n > s->max) return 0;
                         s->data.append(data, size * n);
                         return size * n;
                     });
    const CURLcode rc = curl_easy_perform(curl.h);
    if (rc != CURLE_OK) {
        if (error) *error = errbuf[0] != '\0' ? std::string(errbuf) : curl_easy_strerror(rc);
        return std::nullopt;
    }
    return std::move(sink.data);
}

// --- Downloader ------------------------------------------------------------------------

Downloader::~Downloader() {
    cancel_ = true;
    join();
}

void Downloader::join() {
    if (thread_.joinable()) thread_.join();
}

void Downloader::start(DownloadSpec spec) {
    if (running_.load()) return;
    join();
    cancel_ = false;
    running_ = true;
    {
        std::lock_guard lock(mutex_);
        progress_ = DownloadProgress{.state = DownloadState::Connecting, .total = spec.expected_size};
        dest_ = spec.dest;
    }
    thread_ = std::thread([this, spec = std::move(spec)] {
        name_current_thread("download");
        auto report = [this](const DownloadProgress& p) {
            {
                std::lock_guard lock(mutex_);
                progress_ = p;
            }
            if (wake_) wake_();
        };
        const DownloadProgress final_state = download_file(spec, report, cancel_);
        {
            std::lock_guard lock(mutex_);
            progress_ = final_state;
        }
        running_ = false;
        if (wake_) wake_();
    });
}

DownloadProgress Downloader::progress() const {
    std::lock_guard lock(mutex_);
    return progress_;
}

std::filesystem::path Downloader::dest() const {
    std::lock_guard lock(mutex_);
    return dest_;
}

}  // namespace pdfread::download
