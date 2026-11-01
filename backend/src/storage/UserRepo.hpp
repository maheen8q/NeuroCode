#pragma once
// users + sessions. Auth is a STUB for now (config.json "auth"): no libsodium, no Argon2, no signup
// endpoint. password_hash is an opaque string the caller supplies, so real hashing can be added later
// in AuthService without touching this file.
#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "storage/Database.hpp"

namespace nc {

struct User {
    int64_t id = 0;
    std::string username;
    std::string password_hash;
    std::string created_at;   // ISO 8601 UTC
};

struct Session {
    std::string token;
    int64_t user_id = 0;
    std::string expires_at;   // ISO 8601 UTC
};

class UserRepo {
public:
    explicit UserRepo(Database& db) : db_(db) {}

    std::optional<User> findById(int64_t id);
    std::optional<User> findByUsername(const std::string& username);

    // nullopt when the username is already taken (API: 409 USERNAME_TAKEN).
    std::optional<User> create(const std::string& username, const std::string& password_hash);

    // Get or create. Used at boot for the stub user.
    User ensureUser(const std::string& username, const std::string& password_hash);

    void createSession(const std::string& token, int64_t user_id, const std::string& expires_at);
    std::optional<Session> findSession(const std::string& token);   // nullopt if unknown or expired
    void deleteSession(const std::string& token);
    int deleteExpiredSessions();

    // api-spec.md: { "id", "username", "created_at" }. Never includes the hash.
    static nlohmann::json toJson(const User& u);

private:
    Database& db_;
};

}  // namespace nc
