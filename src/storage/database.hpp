#pragma once
// Lớp RAII mỏng bọc SQLite (sqlite3*, sqlite3_stmt*). Lỗi -> StorageError.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace pdfread::storage {

class StorageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Statement {
public:
    Statement(sqlite3* db, std::string_view sql);
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept : db_(other.db_), stmt_(other.stmt_) { other.stmt_ = nullptr; }
    Statement& operator=(Statement&&) = delete;

    // Chỉ số tham số bắt đầu từ 1 (như SQLite).
    Statement& bind(int index, std::string_view value);
    Statement& bind(int index, std::int64_t value);
    Statement& bind(int index, double value);

    // true: có một hàng mới (đọc bằng column_*); false: hết.
    bool step();
    void reset();

    [[nodiscard]] std::string column_text(int index) const;  // cột từ 0
    [[nodiscard]] std::int64_t column_int(int index) const;
    [[nodiscard]] double column_double(int index) const;
    [[nodiscard]] bool column_null(int index) const;

private:
    void check(int rc, const char* what) const;

    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};

class Database {
public:
    // Mở (tạo nếu chưa có). ":memory:" cho test.
    explicit Database(const std::filesystem::path& path);
    explicit Database(std::string_view special);  // ":memory:"
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void exec(std::string_view sql);
    [[nodiscard]] Statement prepare(std::string_view sql) { return Statement(db_, sql); }
    [[nodiscard]] std::int64_t changes() const;
    [[nodiscard]] sqlite3* handle() const noexcept { return db_; }

private:
    void open_utf8(const std::string& path);

    sqlite3* db_ = nullptr;
};

// Giao dịch: commit khi gọi commit(), tự rollback nếu ra khỏi scope mà chưa commit.
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void commit();

private:
    Database& db_;
    bool done_ = false;
};

}  // namespace pdfread::storage
