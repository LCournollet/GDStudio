#pragma once

// One studio session per opened editor: the backend, the Studio, the project
// file and the UI state (selected layer / mask / catalog object).

#include "EditorBackend.hpp"
#include "MaskTool.hpp"

#include <gdds/DecoCatalog.hpp>
#include <gdds/Presets.hpp>
#include <gdds/Studio.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>
#include <optional>
#include <string>
#include <vector>

class StudioSession {
public:
    static StudioSession* current();
    static StudioSession* begin(LevelEditorLayer* lel, EditorUI* ui);
    static void end();

    LevelEditorLayer* editor() const { return m_lel; }
    EditorBackend& backend() { return *m_backend; }
    gdds::Studio& studio() { return *m_studio; }
    gdds::Project& project() { return m_studio->project(); }
    gdds::DecoCatalog& catalog() { return m_catalog; }
    MaskTool& maskTool() { return *m_maskTool; }

    // Loads the project (or creates one) and reconciles it with the level.
    // Called lazily the first time the studio is opened.
    void ensureReady();
    bool ready() const { return m_ready; }
    gdds::ReconcileReport const& lastReconcile() const { return m_reconcile; }

    // Persists the project next to the mod's save data.
    void save();
    std::filesystem::path projectPath() const { return m_projectPath; }

    // Runs an operation and reports it (notification + status line). The
    // computation runs on a worker thread; the level is only modified on the
    // main thread when it completes (see pollJob). Returns false if another
    // operation is still running.
    bool run(gdds::GenerationRequest const& request);
    bool preview(gdds::GenerationRequest const& request);
    // Called regularly on the main thread (toolbar scheduler).
    void pollJob();
    void cancelJob();
    // Cancels and discards a running job without applying anything (save / exit).
    void abortJob();
    bool jobRunning() const { return m_job != nullptr; }
    double jobSeconds() const;
    void report(gdds::OperationOutcome const& out);
    void status(std::string message, bool error = false);
    std::string const& lastStatus() const { return m_status; }

    // Config edits that should be undoable: call before changing the config.
    void beginConfigEdit();
    void commitConfigEdit(std::string const& description);

    // UI state
    std::string selectedLayer;
    std::string selectedMask;
    int selectedCatalogId = 0;
    bool editLayerPalette = false;  // colors tab edits the selected layer's override
    int activeTab = 0;

    // Favorites / collections (global, shared by all levels)
    std::vector<int> favorites() const;
    void toggleFavorite(int id);
    std::vector<std::pair<std::string, std::vector<int>>> collections() const;
    void saveCollection(std::string const& name, std::vector<int> const& ids);

    // User presets (config/presets/*.json)
    std::vector<std::pair<gdds::Preset, std::filesystem::path>> userPresets() const;
    std::filesystem::path presetsDir() const;

    ~StudioSession();

private:
    StudioSession(LevelEditorLayer* lel, EditorUI* ui);
    void verifyCatalog();
    bool startJob(gdds::GenerationRequest const& request, bool preview);
    void handleOutcome(gdds::OperationOutcome const& out, gdds::GenerationRequest const& request, bool preview);

    struct Job {
        gdds::Studio::Prepared prepared;
        bool preview = false;
        std::atomic<bool> cancel{false};
        std::atomic<bool> done{false};
        gdds::GenerationResult result;
        std::thread thread;
        std::chrono::steady_clock::time_point started;
    };
    std::unique_ptr<Job> m_job;

    LevelEditorLayer* m_lel;
    gdds::DecoCatalog m_catalog;
    std::unique_ptr<EditorBackend> m_backend;
    std::unique_ptr<gdds::Studio> m_studio;
    std::unique_ptr<MaskTool> m_maskTool;
    std::filesystem::path m_projectPath;
    gdds::ReconcileReport m_reconcile;
    std::optional<gdds::json::Value> m_configBefore;
    std::string m_status;
    bool m_ready = false;
};
