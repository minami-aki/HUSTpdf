#pragma once
// Interface chung cho backend dịch Anh -> Việt (xem CLAUDE.md mục 4.1).

#include <atomic>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "translate/backend.hpp"

namespace pdfread::translate {

struct GlossaryEntry {
    std::string term_en;
    std::string term_vi;
    bool keep_english = false;  // giữ nguyên thuật ngữ tiếng Anh trong bản dịch
};

struct DomainScore {
    std::string domain_id;
    float score = 0;
};

struct TranslateRequest {
    std::string text;                  // câu/đoạn cần dịch (đã thay placeholder)
    std::string context;               // 1–2 câu xung quanh (có thể rỗng)
    std::vector<GlossaryEntry> terms;  // chỉ các thuật ngữ xuất hiện trong text
    std::vector<DomainScore> domains;  // lĩnh vực của tài liệu/mục
    // Chỉ dùng cho công cụ thử nghiệm: nếu khác rỗng, gửi nguyên văn làm tin nhắn user
    // thay cho mẫu prompt dựng từ các trường trên.
    std::string raw_user_message;
};

struct TranslationStats {
    double prompt_ms = 0;       // xử lý prompt
    double first_token_ms = 0;  // từ lúc bắt đầu tới token đầu tiên
    double total_ms = 0;
    int prompt_tokens = 0;
    int output_tokens = 0;

    [[nodiscard]] double tokens_per_second() const noexcept {
        const double gen_ms = total_ms - first_token_ms;
        return output_tokens > 1 && gen_ms > 0 ? (output_tokens - 1) * 1000.0 / gen_ms : 0.0;
    }
};

struct ModelConfig {
    std::filesystem::path model_path;
    int threads = 0;      // 0 = mặc định theo số nhân
    int context = 2048;   // số token ngữ cảnh của mô hình
    // false: kết thúc prompt như ví dụ chính thức của model card (add_generation_prompt=False).
    bool add_generation_prompt = false;
    // true: backend CPU sắp xếp lại trọng số sang layout tối ưu AVX2/AVX-512 ("repack") và giữ
    // thêm một bản sao trọng số trong RAM. Đo trên Ryzen 5 4600H với HY-MT1.5 Q4_K_M:
    // bật = 1208 MB RAM riêng, 20,6 token/s, token đầu 311 ms; tắt = 495 MB, 22,0 token/s, 404 ms.
    // Mặc định tắt (ưu tiên nhẹ).
    bool repack_weights = false;
    // Thiết bị suy luận (M8). GPU lỗi khi nạp thì tự quay về CPU.
    Backend backend = Backend::Auto;
    std::string device;  // description của GPU ưa dùng (Vulkan/Cuda); rỗng = tự chọn
};

class TranslateError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Translator {
public:
    virtual ~Translator() = default;

    [[nodiscard]] virtual bool supports_glossary() const = 0;
    [[nodiscard]] virtual bool supports_context() const = 0;
    // Tên mô hình (dùng cho khóa cache bản dịch).
    [[nodiscard]] virtual std::string name() const = 0;
    // Thiết bị đang chạy, để hiển thị: "CPU", "Vulkan · NVIDIA GeForce RTX 3050…".
    [[nodiscard]] virtual std::string device() const { return "CPU"; }

    // Dịch, gọi on_token cho mỗi đoạn kết quả (UTF-8 trọn ký tự). Trả false nếu bị hủy
    // (cancel chuyển thành true). Ném TranslateError khi lỗi.
    virtual bool translate(const TranslateRequest& request, const std::function<void(std::string_view)>& on_token,
                           const std::atomic<bool>& cancel, TranslationStats* stats) = 0;
};

}  // namespace pdfread::translate
