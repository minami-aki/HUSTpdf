#include "terminology/domains.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "util/utf8.hpp"

namespace pdfread::terminology {
namespace {

std::string lower_ascii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::string get_string(const nlohmann::json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_string()) throw DomainError(std::format("thiếu trường \"{}\"", key));
    return j[key].get<std::string>();
}

}  // namespace

DomainCatalog DomainCatalog::load(const std::filesystem::path& json_path) {
    std::ifstream f(json_path, std::ios::binary);
    if (!f) throw DomainError(std::format("Không mở được {}", path_to_utf8(json_path)));
    std::stringstream ss;
    ss << f.rdbuf();
    return parse(ss.str(), json_path.parent_path());
}

DomainCatalog DomainCatalog::parse(const std::string& json, const std::filesystem::path& base_dir) {
    DomainCatalog cat;
    try {
        const nlohmann::json root = nlohmann::json::parse(json);
        for (const auto& g : root.at("groups")) {
            DomainGroup group{get_string(g, "id"), get_string(g, "name_vi"), get_string(g, "name_en"), {}};
            for (const auto& s : g.at("subdomains")) {
                Subdomain sub;
                sub.id = get_string(s, "id");
                sub.group_id = group.id;
                sub.name_vi = get_string(s, "name_vi");
                sub.name_en = get_string(s, "name_en");
                if (s.contains("keywords")) {
                    for (const auto& k : s["keywords"]) sub.keywords.push_back(lower_ascii(k.get<std::string>()));
                }
                if (s.contains("glossary")) sub.glossary = base_dir / path_from_utf8(s["glossary"].get<std::string>());
                if (cat.find(sub.id) != nullptr) throw DomainError(std::format("trùng id lĩnh vực \"{}\"", sub.id));
                group.subdomains.push_back(cat.subdomains_.size());
                cat.subdomains_.push_back(std::move(sub));
            }
            cat.groups_.push_back(std::move(group));
        }
    } catch (const nlohmann::json::exception& e) {
        throw DomainError(std::format("domains.json không hợp lệ: {}", e.what()));
    }
    return cat;
}

const Subdomain* DomainCatalog::find(std::string_view id) const noexcept {
    auto it = std::find_if(subdomains_.begin(), subdomains_.end(), [&](const Subdomain& s) { return s.id == id; });
    return it == subdomains_.end() ? nullptr : &*it;
}

const DomainGroup* DomainCatalog::group_of(const Subdomain& s) const noexcept {
    auto it = std::find_if(groups_.begin(), groups_.end(), [&](const DomainGroup& g) { return g.id == s.group_id; });
    return it == groups_.end() ? nullptr : &*it;
}

}  // namespace pdfread::terminology
