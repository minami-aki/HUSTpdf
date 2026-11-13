#include "translate/llama_translator.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <mutex>
#include <thread>
#include <vector>

#include <ggml-backend.h>
#include <llama.h>

#include "translate/prompt.hpp"
#include "translate/utf8_stream.hpp"
#include "util/log.hpp"
#include "util/timer.hpp"
#include "util/utf8.hpp"

namespace pdfread::translate {
namespace {

void on_llama_log(ggml_log_level level, const char* text, void*) {
    std::string_view msg(text);
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.remove_suffix(1);
    if (msg.empty()) return;
    if (level == GGML_LOG_LEVEL_ERROR) log::warn("llama: {}", msg);
    else log::debug("llama: {}", msg);
}

void init_backend_once() {
    static std::once_flag once;
    std::call_once(once, [] {
        llama_log_set(on_llama_log, nullptr);
        llama_backend_init();
        ggml_backend_load_all();  // nạp DLL backend CPU phù hợp nhất với máy
    });
}

std::vector<llama_token> tokenize(const llama_vocab* vocab, const std::string& text) {
    // Prompt đã chứa sẵn token mở đầu dạng chữ: không thêm BOS, có phân tích token đặc biệt.
    const int n = -llama_tokenize(vocab, text.data(), static_cast<int32_t>(text.size()), nullptr, 0, false, true);
    std::vector<llama_token> tokens(static_cast<std::size_t>(std::max(n, 0)));
    if (n > 0 && llama_tokenize(vocab, text.data(), static_cast<int32_t>(text.size()), tokens.data(), n, false, true) < 0) {
        throw TranslateError("Không tách được token của đoạn văn");
    }
    return tokens;
}

std::string token_piece(const llama_vocab* vocab, llama_token token) {
    char buf[256];
    int n = llama_token_to_piece(vocab, token, buf, sizeof(buf), 0, false);
    if (n >= 0) return std::string(buf, static_cast<std::size_t>(n));
    std::string big(static_cast<std::size_t>(-n), '\0');
    n = llama_token_to_piece(vocab, token, big.data(), static_cast<int32_t>(big.size()), 0, false);
    big.resize(static_cast<std::size_t>(std::max(n, 0)));
    return big;
}

}  // namespace

int LlamaTranslator::default_threads() noexcept {
    const unsigned hw = std::thread::hardware_concurrency();
    return std::clamp(static_cast<int>(hw / 2), 1, 8);
}

LlamaTranslator::LlamaTranslator(const ModelConfig& config) : config_(config) {
    init_backend_once();
    ScopedTimer timer("load model");
    if (config_.threads <= 0) config_.threads = default_threads();

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = 0;  // chỉ CPU
    mparams.use_extra_bufts = config_.repack_weights;
    const std::string path = path_to_utf8(config_.model_path);
    model_ = llama_model_load_from_file(path.c_str(), mparams);
    if (model_ == nullptr) throw TranslateError(std::format("Không nạp được mô hình: {}", path));

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = static_cast<uint32_t>(config_.context);
    // Lô 256 token: buffer tính toán ~1/2 so với 512 (258 MB -> ~130 MB); prompt dịch câu
    // thường < 600 token nên chỉ tốn thêm vài lượt decode.
    cparams.n_batch = 256;
    cparams.n_ubatch = 256;
    cparams.n_threads = config_.threads;
    cparams.n_threads_batch = config_.threads;
    cparams.no_perf = true;
    cparams.abort_callback = &LlamaTranslator::abort_callback;
    cparams.abort_callback_data = this;
    ctx_ = llama_init_from_model(model_, cparams);
    if (ctx_ == nullptr) {
        llama_model_free(model_);
        model_ = nullptr;
        throw TranslateError("Không tạo được ngữ cảnh suy luận");
    }
    vocab_ = llama_model_get_vocab(model_);

    llama_sampler_chain_params sp = llama_sampler_chain_default_params();
    sp.no_perf = true;
    // Thứ tự tương đương transformers (phạt lặp -> nhiệt độ -> top-k -> top-p). top-k đặt
    // lên đầu để bộ phạt lặp chỉ duyệt 20 ứng viên (khuyến nghị của llama.h); nhiệt độ và
    // top-k không ảnh hưởng thứ tự của nhau nên kết quả không đổi.
    sampler_ = llama_sampler_chain_init(sp);
    llama_sampler_chain_add(sampler_, llama_sampler_init_top_k(kTopK));
    llama_sampler_chain_add(sampler_, llama_sampler_init_penalties(llama_vocab_n_tokens(llama_model_get_vocab(model_)),
                                                                   kPenaltyLastN, kRepetitionPenalty, 0.0f, 0.0f));
    llama_sampler_chain_add(sampler_, llama_sampler_init_temp(kTemperature));
    llama_sampler_chain_add(sampler_, llama_sampler_init_top_p(kTopP, 1));
    llama_sampler_chain_add(sampler_, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    char desc[256] = {};
    llama_model_desc(model_, desc, sizeof(desc));
    name_ = path_to_utf8(config_.model_path.filename());
    log::info("Đã nạp mô hình {} ({}), {} luồng, ngữ cảnh {} token", name_, desc, config_.threads, config_.context);
}

LlamaTranslator::~LlamaTranslator() {
    if (sampler_ != nullptr) llama_sampler_free(sampler_);
    if (ctx_ != nullptr) llama_free(ctx_);
    if (model_ != nullptr) llama_model_free(model_);
}

bool LlamaTranslator::abort_callback(void* data) {
    const auto* self = static_cast<const LlamaTranslator*>(data);
    return self->cancel_ != nullptr && self->cancel_->load(std::memory_order_relaxed);
}

bool LlamaTranslator::translate(const TranslateRequest& request, const std::function<void(std::string_view)>& on_token,
                                const std::atomic<bool>& cancel, TranslationStats* stats) {
    Stopwatch total;
    TranslationStats st;

    const std::string prompt = format_chat(build_user_prompt(request), config_.add_generation_prompt);
    const std::vector<llama_token> tokens = tokenize(vocab_, prompt);
    const int n_prompt = static_cast<int>(tokens.size());
    const int n_ctx = static_cast<int>(llama_n_ctx(ctx_));
    st.prompt_tokens = n_prompt;
    if (n_prompt == 0) return true;
    if (n_prompt > n_ctx - 64) {
        throw TranslateError(std::format("Đoạn văn quá dài ({} token, tối đa khoảng {})", n_prompt, n_ctx - 64));
    }

    cancel_ = &cancel;
    struct ResetCancel {
        const std::atomic<bool>** p;
        ~ResetCancel() { *p = nullptr; }
    } reset{&cancel_};

    llama_memory_clear(llama_get_memory(ctx_), true);
    llama_sampler_reset(sampler_);

    // Xử lý prompt theo lô.
    const int n_batch = static_cast<int>(llama_n_batch(ctx_));
    for (int i = 0; i < n_prompt; i += n_batch) {
        const int n = std::min(n_batch, n_prompt - i);
        const int rc = llama_decode(ctx_, llama_batch_get_one(const_cast<llama_token*>(tokens.data()) + i, n));
        if (cancel.load()) return false;
        if (rc != 0) throw TranslateError(std::format("llama_decode lỗi (mã {}) khi xử lý prompt", rc));
    }
    st.prompt_ms = total.elapsed_ms();

    // Bản dịch tiếng Việt thường dài hơn bản gốc tính theo token, và mô hình hay diễn giải
    // thêm; giới hạn rộng (3x) chỉ để chặn trường hợp sinh lặp vô tận.
    const int max_new = std::min(n_ctx - n_prompt - 1, std::clamp(3 * n_prompt + 32, 64, 1024));
    Utf8Stream utf8;
    bool started = false;  // đã gặp ký tự khác khoảng trắng
    for (int i = 0; i < max_new; ++i) {
        if (cancel.load()) return false;
        llama_token tok = llama_sampler_sample(sampler_, ctx_, -1);
        if (llama_vocab_is_eog(vocab_, tok) || llama_vocab_is_control(vocab_, tok)) break;

        std::string piece = utf8.push(token_piece(vocab_, tok));
        if (st.output_tokens == 0) st.first_token_ms = total.elapsed_ms();
        ++st.output_tokens;
        if (!started) {
            const auto first = std::find_if(piece.begin(), piece.end(),
                                            [](char c) { return !std::isspace(static_cast<unsigned char>(c)); });
            piece.erase(piece.begin(), first);
            started = !piece.empty();
        }
        if (!piece.empty()) on_token(piece);

        const int rc = llama_decode(ctx_, llama_batch_get_one(&tok, 1));
        if (cancel.load()) return false;
        if (rc != 0) throw TranslateError(std::format("llama_decode lỗi (mã {}) khi sinh token", rc));
    }
    if (std::string rest = utf8.flush(); !rest.empty() && started) on_token(rest);

    st.total_ms = total.elapsed_ms();
    log::info("[timing] dịch: prompt {} token {:.0f} ms, token đầu {:.0f} ms, {} token ra, {:.1f} token/s, tổng {:.0f} ms",
              st.prompt_tokens, st.prompt_ms, st.first_token_ms, st.output_tokens, st.tokens_per_second(), st.total_ms);
    if (stats != nullptr) *stats = st;
    return true;
}

}  // namespace pdfread::translate
