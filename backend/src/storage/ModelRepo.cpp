#include "storage/ModelRepo.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

namespace nc {

namespace fs = std::filesystem;

namespace {

constexpr const char* kCols =
    "id, version, checkpoint_path, accuracy, loss, epochs, dataset_size, trained_at, is_active";

ModelRow readRow(Statement& st) {
    ModelRow m;
    m.id = st.getInt(0);
    m.version = st.getText(1);
    m.checkpoint_path = st.getText(2);
    if (!st.isNull(3)) m.accuracy = st.getDouble(3);
    if (!st.isNull(4)) m.loss = st.getDouble(4);
    if (!st.isNull(5)) m.epochs = st.getInt(5);
    if (!st.isNull(6)) m.dataset_size = st.getInt(6);
    m.trained_at = normalizeTimestamp(st.getText(7));
    m.is_active = st.getInt(8) != 0;
    return m;
}

// Only the fields the API needs. Everything else in the file (mean, std, labels, ...) is ignored.
struct Sidecar {
    std::string version;
    std::optional<double> accuracy;
    std::optional<int64_t> epochs;
    std::optional<int64_t> dataset_size;
    std::string trained_at;
};

std::optional<Sidecar> readSidecar(const fs::path& json_path, const std::string& fallback_version) {
    std::ifstream in(json_path);   // read only
    if (!in) return std::nullopt;
    auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;

    Sidecar s;
    s.version = fallback_version;
    if (j.contains("version") && j["version"].is_string() && !j["version"].get<std::string>().empty())
        s.version = j["version"].get<std::string>();

    // "accuracy" in the API = test_accuracy (held-out seeds); falls back to val_accuracy.
    // The sidecar has no per-epoch loss, so ModelRow::loss stays empty.
    if (j.contains("metrics") && j["metrics"].is_object()) {
        const auto& m = j["metrics"];
        if (m.contains("test_accuracy") && m["test_accuracy"].is_number())
            s.accuracy = m["test_accuracy"].get<double>();
        else if (m.contains("val_accuracy") && m["val_accuracy"].is_number())
            s.accuracy = m["val_accuracy"].get<double>();
    }
    // The sidecar stores best_epoch, not the number of epochs that ran.
    if (j.contains("best_epoch") && j["best_epoch"].is_number_integer())
        s.epochs = j["best_epoch"].get<int64_t>();
    if (j.contains("dataset_size") && j["dataset_size"].is_number_integer())
        s.dataset_size = j["dataset_size"].get<int64_t>();
    if (j.contains("trained_at") && j["trained_at"].is_string())
        s.trained_at = normalizeTimestamp(j["trained_at"].get<std::string>());
    return s;
}

}  // namespace

int ModelRepo::syncFromDisk(const std::string& models_dir) {
    // Same listing and ordering as makePredictor(): *.pt, sorted as strings, newest last.
    std::vector<std::string> pts;
    std::error_code ec;
    if (fs::is_directory(models_dir, ec))
        for (const auto& e : fs::directory_iterator(models_dir, ec))
            if (e.path().extension() == ".pt") pts.push_back(e.path().string());
    std::sort(pts.begin(), pts.end());

    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    Transaction tx(db_);

    std::set<std::string> kept_versions;
    std::string newest_version;
    int found = 0;

    for (const auto& pt : pts) {
        fs::path json_path = fs::path(pt).replace_extension(".json");
        auto sc = readSidecar(json_path, fs::path(pt).stem().string());
        if (!sc) {
            std::fprintf(stderr, "ModelRepo: skipping %s (no readable sidecar .json)\n", pt.c_str());
            continue;
        }
        auto st = db_.prepare(
            "INSERT INTO models (version, checkpoint_path, accuracy, loss, epochs, dataset_size, trained_at) "
            "VALUES (?, ?, ?, NULL, ?, ?, ?) "
            "ON CONFLICT(version) DO UPDATE SET checkpoint_path = excluded.checkpoint_path, "
            "accuracy = excluded.accuracy, loss = excluded.loss, epochs = excluded.epochs, "
            "dataset_size = excluded.dataset_size, trained_at = excluded.trained_at");
        st.bindText(1, sc->version).bindText(2, pt);
        if (sc->accuracy) st.bindDouble(3, *sc->accuracy); else st.bindNull(3);
        if (sc->epochs) st.bindInt(4, *sc->epochs); else st.bindNull(4);
        if (sc->dataset_size) st.bindInt(5, *sc->dataset_size); else st.bindNull(5);
        st.bindOptText(6, sc->trained_at);
        st.step();

        kept_versions.insert(sc->version);
        newest_version = sc->version;   // pts is sorted, so the last usable one wins
        ++found;
    }

    // Drop rows whose checkpoint is gone.
    std::vector<int64_t> stale;
    {
        auto st = db_.prepare("SELECT id, version FROM models");
        while (st.step())
            if (!kept_versions.count(st.getText(1))) stale.push_back(st.getInt(0));
    }
    for (int64_t id : stale) {
        auto st = db_.prepare("DELETE FROM models WHERE id = ?");
        st.bindInt(1, id);
        st.step();
    }

    db_.exec("UPDATE models SET is_active = 0");
    if (!newest_version.empty()) {
        auto st = db_.prepare("UPDATE models SET is_active = 1 WHERE version = ?");
        st.bindText(1, newest_version);
        st.step();
    }
    tx.commit();
    return found;
}

std::vector<ModelRow> ModelRepo::list() {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    std::vector<ModelRow> out;
    auto st = db_.prepare(std::string("SELECT ") + kCols + " FROM models ORDER BY checkpoint_path DESC");
    while (st.step()) out.push_back(readRow(st));
    return out;
}

std::optional<ModelRow> ModelRepo::find(int64_t id) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    auto st = db_.prepare(std::string("SELECT ") + kCols + " FROM models WHERE id = ?");
    st.bindInt(1, id);
    if (!st.step()) return std::nullopt;
    return readRow(st);
}

std::optional<ModelRow> ModelRepo::findByVersion(const std::string& version) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    auto st = db_.prepare(std::string("SELECT ") + kCols + " FROM models WHERE version = ?");
    st.bindText(1, version);
    if (!st.step()) return std::nullopt;
    return readRow(st);
}

std::optional<ModelRow> ModelRepo::active() {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    auto st = db_.prepare(std::string("SELECT ") + kCols + " FROM models WHERE is_active = 1 LIMIT 1");
    if (!st.step()) return std::nullopt;
    return readRow(st);
}

bool ModelRepo::setActiveVersion(const std::string& version) {
    std::lock_guard<std::recursive_mutex> lock(db_.mutex());
    if (!findByVersion(version)) return false;
    Transaction tx(db_);
    db_.exec("UPDATE models SET is_active = 0");
    auto st = db_.prepare("UPDATE models SET is_active = 1 WHERE version = ?");
    st.bindText(1, version);
    st.step();
    tx.commit();
    return true;
}

bool ModelRepo::checkpointExists(const ModelRow& m) {
    std::error_code ec;
    return fs::is_regular_file(m.checkpoint_path, ec);
}

nlohmann::json ModelRepo::toJson(const ModelRow& m) {
    auto opt = [](const auto& o) { return o ? nlohmann::json(*o) : nlohmann::json(nullptr); };
    return {{"id", m.id},
            {"version", m.version},
            {"accuracy", opt(m.accuracy)},
            {"loss", opt(m.loss)},
            {"epochs", opt(m.epochs)},
            {"dataset_size", opt(m.dataset_size)},
            {"trained_at", m.trained_at.empty() ? nlohmann::json(nullptr) : nlohmann::json(m.trained_at)},
            {"is_active", m.is_active}};
}

nlohmann::json ModelRepo::listJson(const std::vector<ModelRow>& models) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& m : models) arr.push_back(toJson(m));
    return {{"models", arr}};
}

}  // namespace nc
