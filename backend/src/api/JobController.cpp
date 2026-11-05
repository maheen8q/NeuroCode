#include "api/JobController.hpp"

#include <algorithm>
#include <cstdlib>

#include "common/AppContext.hpp"
#include "common/Json.hpp"
#include "services/JobManager.hpp"
#include "services/ProblemCatalog.hpp"

namespace nc::api {

void JobController::create(const drogon::HttpRequestPtr& req, Callback&& cb) const {
    auto& c = ctx();
    if (!c.jobs) return cb(errorResponse(ErrorCode::Internal, "job manager not ready"));

    const std::string raw(req->body());
    const nlohmann::json body = nlohmann::json::parse(raw, nullptr, /*allow_exceptions=*/false);
    if (body.is_discarded() || !body.is_object()) {
        return cb(errorResponse(ErrorCode::InvalidJson, "request body must be a JSON object"));
    }

    // type: only analyze and optimize go through this endpoint; training has its own.
    auto typeIt = body.find("type");
    if (typeIt == body.end() || !typeIt->is_string()) {
        return cb(errorResponse(ErrorCode::ValidationFailed, "type is required (\"analyze\" or \"optimize\")"));
    }
    const auto type = jobTypeFromString(typeIt->get<std::string>());
    if (!type || *type == JobType::Train) {
        return cb(errorResponse(ErrorCode::ValidationFailed, "type must be \"analyze\" or \"optimize\""));
    }

    auto codeIt = body.find("code");
    if (codeIt == body.end() || !codeIt->is_string() || codeIt->get<std::string>().empty()) {
        return cb(errorResponse(ErrorCode::ValidationFailed, "code is required and must be a non-empty string"));
    }
    std::string code = codeIt->get<std::string>();
    if (code.size() > c.maxCodeBytes) {
        return cb(errorResponse(ErrorCode::PayloadTooLarge,
                                "code is larger than " + std::to_string(c.maxCodeBytes) + " bytes"));
    }

        auto probIt = body.find("problem");
    auto problemIdIt = body.find("problem_id");

    nlohmann::json problemJson;
    if (probIt != body.end() && !probIt->is_null()) {
        // Custom mode: the full spec is given inline.
        problemJson = *probIt;
    } else if (problemIdIt != body.end() && problemIdIt->is_string()) {
        // Catalog mode: only an id is given, look the rest up.
        if (!c.problems) {
            return cb(errorResponse(ErrorCode::Internal, "problem catalog not ready"));
        }
        const std::string pid = problemIdIt->get<std::string>();
        const nc::Problem* p = c.problems->find(pid);
        if (!p) {
            return cb(errorResponse(ErrorCode::ValidationFailed, "unknown problem_id: " + pid));
        }
        problemJson = {{"problem_id", p->id}, {"input_type", p->input_type},
                       {"output_type", p->output_type}, {"operation", p->operation},
                       {"max_n", p->max_n}};
    } else {
        return cb(errorResponse(ErrorCode::ValidationFailed, "problem is required"));
    }

    ProblemSpec spec;
    std::string err;
    if (!parseProblemSpec(problemJson, spec, err)) {
        return cb(errorResponse(ErrorCode::ValidationFailed, err));
    }
    if (!spec.problem_id.empty() && c.problems && !c.problems->find(spec.problem_id)) {
        return cb(errorResponse(ErrorCode::ValidationFailed, "unknown problem.problem_id: " + spec.problem_id));
    }

    const auto id = c.jobs->submit(*type, std::move(code), std::move(spec));
    cb(jsonResponse({{"job_id", id}, {"status", "queued"}}, drogon::k202Accepted));
}

void JobController::list(const drogon::HttpRequestPtr& req, Callback&& cb) const {
    auto& c = ctx();
    if (!c.jobs) return cb(errorResponse(ErrorCode::Internal, "job manager not ready"));

    std::size_t limit = 50;
    const std::string raw = req->getParameter("limit");
    if (!raw.empty()) {
        char* end = nullptr;
        const long v = std::strtol(raw.c_str(), &end, 10);
        if (*end != '\0' || v < 1) {
            return cb(errorResponse(ErrorCode::ValidationFailed, "limit must be a positive integer"));
        }
        limit = static_cast<std::size_t>(std::min<long>(v, 200));
    }

    const std::string typeFilter = req->getParameter("type");
    const std::string statusFilter = req->getParameter("status");

    auto jobList = c.jobs->list(limit * 4 > 200 ? 200 : limit * 4);  // over-fetch, then filter
    if (!typeFilter.empty()) {
        jobList.erase(std::remove_if(jobList.begin(), jobList.end(),
            [&](const Job& j) { return toString(j.type) != typeFilter; }), jobList.end());
    }
    if (!statusFilter.empty()) {
        jobList.erase(std::remove_if(jobList.begin(), jobList.end(),
            [&](const Job& j) { return toString(j.status) != statusFilter; }), jobList.end());
    }
    if (jobList.size() > limit) jobList.resize(limit);

    cb(jsonResponse({{"jobs", jobList}, {"total", jobList.size()}, {"limit", limit}, {"offset", 0}}));
}

void JobController::get(const drogon::HttpRequestPtr&, Callback&& cb, std::string&& idStr) const {
    auto& c = ctx();
    if (!c.jobs) return cb(errorResponse(ErrorCode::Internal, "job manager not ready"));

    std::int64_t id = 0;
    if (!parseJobId(idStr, id)) return cb(errorResponse(ErrorCode::ValidationFailed, "invalid job id"));

    const auto job = c.jobs->get(id);
    if (!job) return cb(errorResponse(ErrorCode::NotFound, "job not found"));
    cb(jsonResponse(*job));
}

void JobController::cancel(const drogon::HttpRequestPtr&, Callback&& cb, std::string&& idStr) const {
    auto& c = ctx();
    if (!c.jobs) return cb(errorResponse(ErrorCode::Internal, "job manager not ready"));

    std::int64_t id = 0;
    if (!parseJobId(idStr, id)) return cb(errorResponse(ErrorCode::ValidationFailed, "invalid job id"));

    switch (c.jobs->cancel(id)) {
        case JobManager::CancelResult::NotFound:
            return cb(errorResponse(ErrorCode::NotFound, "job not found"));
        case JobManager::CancelResult::AlreadyFinished:
            return cb(errorResponse(ErrorCode::Conflict, "job already finished"));
        case JobManager::CancelResult::Cancelled:
        case JobManager::CancelResult::Requested: {
            // Return the job as it is now: "running" until the engine notices the request.
            const auto job = c.jobs->get(id);
            return cb(jsonResponse(*job, drogon::k202Accepted));
        }
    }
}

}  // namespace nc::api
