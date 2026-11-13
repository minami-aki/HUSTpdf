#include "storage/database.hpp"

#include <format>

#include <sqlite3.h>

#include "util/utf8.hpp"

namespace pdfread::storage {

// --- Statement -------------------------------------------------------------------------

Statement::Statement(sqlite3* db, std::string_view sql) : db_(db) {
    const int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr);
    if (rc != SQLITE_OK) {
        throw StorageError(std::format("SQLite prepare: {} ({})", sqlite3_errmsg(db_), sql));
    }
}

Statement::~Statement() {
    if (stmt_ != nullptr) sqlite3_finalize(stmt_);
}

void Statement::check(int rc, const char* what) const {
    if (rc != SQLITE_OK) throw StorageError(std::format("SQLite {}: {}", what, sqlite3_errmsg(db_)));
}

Statement& Statement::bind(int index, std::string_view value) {
    check(sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT), "bind");
    return *this;
}

Statement& Statement::bind(int index, std::int64_t value) {
    check(sqlite3_bind_int64(stmt_, index, value), "bind");
    return *this;
}

Statement& Statement::bind(int index, double value) {
    check(sqlite3_bind_double(stmt_, index, value), "bind");
    return *this;
}

bool Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    throw StorageError(std::format("SQLite step: {}", sqlite3_errmsg(db_)));
}

void Statement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

std::string Statement::column_text(int index) const {
    const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, index));
    if (p == nullptr) return {};
    return std::string(p, static_cast<std::size_t>(sqlite3_column_bytes(stmt_, index)));
}

std::int64_t Statement::column_int(int index) const { return sqlite3_column_int64(stmt_, index); }

double Statement::column_double(int index) const { return sqlite3_column_double(stmt_, index); }

bool Statement::column_null(int index) const { return sqlite3_column_type(stmt_, index) == SQLITE_NULL; }

// --- Database --------------------------------------------------------------------------

Database::Database(const std::filesystem::path& path) { open_utf8(path_to_utf8(path)); }

Database::Database(std::string_view special) { open_utf8(std::string(special)); }

void Database::open_utf8(const std::string& path) {
    const int rc = sqlite3_open_v2(path.c_str(), &db_,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        std::string msg = db_ != nullptr ? sqlite3_errmsg(db_) : "out of memory";
        sqlite3_close(db_);
        db_ = nullptr;
        throw StorageError(std::format("Không mở được cơ sở dữ liệu {}: {}", path, msg));
    }
    sqlite3_busy_timeout(db_, 2000);
}

Database::~Database() {
    if (db_ != nullptr) sqlite3_close_v2(db_);
}

void Database::exec(std::string_view sql) {
    char* err = nullptr;
    const std::string s(sql);
    if (sqlite3_exec(db_, s.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err != nullptr ? err : sqlite3_errmsg(db_);
        sqlite3_free(err);
        throw StorageError(std::format("SQLite exec: {}", msg));
    }
}

std::int64_t Database::changes() const { return sqlite3_changes64(db_); }

// --- Transaction -----------------------------------------------------------------------

Transaction::Transaction(Database& db) : db_(db) { db_.exec("BEGIN IMMEDIATE"); }

Transaction::~Transaction() {
    if (done_) return;
    try {
        db_.exec("ROLLBACK");
    } catch (...) {
    }
}

void Transaction::commit() {
    db_.exec("COMMIT");
    done_ = true;
}

}  // namespace pdfread::storage
