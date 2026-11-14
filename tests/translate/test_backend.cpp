// Chọn backend / GPU cho llama.cpp (M8): khóa cài đặt, chọn thiết bị, nhãn hiển thị.

#include <catch2/catch_test_macros.hpp>

#include "translate/backend.hpp"

using namespace pdfread::translate;

namespace {

constexpr std::uint64_t kGiB = 1024ull * 1024 * 1024;

GpuDevice dev(Backend b, std::string name, std::string desc, bool integrated, std::uint64_t gib) {
    return GpuDevice{.backend = b, .name = std::move(name), .description = std::move(desc), .integrated = integrated,
                     .memory_total = gib * kGiB, .memory_free = gib * kGiB};
}

// Laptop thử nghiệm: iGPU AMD + RTX 3050 4 GB, có cả Vulkan lẫn CUDA.
std::vector<GpuDevice> laptop() {
    return {
        dev(Backend::Vulkan, "Vulkan0", "AMD Radeon(TM) Graphics", true, 8),
        dev(Backend::Vulkan, "Vulkan1", "NVIDIA GeForce RTX 3050 Laptop GPU", false, 4),
        dev(Backend::Cuda, "CUDA0", "NVIDIA GeForce RTX 3050 Laptop GPU", false, 4),
    };
}

}  // namespace

TEST_CASE("Backend keys round-trip and reject unknown values", "[translate][backend]") {
    for (Backend b : {Backend::Auto, Backend::Cpu, Backend::Vulkan, Backend::Cuda}) {
        REQUIRE(backend_from_key(backend_key(b)) == b);
    }
    CHECK_FALSE(backend_from_key("gpu").has_value());
    CHECK_FALSE(backend_from_key("").has_value());
    CHECK(backend_label(Backend::Auto) == "Tự động");
}

TEST_CASE("Auto prefers a discrete CUDA GPU, then discrete Vulkan, never an iGPU", "[translate][backend]") {
    const auto all = laptop();
    REQUIRE(choose_device(all, Backend::Auto) == 2u);  // CUDA0

    const std::vector<GpuDevice> no_cuda(all.begin(), all.begin() + 2);
    CHECK(choose_device(no_cuda, Backend::Auto) == 1u);  // Vulkan1 (RTX), không phải iGPU

    const std::vector<GpuDevice> igpu_only(all.begin(), all.begin() + 1);
    CHECK_FALSE(choose_device(igpu_only, Backend::Auto).has_value());  // chỉ iGPU -> CPU
    CHECK_FALSE(choose_device({}, Backend::Auto).has_value());
}

TEST_CASE("Explicit backend honours the preferred GPU and falls back to the best one", "[translate][backend]") {
    const auto all = laptop();
    CHECK(choose_device(all, Backend::Vulkan, "AMD Radeon(TM) Graphics") == 0u);
    CHECK(choose_device(all, Backend::Vulkan) == 1u);                    // GPU rời trước
    CHECK(choose_device(all, Backend::Vulkan, "GPU đã tháo ra") == 1u);  // không còn: chọn GPU tốt nhất
    CHECK(choose_device(all, Backend::Cuda, "NVIDIA GeForce RTX 3050 Laptop GPU") == 2u);
    // Tên GPU của backend khác không được dùng cho backend này.
    CHECK(choose_device(all, Backend::Cuda, "AMD Radeon(TM) Graphics") == 2u);
    CHECK_FALSE(choose_device(all, Backend::Cpu).has_value());

    // Chỉ có iGPU: chọn tường minh Vulkan thì vẫn dùng iGPU.
    const std::vector<GpuDevice> igpu_only(all.begin(), all.begin() + 1);
    CHECK(choose_device(igpu_only, Backend::Vulkan) == 0u);
    CHECK_FALSE(choose_device(igpu_only, Backend::Cuda).has_value());

    // Hai GPU rời: nhiều bộ nhớ hơn thắng.
    const std::vector<GpuDevice> two = {dev(Backend::Vulkan, "Vulkan0", "small", false, 2),
                                        dev(Backend::Vulkan, "Vulkan1", "big", false, 8)};
    CHECK(choose_device(two, Backend::Vulkan) == 1u);
}

TEST_CASE("Device labels show backend, GPU name, memory and iGPU flag", "[translate][backend]") {
    const auto all = laptop();
    CHECK(device_label(all[1]) == "Vulkan · NVIDIA GeForce RTX 3050 Laptop GPU (4,0 GB)");
    CHECK(device_label(all[0]) == "Vulkan · AMD Radeon(TM) Graphics (8,0 GB, tích hợp)");
    CHECK(device_label(GpuDevice{.backend = Backend::Cuda, .name = "CUDA0"}) == "CUDA · CUDA0");
}

TEST_CASE("CPU backend is always installed", "[translate][backend]") {
    CHECK(backend_installed(Backend::Cpu));
    CHECK(backend_installed(Backend::Auto));
}
