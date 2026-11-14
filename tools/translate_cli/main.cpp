// Dịch thử từ dòng lệnh (không cần giao diện), để kiểm tra mô hình và prompt:
//
//   pdfread_translate --model models/HY-MT1.5-1.8B-Q4_K_M.gguf [--threads N] [--gen-prompt]
//                     [--context "câu xung quanh"] [--term "en=vi"]... "câu tiếng Anh"
//                     [--backend auto|cpu|vulkan|cuda] [--device "tên GPU"]
//   pdfread_translate --list-devices        # liệt kê GPU dùng được (Vulkan / CUDA)
//
// Không truyền câu thì đọc từng dòng từ stdin. Kết quả in ra stdout (UTF-8), số liệu ra stderr.

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "translate/llama_translator.hpp"
#include "util/cmdline.hpp"
#include "util/log.hpp"
#include "util/utf8.hpp"

using namespace pdfread;

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    translate::ModelConfig config;
    translate::TranslateRequest base;
    std::vector<std::string> inputs;
    bool raw = false;
    bool list_devices = false;  // --raw: gửi nguyên văn làm tin nhắn user (thử mẫu prompt khác); "\n" trong tham số = xuống dòng
    const std::vector<std::string> args = utf8_args(argc, argv);
    const std::size_t nargs = args.size();
    for (std::size_t i = 1; i < nargs; ++i) {
        const std::string a = args[i];
        auto next = [&]() -> std::string { return i + 1 < nargs ? args[++i] : std::string(); };
        if (a == "--model") config.model_path = path_from_utf8(next());
        else if (a == "--threads") config.threads = std::atoi(next().c_str());
        else if (a == "--gen-prompt") config.add_generation_prompt = true;
        else if (a == "--no-repack") config.repack_weights = false;
        else if (a == "--repack") config.repack_weights = true;
        else if (a == "--ctx") config.context = std::atoi(next().c_str());
        else if (a == "--context") base.context = next();
        else if (a == "--term") {
            const std::string t = next();
            const auto eq = t.find('=');
            if (eq != std::string::npos) base.terms.push_back({t.substr(0, eq), t.substr(eq + 1), false});
        } else if (a == "--verbose") log::set_level(log::Level::Debug);
        else if (a == "--raw") raw = true;
        else if (a == "--backend") {
            const auto b = translate::backend_from_key(next());
            if (!b) {
                std::fprintf(stderr, "--backend: auto | cpu | vulkan | cuda\n");
                return 1;
            }
            config.backend = *b;
        } else if (a == "--device") config.device = next();
        else if (a == "--list-devices") list_devices = true;
        else inputs.push_back(a);
    }
    if (list_devices) {
        const auto devices = translate::LlamaTranslator::probe_devices();
        for (const auto& d : devices) {
            std::printf("%s\t%s\t%s\n", d.name.c_str(), std::string(translate::backend_key(d.backend)).c_str(),
                        translate::device_label(d).c_str());
        }
        if (devices.empty()) std::printf("(không có GPU dùng được)\n");
        return 0;
    }
    if (config.model_path.empty()) {
        std::fprintf(stderr, "usage: pdfread_translate --model <file.gguf> [options] [text]\n");
        return 1;
    }
    if (inputs.empty()) {
        for (std::string line; std::getline(std::cin, line);) {
            if (!line.empty()) inputs.push_back(line);
        }
    }

    try {
        translate::LlamaTranslator translator(config);
        const std::atomic<bool> cancel{false};
        for (const std::string& text : inputs) {
            translate::TranslateRequest request = base;
            request.text = text;
            if (raw) {
                std::string msg = text;
                for (std::size_t p = 0; (p = msg.find("\\n", p)) != std::string::npos;) msg.replace(p, 2, "\n");
                request.raw_user_message = msg;
            }
            translate::TranslationStats stats;
            translator.translate(
                request, [](std::string_view piece) { std::fwrite(piece.data(), 1, piece.size(), stdout); std::fflush(stdout); },
                cancel, &stats);
            std::fputc('\n', stdout);
            std::fprintf(stderr, "[%d token vào, %d token ra, token đầu %.0f ms, %.1f token/s, tổng %.0f ms]\n",
                         stats.prompt_tokens, stats.output_tokens, stats.first_token_ms, stats.tokens_per_second(),
                         stats.total_ms);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
