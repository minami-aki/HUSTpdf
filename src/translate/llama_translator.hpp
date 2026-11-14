#pragma once
// Backend dịch dùng llama.cpp (liên kết trực tiếp) + Tencent HY-MT1.5-1.8B (GGUF).
// Chỉ dùng trên MỘT luồng (inference thread của TranslationService).

#include <atomic>
#include <string>
#include <vector>

#include "translate/translator.hpp"

struct llama_model;
struct llama_context;
struct llama_sampler;
struct llama_vocab;

namespace pdfread::translate {

class LlamaTranslator final : public Translator {
public:
    // Nạp mô hình (chậm, vài giây). Ném TranslateError khi lỗi.
    explicit LlamaTranslator(const ModelConfig& config);
    ~LlamaTranslator() override;

    LlamaTranslator(const LlamaTranslator&) = delete;
    LlamaTranslator& operator=(const LlamaTranslator&) = delete;

    [[nodiscard]] bool supports_glossary() const override { return true; }
    [[nodiscard]] bool supports_context() const override { return true; }
    [[nodiscard]] std::string name() const override { return name_; }
    [[nodiscard]] std::string device() const override { return device_; }

    bool translate(const TranslateRequest& request, const std::function<void(std::string_view)>& on_token,
                   const std::atomic<bool>& cancel, TranslationStats* stats) override;

    // Số luồng suy luận mặc định: số nhân vật lý ước tính (luồng phần cứng / 2), tối đa 8.
    [[nodiscard]] static int default_threads() noexcept;

    // Nạp mọi DLL backend GPU đã cài (Vulkan, CUDA) và liệt kê GPU. Chậm (khởi tạo driver,
    // có thể đánh thức GPU rời): chỉ gọi trên luồng nền.
    [[nodiscard]] static std::vector<GpuDevice> probe_devices();

    // Tham số sinh theo model card.
    static constexpr int kTopK = 20;
    static constexpr float kTopP = 0.6f;
    static constexpr float kTemperature = 0.7f;
    static constexpr float kRepetitionPenalty = 1.05f;
    static constexpr int kPenaltyLastN = 64;

private:
    static bool abort_callback(void* data);
    // Nạp mô hình + ngữ cảnh trên `gpu` (nullptr = chỉ CPU). Lỗi: dọn dẹp, trả thông báo.
    std::string try_load(void* gpu);
    void warm_up();

    ModelConfig config_;
    std::string name_;
    std::string device_ = "CPU";
    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    llama_sampler* sampler_ = nullptr;
    const llama_vocab* vocab_ = nullptr;
    const std::atomic<bool>* cancel_ = nullptr;  // của yêu cầu đang chạy (cho abort_callback)
};

}  // namespace pdfread::translate
