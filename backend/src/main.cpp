// T3: boots the server. Order matters: config -> database -> recovery -> models -> predictor -> Drogon.
//
// Auth is a stub (config.json "auth.mode": "stub"): one seeded user, no hashing library.
// Controllers, JobManager and the WebSocket (src/api, src/services) are added in later batches;
// Drogon registers HttpController classes by itself, so they need no change here. Only /api/v1/health
// is served from this file, so `curl` works as soon as the binary starts.
#include <drogon/drogon.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "ml/IPredictor.hpp"
#include "storage/Database.hpp"
#include "storage/JobRepo.hpp"
#include "storage/ModelRepo.hpp"
#include "storage/UserRepo.hpp"

using json = nlohmann::json;

namespace {

json section(const json& cfg, const char* name) {
    return cfg.contains(name) && cfg[name].is_object() ? cfg[name] : json::object();
}

void addCors(const drogon::HttpResponsePtr& resp, const std::string& origin) {
    resp->addHeader("Access-Control-Allow-Origin", origin);
    resp->addHeader("Access-Control-Allow-Headers", "Authorization, Content-Type");
    resp->addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
}

}  // namespace

int main(int argc, char** argv) {
    const auto started = std::chrono::steady_clock::now();
    const std::string config_path = argc > 1 ? argv[1] : "config.json";

    json cfg;
    {
        std::ifstream in(config_path);
        if (!in) {
            std::fprintf(stderr, "neurocode: cannot open %s (run from backend/ or pass the path)\n", config_path.c_str());
            return 1;
        }
        cfg = json::parse(in, nullptr, false);
        if (cfg.is_discarded() || !cfg.is_object()) {
            std::fprintf(stderr, "neurocode: %s is not valid JSON\n", config_path.c_str());
            return 1;
        }
    }
    const json server = section(cfg, "server"), dbc = section(cfg, "db"), auth = section(cfg, "auth");
    const std::string models_dir = cfg.value("models_dir", std::string("models"));
    const std::string cors_origin = server.value("cors_origin", std::string("http://localhost:5173"));

    try {
        // 1. Database: schema and seed are idempotent, so running them on every boot is safe.
        nc::Database db(dbc.value("path", std::string("data/neurocode.db")));
        db.execFile(dbc.value("schema", std::string("db/schema.sql")));
        db.execFile(dbc.value("seed", std::string("db/seed.sql")));

        nc::UserRepo users(db);
        nc::JobRepo jobs(db);
        nc::ModelRepo models(db);

        // 2. Jobs left queued/running by a previous process can never finish: mark them failed.
        if (int n = jobs.failInterrupted(); n > 0)
            std::fprintf(stderr, "neurocode: marked %d interrupted job(s) as failed\n", n);

        // 3. Stub auth: one known user. Real signup/Argon2 comes after jobs work end to end.
        if (auth.value("mode", std::string("stub")) == "stub") {
            auto u = users.ensureUser(auth.value("username", std::string("demo")), "STUB-NO-HASH");
            std::fprintf(stderr, "neurocode: stub auth, user '%s' (id %lld)\n", u.username.c_str(), (long long)u.id);
        }

        // 4. Models: refresh the cache from disk, then load the predictor with the SAME rule
        //    (newest .pt). If the predictor had to fall back to an older checkpoint, follow it.
        int n_models = models.syncFromDisk(models_dir);
        auto predictor = nc::makePredictor(models_dir);
        const std::string loaded_version = predictor->activeModelVersion();
        if (!loaded_version.empty()) models.setActiveVersion(loaded_version);   // false for the stub: ignored
        const bool model_loaded = !loaded_version.empty() && models.findByVersion(loaded_version).has_value();
        std::fprintf(stderr, "neurocode: %d checkpoint(s) in %s, active model: %s\n", n_models, models_dir.c_str(),
                     model_loaded ? loaded_version.c_str() : "none (stub predictor)");

        // 5. HTTP.
        auto& app = drogon::app();
        app.setThreadNum(server.value("http_threads", 2));
        app.addListener(server.value("host", std::string("127.0.0.1")), server.value("port", 8080));

        app.registerPreRoutingAdvice([cors_origin](const drogon::HttpRequestPtr& req,
                                                   drogon::AdviceCallback&& cb,
                                                   drogon::AdviceChainCallback&& next) {
            if (req->method() == drogon::Options) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k204NoContent);
                addCors(resp, cors_origin);
                cb(resp);
            } else {
                next();
            }
        });
        app.registerPostHandlingAdvice([cors_origin](const drogon::HttpRequestPtr&, const drogon::HttpResponsePtr& resp) {
            addCors(resp, cors_origin);
        });

        app.registerHandler(
            "/api/v1/health",
            [&](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
                const bool db_ok = db.ping();
                json body = {{"status", db_ok && model_loaded ? "ok" : "degraded"},
                             {"db", db_ok ? "ok" : "error"},
                             {"model_loaded", model_loaded},
                             {"model_version", model_loaded ? json(loaded_version) : json(nullptr)},
                             {"queue_depth", db_ok ? jobs.countByStatus("queued") : 0},
                             {"uptime_s", std::chrono::duration_cast<std::chrono::seconds>(
                                              std::chrono::steady_clock::now() - started).count()}};
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setContentTypeString("application/json; charset=utf-8");
                resp->setBody(body.dump());
                cb(resp);
            },
            {drogon::Get});

        std::fprintf(stderr, "neurocode: listening on http://%s:%d\n",
                     server.value("host", std::string("127.0.0.1")).c_str(), server.value("port", 8080));
        app.run();   // blocks until SIGINT / SIGTERM; db, repos and predictor outlive it
    } catch (const std::exception& e) {
        std::fprintf(stderr, "neurocode: fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
