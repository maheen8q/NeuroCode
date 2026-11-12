#include "services/AuthService.hpp"

#include <sodium.h>

#include <cctype>
#include <stdexcept>

namespace nc {

namespace {

// Compares without stopping at the first difference.
bool constantTimeEquals(const std::string& a, const std::string& b) {
    unsigned char diff = static_cast<unsigned char>(a.size() ^ b.size());
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

std::string randomToken() {
    unsigned char bytes[32];
    randombytes_buf(bytes, sizeof bytes);
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (unsigned char b : bytes) {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0xF]);
    }
    return out;
}

bool validUsername(const std::string& u) {
    if (u.size() < 3 || u.size() > 32) return false;
    for (char c : u)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')) return false;
    return true;
}

std::string hashPassword(const std::string& pw) {
    char out[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(out, pw.data(), pw.size(),
                          crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
        return {};
    return out;
}

bool verifyPassword(const std::string& hash, const std::string& pw) {
    if (hash.rfind("$argon2", 0) != 0) return false;   // e.g. the old "STUB-NO-HASH" row
    return crypto_pwhash_str_verify(hash.c_str(), pw.data(), pw.size()) == 0;
}

}  // namespace

AuthService::AuthService(UserRepo& users, std::string demoUsername, std::string demoPassword)
    : users_(users), demoUsername_(std::move(demoUsername)), demoPassword_(std::move(demoPassword)) {
    if (sodium_init() < 0) throw std::runtime_error("libsodium failed to initialise");
}

std::string AuthService::issueToken(int64_t userId) {
    std::string token = randomToken();
    std::lock_guard<std::mutex> lk(mu_);
    tokens_[token] = userId;
    return token;
}

std::optional<std::string> AuthService::login(const std::string& username, const std::string& password) {
    // Demo account: checked against config.json, exactly as before.
    if (constantTimeEquals(username, demoUsername_)) {
        if (!constantTimeEquals(password, demoPassword_)) return std::nullopt;
        int64_t id = 1;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (auto u = users_.findByUsername(demoUsername_)) id = u->id;
        }
        return issueToken(id);
    }

    std::optional<User> u;
    {
        std::lock_guard<std::mutex> lk(mu_);
        u = users_.findByUsername(username);
    }
    if (!u || !verifyPassword(u->password_hash, password)) return std::nullopt;
    return issueToken(u->id);
}

std::optional<std::string> AuthService::signup(const std::string& username, const std::string& password,
                                               SignupError& err) {
    err = SignupError::None;
    if (!validUsername(username)) { err = SignupError::InvalidUsername; return std::nullopt; }
    if (password.size() < 8 || password.size() > 128) { err = SignupError::WeakPassword; return std::nullopt; }

    const std::string hash = hashPassword(password);   // slow on purpose; done outside the lock
    if (hash.empty()) { err = SignupError::Internal; return std::nullopt; }

    std::optional<User> created;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (username == demoUsername_) { err = SignupError::UsernameTaken; return std::nullopt; }
        created = users_.create(username, hash);
    }
    if (!created) { err = SignupError::UsernameTaken; return std::nullopt; }
    return issueToken(created->id);
}

bool AuthService::validate(const std::string& token) const {
    if (token.empty()) return false;
    std::lock_guard<std::mutex> lk(mu_);
    return tokens_.count(token) > 0;
}

std::optional<User> AuthService::userFor(const std::string& token) const {
    if (token.empty()) return std::nullopt;
    std::lock_guard<std::mutex> lk(mu_);
    auto it = tokens_.find(token);
    if (it == tokens_.end()) return std::nullopt;
    return users_.findById(it->second);
}

void AuthService::logout(const std::string& token) {
    std::lock_guard<std::mutex> lk(mu_);
    tokens_.erase(token);
}

}  // namespace nc