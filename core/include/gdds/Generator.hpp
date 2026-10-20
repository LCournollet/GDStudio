#pragma once

// Generation orchestrator: turns (project, analysis, request) into a set of
// validated placements and a set of records to remove. It never touches the
// level itself - the Studio applies the result through a LevelBackend.

#include "Analysis.hpp"
#include "Composition.hpp"
#include "DecoCatalog.hpp"
#include "Engine.hpp"
#include "Placement.hpp"
#include "Project.hpp"

#include <atomic>
#include <map>
#include <string>
#include <vector>

namespace gdds {

enum class OperationMode : uint8_t { Generate, Regenerate, Remove };

struct GenerationRequest {
    OperationMode mode = OperationMode::Generate;
    std::vector<std::string> layers;  // empty = every enabled layer
    std::vector<std::string> masks;   // operation masks ("... in mask"); empty = whole level
    // Overrides the project's policy for user-modified objects (used after
    // the user answered an "Ask" confirmation).
    std::optional<ModifiedPolicy> modifiedPolicy;
};

struct LayerStats {
    std::string layerId;
    int placed = 0;
    int rejected = 0;
    int removed = 0;
    int keptModified = 0;
    double ms = 0;
};

enum class IssueSeverity : uint8_t { Info, Warning, Error };

struct Issue {
    IssueSeverity severity = IssueSeverity::Warning;
    std::string code;     // stable identifier ("hazard-overlap", "unsafe-object"...)
    std::string message;
    Vec2 where;
    std::string layerId;
};

struct HarmonizeReport {
    int overlapsRemoved = 0;
    int densityTrimmed = 0;
    int repetitionsVaried = 0;
    int emptyZones = 0;
    int crowdedZones = 0;
    std::vector<Issue> issues;
};

struct ValidationReport {
    int checked = 0;
    int rejected = 0;
    std::vector<Issue> issues;
    // What the validation can and cannot guarantee (shown in the UI).
    std::vector<std::string> limits;
};

struct GenerationResult {
    std::vector<Placement> placements;       // final, colored, tagged objects to create
    std::vector<uint64_t> removeRecords;     // records whose objects must be deleted
    std::vector<uint64_t> keptModified;      // modified records kept by policy
    std::vector<uint64_t> askModified;       // modified records awaiting a decision
    std::vector<std::string> skippedLocked;  // locked layers left untouched
    std::vector<std::string> warnings;
    std::vector<LayerStats> layerStats;
    std::map<int, int> objectsPerSection;    // section index -> placements
    HarmonizeReport harmony;
    ValidationReport validation;
    double ms = 0;
    bool cancelled = false;
    bool finalized = false;  // colors / channels / tag groups assigned
};

class Generator {
public:
    explicit Generator(DecoCatalog const& catalog) : m_catalog(catalog) {}

    // Pure: reads the project, never modifies it (safe on a worker thread with
    // a project snapshot). Call finalizePlacements() before applying.
    GenerationResult run(Project const& project, LevelAnalysis const& analysis, GenerationRequest const& request,
                         std::atomic<bool> const* cancel = nullptr) const;

    // Layers a request applies to (groups expanded, locked/disabled filtered).
    std::vector<std::string> resolveLayers(Project const& project, GenerationRequest const& request,
                                           std::vector<std::string>* skippedLocked = nullptr) const;

    // Default draw slot of a layer kind relative to the level's gameplay.
    static ZSlot autoZ(LayerKind kind, LevelAnalysis const& analysis);

private:
    DecoCatalog const& m_catalog;
};

// Assigns color channels (allocating them in the project), the layers' tag
// groups and per-object color variation. Main thread, before applying.
void finalizePlacements(Project& project, GenerationResult& result);

// Post-pass on the proposals of one operation.
HarmonizeReport harmonize(std::vector<Placement>& placements, LevelAnalysis const& analysis,
                          CompositionPlan const& plan, Project const& project);

// Safety validation of proposals (ids, behaviour, readability).
ValidationReport validatePlacements(std::vector<Placement>& placements, LevelAnalysis const& analysis,
                                    DecoCatalog const& catalog, Project const& project);

}  // namespace gdds
