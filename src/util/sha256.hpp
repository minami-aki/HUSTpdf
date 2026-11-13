#pragma once
// SHA-256 (FIPS 180-4) tự viết: kiểm tra file mô hình tải về và làm khóa cache.
// Đủ nhanh cho file ~1 GB (vài giây); không cần thư viện mã hóa ngoài.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace pdfread {

class Sha256 {
public:
    using Digest = std::array<std::uint8_t, 32>;

    Sha256() { reset(); }

    void reset();
    void update(const void* data, std::size_t size);
    void update(std::string_view s) { update(s.data(), s.size()); }
    [[nodiscard]] Digest finish();  // xong thì đối tượng cần reset() trước khi dùng lại

    [[nodiscard]] static std::string to_hex(const Digest& d);
    [[nodiscard]] static std::string hex(std::string_view data);

private:
    void block(const std::uint8_t* p);

    std::array<std::uint32_t, 8> h_{};
    std::array<std::uint8_t, 64> buf_{};
    std::size_t buf_len_ = 0;
    std::uint64_t total_ = 0;
};

// Băm cả file. progress(bytes_done) được gọi định kỳ; trả false để hủy (khi đó trả nullopt).
[[nodiscard]] std::optional<std::string> sha256_file(
    const std::filesystem::path& path,
    const std::function<bool(std::uint64_t)>& progress = {});

}  // namespace pdfread
