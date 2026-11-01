#include "storage/UserRepo.hpp"

namespace nc {

namespace {
constexpr const char* kUserCols = "id, username, password_hash, created_at";

User readUser(Statement& st) {
    User u;
    u.id = st.getInt(0);
    u.username = st.getText(1);
    u.password_hash = st.getText(2);
    u.created_at = normalizeTimestamp(st.getText(3));
    return u;
}
}  // namespace

std::optional<User> UserRepo::findById(int64_t id) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    auto st = db_.prepare(std::string("SELECT ") + kUserCols + " FROM users WHERE id = ?");
    st.bindInt(1, id);
    if (!st.step()) return std::nullopt;
    return readUser(st);
}

std::optional<User> UserRepo::findByUsername(const std::string& username) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    auto st = db_.prepare(std::string("SELECT ") + kUserCols + " FROM users WHERE username = ?");
    st.bindText(1, username);
    if (!st.step()) return std::nullopt;
    return readUser(st);
}

std::optional<User> UserRepo::create(const std::string& username, const std::string& password_hash) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    if (findByUsername(username)) return std::nullopt;
    auto st = db_.prepare("INSERT INTO users (username, password_hash, created_at) VALUES (?, ?, ?)");
    st.bindText(1, username).bindText(2, password_hash).bindText(3, nowIso());
    st.step();
    return findById(db_.lastInsertId());
}

User UserRepo::ensureUser(const std::string& username, const std::string& password_hash) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    if (auto u = findByUsername(username)) return *u;
    return *create(username, password_hash);
}

void UserRepo::createSession(const std::string& token, int64_t user_id, const std::string& expires_at) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    auto st = db_.prepare("INSERT OR REPLACE INTO sessions (token, user_id, expires_at) VALUES (?, ?, ?)");
    st.bindText(1, token).bindInt(2, user_id).bindText(3, expires_at);
    st.step();
}

std::optional<Session> UserRepo::findSession(const std::string& token) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    auto st = db_.prepare("SELECT token, user_id, expires_at FROM sessions WHERE token = ? AND expires_at > ?");
    st.bindText(1, token).bindText(2, nowIso());   // ISO strings sort chronologically
    if (!st.step()) return std::nullopt;
    Session s;
    s.token = st.getText(0);
    s.user_id = st.getInt(1);
    s.expires_at = st.getText(2);
    return s;
}

void UserRepo::deleteSession(const std::string& token) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    auto st = db_.prepare("DELETE FROM sessions WHERE token = ?");
    st.bindText(1, token);
    st.step();
}

int UserRepo::deleteExpiredSessions() {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    auto st = db_.prepare("DELETE FROM sessions WHERE expires_at <= ?");
    st.bindText(1, nowIso());
    st.step();
    return db_.changes();
}

nlohmann::json UserRepo::toJson(const User& u) {
    return {{"id", u.id}, {"username", u.username}, {"created_at", u.created_at}};
}

}  // namespace nc
