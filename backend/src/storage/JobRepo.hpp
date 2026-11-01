#pragma once
// The jobs table. Analysis, optimization and training are all rows here; the whole result lives in
// result_json (architecture.md section 6), so there are no analyses/findings/... tables.
//
// Text columns that are NULL in the database read as "" here (and "" is written as NULL).
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "storage/Database.hpp"

namespace nc {

struct JobRow {
    int64_t id = 0;
    std::optional<int64_t> user_id;
    std::string type;          // analyze | optimize | train
    std::string status;        // queued | running | done | failed | cancelled
    std::string problem_id;    // catalog slug, "" for a custom problem
    std::string problem_json;  // custom ProblemSpec as JSON, "" for a catalog problem
    std::string input_code;
    int progress = 0;
    std::string stage;
    std::string result_json;   // "" until done
    std::string error;         // JSON {"code","message"}, "" unless failed
    std::string created_at;    // ISO 8601 UTC
    std::string finished_at;   // "" until finished
};

struct NewJob {
    std::optional<int64_t> user_id;
    std::string type;
    std::string problem_id;
    std::string problem_json;
    std::string input_code;
};

struct JobFilter {
    std::optional<int64_t> user_id;
    std::string type;     // "" = any
    std::string status;   // "" = any
    int limit = 20;       // clamped to 1..100
    int offset = 0;
};

struct JobPage {
    std::vector<JobRow> rows;
    int64_t total = 0;
    int limit = 20;
    int offset = 0;
};

class JobRepo {
public:
    explicit JobRepo(Database& db) : db_(db) {}

    // Inserts a queued job. Throws DbError if problem_id is not in the catalog (foreign key).
    JobRow create(const NewJob& job);
    std::optional<JobRow> find(int64_t id);

    // State changes. Each returns false when the job was not in a state that allows it
    // (for example a cancelled job never flips back to running or done).
    bool markRunning(int64_t id);                                   // queued -> running
    bool updateProgress(int64_t id, int percent, const std::string& stage);   // running only
    bool markDone(int64_t id, const std::string& result_json);      // queued|running -> done
    bool markFailed(int64_t id, const std::string& code, const std::string& message);
    bool markCancelled(int64_t id);                                 // false -> API answers 409

    // Boot-time recovery: jobs still queued/running belong to a process that no longer exists.
    // They are marked failed (INTERNAL). Returns how many.
    int failInterrupted();

    JobPage list(const JobFilter& filter);
    int64_t countSince(int64_t user_id, int seconds);   // rate limit: 30 jobs per hour per user
    int64_t countByStatus(const std::string& status);   // queue_depth for /health

    // JSON shapes from api-spec.md section 5.
    static nlohmann::json createdJson(const JobRow& r);   // 202 body of POST /jobs
    static nlohmann::json detailJson(const JobRow& r);    // GET /jobs/{id}
    static nlohmann::json summaryJson(const JobRow& r);   // one row of GET /jobs
    static nlohmann::json pageJson(const JobPage& p);     // { jobs, total, limit, offset }

private:
    Database& db_;
};

}  // namespace nc
