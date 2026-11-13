#pragma once
// Bảng phân loại lĩnh vực (tầng 1): nhóm -> lĩnh vực con, đọc từ data/domains.json.
//
//   { "groups": [ { "id": "mechanical", "name_vi": "...", "name_en": "...",
//                   "subdomains": [ { "id": "mechanical.mechatronics", "name_vi": "...", "name_en": "...",
//                                     "keywords": ["mechatronics", "servo motor", ...],
//                                     "glossary": "glossary/mechanical.mechatronics.csv" } ] } ] }

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace pdfread::terminology {

struct Subdomain {
    std::string id;
    std::string group_id;
    std::string name_vi;
    std::string name_en;
    std::vector<std::string> keywords;  // tiếng Anh, viết thường
    std::filesystem::path glossary;     // đường dẫn tuyệt đối tới file CSV
};

struct DomainGroup {
    std::string id;
    std::string name_vi;
    std::string name_en;
    std::vector<std::size_t> subdomains;  // chỉ số trong DomainCatalog::subdomains()
};

class DomainError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class DomainCatalog {
public:
    // Đọc file JSON; đường dẫn glossary tương đối tính từ thư mục chứa file. Ném DomainError.
    [[nodiscard]] static DomainCatalog load(const std::filesystem::path& json_path);
    [[nodiscard]] static DomainCatalog parse(const std::string& json, const std::filesystem::path& base_dir);

    [[nodiscard]] const std::vector<DomainGroup>& groups() const noexcept { return groups_; }
    [[nodiscard]] const std::vector<Subdomain>& subdomains() const noexcept { return subdomains_; }
    [[nodiscard]] const Subdomain* find(std::string_view id) const noexcept;
    [[nodiscard]] const DomainGroup* group_of(const Subdomain& s) const noexcept;
    [[nodiscard]] bool empty() const noexcept { return subdomains_.empty(); }

private:
    std::vector<DomainGroup> groups_;
    std::vector<Subdomain> subdomains_;
};

}  // namespace pdfread::terminology
