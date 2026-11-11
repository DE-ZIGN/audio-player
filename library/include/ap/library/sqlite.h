#pragma once

// A thin RAII wrapper around SQLite: errors become exceptions, statements and
// transactions clean up after themselves.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace ap::library {

class SqliteError : public std::runtime_error
{
public:
    SqliteError(int code, const std::string& what) : std::runtime_error(what), code_(code) {}
    int code() const noexcept { return code_; }

private:
    int code_;
};

class Statement
{
public:
    Statement(sqlite3* db, std::string_view sql);
    ~Statement();
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&&) = delete;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    // Parameters are numbered from 1, as in SQLite.
    Statement& bind(int i, std::int64_t v);
    Statement& bind(int i, double v);
    Statement& bind(int i, std::string_view v);
    Statement& bind(int i, std::span<const std::byte> v);
    Statement& bindNull(int i);
    template <typename T>
    Statement& bind(int i, const std::optional<T>& v)
    {
        return v ? bind(i, *v) : bindNull(i);
    }

    // true: a row is ready; false: done.
    bool step();
    // Runs to the end (for statements that return no rows).
    void run();
    void reset();

    // Columns are numbered from 0.
    bool isNull(int c) const;
    std::int64_t integer(int c) const;
    double real(int c) const;
    std::string text(int c) const;
    std::vector<std::byte> blob(int c) const;

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

class Database
{
public:
    // Opens (and creates) a database file. Foreign keys are enforced.
    static Database open(const std::filesystem::path& file);
    static Database openInMemory();

    ~Database();
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    sqlite3* handle() const noexcept { return db_; }
    const std::filesystem::path& file() const noexcept { return file_; } // empty in memory

    void exec(std::string_view sql); // one or more statements, no results
    Statement prepare(std::string_view sql) { return Statement(db_, sql); }

    int userVersion();
    std::int64_t lastInsertRowId() const;

    // Copies the whole database to `target` (overwritten) with SQLite's online backup.
    void backupTo(const std::filesystem::path& target);

    // Rolled back on destruction unless committed.
    class Transaction
    {
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

private:
    Database(sqlite3* db, std::filesystem::path file) : db_(db), file_(std::move(file)) {}
    void configure();

    sqlite3* db_ = nullptr;
    std::filesystem::path file_;
};

// "Already exists" and similar constraint failures, for callers that expect them.
bool isConstraintError(const SqliteError& e) noexcept;

} // namespace ap::library
