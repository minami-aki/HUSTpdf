#include "storage/store.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <format>
#include <fstream>
#include <vector>

#include "translate/prompt.hpp"
#include "util/log.hpp"
#include "util/sha256.hpp"

namespace pdfread::storage {

namespace {

constexpr int kSchemaVersion = 1;

constexpr std::string_view kSchema = R"sql(
CREATE TABLE IF NOT EXISTS settings(
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS translations(
    key     TEXT PRIMARY KEY,
    model   TEXT NOT NULL,
    source  TEXT NOT NULL,
    output  TEXT NOT NULL,
    created INTEGER NOT NULL,
    used    INTEGER NOT NULL,
    hits    INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS translations_used ON translations(used);

CREATE TABLE IF NOT EXISTS documents(
    fingerprint     TEXT PRIMARY KEY,
    path            TEXT NOT NULL,
    catalog         TEXT NOT NULL DEFAULT '',
    domains         TEXT NOT NULL DEFAULT '',
    domain_override TEXT NOT NULL DEFAULT '',
    last_page       INTEGER NOT NULL DEFAULT 0
) WITHOUT ROWID;
)sql";

}  // namespace

std::unique_ptr<Store> Store::open(const std::filesystem::path& path, std::int64_t max_translations) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    auto store = std::unique_ptr<Store>(new Store(std::make_unique<Database>(path)));
    store->db_->exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;");
    store->prune_translations(max_translations);
    return store;
}

std::unique_ptr<Store> Store::open_in_memory() {
    return std::unique_ptr<Store>(new Store(std::make_unique<Database>(std::string_view(":memory:"))));
}

Store::Store(std::unique_ptr<Database> db) : db_(std::move(db)) {
    migrate();
    auto st = db_->prepare("SELECT COALESCE(MAX(used), 0) FROM translations");
    if (st.step()) clock_ = st.column_int(0);
}

void Store::migrate() {
    auto st = db_->prepare("PRAGMA user_version");
    const std::int64_t version = st.step() ? st.column_int(0) : 0;
    if (version > kSchemaVersion) {
        throw StorageError(std::format("Cơ sở dữ liệu thuộc phiên bản mới hơn ({} > {})", version, kSchemaVersion));
    }
    Transaction tx(*db_);
    db_->exec(kSchema);
    db_->exec(std::format("PRAGMA user_version={}", kSchemaVersion));
    tx.commit();
}

std::int64_t Store::tick() { return ++clock_; }

// --- cài đặt ---------------------------------------------------------------------------

std::optional<std::string> Store::get(std::string_view key) {
    std::lock_guard lock(mutex_);
    auto st = db_->prepare("SELECT value FROM settings WHERE key = ?1");
    st.bind(1, key);
    if (!st.step()) return std::nullopt;
    return st.column_text(0);
}

void Store::set(std::string_view key, std::string_view value) {
    std::lock_guard lock(mutex_);
    auto st = db_->prepare("INSERT INTO settings(key, value) VALUES(?1, ?2) "
                           "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    st.bind(1, key).bind(2, value);
    st.step();
}

void Store::erase(std::string_view key) {
    std::lock_guard lock(mutex_);
    auto st = db_->prepare("DELETE FROM settings WHERE key = ?1");
    st.bind(1, key);
    st.step();
}

std::optional<int> Store::get_int(std::string_view key) {
    const auto v = get(key);
    if (!v) return std::nullopt;
    int out = 0;
    const auto [p, ec] = std::from_chars(v->data(), v->data() + v->size(), out);
    if (ec != std::errc{} || p != v->data() + v->size()) return std::nullopt;
    return out;
}

std::optional<bool> Store::get_bool(std::string_view key) {
    const auto v = get(key);
    if (!v) return std::nullopt;
    if (*v == "1" || *v == "true") return true;
    if (*v == "0" || *v == "false") return false;
    return std::nullopt;
}

// --- cache bản dịch -------------------------------------------------------------------

std::optional<std::string> Store::find_translation(std::string_view key) {
    std::lock_guard lock(mutex_);
    auto st = db_->prepare("SELECT output FROM translations WHERE key = ?1");
    st.bind(1, key);
    if (!st.step()) return std::nullopt;
    std::string out = st.column_text(0);
    auto up = db_->prepare("UPDATE translations SET used = ?2, hits = hits + 1 WHERE key = ?1");
    up.bind(1, key).bind(2, tick());
    up.step();
    return out;
}

void Store::put_translation(std::string_view key, std::string_view model, std::string_view source,
                            std::string_view output) {
    std::lock_guard lock(mutex_);
    const std::int64_t now = tick();
    auto st = db_->prepare(
        "INSERT INTO translations(key, model, source, output, created, used) VALUES(?1, ?2, ?3, ?4, ?5, ?5) "
        "ON CONFLICT(key) DO UPDATE SET output = excluded.output, model = excluded.model, used = excluded.used");
    st.bind(1, key).bind(2, model).bind(3, source).bind(4, output).bind(5, now);
    st.step();
}

void Store::erase_translation(std::string_view key) {
    std::lock_guard lock(mutex_);
    auto st = db_->prepare("DELETE FROM translations WHERE key = ?1");
    st.bind(1, key);
    st.step();
}

void Store::clear_translations() {
    std::lock_guard lock(mutex_);
    db_->exec("DELETE FROM translations");
}

CacheStats Store::translation_stats() {
    std::lock_guard lock(mutex_);
    auto st = db_->prepare("SELECT COUNT(*), COALESCE(SUM(hits), 0) FROM translations");
    CacheStats s;
    if (st.step()) {
        s.entries = st.column_int(0);
        s.hits = st.column_int(1);
    }
    return s;
}

void Store::prune_translations(std::int64_t max_entries) {
    std::lock_guard lock(mutex_);
    auto st = db_->prepare(
        "DELETE FROM translations WHERE key IN "
        "(SELECT key FROM translations ORDER BY used DESC LIMIT -1 OFFSET ?1)");
    st.bind(1, std::max<std::int64_t>(0, max_entries));
    st.step();
    if (db_->changes() > 0) log::info("Cache bản dịch: xóa {} mục cũ", db_->changes());
}

// --- tài liệu --------------------------------------------------------------------------

std::optional<DocumentInfo> Store::find_document(std::string_view fingerprint) {
    std::lock_guard lock(mutex_);
    auto st = db_->prepare(
        "SELECT path, catalog, domains, domain_override, last_page FROM documents WHERE fingerprint = ?1");
    st.bind(1, fingerprint);
    if (!st.step()) return std::nullopt;
    DocumentInfo info;
    info.fingerprint = std::string(fingerprint);
    info.path = st.column_text(0);
    info.catalog = st.column_text(1);
    info.domains = decode_domains(st.column_text(2));
    info.domain_override = st.column_text(3);
    info.last_page = static_cast<int>(st.column_int(4));
    return info;
}

void Store::put_document(const DocumentInfo& info) {
    std::lock_guard lock(mutex_);
    auto st = db_->prepare(
        "INSERT INTO documents(fingerprint, path, catalog, domains, domain_override, last_page) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6) ON CONFLICT(fingerprint) DO UPDATE SET "
        "path = excluded.path, catalog = excluded.catalog, domains = excluded.domains, "
        "domain_override = excluded.domain_override, last_page = excluded.last_page");
    st.bind(1, info.fingerprint)
        .bind(2, info.path)
        .bind(3, info.catalog)
        .bind(4, encode_domains(info.domains))
        .bind(5, info.domain_override)
        .bind(6, std::int64_t{info.last_page});
    st.step();
}

void Store::set_domain_override(std::string_view fingerprint, std::string_view domain_id) {
    std::lock_guard lock(mutex_);
    auto st = db_->prepare("UPDATE documents SET domain_override = ?2 WHERE fingerprint = ?1");
    st.bind(1, fingerprint).bind(2, domain_id);
    st.step();
}

void Store::set_last_page(std::string_view fingerprint, int page) {
    std::lock_guard lock(mutex_);
    auto st = db_->prepare("UPDATE documents SET last_page = ?2 WHERE fingerprint = ?1");
    st.bind(1, fingerprint).bind(2, std::int64_t{page});
    st.step();
}

// --- tiện ích --------------------------------------------------------------------------

std::string translation_cache_key(const translate::TranslateRequest& request, std::string_view model_name,
                                  bool add_generation_prompt) {
    Sha256 h;
    h.update("pdfread-cache-v1\x1f");
    h.update(model_name);
    h.update(add_generation_prompt ? "\x1fgen\x1f" : "\x1fplain\x1f");
    h.update(request.raw_user_message.empty() ? translate::build_user_prompt(request) : request.raw_user_message);
    return Sha256::to_hex(h.finish());
}

std::string document_fingerprint(const std::filesystem::path& path) {
    constexpr std::size_t kChunk = 1 << 20;
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::uint64_t>(in.tellg());
    in.seekg(0);
    Sha256 h;
    h.update(std::format("pdfread-doc-v1:{}:", size));
    std::vector<char> buf(kChunk);
    in.read(buf.data(), static_cast<std::streamsize>(std::min<std::uint64_t>(size, kChunk)));
    h.update(buf.data(), static_cast<std::size_t>(in.gcount()));
    if (size > kChunk) {
        const std::uint64_t tail = std::min<std::uint64_t>(size - kChunk, kChunk);
        in.seekg(static_cast<std::streamoff>(size - tail));
        in.read(buf.data(), static_cast<std::streamsize>(tail));
        h.update(buf.data(), static_cast<std::size_t>(in.gcount()));
    }
    if (in.bad()) return {};
    return Sha256::to_hex(h.finish());
}

std::string encode_domains(const std::vector<translate::DomainScore>& domains) {
    std::string out;
    for (const auto& d : domains) {
        if (!out.empty()) out += ';';
        out += std::format("{}:{:.4f}", d.domain_id, d.score);
    }
    return out;
}

std::vector<translate::DomainScore> decode_domains(std::string_view s) {
    std::vector<translate::DomainScore> out;
    while (!s.empty()) {
        const std::size_t semi = s.find(';');
        const std::string_view item = s.substr(0, semi);
        s = semi == std::string_view::npos ? std::string_view{} : s.substr(semi + 1);
        const std::size_t colon = item.rfind(':');
        if (colon == std::string_view::npos || colon == 0) continue;
        translate::DomainScore d;
        d.domain_id = std::string(item.substr(0, colon));
        const std::string_view num = item.substr(colon + 1);
        if (std::from_chars(num.data(), num.data() + num.size(), d.score).ec != std::errc{}) continue;
        out.push_back(std::move(d));
    }
    return out;
}

}  // namespace pdfread::storage
