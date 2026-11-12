#pragma once
// Accounts: the demo user from config.json (checked against config, as before) plus real users
// stored in the users table with Argon2 password hashes (libsodium). Bearer tokens are random,
// kept in memory, and cleared when the server restarts.

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "storage/UserRepo.hpp"

namespace nc {

class AuthService {
public:
    enum class SignupError { None, InvalidUsername, WeakPassword, UsernameTaken, Internal };

    AuthService(UserRepo& users, std::string demoUsername, std::string demoPassword);

    // Returns a new token when the credentials match.
    std::optional<std::string> login(const std::string& username, const std::string& password);
    // Creates the account and returns a token, or sets err and returns nullopt.
    std::optional<std::string> signup(const std::string& username, const std::string& password, SignupError& err);

    bool validate(const std::string& token) const;
    std::optional<User> userFor(const std::string& token) const;
    void logout(const std::string& token);

    // Kept for older callers that assumed one account.
    int userId() const { return 1; }
    const std::string& username() const { return demoUsername_; }

private:
    std::string issueToken(int64_t userId);

    UserRepo& users_;
    std::string demoUsername_;
    std::string demoPassword_;
    mutable std::mutex mu_;                               // guards tokens_ and users_ access
    std::unordered_map<std::string, int64_t> tokens_;     // token -> user id
};

}  // namespace nc