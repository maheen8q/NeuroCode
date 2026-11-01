#pragma once
// A read-only cache of models/*.pt and their sidecar .json files (architecture.md section 6).
// The files on disk are the source of truth. ModelRepo only READS the sidecar (it also holds the
// training mean/std the predictor needs) and never writes to it.
//
// "Active" follows the same rule as makePredictor("models"): sort the .pt paths as strings and
// take the last one, so the UI and the predictor cannot disagree. Checkpoints without a readable
// sidecar are skipped, because Predictor::loadModel refuses them too.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "storage/Database.hpp"

namespace nc {

struct ModelRow {
    int64_t id = 0;
    std::string version;           // "v20261028-0013"
    std::string checkpoint_path;   // models/v20261028-0013.pt
    std::optional<double> accuracy;
    std::optional<double> loss;
    std::optional<int64_t> epochs;
    std::optional<int64_t> dataset_size;
    std::string trained_at;        // ISO 8601 UTC, "" if the sidecar had none
    bool is_active = false;
};

class ModelRepo {
public:
    explicit ModelRepo(Database& db) : db_(db) {}

    // Re-scan models_dir and bring the table in line with it (insert/update/delete, re-pick the
    // active row). Call at boot and after every finished training job. Returns the number of
    // usable checkpoints found.
    int syncFromDisk(const std::string& models_dir);

    std::vector<ModelRow> list();                      // newest first
    std::optional<ModelRow> find(int64_t id);
    std::optional<ModelRow> findByVersion(const std::string& version);
    std::optional<ModelRow> active();

    // For POST /models/{id}/activate and for boot, when the predictor fell back to an older
    // checkpoint. Call it only after the predictor loaded that version. False if unknown.
    bool setActiveVersion(const std::string& version);

    // POST /models/{id}/activate answers 404 when this is false.
    static bool checkpointExists(const ModelRow& m);

    // api-spec.md section 8: id, version, accuracy, loss, epochs, dataset_size, trained_at, is_active.
    static nlohmann::json toJson(const ModelRow& m);
    static nlohmann::json listJson(const std::vector<ModelRow>& models);   // { "models": [...] }

private:
    Database& db_;
};

}  // namespace nc
