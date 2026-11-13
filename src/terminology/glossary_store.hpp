#pragma once
// Quản lý các glossary đang dùng và dựng chỉ mục theo lĩnh vực của tài liệu đang mở.
//
//  - Glossary lĩnh vực: data/glossary/<id>.csv (chỉ đọc; sửa thì ghi vào glossary người dùng).
//  - Glossary người dùng: %APPDATA%/Pdfread/glossary/user.csv (ưu tiên cao nhất).
//  - Glossary tài liệu: "<tên file>.glossary.csv" đặt cạnh PDF.
// Mọi file được đọc vào bộ nhớ; refresh() đọc lại file nào đã đổi (theo thời điểm sửa).
// Chỉ dùng trên một luồng (UI).

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "terminology/domains.hpp"
#include "terminology/glossary.hpp"
#include "translate/translator.hpp"

namespace pdfread::terminology {

class GlossaryStore {
public:
    GlossaryStore(DomainCatalog catalog, std::filesystem::path user_glossary);

    [[nodiscard]] const DomainCatalog& catalog() const noexcept { return catalog_; }

    // Tài liệu đang mở (rỗng = không có). Glossary tài liệu: "<stem>.glossary.csv" cạnh file.
    void set_document(const std::filesystem::path& pdf);
    [[nodiscard]] const std::filesystem::path& document_glossary_path() const noexcept { return doc_path_; }
    [[nodiscard]] static std::filesystem::path document_glossary_for(const std::filesystem::path& pdf);

    // Lĩnh vực của tài liệu, theo điểm giảm dần (lĩnh vực đầu = "điểm cao nhất").
    void set_domains(std::vector<translate::DomainScore> domains);
    [[nodiscard]] const std::vector<translate::DomainScore>& domains() const noexcept { return domains_; }

    // Đọc lại các file đã thay đổi trên đĩa. Trả true nếu có gì thay đổi.
    bool refresh();

    // Chỉ mục hiện tại (dựng lại khi cần).
    [[nodiscard]] std::shared_ptr<const GlossaryIndex> index();

    [[nodiscard]] const std::vector<GlossaryEntry>& user_entries() const noexcept { return user_.entries; }
    [[nodiscard]] const std::vector<GlossaryEntry>& document_entries() const noexcept { return doc_.entries; }
    [[nodiscard]] const std::filesystem::path& user_glossary_path() const noexcept { return user_.path; }

    // Thêm hoặc thay (theo term_en, không phân biệt hoa/thường) rồi ghi file ngay.
    // Ném std::runtime_error khi không ghi được.
    void upsert(Layer layer, GlossaryEntry entry);
    void remove(Layer layer, const std::string& term_en);
    // Gộp các mục của một file CSV vào glossary người dùng. Trả số mục đã thêm/thay.
    std::size_t import_into_user(const std::filesystem::path& csv);

    // Lỗi đọc file gần nhất (để hiển thị), rỗng nếu không có.
    [[nodiscard]] const std::vector<std::string>& errors() const noexcept { return errors_; }

private:
    struct File {
        std::filesystem::path path;
        std::filesystem::file_time_type mtime{};
        bool exists = false;
        std::vector<GlossaryEntry> entries;
    };
    bool load(File& f, bool must_exist);
    File& file_for(Layer layer);

    DomainCatalog catalog_;
    std::map<std::string, File> domain_files_;  // theo id lĩnh vực
    File user_;
    File doc_;
    std::filesystem::path doc_path_;
    std::vector<translate::DomainScore> domains_;
    std::shared_ptr<const GlossaryIndex> index_;
    bool dirty_ = true;
    std::vector<std::string> errors_;
};

}  // namespace pdfread::terminology
