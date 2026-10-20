#pragma once

// Studio: the single entry point used by the editor UI, the CLI and the tests.
// It owns the project, the analysis cache, the pending preview and the
// history, and talks to the level only through a LevelBackend.

#include "Analysis.hpp"
#include "DecoCatalog.hpp"
#include "Generator.hpp"
#include "Project.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gdds {

// Abstract access to a level (the live editor, or a level string).
class LevelBackend {
public:
    virtual ~LevelBackend() = default;

    // Every object of the level, parsed from its native save string.
    virtual std::vector<LevelObject> readObjects() = 0;
    // Analysis inputs for the objects (the editor uses live type/hitbox data).
    virtual std::vector<AnalysisInput> analysisInputs(std::vector<LevelObject> const& objects,
                                                      Project const& project);
    virtual LevelHeader header() = 0;
    virtual std::string levelName() = 0;

    // Creates objects; returns the save strings as actually stored by the
    // level (may normalize keys), in the same order. Empty string = failed.
    virtual std::vector<std::string> createObjects(std::vector<LevelObject> const& objects) = 0;
    // Removes one live object per entry, matched by exact fingerprint.
    // Returns the number of objects removed.
    virtual size_t removeObjects(std::vector<LevelObject> const& objects) = 0;

    // Color channel table of the level.
    virtual std::vector<ColorChannel> readChannels() = 0;
    virtual void writeChannels(std::vector<ColorChannel> const& channels) = 0;

    // Optional: preview support (objects shown but not committed).
    virtual bool supportsPreview() const { return false; }
    virtual void showPreview(std::vector<LevelObject> const& add, std::vector<LevelObject> const& hide) {}
    virtual void clearPreview() {}

    // Optional: layer visibility in the editor (not saved in the level).
    virtual void setHiddenTagGroups(std::vector<int> const& groups) {}
};

struct StudioStats {
    int gameplayObjects = 0;
    int structures = 0;
    int generatedObjects = 0;
    std::map<std::string, int> perLayer;
    std::map<int, int> perSection;
    double analysisMs = 0;
    double generationMs = 0;
    double complexity = 0;  // rough estimate: objects weighted by scale/blending
};

struct OperationOutcome {
    bool ok = false;
    std::string message;
    int created = 0;
    int removed = 0;
    int keptModified = 0;
    std::vector<uint64_t> needsConfirmation;  // modified records (Ask policy)
    std::vector<std::string> skippedLocked;
    bool gameplayIntact = true;
    std::vector<Issue> issues;
    GenerationResult const* result = nullptr;  // valid until the next operation
};

class Studio {
public:
    Studio(LevelBackend& backend, DecoCatalog const& catalog = DecoCatalog::builtin());

    Project& project() { return m_project; }
    Project const& project() const { return m_project; }
    History const& history() const { return m_history; }

    // Loads a project (or creates the default one) and reconciles it with the level.
    ReconcileReport attach(std::optional<Project> project);

    // (Re)analyses the level. Cached until invalidate() or a level change.
    LevelAnalysis const& analysis();
    void invalidateAnalysis() { m_analysis.reset(); }

    // Direct operations (generate + apply in one step).
    OperationOutcome run(GenerationRequest const& request, std::atomic<bool> const* cancel = nullptr);

    // Preview workflow: preview -> (tweak + preview again) -> apply / cancel.
    OperationOutcome preview(GenerationRequest const& request, std::atomic<bool> const* cancel = nullptr);

    // Asynchronous workflow (used by the editor so long generations do not
    // freeze the game): prepare() and finish() on the main thread, compute()
    // on any thread. run() == prepare + compute + finish.
    struct Prepared {
        GenerationRequest request;
        Project project;                                   // snapshot
        std::shared_ptr<LevelAnalysis const> analysis;     // kept alive for the worker
        DecoCatalog const* catalog = nullptr;
    };
    Prepared prepare(GenerationRequest const& request);
    static GenerationResult compute(Prepared const& prepared, std::atomic<bool> const* cancel = nullptr);
    OperationOutcome finish(Prepared const& prepared, GenerationResult result, bool asPreview);
    bool hasPendingPreview() const { return m_pending.has_value(); }
    OperationOutcome applyPreview();
    void cancelPreview();

    // Colors only: recompute and write the studio's channels (no object changes).
    void applyColors();

    // Deletes a layer (and its sub-layers): their generated objects are
    // removed from the level, their channels released. Refused if locked.
    OperationOutcome deleteLayer(std::string const& layerId);
    // Re-assigns generated objects to another layer (changes their tag group).
    OperationOutcome moveObjectsToLayer(std::vector<LevelObject> const& objects, std::string const& layerId);
    // Layer owning a live object (tag group or record), empty if none.
    std::string layerOfObject(LevelObject const& obj) const;

    // History of studio operations.
    bool undo();
    bool redo();
    // Restores the configuration (palette/layers/masks) before the last change.
    bool restorePreviousConfiguration();
    void pushConfigChange(std::string description, json::Value before);

    // Visibility of layers in the editor.
    void refreshVisibility();

    StudioStats stats();
    std::string lastError() const { return m_lastError; }

private:
    OperationOutcome commit(GenerationResult result, GenerationRequest const& request);
    std::vector<LevelObject> objectsOfRecords(std::vector<uint64_t> const& uids) const;
    void refreshUsedIds(std::vector<LevelObject> const& objects);

    LevelBackend& m_backend;
    DecoCatalog const& m_catalog;
    Project m_project;
    History m_history;
    std::shared_ptr<LevelAnalysis> m_analysis;
    struct Pending {
        GenerationRequest request;
        GenerationResult result;
    };
    std::optional<Pending> m_pending;
    GenerationResult m_lastResult;
    std::string m_lastError;
    std::vector<json::Value> m_configHistory;
};

// Level-string backend (tests, CLI).
class StringLevelBackend : public LevelBackend {
public:
    explicit StringLevelBackend(Level level, std::string name = "Level") : m_level(std::move(level)), m_name(std::move(name)) {}

    Level const& level() const { return m_level; }
    Level& level() { return m_level; }

    std::vector<LevelObject> readObjects() override { return m_level.objects; }
    LevelHeader header() override { return m_level.header; }
    std::string levelName() override { return m_name; }
    std::vector<std::string> createObjects(std::vector<LevelObject> const& objects) override;
    size_t removeObjects(std::vector<LevelObject> const& objects) override;
    std::vector<ColorChannel> readChannels() override { return m_level.header.colors(); }
    void writeChannels(std::vector<ColorChannel> const& channels) override;

private:
    Level m_level;
    std::string m_name;
};

}  // namespace gdds
