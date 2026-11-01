#include "storage/JobRepo.hpp"

#include <algorithm>

namespace nc {

namespace {

constexpr const char* kCols =
    "id, user_id, type, status, problem_id, problem_json, input_code, progress, stage, "
    "result_json, error, created_at, finished_at";

JobRow readRow(Statement& st) {
    JobRow r;
    r.id = st.getInt(0);
    if (!st.isNull(1)) r.user_id = st.getInt(1);
    r.type = st.getText(2);
    r.status = st.getText(3);
    r.problem_id = st.getText(4);
    r.problem_json = st.getText(5);
    r.input_code = st.getText(6);
    r.progress = static_cast<int>(st.getInt(7));
    r.stage = st.getText(8);
    r.result_json = st.getText(9);
    r.error = st.getText(10);
    r.created_at = normalizeTimestamp(st.getText(11));
    r.finished_at = normalizeTimestamp(st.getText(12));
    return r;
}

nlohmann::json parseOrNull(const std::string& s) {
    if (s.empty()) return nullptr;
    auto j = nlohmann::json::parse(s, nullptr, false);
    return j.is_discarded() ? nlohmann::json(nullptr) : j;
}

nlohmann::json strOrNull(const std::string& s) {
    return s.empty() ? nlohmann::json(nullptr) : nlohmann::json(s);
}

}  // namespace

JobRow JobRepo::create(const NewJob& job) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    auto st = db_.prepare(
        "INSERT INTO jobs (user_id, type, status, problem_id, problem_json, input_code, progress, stage, created_at) "
        "VALUES (?, ?, 'queued', ?, ?, ?, 0, 'queued', ?)");
    if (job.user_id) st.bindInt(1, *job.user_id); else st.bindNull(1);
    st.bindText(2, job.type)
      .bindOptText(3, job.problem_id)
      .bindOptText(4, job.problem_json)
      .bindText(5, job.input_code)
      .bindText(6, nowIso());
    st.step();
    return *find(db_.lastInsertId());
}

std::optional<JobRow> JobRepo::find(int64_t id) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    auto st = db_.prepare(std::string("SELECT ") + kCols + " FROM jobs WHERE id = ?");
    st.bindInt(1, id);
    if (!st.step()) return std::nullopt;
    return readRow(st);
}

bool JobRepo::markRunning(int64_t id) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    auto st = db_.prepare("UPDATE jobs SET status = 'running', stage = 'starting' WHERE id = ? AND status = 'queued'");
    st.bindInt(1, id);
    st.step();
    return db_.changes() == 1;
}

bool JobRepo::updateProgress(int64_t id, int percent, const std::string& stage) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    auto st = db_.prepare("UPDATE jobs SET progress = ?, stage = ? WHERE id = ? AND status = 'running'");
    st.bindInt(1, std::clamp(percent, 0, 100)).bindText(2, stage).bindInt(3, id);
    st.step();
    return db_.changes() == 1;
}

bool JobRepo::markDone(int64_t id, const std::string& result_json) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    auto st = db_.prepare(
        "UPDATE jobs SET status = 'done', progress = 100, stage = 'complete', result_json = ?, finished_at = ? "
        "WHERE id = ? AND status IN ('queued', 'running')");
    st.bindText(1, result_json).bindText(2, nowIso()).bindInt(3, id);
    st.step();
    return db_.changes() == 1;
}

bool JobRepo::markFailed(int64_t id, const std::string& code, const std::string& message) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    nlohmann::json err = {{"code", code}, {"message", message}};
    auto st = db_.prepare(
        "UPDATE jobs SET status = 'failed', error = ?, finished_at = ? "
        "WHERE id = ? AND status IN ('queued', 'running')");
    st.bindText(1, err.dump()).bindText(2, nowIso()).bindInt(3, id);
    st.step();
    return db_.changes() == 1;
}

bool JobRepo::markCancelled(int64_t id) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    auto st = db_.prepare(
        "UPDATE jobs SET status = 'cancelled', stage = 'cancelled', finished_at = ? "
        "WHERE id = ? AND status IN ('queued', 'running')");
    st.bindText(1, nowIso()).bindInt(2, id);
    st.step();
    return db_.changes() == 1;
}

int JobRepo::failInterrupted() {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    nlohmann::json err = {{"code", "INTERNAL"}, {"message", "The server restarted before this job finished."}};
    auto st = db_.prepare(
        "UPDATE jobs SET status = 'failed', error = ?, finished_at = ? WHERE status IN ('queued', 'running')");
    st.bindText(1, err.dump()).bindText(2, nowIso());
    st.step();
    return db_.changes();
}

JobPage JobRepo::list(const JobFilter& f) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    JobPage page;
    page.limit = std::clamp(f.limit, 1, 100);
    page.offset = std::max(f.offset, 0);

    std::string where = " WHERE 1 = 1";
    if (f.user_id) where += " AND user_id = ?";
    if (!f.type.empty()) where += " AND type = ?";
    if (!f.status.empty()) where += " AND status = ?";

    auto bindFilters = [&](Statement& st) {
        int i = 1;
        if (f.user_id) st.bindInt(i++, *f.user_id);
        if (!f.type.empty()) st.bindText(i++, f.type);
        if (!f.status.empty()) st.bindText(i++, f.status);
        return i;   // next free parameter
    };

    {
        auto st = db_.prepare("SELECT COUNT(*) FROM jobs" + where);
        bindFilters(st);
        st.step();
        page.total = st.getInt(0);
    }
    auto st = db_.prepare(std::string("SELECT ") + kCols + " FROM jobs" + where +
                          " ORDER BY created_at DESC, id DESC LIMIT ? OFFSET ?");
    int i = bindFilters(st);
    st.bindInt(i, page.limit).bindInt(i + 1, page.offset);
    while (st.step()) page.rows.push_back(readRow(st));
    return page;
}

int64_t JobRepo::countSince(int64_t user_id, int seconds) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    auto st = db_.prepare("SELECT COUNT(*) FROM jobs WHERE user_id = ? AND created_at >= ?");
    st.bindInt(1, user_id).bindText(2, isoSecondsAgo(seconds));
    st.step();
    return st.getInt(0);
}

int64_t JobRepo::countByStatus(const std::string& status) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    auto st = db_.prepare("SELECT COUNT(*) FROM jobs WHERE status = ?");
    st.bindText(1, status);
    st.step();
    return st.getInt(0);
}

// ---------- JSON ----------

nlohmann::json JobRepo::createdJson(const JobRow& r) {
    return {{"job_id", r.id},
            {"status", r.status},
            {"created_at", r.created_at},
            {"ws_url", "/api/v1/ws/jobs/" + std::to_string(r.id)}};
}

nlohmann::json JobRepo::detailJson(const JobRow& r) {
    nlohmann::json j = {{"job_id", r.id},
                        {"type", r.type},
                        {"status", r.status},
                        {"progress", r.progress},
                        {"stage", r.stage},
                        {"created_at", r.created_at},
                        {"finished_at", strOrNull(r.finished_at)},
                        {"result", r.status == "done" ? parseOrNull(r.result_json) : nlohmann::json(nullptr)}};
    if (r.status == "failed") {
        auto e = parseOrNull(r.error);
        if (e.is_null() || !e.is_object()) e = {{"code", "INTERNAL"}, {"message", r.error}};
        j["error"] = e;
    }
    return j;
}

nlohmann::json JobRepo::summaryJson(const JobRow& r) {
    nlohmann::json algorithm = nullptr, time = nullptr;
    if (r.status == "done") {
        auto res = parseOrNull(r.result_json);
        if (res.is_object()) {
            if (res.contains("algorithms") && res["algorithms"].is_array() && !res["algorithms"].empty() &&
                res["algorithms"][0].is_object() && res["algorithms"][0].contains("label"))
                algorithm = res["algorithms"][0]["label"];
            if (res.contains("complexity") && res["complexity"].is_object() && res["complexity"].contains("time"))
                time = res["complexity"]["time"];
        }
    }
    return {{"job_id", r.id},
            {"type", r.type},
            {"status", r.status},
            {"problem_id", strOrNull(r.problem_id)},
            {"algorithm", algorithm},
            {"time_complexity", time},
            {"created_at", r.created_at}};
}

nlohmann::json JobRepo::pageJson(const JobPage& p) {
    nlohmann::json jobs = nlohmann::json::array();
    for (const auto& r : p.rows) jobs.push_back(summaryJson(r));
    return {{"jobs", jobs}, {"total", p.total}, {"limit", p.limit}, {"offset", p.offset}};
}

}  // namespace nc
