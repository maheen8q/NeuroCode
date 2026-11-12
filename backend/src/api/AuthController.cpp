#include "api/AuthController.hpp"

#include "api/Middleware.hpp"
#include "common/AppContext.hpp"
#include "common/Json.hpp"
#include "services/AuthService.hpp"
#include "storage/UserRepo.hpp"

namespace nc::api {

namespace {

// Parses {username, password}. On failure sends the error and returns false.
bool readCredentials(const drogon::HttpRequestPtr& req, std::string& username, std::string& password,
                     const AuthController::Callback& cb) {
    const nlohmann::json body = nlohmann::json::parse(std::string(req->body()), nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        cb(errorResponse(ErrorCode::InvalidJson, "request body must be a JSON object"));
        return false;
    }
    const auto u = body.find("username");
    const auto p = body.find("password");
    if (u == body.end() || !u->is_string() || p == body.end() || !p->is_string()) {
        cb(errorResponse(ErrorCode::ValidationFailed, "username and password are required strings"));
        return false;
    }
    username = u->get<std::string>();
    password = p->get<std::string>();
    return true;
}

nlohmann::json userJson(const AuthService& auth, const std::string& token) {
    if (auto u = auth.userFor(token)) return UserRepo::toJson(*u);
    return {{"id", auth.userId()}, {"username", auth.username()}};
}

}  // namespace

void AuthController::login(const drogon::HttpRequestPtr& req, Callback&& cb) const {
    auto* auth = ctx().auth;
    if (!auth) return cb(errorResponse(ErrorCode::Internal, "auth not ready"));
    std::string username, password;
    if (!readCredentials(req, username, password, cb)) return;

    const auto token = auth->login(username, password);
    if (!token) return cb(errorResponse(ErrorCode::Unauthorized, "invalid username or password"));
    cb(jsonResponse({{"token", *token}, {"user", userJson(*auth, *token)}}));
}

void AuthController::signup(const drogon::HttpRequestPtr& req, Callback&& cb) const {
    auto* auth = ctx().auth;
    if (!auth) return cb(errorResponse(ErrorCode::Internal, "auth not ready"));
    std::string username, password;
    if (!readCredentials(req, username, password, cb)) return;

    AuthService::SignupError err = AuthService::SignupError::None;
    const auto token = auth->signup(username, password, err);
    if (!token) {
        switch (err) {
            case AuthService::SignupError::InvalidUsername:
                return cb(errorResponse(ErrorCode::ValidationFailed,
                                        "username must be 3 to 32 characters: letters, digits, _ - ."));
            case AuthService::SignupError::WeakPassword:
                return cb(errorResponse(ErrorCode::ValidationFailed, "password must be at least 8 characters"));
            case AuthService::SignupError::UsernameTaken:
                return cb(errorResponse(ErrorCode::Conflict, "That username is already taken."));
            default:
                return cb(errorResponse(ErrorCode::Internal, "could not create the account"));
        }
    }
    cb(jsonResponse({{"token", *token}, {"user", userJson(*auth, *token)}}, drogon::k201Created));
}

void AuthController::logout(const drogon::HttpRequestPtr& req, Callback&& cb) const {
    if (auto* auth = ctx().auth) auth->logout(extractToken(req));
    cb(jsonResponse({{"ok", true}}));
}

void AuthController::me(const drogon::HttpRequestPtr& req, Callback&& cb) const {
    auto* auth = ctx().auth;
    const std::string token = extractToken(req);
    if (!auth || !auth->validate(token)) {
        return cb(errorResponse(ErrorCode::Unauthorized, "missing or invalid token"));
    }
    cb(jsonResponse(userJson(*auth, token)));
}

}  // namespace nc::api