#include "translate/llama_translator.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

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

// --- nạp DLL backend ------------------------------------------------------------
// Không dùng ggml_backend_load_all(): hàm đó nạp cả DLL GPU (khởi tạo driver, đánh thức GPU
// rời) ngay cả khi chỉ chạy CPU. Ở đây chỉ nạp những DLL chế độ đang chọn cần.

std::mutex g_backend_mutex;
bool g_cpu_loaded = false;
bool g_vulkan_tried = false;
bool g_cuda_tried = false;

std::filesystem::path executable_dir() {
#ifdef _WIN32
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return std::filesystem::path(buf).parent_path();
#else
    return {};
#endif
}

// Biến thể CPU tốt nhất cho máy (như ggml_backend_load_best("cpu"): điểm từ ggml_backend_score).
void load_cpu_backend() {
#ifdef _WIN32
    const std::filesystem::path dir = executable_dir();
    std::filesystem::path best;
    int best_score = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        const std::wstring file = entry.path().filename().wstring();
        if (!file.starts_with(L"ggml-cpu") || entry.path().extension() != L".dll") continue;
        HMODULE h = LoadLibraryW(entry.path().c_str());
        if (h == nullptr) continue;
        using ScoreFn = int (*)();
        const auto score = reinterpret_cast<ScoreFn>(reinterpret_cast<void*>(GetProcAddress(h, "ggml_backend_score")));
        const int s = score != nullptr ? score() : 0;
        FreeLibrary(h);
        if (s > best_score) {
            best_score = s;
            best = entry.path();
        }
    }
    if (!best.empty() && ggml_backend_load(path_to_utf8(best).c_str()) != nullptr) {
        log::debug("Backend CPU: {}", path_to_utf8(best.filename()));
        return;
    }
    log::warn("Không chọn được biến thể backend CPU, nạp mọi backend");
#endif
    ggml_backend_load_all();
}

bool load_gpu_backend(const char* file) {
    const std::filesystem::path path = executable_dir() / file;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return false;
    Stopwatch sw;
    ggml_backend_reg_t reg = ggml_backend_load(path_to_utf8(path).c_str());
    if (reg == nullptr) {
        log::warn("Không nạp được {} (thiếu driver hoặc thư viện đi kèm)", file);
        return false;
    }
    log::info("Đã nạp backend {}: {} thiết bị ({:.0f} ms)", ggml_backend_reg_name(reg), ggml_backend_reg_dev_count(reg),
              sw.elapsed_ms());
    return true;
}

std::vector<GpuDevice> list_devices(std::vector<ggml_backend_dev_t>* handles = nullptr) {
    std::vector<GpuDevice> out;
    for (std::size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);
        const enum ggml_backend_dev_type type = ggml_backend_dev_type(dev);
        if (type != GGML_BACKEND_DEVICE_TYPE_GPU && type != GGML_BACKEND_DEVICE_TYPE_IGPU) continue;
        const std::string reg = ggml_backend_reg_name(ggml_backend_dev_backend_reg(dev));
        GpuDevice d;
        if (reg == "Vulkan") d.backend = Backend::Vulkan;
        else if (reg == "CUDA") d.backend = Backend::Cuda;
        else continue;
        d.name = ggml_backend_dev_name(dev);
        d.description = ggml_backend_dev_description(dev);
        d.integrated = type == GGML_BACKEND_DEVICE_TYPE_IGPU;
        std::size_t free = 0;
        std::size_t total = 0;
        ggml_backend_dev_memory(dev, &free, &total);
        d.memory_free = free;
        d.memory_total = total;
        out.push_back(std::move(d));
        if (handles != nullptr) handles->push_back(dev);
    }
    return out;
}

// Nạp các backend cần cho `backend` (gọi nhiều lần được). Auto: CUDA trước; chỉ nạp thêm
// Vulkan khi CUDA không có GPU rời.
void ensure_backends(Backend backend) {
    std::lock_guard lock(g_backend_mutex);
    if (!g_cpu_loaded) {
        llama_log_set(on_llama_log, nullptr);
        // Nạp CPU TRƯỚC llama_backend_init(): hàm đó tự gọi ggml_backend_load_all() (nạp cả DLL
        // GPU) nếu chưa có backend nào được đăng ký.
        load_cpu_backend();
        llama_backend_init();
        g_cpu_loaded = true;
    }
    const bool want_cuda = backend == Backend::Cuda || backend == Backend::Auto;
    if (want_cuda && !g_cuda_tried) {
        g_cuda_tried = true;
        if (backend_installed(Backend::Cuda)) load_gpu_backend("ggml-cuda.dll");
    }
    bool want_vulkan = backend == Backend::Vulkan;
    if (backend == Backend::Auto) {
        const auto devices = list_devices();
        want_vulkan = !choose_device(devices, Backend::Auto).has_value();
    }
    if (want_vulkan && !g_vulkan_tried) {
        g_vulkan_tried = true;
        load_gpu_backend("ggml-vulkan.dll");
    }
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

std::vector<GpuDevice> LlamaTranslator::probe_devices() {
    ensure_backends(Backend::Cuda);
    ensure_backends(Backend::Vulkan);
    std::lock_guard lock(g_backend_mutex);
    return list_devices();
}

LlamaTranslator::LlamaTranslator(const ModelConfig& config) : config_(config) {
    ensure_backends(config_.backend);
    ScopedTimer timer("load model");
    if (config_.threads <= 0) config_.threads = default_threads();

    std::vector<ggml_backend_dev_t> handles;
    std::vector<GpuDevice> devices;
    {
        std::lock_guard lock(g_backend_mutex);
        devices = list_devices(&handles);
    }
    std::optional<std::size_t> choice = choose_device(devices, config_.backend, config_.device);
    if (!choice && config_.backend == Backend::Cuda) {
        // Đã chọn CUDA nhưng thiếu gói CUDA (hoặc nạp lỗi): dùng cùng GPU qua Vulkan trước khi về CPU.
        log::warn("CUDA không dùng được, thử Vulkan");
        ensure_backends(Backend::Vulkan);
        std::lock_guard lock(g_backend_mutex);
        handles.clear();
        devices = list_devices(&handles);
        choice = choose_device(devices, Backend::Vulkan, config_.device);
    }
    std::string error;
    if (choice) {
        const GpuDevice& d = devices[*choice];
        error = try_load(handles[*choice]);
        if (error.empty()) {
            device_ = device_label(d);
        } else {
            log::warn("Không chạy được trên {} ({}), chuyển về CPU", device_label(d), error);
        }
    } else if (config_.backend == Backend::Vulkan || config_.backend == Backend::Cuda) {
        log::warn("Không tìm thấy GPU cho backend {}, chạy CPU", backend_label(config_.backend));
        error = "không tìm thấy GPU";
    }
    if (model_ == nullptr) {
        const std::string cpu_error = try_load(nullptr);
        if (!cpu_error.empty()) throw TranslateError(cpu_error);
        device_ = error.empty() ? "CPU" : std::format("CPU (GPU lỗi: {})", error);
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
    log::info("Đã nạp mô hình {} ({}) trên {}, {} luồng, ngữ cảnh {} token", name_, desc, device_, config_.threads,
              config_.context);
    if (!device_.starts_with("CPU")) warm_up();
}

std::string LlamaTranslator::try_load(void* gpu) {
    llama_model_params mparams = llama_model_default_params();
    // Danh sách thiết bị kết thúc bằng nullptr; danh sách rỗng = chỉ CPU (kể cả khi DLL GPU đã
    // được nạp trong phiên này).
    ggml_backend_dev_t devices[2] = {static_cast<ggml_backend_dev_t>(gpu), nullptr};
    mparams.devices = devices;
    mparams.n_gpu_layers = gpu != nullptr ? -1 : 0;  // GPU: đưa mọi lớp lên VRAM
    mparams.split_mode = LLAMA_SPLIT_MODE_NONE;
    mparams.main_gpu = 0;
    mparams.use_extra_bufts = gpu == nullptr && config_.repack_weights;
    // GPU: đọc file thường thay vì mmap — trọng số nằm trên VRAM, không cần giữ các trang file
    // (~1 GB) trong RAM sau khi chép lên GPU.
    if (gpu != nullptr) mparams.load_mode = LLAMA_LOAD_MODE_NONE;
    const std::string path = path_to_utf8(config_.model_path);
    model_ = llama_model_load_from_file(path.c_str(), mparams);
    if (model_ == nullptr) return std::format("Không nạp được mô hình: {}", path);

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
        return "Không tạo được ngữ cảnh suy luận";
    }
    return {};
}

void LlamaTranslator::warm_up() {
    // GPU dựng pipeline/kernel ở lần chạy đầu (Vulkan: biên dịch shader, có thể mất cả giây).
    // Chạy thử một lô nhiều token (đường xử lý prompt) và một token (đường sinh) ngay khi nạp
    // để bản dịch đầu tiên không bị chậm.
    // Vulkan chọn kernel nhân ma trận theo số token của lô (nhỏ / vừa / lớn): chạy thử vài cỡ lô
    // trải từ prompt câu ngắn tới lô đầy (256), nếu không thì câu đầu tiên có độ dài "mới" phải
    // chờ biên dịch shader (đo: 3,4 s ở lần chạy đầu sau khi cài). Driver lưu shader đã biên dịch
    // ra đĩa nên từ lần mở app sau, bước này chỉ tốn vài chục ms.
    Stopwatch sw;
    const std::vector<llama_token> sample = tokenize(vocab_, "The reactor core is cooled by water at high pressure.");
    if (sample.empty()) return;
    for (const int n : {16, 40, 72, 136, 256}) {
        std::vector<llama_token> tokens;
        tokens.reserve(static_cast<std::size_t>(n));
        while (static_cast<int>(tokens.size()) < n) tokens.push_back(sample[tokens.size() % sample.size()]);
        llama_memory_clear(llama_get_memory(ctx_), true);
        if (llama_decode(ctx_, llama_batch_get_one(tokens.data(), n)) != 0) break;
        llama_token tok = tokens.front();
        if (llama_decode(ctx_, llama_batch_get_one(&tok, 1)) != 0) break;
    }
    llama_memory_clear(llama_get_memory(ctx_), true);
    log::info("Khởi động GPU: {:.0f} ms", sw.elapsed_ms());
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
