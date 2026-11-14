#pragma once
// Backend suy luận của llama.cpp (M8): CPU, Vulkan, CUDA; chọn thiết bị GPU.
//
// Mỗi backend là một DLL ggml cạnh file chạy (GGML_BACKEND_DL). App chỉ nạp DLL GPU khi chế
// độ đang chọn cần đến: chế độ CPU không đánh thức GPU rời (tốn pin) và không tốn RAM cho
// driver GPU. Phần này không phụ thuộc ggml (test được); phần nạp DLL ở llama_translator.cpp.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pdfread::translate {

enum class Backend {
    Auto,    // GPU rời nếu có (CUDA trước, rồi Vulkan), không thì CPU
    Cpu,
    Vulkan,  // mọi GPU có driver Vulkan (NVIDIA, AMD, Intel; cả GPU tích hợp)
    Cuda,    // GPU NVIDIA, cần gói bổ sung CUDA (cudart, cuBLAS)
};

[[nodiscard]] std::string_view backend_key(Backend b) noexcept;  // "auto" | "cpu" | "vulkan" | "cuda"
[[nodiscard]] std::optional<Backend> backend_from_key(std::string_view key) noexcept;
[[nodiscard]] std::string_view backend_label(Backend b) noexcept;  // "Tự động", "CPU", "Vulkan", "CUDA"

struct GpuDevice {
    Backend backend = Backend::Vulkan;  // Vulkan hoặc Cuda
    std::string name;                   // tên ggml, vd. "Vulkan0", "CUDA0"
    std::string description;            // tên GPU, vd. "NVIDIA GeForce RTX 3050 Laptop GPU"
    bool integrated = false;            // GPU tích hợp (dùng chung RAM)
    std::uint64_t memory_total = 0;     // byte
    std::uint64_t memory_free = 0;
};

// Chọn thiết bị cho chế độ `backend` trong danh sách đã dò.
// - Vulkan / Cuda: thiết bị cùng backend có description == `preferred` (nếu có); không thì GPU
//   rời có nhiều bộ nhớ nhất; không có GPU rời thì GPU tích hợp.
// - Auto: GPU rời CUDA, rồi GPU rời Vulkan; GPU tích hợp không được chọn tự động (thường không
//   nhanh hơn CPU mà lại chia băng thông RAM).
// - Cpu, hoặc không có thiết bị phù hợp: nullopt (= chạy CPU).
[[nodiscard]] std::optional<std::size_t> choose_device(const std::vector<GpuDevice>& devices, Backend backend,
                                                       std::string_view preferred = {});

// DLL backend có nằm cạnh file chạy không (chỉ kiểm tra file, gọi từ UI thread được).
// Cpu/Auto luôn true. Cuda cần cả ggml-cuda.dll lẫn thư viện CUDA runtime đi kèm.
[[nodiscard]] bool backend_installed(Backend b);

// Chuỗi hiển thị: "Vulkan · NVIDIA GeForce RTX 3050 Laptop GPU (4,0 GB)".
[[nodiscard]] std::string device_label(const GpuDevice& d);

}  // namespace pdfread::translate
