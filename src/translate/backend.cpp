#include "translate/backend.hpp"

#include <filesystem>
#include <format>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace pdfread::translate {
namespace {

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

bool exists_beside_exe(const char* file) {
    std::error_code ec;
    return std::filesystem::is_regular_file(executable_dir() / file, ec);
}

}  // namespace

std::string_view backend_key(Backend b) noexcept {
    switch (b) {
        case Backend::Auto: return "auto";
        case Backend::Cpu: return "cpu";
        case Backend::Vulkan: return "vulkan";
        case Backend::Cuda: return "cuda";
    }
    return "auto";
}

std::optional<Backend> backend_from_key(std::string_view key) noexcept {
    for (Backend b : {Backend::Auto, Backend::Cpu, Backend::Vulkan, Backend::Cuda}) {
        if (backend_key(b) == key) return b;
    }
    return std::nullopt;
}

std::string_view backend_label(Backend b) noexcept {
    switch (b) {
        case Backend::Auto: return "Tự động";
        case Backend::Cpu: return "CPU";
        case Backend::Vulkan: return "Vulkan";
        case Backend::Cuda: return "CUDA";
    }
    return "Tự động";
}

std::optional<std::size_t> choose_device(const std::vector<GpuDevice>& devices, Backend backend,
                                         std::string_view preferred) {
    auto best_of = [&](Backend family, bool allow_integrated) -> std::optional<std::size_t> {
        std::optional<std::size_t> best;
        for (std::size_t i = 0; i < devices.size(); ++i) {
            const GpuDevice& d = devices[i];
            if (d.backend != family || (d.integrated && !allow_integrated)) continue;
            if (!best) {
                best = i;
                continue;
            }
            const GpuDevice& b = devices[*best];
            // GPU rời trước GPU tích hợp, rồi nhiều bộ nhớ hơn.
            if ((b.integrated && !d.integrated) ||
                (b.integrated == d.integrated && d.memory_total > b.memory_total)) {
                best = i;
            }
        }
        return best;
    };

    switch (backend) {
        case Backend::Cpu: return std::nullopt;
        case Backend::Auto:
            if (auto i = best_of(Backend::Cuda, false)) return i;
            return best_of(Backend::Vulkan, false);
        case Backend::Vulkan:
        case Backend::Cuda:
            if (!preferred.empty()) {
                for (std::size_t i = 0; i < devices.size(); ++i) {
                    if (devices[i].backend == backend && devices[i].description == preferred) return i;
                }
            }
            return best_of(backend, true);
    }
    return std::nullopt;
}

bool backend_installed(Backend b) {
    switch (b) {
        case Backend::Auto:
        case Backend::Cpu: return true;
        case Backend::Vulkan: return exists_beside_exe("ggml-vulkan.dll");
        case Backend::Cuda: {
            if (!exists_beside_exe("ggml-cuda.dll")) return false;
            // ggml-cuda.dll phụ thuộc cuBLAS: thiếu thì DLL không nạp được.
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(executable_dir(), ec)) {
                const std::wstring file = entry.path().filename().wstring();
                if (file.starts_with(L"cublas64_")) return true;
            }
            return false;
        }
    }
    return false;
}

std::string device_label(const GpuDevice& d) {
    std::string s = std::format("{} · {}", backend_label(d.backend), d.description.empty() ? d.name : d.description);
    if (d.memory_total > 0) {
        std::string gb = std::format("{:.1f}", static_cast<double>(d.memory_total) / (1024.0 * 1024.0 * 1024.0));
        for (char& c : gb) {
            if (c == '.') c = ',';
        }
        s += std::format(" ({} GB{})", gb, d.integrated ? ", tích hợp" : "");
    } else if (d.integrated) {
        s += " (tích hợp)";
    }
    return s;
}

}  // namespace pdfread::translate
