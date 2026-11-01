#pragma once
// Thin RAII wrapper over one SQLite connection. Only storage/ includes this; layers above
// talk to the repos. One connection, FULLMUTEX, plus a recursive mutex that every repo method
// holds for its whole body, so Drogon threads and JobManager workers can share it safely.
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>

struct sqlite3;
struct sqlite3_stmt;

namespace nc {

struct DbError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ISO 8601 UTC, the format api-spec.md uses everywhere: 2026-04-12T09:31:07Z
std::string nowIso();
std::string isoSecondsAgo(int seconds);
// "2026-04-12 09:31:07" or "2026-04-12T09:31:07" -> "2026-04-12T09:31:07Z". Other input is returned as is.
std::string normalizeTimestamp(std::string ts);

class Statement {
public:
    Statement(sqlite3* db, const std::string& sql);
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& o) noexcept;
    Statement& operator=(Statement&& o) noexcept;

    // Parameters are 1-based, as in SQLite.
    Statement& bindInt(int idx, int64_t v);
    Statement& bindDouble(int idx, double v);
    Statement& bindText(int idx, const std::string& v);
    Statement& bindOptText(int idx, const std::string& v);   // empty string is stored as NULL
    Statement& bindNull(int idx);

    bool step();                       // true: a row is ready; false: done. Throws DbError on failure
    void reset();

    // Columns are 0-based.
    int64_t     getInt(int col) const;
    double      getDouble(int col) const;
    std::string getText(int col) const;   // NULL reads as ""
    bool        isNull(int col) const;

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

class Database {
public:
    // ":memory:" works for tests. Creates the parent directory of a file path.
    explicit Database(const std::string& path);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void exec(const std::string& sql);            // one or more statements, no results
    void execFile(const std::string& path);       // run a .sql script (schema.sql, seed.sql)
    Statement prepare(const std::string& sql);

    int64_t lastInsertId() const;
    int     changes() const;                      // rows touched by the last INSERT/UPDATE/DELETE
    bool    ping() noexcept;                      // for GET /health ("db": "ok")

    std::recursive_mutex& mutex() { return mu_; }

private:
    sqlite3* db_ = nullptr;
    std::recursive_mutex mu_;
};

// BEGIN IMMEDIATE ... COMMIT; rolls back if commit() was not called. Holds the db mutex meanwhile.
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void commit();

private:
    Database& db_;
    std::unique_lock<std::recursive_mutex> lock_;
    bool done_ = false;
};

}  // namespace nc
