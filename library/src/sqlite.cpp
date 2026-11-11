#include <ap/library/sqlite.h>

#include <sqlite3.h>

#include <utility>

namespace ap::library {

namespace {

std::string utf8(const std::filesystem::path& p)
{
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

[[noreturn]] void fail(sqlite3* db, int rc, const std::string& context)
{
    const char* msg = db ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
    throw SqliteError(rc, context + ": " + (msg ? msg : "unknown error"));
}

sqlite3* openHandle(const std::string& name, int flags)
{
    sqlite3* db = nullptr;
    const int rc = sqlite3_open_v2(name.c_str(), &db, flags | SQLITE_OPEN_EXRESCODE, nullptr);
    if (rc != SQLITE_OK)
    {
        const std::string msg = db ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
        sqlite3_close(db);
        throw SqliteError(rc, "cannot open database " + name + ": " + msg);
    }
    return db;
}

} // namespace

// --- Statement ---------------------------------------------------------------

Statement::Statement(sqlite3* db, std::string_view sql) : db_(db)
{
    const int rc = sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr);
    if (rc != SQLITE_OK)
        fail(db, rc, "prepare: " + std::string(sql));
}

Statement::~Statement()
{
    sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)), stmt_(std::exchange(other.stmt_, nullptr))
{
}

Statement& Statement::bind(int i, std::int64_t v)
{
    if (const int rc = sqlite3_bind_int64(stmt_, i, v); rc != SQLITE_OK)
        fail(db_, rc, "bind");
    return *this;
}

Statement& Statement::bind(int i, double v)
{
    if (const int rc = sqlite3_bind_double(stmt_, i, v); rc != SQLITE_OK)
        fail(db_, rc, "bind");
    return *this;
}

Statement& Statement::bind(int i, std::string_view v)
{
    if (const int rc = sqlite3_bind_text64(stmt_, i, v.data(), v.size(), SQLITE_TRANSIENT, SQLITE_UTF8); rc != SQLITE_OK)
        fail(db_, rc, "bind");
    return *this;
}

Statement& Statement::bind(int i, std::span<const std::byte> v)
{
    if (const int rc = sqlite3_bind_blob64(stmt_, i, v.data(), v.size(), SQLITE_TRANSIENT); rc != SQLITE_OK)
        fail(db_, rc, "bind");
    return *this;
}

Statement& Statement::bindNull(int i)
{
    if (const int rc = sqlite3_bind_null(stmt_, i); rc != SQLITE_OK)
        fail(db_, rc, "bind");
    return *this;
}

bool Statement::step()
{
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW)
        return true;
    if (rc == SQLITE_DONE)
        return false;
    fail(db_, rc, std::string("step: ") + sqlite3_sql(stmt_));
}

void Statement::run()
{
    while (step())
    {
    }
}

void Statement::reset()
{
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

bool Statement::isNull(int c) const
{
    return sqlite3_column_type(stmt_, c) == SQLITE_NULL;
}

std::int64_t Statement::integer(int c) const
{
    return sqlite3_column_int64(stmt_, c);
}

double Statement::real(int c) const
{
    return sqlite3_column_double(stmt_, c);
}

std::string Statement::text(int c) const
{
    const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, c));
    return p ? std::string(p, static_cast<std::size_t>(sqlite3_column_bytes(stmt_, c))) : std::string();
}

std::vector<std::byte> Statement::blob(int c) const
{
    const auto* p = static_cast<const std::byte*>(sqlite3_column_blob(stmt_, c));
    const auto n = static_cast<std::size_t>(sqlite3_column_bytes(stmt_, c));
    return p ? std::vector<std::byte>(p, p + n) : std::vector<std::byte>();
}

// --- Database ----------------------------------------------------------------

Database Database::open(const std::filesystem::path& file)
{
    Database db(openHandle(utf8(file), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE), file);
    db.configure();
    return db;
}

Database Database::openInMemory()
{
    Database db(openHandle(":memory:", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_MEMORY), {});
    db.configure();
    return db;
}

void Database::configure()
{
    sqlite3_extended_result_codes(db_, 1);
    sqlite3_busy_timeout(db_, 5000);
    exec("PRAGMA foreign_keys = ON;");
}

Database::~Database()
{
    sqlite3_close(db_);
}

Database::Database(Database&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)), file_(std::move(other.file_))
{
}

Database& Database::operator=(Database&& other) noexcept
{
    if (this != &other)
    {
        sqlite3_close(db_);
        db_ = std::exchange(other.db_, nullptr);
        file_ = std::move(other.file_);
    }
    return *this;
}

void Database::exec(std::string_view sql)
{
    char* err = nullptr;
    const std::string s(sql);
    const int rc = sqlite3_exec(db_, s.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK)
    {
        const std::string msg = err ? err : sqlite3_errmsg(db_);
        sqlite3_free(err);
        throw SqliteError(rc, "exec: " + msg);
    }
}

int Database::userVersion()
{
    auto st = prepare("PRAGMA user_version");
    st.step();
    return static_cast<int>(st.integer(0));
}

std::int64_t Database::lastInsertRowId() const
{
    return sqlite3_last_insert_rowid(db_);
}

void Database::backupTo(const std::filesystem::path& target)
{
    std::error_code ec;
    std::filesystem::remove(target, ec);
    sqlite3* dest = openHandle(utf8(target), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    sqlite3_backup* b = sqlite3_backup_init(dest, "main", db_, "main");
    if (!b)
    {
        const std::string msg = sqlite3_errmsg(dest);
        sqlite3_close(dest);
        throw SqliteError(SQLITE_ERROR, "backup: " + msg);
    }
    const int rc = sqlite3_backup_step(b, -1);
    sqlite3_backup_finish(b);
    const std::string msg = sqlite3_errmsg(dest);
    sqlite3_close(dest);
    if (rc != SQLITE_DONE)
        throw SqliteError(rc, "backup: " + msg);
}

Database::Transaction::Transaction(Database& db) : db_(db), nested_(sqlite3_get_autocommit(db.handle()) == 0)
{
    db_.exec(nested_ ? "SAVEPOINT nested" : "BEGIN IMMEDIATE");
}

Database::Transaction::~Transaction()
{
    if (done_)
        return;
    if (nested_)
        sqlite3_exec(db_.handle(), "ROLLBACK TO nested; RELEASE nested", nullptr, nullptr, nullptr);
    else
        sqlite3_exec(db_.handle(), "ROLLBACK", nullptr, nullptr, nullptr);
}

void Database::Transaction::commit()
{
    db_.exec(nested_ ? "RELEASE nested" : "COMMIT");
    done_ = true;
}

bool isConstraintError(const SqliteError& e) noexcept
{
    return (e.code() & 0xff) == SQLITE_CONSTRAINT;
}

} // namespace ap::library
