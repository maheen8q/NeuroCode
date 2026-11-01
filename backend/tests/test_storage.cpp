// T3 storage tests. They need only SQLite and nlohmann/json (no Drogon, no LibTorch).
// NC_DB_DIR is set by CMake to backend/db so the real schema.sql and seed.sql are exercised.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include "storage/Database.hpp"
#include "storage/JobRepo.hpp"
#include "storage/ModelRepo.hpp"
#include "storage/UserRepo.hpp"

using namespace nc;
namespace fs = std::filesystem;

namespace {

class StorageTest : public ::testing::Test {
protected:
    Database db{":memory:"};
    UserRepo users{db};
    JobRepo jobs{db};
    ModelRepo models{db};
    int64_t uid = 0;

    void SetUp() override {
        db.execFile(std::string(NC_DB_DIR) + "/schema.sql");
        db.execFile(std::string(NC_DB_DIR) + "/seed.sql");
        uid = users.ensureUser("demo", "STUB").id;
    }

    int64_t scalar(const std::string& sql) {
        auto st = db.prepare(sql);
        st.step();
        return st.getInt(0);
    }

    NewJob analyzeJob(const std::string& problem = "pair_sum") {
        NewJob j;
        j.user_id = uid;
        j.type = "analyze";
        j.problem_id = problem;
        j.input_code = "bool solve(vector<int>& a, int k) { return false; }";
        return j;
    }
};

// ---------- schema and seed ----------

TEST_F(StorageTest, SchemaHasNoDroppedTables) {
    for (const char* t : {"analyses", "findings", "counterexamples", "optimizations"})
        EXPECT_EQ(scalar(std::string("SELECT COUNT(*) FROM sqlite_master WHERE name = '") + t + "'"), 0) << t;
    for (const char* t : {"users", "sessions", "problems", "jobs", "models"})
        EXPECT_EQ(scalar(std::string("SELECT COUNT(*) FROM sqlite_master WHERE name = '") + t + "'"), 1) << t;
}

TEST_F(StorageTest, SeedIsIdempotentAndComplete) {
    EXPECT_EQ(scalar("SELECT COUNT(*) FROM problems"), 5);
    db.execFile(std::string(NC_DB_DIR) + "/seed.sql");   // second run must not duplicate or fail
    db.execFile(std::string(NC_DB_DIR) + "/schema.sql");
    EXPECT_EQ(scalar("SELECT COUNT(*) FROM problems"), 5);
    EXPECT_EQ(scalar("SELECT COUNT(*) FROM problems WHERE reference_code = ''"), 0);
}

TEST_F(StorageTest, SeedReseedKeepsJobs) {
    jobs.create(analyzeJob());
    db.execFile(std::string(NC_DB_DIR) + "/seed.sql");   // upsert, not delete + insert
    EXPECT_EQ(scalar("SELECT COUNT(*) FROM jobs"), 1);
}

TEST_F(StorageTest, SeedOperationsAreCanonical) {
    auto st = db.prepare("SELECT operation FROM problems ORDER BY id");
    std::vector<std::string> ops;
    while (st.step()) ops.push_back(st.getText(0));
    std::vector<std::string> want = {"search_sorted", "fibonacci", "search", "pair_sum", "range_sum"};
    EXPECT_EQ(ops, want);   // ordered by id: binary_search, fibonacci, linear_search, pair_sum, range_sum
}

TEST_F(StorageTest, PingWorks) { EXPECT_TRUE(db.ping()); }

// ---------- jobs ----------

TEST_F(StorageTest, CreateReturnsApiShape) {
    JobRow r = jobs.create(analyzeJob());
    EXPECT_EQ(r.status, "queued");
    auto j = JobRepo::createdJson(r);
    EXPECT_EQ(j["job_id"], r.id);
    EXPECT_EQ(j["status"], "queued");
    EXPECT_EQ(j["ws_url"], "/api/v1/ws/jobs/" + std::to_string(r.id));
    EXPECT_EQ(j["created_at"].get<std::string>().back(), 'Z');
    EXPECT_EQ(j.size(), 4u);   // exactly job_id, status, created_at, ws_url
}

TEST_F(StorageTest, UnknownProblemIdViolatesForeignKey) {
    EXPECT_THROW(jobs.create(analyzeJob("no_such_problem")), DbError);
}

TEST_F(StorageTest, CustomProblemStoresJsonAndNullProblemId) {
    NewJob n = analyzeJob("");
    n.type = "optimize";
    n.problem_json = R"({"input_type":"int_array","operation":"pair_sum","max_n":100000})";
    JobRow r = jobs.create(n);
    EXPECT_EQ(r.problem_id, "");
    EXPECT_EQ(r.problem_json, n.problem_json);
    EXPECT_TRUE(JobRepo::summaryJson(r)["problem_id"].is_null());
}

TEST_F(StorageTest, LifecycleQueuedRunningDone) {
    JobRow r = jobs.create(analyzeJob());
    EXPECT_FALSE(jobs.updateProgress(r.id, 10, "parsing"));   // not running yet
    ASSERT_TRUE(jobs.markRunning(r.id));
    EXPECT_FALSE(jobs.markRunning(r.id));
    ASSERT_TRUE(jobs.updateProgress(r.id, 45, "complexity analysis"));

    auto running = JobRepo::detailJson(*jobs.find(r.id));
    EXPECT_EQ(running["status"], "running");
    EXPECT_EQ(running["progress"], 45);
    EXPECT_EQ(running["stage"], "complexity analysis");
    EXPECT_TRUE(running["result"].is_null());
    EXPECT_TRUE(running["finished_at"].is_null());
    EXPECT_FALSE(running.contains("error"));

    std::string result = R"j({"algorithms":[{"label":"brute_force_pair_search","probability":0.94}],)j"
                         R"j("complexity":{"time":"O(n^2)","space":"O(1)"}})j";
    ASSERT_TRUE(jobs.markDone(r.id, result));
    auto done = JobRepo::detailJson(*jobs.find(r.id));
    EXPECT_EQ(done["status"], "done");
    EXPECT_EQ(done["progress"], 100);
    EXPECT_EQ(done["stage"], "complete");
    EXPECT_EQ(done["result"]["complexity"]["time"], "O(n^2)");
    EXPECT_FALSE(done["finished_at"].is_null());

    auto s = JobRepo::summaryJson(*jobs.find(r.id));
    EXPECT_EQ(s["algorithm"], "brute_force_pair_search");
    EXPECT_EQ(s["time_complexity"], "O(n^2)");
    EXPECT_EQ(s["problem_id"], "pair_sum");
}

TEST_F(StorageTest, FailedJobCarriesErrorObjectAndNullResult) {
    JobRow r = jobs.create(analyzeJob());
    jobs.markRunning(r.id);
    ASSERT_TRUE(jobs.markFailed(r.id, "COMPILE_ERROR", "line 7: expected ';' before '}'"));
    auto j = JobRepo::detailJson(*jobs.find(r.id));
    EXPECT_EQ(j["status"], "failed");
    EXPECT_EQ(j["error"]["code"], "COMPILE_ERROR");
    EXPECT_EQ(j["error"]["message"], "line 7: expected ';' before '}'");
    EXPECT_TRUE(j["result"].is_null());
}

TEST_F(StorageTest, CancelRules) {
    JobRow queued = jobs.create(analyzeJob());
    EXPECT_TRUE(jobs.markCancelled(queued.id));
    EXPECT_FALSE(jobs.markRunning(queued.id));                 // a cancelled job never starts
    EXPECT_FALSE(jobs.markDone(queued.id, "{}"));              // ...nor completes
    EXPECT_EQ(jobs.find(queued.id)->status, "cancelled");

    JobRow finished = jobs.create(analyzeJob());
    jobs.markDone(finished.id, "{}");
    EXPECT_FALSE(jobs.markCancelled(finished.id));             // API answers 409
    EXPECT_EQ(jobs.find(finished.id)->status, "done");
}

TEST_F(StorageTest, FailInterruptedMarksOnlyUnfinishedJobs) {
    JobRow a = jobs.create(analyzeJob());
    JobRow b = jobs.create(analyzeJob());
    JobRow c = jobs.create(analyzeJob());
    jobs.markRunning(b.id);
    jobs.markDone(c.id, "{}");
    EXPECT_EQ(jobs.failInterrupted(), 2);
    EXPECT_EQ(jobs.find(a.id)->status, "failed");
    EXPECT_EQ(jobs.find(b.id)->status, "failed");
    EXPECT_EQ(jobs.find(c.id)->status, "done");
    EXPECT_EQ(JobRepo::detailJson(*jobs.find(a.id))["error"]["code"], "INTERNAL");
    EXPECT_EQ(jobs.failInterrupted(), 0);
}

TEST_F(StorageTest, ListIsWrappedPagedAndFiltered) {
    for (int i = 0; i < 5; ++i) jobs.create(analyzeJob());
    NewJob opt = analyzeJob();
    opt.type = "optimize";
    jobs.create(opt);
    JobRow done = jobs.create(analyzeJob());
    jobs.markDone(done.id, "{}");

    JobFilter f;
    f.user_id = uid;
    f.limit = 4;
    auto page = jobs.list(f);
    auto j = JobRepo::pageJson(page);
    EXPECT_EQ(j["total"], 7);
    EXPECT_EQ(j["limit"], 4);
    EXPECT_EQ(j["offset"], 0);
    ASSERT_EQ(j["jobs"].size(), 4u);
    EXPECT_EQ(j["jobs"][0]["job_id"], done.id);   // newest first

    f.offset = 4;
    EXPECT_EQ(jobs.list(f).rows.size(), 3u);

    f.offset = 0;
    f.type = "optimize";
    EXPECT_EQ(jobs.list(f).total, 1);
    f.type = "";
    f.status = "done";
    EXPECT_EQ(jobs.list(f).total, 1);

    JobFilter huge;
    huge.limit = 5000;
    EXPECT_EQ(jobs.list(huge).limit, 100);       // clamped
}

TEST_F(StorageTest, RateLimitCounterAndQueueDepth) {
    for (int i = 0; i < 3; ++i) jobs.create(analyzeJob());
    EXPECT_EQ(jobs.countSince(uid, 3600), 3);
    EXPECT_EQ(jobs.countSince(uid + 999, 3600), 0);
    EXPECT_EQ(jobs.countByStatus("queued"), 3);
}

// ---------- users ----------

TEST_F(StorageTest, EnsureUserIsIdempotentAndHidesHash) {
    User again = users.ensureUser("demo", "OTHER");
    EXPECT_EQ(again.id, uid);
    EXPECT_EQ(again.password_hash, "STUB");   // first one wins
    EXPECT_FALSE(users.create("demo", "x").has_value());   // USERNAME_TAKEN
    auto j = UserRepo::toJson(again);
    EXPECT_FALSE(j.contains("password_hash"));
    EXPECT_EQ(j["username"], "demo");
}

TEST_F(StorageTest, SessionsExpire) {
    users.createSession("live", uid, isoSecondsAgo(-3600));   // expires in an hour
    users.createSession("dead", uid, isoSecondsAgo(3600));    // expired an hour ago
    EXPECT_TRUE(users.findSession("live").has_value());
    EXPECT_FALSE(users.findSession("dead").has_value());
    EXPECT_FALSE(users.findSession("nope").has_value());
    EXPECT_EQ(users.deleteExpiredSessions(), 1);
    users.deleteSession("live");
    EXPECT_FALSE(users.findSession("live").has_value());
}

// ---------- models ----------

class ModelRepoTest : public StorageTest {
protected:
    fs::path dir;

    void SetUp() override {
        StorageTest::SetUp();
        dir = fs::temp_directory_path() / ("nc_models_" + std::to_string(::getpid()));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    void TearDown() override { fs::remove_all(dir); }

    std::string read(const fs::path& p) {
        std::ifstream in(p);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
    void checkpoint(const std::string& stem, double test_acc, bool with_sidecar = true) {
        std::ofstream(dir / (stem + ".pt")) << "weights";
        if (!with_sidecar) return;
        std::ofstream(dir / (stem + ".json")) << R"({
          "version": ")" << stem << R"(",
          "n_features": 53,
          "feature_mean": [1.0, 2.0],
          "feature_std": [0.5, 0.25],
          "labels": ["binary_search", "dp"],
          "metrics": {"val_accuracy": 0.78, "test_accuracy": )" << test_acc << R"(, "cv_accuracy": 0.84},
          "best_epoch": 7,
          "dataset_size": 2067,
          "trained_at": "2026-10-28T00:14:01"
        })";
    }
};

TEST_F(ModelRepoTest, NewestCheckpointByFilenameIsActive) {
    checkpoint("v20261027-1430", 0.88);
    checkpoint("v20261028-0013", 0.9262);
    EXPECT_EQ(models.syncFromDisk(dir.string()), 2);

    auto all = models.list();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].version, "v20261028-0013");   // newest first
    ASSERT_TRUE(models.active().has_value());
    EXPECT_EQ(models.active()->version, "v20261028-0013");
    EXPECT_FALSE(all[1].is_active);
}

TEST_F(ModelRepoTest, MapsOnlyTheNeededSidecarFields) {
    checkpoint("v20261028-0013", 0.9262);
    models.syncFromDisk(dir.string());
    auto j = ModelRepo::toJson(*models.active());
    EXPECT_NEAR(j["accuracy"].get<double>(), 0.9262, 1e-9);   // test_accuracy
    EXPECT_TRUE(j["loss"].is_null());
    EXPECT_EQ(j["epochs"], 7);                                // best_epoch
    EXPECT_EQ(j["dataset_size"], 2067);
    EXPECT_EQ(j["trained_at"], "2026-10-28T00:14:01Z");
    EXPECT_TRUE(j["is_active"].get<bool>());
    EXPECT_EQ(ModelRepo::listJson(models.list())["models"].size(), 1u);
}

TEST_F(ModelRepoTest, NeverWritesTheSidecar) {
    checkpoint("v20261028-0013", 0.9262);
    const std::string before = read(dir / "v20261028-0013.json");
    models.syncFromDisk(dir.string());
    models.syncFromDisk(dir.string());
    models.setActiveVersion("v20261028-0013");
    EXPECT_EQ(read(dir / "v20261028-0013.json"), before);
}

TEST_F(ModelRepoTest, CheckpointWithoutSidecarIsSkipped) {
    checkpoint("v20261027-1430", 0.88);
    checkpoint("v20261029-0900", 0.95, /*with_sidecar=*/false);   // the predictor would refuse it
    EXPECT_EQ(models.syncFromDisk(dir.string()), 1);
    EXPECT_EQ(models.active()->version, "v20261027-1430");
}

TEST_F(ModelRepoTest, ResyncIsIdempotentAndDropsDeletedCheckpoints) {
    checkpoint("v20261027-1430", 0.88);
    checkpoint("v20261028-0013", 0.9262);
    models.syncFromDisk(dir.string());
    models.syncFromDisk(dir.string());
    EXPECT_EQ(models.list().size(), 2u);

    fs::remove(dir / "v20261028-0013.pt");
    fs::remove(dir / "v20261028-0013.json");
    models.syncFromDisk(dir.string());
    ASSERT_EQ(models.list().size(), 1u);
    EXPECT_EQ(models.active()->version, "v20261027-1430");
}

TEST_F(ModelRepoTest, ActivateAndMissingCheckpoint) {
    checkpoint("v20261027-1430", 0.88);
    checkpoint("v20261028-0013", 0.9262);
    models.syncFromDisk(dir.string());

    EXPECT_TRUE(models.setActiveVersion("v20261027-1430"));   // predictor fell back to the older one
    EXPECT_EQ(models.active()->version, "v20261027-1430");
    EXPECT_FALSE(models.setActiveVersion("v-unknown"));
    EXPECT_EQ(models.active()->version, "v20261027-1430");    // unchanged

    auto older = *models.findByVersion("v20261027-1430");
    EXPECT_TRUE(ModelRepo::checkpointExists(older));
    fs::remove(dir / "v20261027-1430.pt");
    EXPECT_FALSE(ModelRepo::checkpointExists(older));         // API: 404
}

TEST_F(ModelRepoTest, MissingDirectoryYieldsNoModels) {
    EXPECT_EQ(models.syncFromDisk((dir / "nope").string()), 0);
    EXPECT_FALSE(models.active().has_value());
    EXPECT_TRUE(ModelRepo::listJson(models.list())["models"].empty());
}

}  // namespace
