#include "terminology/glossary_store.hpp"

#include <algorithm>
#include <exception>
#include <format>

#include "terminology/words.hpp"
#include "util/log.hpp"
#include "util/utf8.hpp"

namespace pdfread::terminology {
namespace {

bool same_term(const std::string& a, const std::string& b) { return lower(to_u32(a)) == lower(to_u32(b)); }

}  // namespace

GlossaryStore::GlossaryStore(DomainCatalog catalog, std::filesystem::path user_glossary) : catalog_(std::move(catalog)) {
    user_.path = std::move(user_glossary);
    for (const Subdomain& s : catalog_.subdomains()) {
        if (!s.glossary.empty()) domain_files_[s.id].path = s.glossary;
    }
    refresh();
}

std::filesystem::path GlossaryStore::document_glossary_for(const std::filesystem::path& pdf) {
    if (pdf.empty()) return {};
    std::filesystem::path p = pdf;
    p.replace_filename(pdf.stem().native() + std::filesystem::path(".glossary.csv").native());
    return p;
}

void GlossaryStore::set_document(const std::filesystem::path& pdf) {
    doc_path_ = document_glossary_for(pdf);
    doc_ = File{};
    doc_.path = doc_path_;
    if (!doc_.path.empty()) load(doc_, false);
    dirty_ = true;
}

void GlossaryStore::set_domains(std::vector<translate::DomainScore> domains) {
    domains_ = std::move(domains);
    dirty_ = true;
}

bool GlossaryStore::load(File& f, bool must_exist) {
    std::error_code ec;
    const bool exists = !f.path.empty() && std::filesystem::exists(f.path, ec);
    if (!exists) {
        const bool changed = f.exists || !f.entries.empty();
        f.exists = false;
        f.entries.clear();
        if (must_exist && !f.path.empty()) errors_.push_back(std::format("Thiếu file glossary {}", path_to_utf8(f.path)));
        return changed;
    }
    const auto mtime = std::filesystem::last_write_time(f.path, ec);
    if (f.exists && mtime == f.mtime) return false;
    try {
        f.entries = load_glossary_csv(f.path);
        f.exists = true;
        f.mtime = mtime;
        log::debug("Đã nạp glossary {} ({} mục)", path_to_utf8(f.path), f.entries.size());
    } catch (const std::exception& e) {
        errors_.push_back(e.what());
        log::warn("{}", e.what());
    }
    return true;
}

bool GlossaryStore::refresh() {
    errors_.clear();
    bool changed = false;
    for (auto& [_, f] : domain_files_) changed |= load(f, true);
    changed |= load(user_, false);
    if (!doc_.path.empty()) changed |= load(doc_, false);
    if (changed) dirty_ = true;
    return changed;
}

std::shared_ptr<const GlossaryIndex> GlossaryStore::index() {
    if (!dirty_ && index_) return index_;
    std::vector<GlossaryEntry> all;
    auto add = [&](const std::vector<GlossaryEntry>& entries, Layer layer, int rank, const std::string& domain) {
        for (GlossaryEntry e : entries) {
            e.layer = layer;
            e.rank = rank;
            if (!domain.empty()) e.domain_id = domain;
            all.push_back(std::move(e));
        }
    };
    add(user_.entries, Layer::User, 0, {});
    add(doc_.entries, Layer::Document, 0, {});
    for (std::size_t i = 0; i < domains_.size(); ++i) {
        auto it = domain_files_.find(domains_[i].domain_id);
        if (it == domain_files_.end()) continue;
        add(it->second.entries, i == 0 ? Layer::PrimaryDomain : Layer::OtherDomain, static_cast<int>(i), it->first);
    }
    // Lĩnh vực ngoài tài liệu: chỉ để hiện "nghĩa khác" khi tra từ.
    int rank = 100;
    for (const auto& [id, f] : domain_files_) {
        const bool used = std::any_of(domains_.begin(), domains_.end(), [&](const auto& d) { return d.domain_id == id; });
        if (!used) add(f.entries, Layer::Unrelated, rank++, id);
    }
    index_ = std::make_shared<const GlossaryIndex>(std::move(all));
    dirty_ = false;
    return index_;
}

GlossaryStore::File& GlossaryStore::file_for(Layer layer) {
    if (layer == Layer::User) return user_;
    if (layer == Layer::Document && !doc_.path.empty()) return doc_;
    throw std::runtime_error("Chỉ sửa được glossary người dùng hoặc glossary của tài liệu đang mở");
}

void GlossaryStore::upsert(Layer layer, GlossaryEntry entry) {
    File& f = file_for(layer);
    entry.domain_id.clear();
    auto it = std::find_if(f.entries.begin(), f.entries.end(), [&](const auto& e) { return same_term(e.term_en, entry.term_en); });
    if (it != f.entries.end()) *it = std::move(entry);
    else f.entries.push_back(std::move(entry));
    save_glossary_csv(f.path, f.entries);
    std::error_code ec;
    f.exists = true;
    f.mtime = std::filesystem::last_write_time(f.path, ec);
    dirty_ = true;
}

void GlossaryStore::remove(Layer layer, const std::string& term_en) {
    File& f = file_for(layer);
    const auto before = f.entries.size();
    std::erase_if(f.entries, [&](const auto& e) { return same_term(e.term_en, term_en); });
    if (f.entries.size() == before) return;
    save_glossary_csv(f.path, f.entries);
    std::error_code ec;
    f.mtime = std::filesystem::last_write_time(f.path, ec);
    dirty_ = true;
}

std::size_t GlossaryStore::import_into_user(const std::filesystem::path& csv) {
    std::vector<GlossaryEntry> incoming = load_glossary_csv(csv);
    for (GlossaryEntry& e : incoming) {
        e.domain_id.clear();
        auto it = std::find_if(user_.entries.begin(), user_.entries.end(),
                               [&](const auto& u) { return same_term(u.term_en, e.term_en); });
        if (it != user_.entries.end()) *it = e;
        else user_.entries.push_back(e);
    }
    save_glossary_csv(user_.path, user_.entries);
    std::error_code ec;
    user_.exists = true;
    user_.mtime = std::filesystem::last_write_time(user_.path, ec);
    dirty_ = true;
    return incoming.size();
}

}  // namespace pdfread::terminology
