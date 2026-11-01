#include "storage/Database.hpp"

#include <sqlite3.h>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace nc {

// ---------- time helpers ----------

std::string isoSecondsAgo(int seconds) {
    std::time_t t = std::time(nullptr) - seconds;
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string nowIso() { return isoSecondsAgo(0); }

std::string normalizeTimestamp(std::string ts) {
    if (ts.size() < 19) return ts;
    if (ts[10] == ' ') ts[10] = 'T';
    if (ts.back() != 'Z') ts += 'Z';
    return ts;
}

// ---------- Statement ----------

Statement::Statement(sqlite3* db, const std::string& sql) : db_(db) {
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK)
        throw DbError(std::string("prepare failed: ") + sqlite3_errmsg(db) + " [" + sql + "]");
}

Statement::~Statement() {
    if (stmt_) sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& o) noexcept : db_(o.db_), stmt_(o.stmt_) {
    o.stmt_ = nullptr;
}

Statement& Statement::operator=(Statement&& o) noexcept {
    if (this != &o) {
        if (stmt_) sqlite3_finalize(stmt_);
        db_ = o.db_;
        stmt_ = o.stmt_;
        o.stmt_ = nullptr;
    }
    return *this;
}

static void check(sqlite3* db, int rc, const char* what) {
    if (rc != SQLITE_OK) throw DbError(std::string(what) + ": " + sqlite3_errmsg(db));
}

Statement& Statement::bindInt(int idx, int64_t v) {
    check(db_, sqlite3_bind_int64(stmt_, idx, v), "bind int");
    return *this;
}
Statement& Statement::bindDouble(int idx, double v) {
    check(db_, sqlite3_bind_double(stmt_, idx, v), "bind double");
    return *this;
}
Statement& Statement::bindText(int idx, const std::string& v) {
    check(db_, sqlite3_bind_text(stmt_, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT), "bind text");
    return *this;
}
Statement& Statement::bindOptText(int idx, const std::string& v) {
    return v.empty() ? bindNull(idx) : bindText(idx, v);
}
Statement& Statement::bindNull(int idx) {
    check(db_, sqlite3_bind_null(stmt_, idx), "bind null");
    return *this;
}

bool Statement::step() {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    throw DbError(std::string("step failed: ") + sqlite3_errmsg(db_));
}

void Statement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

int64_t Statement::getInt(int col) const { return sqlite3_column_int64(stmt_, col); }
double Statement::getDouble(int col) const { return sqlite3_column_double(stmt_, col); }
bool Statement::isNull(int col) const { return sqlite3_column_type(stmt_, col) == SQLITE_NULL; }

std::string Statement::getText(int col) const {
    const unsigned char* p = sqlite3_column_text(stmt_, col);
    if (!p) return {};
    return std::string(reinterpret_cast<const char*>(p), static_cast<size_t>(sqlite3_column_bytes(stmt_, col)));
}

// ---------- Database ----------

Database::Database(const std::string& path) {
    if (path.empty()) throw DbError("database path is empty");
    if (path != ":memory:") {
        auto parent = std::filesystem::path(path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
    }
    int rc = sqlite3_open_v2(path.c_str(), &db_,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "out of memory";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw DbError("cannot open database " + path + ": " + msg);
    }
    sqlite3_busy_timeout(db_, 5000);
    exec("PRAGMA foreign_keys = ON;");     // off by default in SQLite, per connection
    exec("PRAGMA journal_mode = WAL;");
    exec("PRAGMA synchronous = NORMAL;");
}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

void Database::exec(const std::string& sql) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw DbError("exec failed: " + msg);
    }
}

void Database::execFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw DbError("cannot read SQL file: " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    exec(ss.str());
}

Statement Database::prepare(const std::string& sql) { return Statement(db_, sql); }

int64_t Database::lastInsertId() const { return sqlite3_last_insert_rowid(db_); }
int Database::changes() const { return sqlite3_changes(db_); }

bool Database::ping() noexcept {
    try {
        std::lock_guard<std::recursive_mutex> lock(mu_);
        auto st = prepare("SELECT 1");
        return st.step() && st.getInt(0) == 1;
    } catch (...) {
        return false;
    }
}

// ---------- Transaction ----------

Transaction::Transaction(Database& db) : db_(db), lock_(db.mutex()) {
    db_.exec("BEGIN IMMEDIATE");
}

Transaction::~Transaction() {
    if (!done_) {
        try { db_.exec("ROLLBACK"); } catch (...) {}
    }
}

void Transaction::commit() {
    db_.exec("COMMIT");
    done_ = true;
}

}  // namespace nc
