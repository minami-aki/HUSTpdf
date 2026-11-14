#pragma once
// Kho dữ liệu SQLite của app (một file, mặc định %LOCALAPPDATA%\HUSTpdf\pdfread.db):
//
//  - settings     : cài đặt dạng khóa -> giá trị (đường dẫn mô hình, số luồng…);
//  - translations : cache bản dịch. Khóa = SHA-256 của (prompt gửi mô hình — đã gồm câu
//                   đã chuẩn hóa + placeholder, ngữ cảnh, đúng các thuật ngữ glossary được
//                   chỉ định — + tên mô hình + biến thể chat template). Lưu bản dịch THÔ
//                   (còn placeholder) để khôi phục + hậu kiểm như khi dịch thật;
//  - documents    : thông tin theo tài liệu, khóa = dấu vân tay file (xem
//                   document_fingerprint): lĩnh vực tự phân loại (kèm phiên bản bảng lĩnh
//                   vực), lĩnh vực người dùng ghi đè, trang đang đọc.
//
// Glossary KHÔNG nằm ở đây: vẫn là CSV + chỉ mục trong bộ nhớ (quyết định M6).
// Thread-safe (một mutex): UI thread đọc/ghi cache và cài đặt, luồng loader đọc thông tin
// tài liệu. Mỗi thao tác là một truy vấn có chỉ mục, cỡ dưới 1 ms.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "storage/database.hpp"
#include "translate/translator.hpp"

namespace pdfread::storage {

struct DocumentInfo {
    std::string fingerprint;
    std::string path;     // UTF-8, lần mở gần nhất
    std::string catalog;  // phiên bản bảng lĩnh vực đã dùng để phân loại (rỗng = chưa phân loại)
    std::vector<translate::DomainScore> domains;
    std::string domain_override;  // rỗng = theo phân loại tự động
    int last_page = 0;            // 0-based
};

struct CacheStats {
    std::int64_t entries = 0;
    std::int64_t hits = 0;
};

class Store {
public:
    static constexpr std::int64_t kDefaultMaxTranslations = 20000;

    // Mở/tạo file (tạo thư mục cha nếu cần). Ném StorageError.
    static std::unique_ptr<Store> open(const std::filesystem::path& path,
                                       std::int64_t max_translations = kDefaultMaxTranslations);
    static std::unique_ptr<Store> open_in_memory();

    // --- cài đặt ---
    [[nodiscard]] std::optional<std::string> get(std::string_view key);
    void set(std::string_view key, std::string_view value);
    void erase(std::string_view key);
    [[nodiscard]] std::optional<int> get_int(std::string_view key);
    void set_int(std::string_view key, int value) { set(key, std::to_string(value)); }
    [[nodiscard]] std::optional<bool> get_bool(std::string_view key);
    void set_bool(std::string_view key, bool value) { set(key, value ? "1" : "0"); }

    // --- cache bản dịch ---
    [[nodiscard]] std::optional<std::string> find_translation(std::string_view key);
    void put_translation(std::string_view key, std::string_view model, std::string_view source,
                         std::string_view output);
    void erase_translation(std::string_view key);
    void clear_translations();
    [[nodiscard]] CacheStats translation_stats();
    // Giữ tối đa `max_entries` bản dịch dùng gần nhất.
    void prune_translations(std::int64_t max_entries);

    // --- tài liệu ---
    [[nodiscard]] std::optional<DocumentInfo> find_document(std::string_view fingerprint);
    void put_document(const DocumentInfo& info);
    void set_domain_override(std::string_view fingerprint, std::string_view domain_id);
    void set_last_page(std::string_view fingerprint, int page);

private:
    explicit Store(std::unique_ptr<Database> db);
    void migrate();
    [[nodiscard]] std::int64_t tick();

    std::mutex mutex_;
    std::unique_ptr<Database> db_;
    std::int64_t clock_ = 0;  // thứ tự dùng gần nhất (đơn điệu, không phụ thuộc đồng hồ hệ thống)
};

// Khóa cache bản dịch (hex SHA-256), xem chú thích đầu file.
[[nodiscard]] std::string translation_cache_key(const translate::TranslateRequest& request,
                                                std::string_view model_name, bool add_generation_prompt);

// Dấu vân tay file tài liệu: SHA-256 của (kích thước + 1 MiB đầu + 1 MiB cuối). Không băm cả
// file để mở PDF hàng trăm MB vẫn nhanh; trùng nhau chỉ khi hai file cùng cỡ và giống hệt
// ở đầu lẫn cuối (thực tế: cùng một tài liệu). Trả rỗng nếu không đọc được.
[[nodiscard]] std::string document_fingerprint(const std::filesystem::path& path);

// Mã hóa / giải mã danh sách lĩnh vực dạng "id:score;id:score".
[[nodiscard]] std::string encode_domains(const std::vector<translate::DomainScore>& domains);
[[nodiscard]] std::vector<translate::DomainScore> decode_domains(std::string_view s);

}  // namespace pdfread::storage
