#include <gdds/Presets.hpp>
#include <gdds/Studio.hpp>

#include <algorithm>
#include <chrono>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace gdds {

std::vector<AnalysisInput> LevelBackend::analysisInputs(std::vector<LevelObject> const& objects, Project const& project) {
    std::vector<AnalysisInput> out;
    out.reserve(objects.size());
    auto& table = StaticObjectTable::get();
    auto index = project.generatedIndex();
    for (auto& o : objects) out.push_back({o, table.info(o.id), index.contains(o)});
    Analyzer::refineInputs(out);
    return out;
}

// ---------------------------------------------------------------------------
// StringLevelBackend

std::vector<std::string> StringLevelBackend::createObjects(std::vector<LevelObject> const& objects) {
    std::vector<std::string> out;
    out.reserve(objects.size());
    for (auto& o : objects) {
        m_level.objects.push_back(o);
        out.push_back(o.serialize());
    }
    return out;
}

size_t StringLevelBackend::removeObjects(std::vector<LevelObject> const& objects) {
    std::unordered_multiset<uint64_t> wanted;
    for (auto& o : objects) wanted.insert(o.fingerprint());
    size_t removed = 0;
    std::vector<LevelObject> kept;
    kept.reserve(m_level.objects.size());
    for (auto& o : m_level.objects) {
        auto it = wanted.find(o.fingerprint());
        if (it != wanted.end()) {
            wanted.erase(it);
            ++removed;
            continue;
        }
        kept.push_back(std::move(o));
    }
    m_level.objects = std::move(kept);
    return removed;
}

void StringLevelBackend::writeChannels(std::vector<ColorChannel> const& channels) { m_level.header.setColors(channels); }

// ---------------------------------------------------------------------------
// Studio

namespace {

std::unordered_map<uint64_t, size_t> countByFingerprint(std::vector<LevelObject> const& objects) {
    std::unordered_map<uint64_t, size_t> out;
    for (auto& o : objects) ++out[o.fingerprint()];
    return out;
}

// Removal check that does not trust the backend's own count: for every
// fingerprint asked for, how many objects can be removed (`expected`) and how
// many actually disappeared between `before` and `after` (`done`).
struct RemovalCheck {
    size_t expected = 0;
    size_t done = 0;
};

RemovalCheck checkRemoval(std::vector<LevelObject> const& wanted, std::vector<LevelObject> const& before,
                          std::vector<LevelObject> const& after) {
    auto asked = countByFingerprint(wanted);
    auto had = countByFingerprint(before);
    auto has = countByFingerprint(after);
    RemovalCheck c;
    for (auto& [fp, n] : asked) {
        size_t present = had.count(fp) ? had[fp] : 0;
        size_t remaining = has.count(fp) ? has[fp] : 0;
        size_t expected = std::min(n, present);
        c.expected += expected;
        c.done += std::min(expected, present > remaining ? present - remaining : 0);
    }
    return c;
}

// After a failed removal: forget the records among `uids` whose object is
// really gone from `now`, keep the others.
void forgetRecordsWithoutObject(Project& project, std::vector<uint64_t> const& uids, std::vector<LevelObject> const& now) {
    std::unordered_multiset<uint64_t> live;
    for (auto& o : now) live.insert(o.fingerprint());
    std::vector<uint64_t> gone;
    for (auto uid : uids) {
        auto r = project.findRecord(uid);
        if (!r) continue;
        auto it = live.find(r->fingerprint);
        if (it != live.end()) live.erase(it);
        else gone.push_back(uid);
    }
    project.removeRecords(gone);
}

std::string removalFailure(RemovalCheck const& c, char const* what) {
    return std::string(what) + " stopped: the game removed " + std::to_string(c.done) + " of " + std::to_string(c.expected) +
           " objects, so nothing was re-created (no duplicates).";
}

}  // namespace

Studio::Studio(LevelBackend& backend, DecoCatalog const& catalog) : m_backend(backend), m_catalog(catalog) {}

void Studio::refreshUsedIds(std::vector<LevelObject> const& objects) {
    auto index = m_project.generatedIndex();
    std::vector<LevelObject> userObjects;
    userObjects.reserve(objects.size());
    for (auto& o : objects)
        if (!index.contains(o)) userObjects.push_back(o);

    std::set<int> tagSet;
    for (auto& [g, l] : m_project.tagGroups()) tagSet.insert(g);
    auto usedGroups = collectUsedGroups(userObjects);
    // A tag group the level started using for its own objects is compromised:
    // give the layer a new one (its existing objects keep matching by fingerprint).
    for (auto& [g, lid] : m_project.tagGroups()) {
        if (usedGroups.count(g)) {
            if (auto l = m_project.layers.find(lid)) l->tagGroup = 0;
        }
    }
    if (m_project.settings.useTagGroups) {
        std::set<int> taken = usedGroups;
        taken.insert(tagSet.begin(), tagSet.end());
        m_project.allocateTagGroups(taken);
    }

    std::set<int> studioChannels;
    for (auto& a : m_project.channels.allocations()) studioChannels.insert(a.channel);
    m_project.channels.setUsedByLevel(collectUsedChannels(userObjects, m_backend.readChannels(), studioChannels));
}

ReconcileReport Studio::attach(std::optional<Project> project) {
    if (project) m_project = std::move(*project);
    else {
        m_project = Project::withDefaultLayers();
        m_project.levelName = m_backend.levelName();
    }
    if (m_project.levelName.empty()) m_project.levelName = m_backend.levelName();
    auto objects = m_backend.readObjects();
    auto rep = m_project.reconcile(objects);
    refreshUsedIds(objects);
    m_history.clear();
    m_analysis.reset();
    m_pending.reset();
    return rep;
}

LevelAnalysis const& Studio::analysis() {
    if (!m_analysis) {
        auto objects = m_backend.readObjects();
        auto inputs = m_backend.analysisInputs(objects, m_project);
        auto header = m_backend.header();
        m_analysis = Analyzer().analyze(std::move(inputs), &header);
    }
    return *m_analysis;
}

std::vector<LevelObject> Studio::objectsOfRecords(std::vector<uint64_t> const& uids) const {
    std::vector<LevelObject> out;
    std::set<uint64_t> wanted(uids.begin(), uids.end());
    for (auto& r : m_project.records) {
        if (!wanted.count(r.uid) || r.state == RecordState::Missing) continue;
        if (auto o = LevelObject::parse(r.objectString)) out.push_back(std::move(*o));
    }
    return out;
}

Studio::Prepared Studio::prepare(GenerationRequest const& request) {
    cancelPreview();
    auto objects = m_backend.readObjects();
    refreshUsedIds(objects);
    analysis();
    return Prepared{request, m_project, m_analysis, &m_catalog};
}

GenerationResult Studio::compute(Prepared const& prepared, std::atomic<bool> const* cancel) {
    Generator gen(*prepared.catalog);
    return gen.run(prepared.project, *prepared.analysis, prepared.request, cancel);
}

OperationOutcome Studio::finish(Prepared const& prepared, GenerationResult result, bool asPreview) {
    OperationOutcome out;
    if (result.cancelled) {
        out.message = asPreview ? "Preview cancelled" : "Generation cancelled";
        return out;
    }
    if (asPreview) {
        finalizePlacements(m_project, result);
        out.ok = true;
        out.needsConfirmation = result.askModified;
        out.created = int(result.placements.size());
        out.removed = int(result.removeRecords.size());
        out.skippedLocked = result.skippedLocked;
        std::vector<LevelObject> add;
        add.reserve(result.placements.size());
        for (auto& p : result.placements) add.push_back(p.object);
        auto hide = objectsOfRecords(result.removeRecords);
        if (m_backend.supportsPreview()) m_backend.showPreview(add, hide);
        m_pending = Pending{prepared.request, std::move(result)};
        out.result = &m_pending->result;
        out.message = "Preview: " + std::to_string(out.created) + " objects proposed, " + std::to_string(out.removed) +
                      " would be replaced";
        return out;
    }
    if (!result.askModified.empty()) {
        out.ok = false;
        out.needsConfirmation = result.askModified;
        out.message = std::to_string(result.askModified.size()) +
                      " decoration objects were modified by hand in this area. Keep or replace them?";
        m_lastResult = std::move(result);
        out.result = &m_lastResult;
        return out;
    }
    finalizePlacements(m_project, result);
    return commit(std::move(result), prepared.request);
}

OperationOutcome Studio::run(GenerationRequest const& request, std::atomic<bool> const* cancel) {
    auto prepared = prepare(request);
    return finish(prepared, compute(prepared, cancel), false);
}

OperationOutcome Studio::preview(GenerationRequest const& request, std::atomic<bool> const* cancel) {
    auto prepared = prepare(request);
    return finish(prepared, compute(prepared, cancel), true);
}

OperationOutcome Studio::applyPreview() {
    if (!m_pending) {
        OperationOutcome out;
        out.message = "Nothing to apply";
        return out;
    }
    if (m_backend.supportsPreview()) m_backend.clearPreview();
    auto pending = std::move(*m_pending);
    m_pending.reset();
    if (!pending.result.askModified.empty()) {
        // Applying a preview counts as an answer: keep the modified objects.
        pending.result.keptModified.insert(pending.result.keptModified.end(), pending.result.askModified.begin(),
                                           pending.result.askModified.end());
        pending.result.askModified.clear();
    }
    return commit(std::move(pending.result), pending.request);
}

void Studio::cancelPreview() {
    if (m_pending && m_backend.supportsPreview()) m_backend.clearPreview();
    m_pending.reset();
}

namespace {
std::vector<ColorChannel> mergeChannels(std::vector<ColorChannel> existing, std::vector<ColorChannel> const& ours) {
    for (auto& c : ours) {
        bool replaced = false;
        for (auto& e : existing)
            if (e.id == c.id) {
                // keep unknown keys of an existing definition, overwrite the rest
                auto extra = e.extra;
                e = c;
                e.extra = extra;
                replaced = true;
                break;
            }
        if (!replaced) existing.push_back(c);
    }
    return existing;
}
}  // namespace

void Studio::applyColors() { m_backend.writeChannels(mergeChannels(m_backend.readChannels(), m_project.channelDefinitions())); }

std::string Studio::layerOfObject(LevelObject const& obj) const {
    for (auto& [g, lid] : m_project.tagGroups())
        if (obj.hasGroup(g)) return lid;
    uint64_t fp = obj.fingerprint();
    for (auto& r : m_project.records)
        if (r.fingerprint == fp) return r.layerId;
    return "";
}

OperationOutcome Studio::deleteLayer(std::string const& layerId) {
    OperationOutcome out;
    if (!m_project.layers.find(layerId)) {
        out.message = "Unknown layer";
        return out;
    }
    if (m_project.layers.isLocked(layerId)) {
        out.message = "Layer is locked";
        return out;
    }
    GenerationRequest req;
    req.mode = OperationMode::Remove;
    req.layers = {layerId};
    req.modifiedPolicy = ModifiedPolicy::Replace;  // deleting a layer deletes all of its objects
    out = run(req);
    if (!out.ok) return out;
    auto before = m_project.configJson();
    for (auto& id : m_project.layers.remove(layerId)) m_project.channels.releaseLayer(id);
    pushConfigChange("delete layer", before);
    out.message = "Layer deleted (" + std::to_string(out.removed) + " objects removed)";
    return out;
}

OperationOutcome Studio::moveObjectsToLayer(std::vector<LevelObject> const& objects, std::string const& layerId) {
    OperationOutcome out;
    auto target = m_project.layers.find(layerId);
    if (!target || target->kind == LayerKind::Group) {
        out.message = "Choose a layer (not a group) as destination";
        return out;
    }
    if (m_project.layers.isLocked(layerId)) {
        out.message = "Destination layer is locked";
        return out;
    }
    auto tags = m_project.tagGroups();
    std::vector<LevelObject> toRemove, toCreate;
    std::vector<uint64_t> recordUids;
    for (auto& o : objects) {
        auto from = layerOfObject(o);
        if (from.empty() || from == layerId || m_project.layers.isLocked(from)) continue;
        LevelObject moved = o;
        for (auto& [g, lid] : tags) moved.removeGroup(g);
        if (target->tagGroup > 0) moved.addGroup(target->tagGroup);
        toRemove.push_back(o);
        toCreate.push_back(moved);
        uint64_t fp = o.fingerprint();
        for (auto& r : m_project.records)
            if (r.fingerprint == fp) { recordUids.push_back(r.uid); break; }
    }
    if (toRemove.empty()) {
        out.message = "Nothing to move (only generated objects of unlocked layers can be moved)";
        return out;
    }
    auto beforeObjects = m_backend.readObjects();
    auto snapBefore = GameplaySnapshot::take(beforeObjects, m_project);
    m_backend.removeObjects(toRemove);
    auto now = m_backend.readObjects();
    if (auto check = checkRemoval(toRemove, beforeObjects, now); check.done < check.expected) {
        forgetRecordsWithoutObject(m_project, recordUids, now);
        out.message = removalFailure(check, "Move");
        m_lastError = out.message;
        return out;
    }
    m_project.removeRecords(recordUids);
    auto stored = m_backend.createObjects(toCreate);
    auto& op = m_project.beginOperation("move-to-layer", {layerId}, {});
    for (size_t i = 0; i < stored.size(); ++i) {
        if (stored[i].empty()) continue;
        GeneratedRecord rec;
        rec.layerId = layerId;
        rec.operationId = op.id;
        rec.objectString = stored[i];
        rec.anchor = toCreate[i].pos;
        rec.state = RecordState::Modified;  // no longer the engine's exact output
        m_project.addRecord(rec);
    }
    auto snapAfter = GameplaySnapshot::take(m_backend.readObjects(), m_project);
    out.gameplayIntact = compareSnapshots(snapBefore, snapAfter).identical();
    out.ok = out.gameplayIntact;
    out.created = int(stored.size());
    out.message = "Moved " + std::to_string(stored.size()) + " objects to " + target->name;
    m_history.clear();  // object identities changed: older history entries no longer apply
    refreshVisibility();
    return out;
}

OperationOutcome Studio::commit(GenerationResult result, GenerationRequest const& request) {
    OperationOutcome out;
    auto t0 = std::chrono::steady_clock::now();
    auto beforeObjects = m_backend.readObjects();
    auto snapBefore = GameplaySnapshot::take(beforeObjects, m_project);

    // ---- removal ----------------------------------------------------------------
    std::vector<GeneratedRecord> removedRecords;
    for (auto uid : result.removeRecords)
        if (auto r = m_project.findRecord(uid)) removedRecords.push_back(*r);
    auto toRemove = objectsOfRecords(result.removeRecords);
    size_t removed = 0;
    if (!toRemove.empty()) {
        m_backend.removeObjects(toRemove);
        // Check what really disappeared instead of trusting the backend.
        auto now = m_backend.readObjects();
        auto check = checkRemoval(toRemove, beforeObjects, now);
        removed = check.done;
        if (check.done < check.expected) {
            // The game did not delete what it was asked to delete (e.g. a wrong
            // binding). Creating the new objects now would duplicate the
            // decoration: stop here and only forget records whose object is gone.
            forgetRecordsWithoutObject(m_project, result.removeRecords, now);
            out.ok = false;
            out.gameplayIntact = compareSnapshots(snapBefore, GameplaySnapshot::take(now, m_project)).identical();
            out.removed = int(check.done);
            out.message = removalFailure(check, "Operation");
            m_lastError = out.message;
            m_lastResult = std::move(result);
            out.result = &m_lastResult;
            return out;
        }
    }
    m_project.removeRecords(result.removeRecords);

    // ---- creation -----------------------------------------------------------------
    std::string opType = request.mode == OperationMode::Remove ? "remove"
                         : result.removeRecords.empty()       ? "generate"
                                                               : "regenerate";
    auto& op = m_project.beginOperation(opType, request.layers, request.masks);
    std::string opId = op.id;
    std::vector<LevelObject> toCreate;
    toCreate.reserve(result.placements.size());
    for (auto& p : result.placements) toCreate.push_back(p.object);
    auto stored = m_backend.createObjects(toCreate);
    std::vector<GeneratedRecord> addedRecords;
    for (size_t i = 0; i < result.placements.size() && i < stored.size(); ++i) {
        if (stored[i].empty()) continue;
        GeneratedRecord rec;
        rec.layerId = result.placements[i].layerId;
        rec.operationId = opId;
        rec.placementKey = result.placements[i].key;
        rec.objectString = stored[i];
        rec.anchor = result.placements[i].anchor;
        uint64_t uid = m_project.addRecord(rec);
        addedRecords.push_back(*m_project.findRecord(uid));
    }
    applyColors();

    // ---- gameplay safety check (atomic: roll back on any difference) ---------------
    auto afterObjects = m_backend.readObjects();
    auto snapAfter = GameplaySnapshot::take(afterObjects, m_project);
    auto diff = compareSnapshots(snapBefore, snapAfter);
    if (!diff.identical()) {
        std::vector<LevelObject> created;
        for (auto& r : addedRecords)
            if (auto o = LevelObject::parse(r.objectString)) created.push_back(*o);
        m_backend.removeObjects(created);
        std::vector<uint64_t> addedUids;
        for (auto& r : addedRecords) addedUids.push_back(r.uid);
        m_project.removeRecords(addedUids);
        std::vector<LevelObject> restore;
        for (auto& r : removedRecords)
            if (auto o = LevelObject::parse(r.objectString)) restore.push_back(*o);
        auto restoredStrings = m_backend.createObjects(restore);
        for (size_t i = 0; i < removedRecords.size() && i < restoredStrings.size(); ++i) {
            auto rec = removedRecords[i];
            rec.objectString = restoredStrings[i];
            rec.fingerprint = 0;
            m_project.addRecord(rec);
        }
        m_project.operations.pop_back();
        out.ok = false;
        out.gameplayIntact = false;
        out.message = "Operation rolled back: the gameplay check detected a difference (" +
                      std::to_string(diff.missing) + " missing, " + std::to_string(diff.added) + " unexpected objects).";
        m_lastError = out.message;
        m_lastResult = std::move(result);
        out.result = &m_lastResult;
        return out;
    }

    auto* opPtr = &m_project.operations.back();
    opPtr->created = int(addedRecords.size());
    opPtr->removed = int(removed);

    HistoryEntry h;
    h.description = opType + " (" + std::to_string(addedRecords.size()) + " added, " + std::to_string(removed) + " removed)";
    h.added = addedRecords;
    h.removed = removedRecords;
    if (!addedRecords.empty() || !removedRecords.empty()) m_history.push(std::move(h));

    out.ok = true;
    out.created = int(addedRecords.size());
    out.removed = int(removed);
    out.keptModified = int(result.keptModified.size());
    out.skippedLocked = result.skippedLocked;
    out.issues = result.validation.issues;
    out.issues.insert(out.issues.end(), result.harmony.issues.begin(), result.harmony.issues.end());
    out.message = "Done: " + std::to_string(out.created) + " objects created, " + std::to_string(out.removed) + " removed";
    if (out.keptModified) out.message += ", " + std::to_string(out.keptModified) + " hand-modified objects kept";
    if (!out.skippedLocked.empty()) out.message += ", " + std::to_string(out.skippedLocked.size()) + " locked layers skipped";
    result.ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    m_lastResult = std::move(result);
    out.result = &m_lastResult;
    refreshVisibility();
    return out;
}

bool Studio::undo() {
    cancelPreview();
    auto e = m_history.peekUndo();
    if (!e) return false;
    // Remove what the operation added (current strings, they may have been retouched).
    std::vector<uint64_t> addedUids;
    std::vector<LevelObject> toRemove;
    for (auto& r : e->added) {
        if (auto cur = m_project.findRecord(r.uid)) {
            if (auto o = LevelObject::parse(cur->objectString)) toRemove.push_back(*o);
            addedUids.push_back(r.uid);
        }
    }
    if (!toRemove.empty()) {
        auto before = m_backend.readObjects();
        m_backend.removeObjects(toRemove);
        auto now = m_backend.readObjects();
        if (auto check = checkRemoval(toRemove, before, now); check.done < check.expected) {
            forgetRecordsWithoutObject(m_project, addedUids, now);
            m_lastError = removalFailure(check, "Undo");
            return false;
        }
    }
    m_project.removeRecords(addedUids);
    // Re-create what it removed.
    std::vector<LevelObject> restore;
    for (auto& r : e->removed)
        if (auto o = LevelObject::parse(r.objectString)) restore.push_back(*o);
    auto stored = m_backend.createObjects(restore);
    for (size_t i = 0; i < e->removed.size() && i < stored.size(); ++i) {
        auto rec = e->removed[i];
        rec.objectString = stored[i];
        rec.fingerprint = 0;
        // keep the uid so a later redo can find it
        auto uid = rec.uid;
        m_project.records.push_back(rec);
        m_project.records.back().uid = uid;
        if (auto o = LevelObject::parse(stored[i])) m_project.records.back().fingerprint = o->fingerprint();
    }
    if (e->configBefore) {
        m_project.loadConfigJson(*e->configBefore);
        applyColors();
    }
    m_history.markUndone();
    m_project.beginOperation("undo", {}, {}).note = e->description;
    refreshVisibility();
    return true;
}

bool Studio::redo() {
    cancelPreview();
    auto e = m_history.peekRedo();
    if (!e) return false;
    std::vector<uint64_t> removedUids;
    std::vector<LevelObject> toRemove;
    for (auto& r : e->removed) {
        if (auto cur = m_project.findRecord(r.uid)) {
            if (auto o = LevelObject::parse(cur->objectString)) toRemove.push_back(*o);
            removedUids.push_back(r.uid);
        }
    }
    if (!toRemove.empty()) {
        auto before = m_backend.readObjects();
        m_backend.removeObjects(toRemove);
        auto now = m_backend.readObjects();
        if (auto check = checkRemoval(toRemove, before, now); check.done < check.expected) {
            forgetRecordsWithoutObject(m_project, removedUids, now);
            m_lastError = removalFailure(check, "Redo");
            return false;
        }
    }
    m_project.removeRecords(removedUids);
    std::vector<LevelObject> recreate;
    for (auto& r : e->added)
        if (auto o = LevelObject::parse(r.objectString)) recreate.push_back(*o);
    auto stored = m_backend.createObjects(recreate);
    for (size_t i = 0; i < e->added.size() && i < stored.size(); ++i) {
        auto rec = e->added[i];
        rec.objectString = stored[i];
        if (auto o = LevelObject::parse(stored[i])) rec.fingerprint = o->fingerprint();
        m_project.records.push_back(rec);
    }
    if (e->configAfter) {
        m_project.loadConfigJson(*e->configAfter);
        applyColors();
    }
    m_history.markRedone();
    m_project.beginOperation("redo", {}, {}).note = e->description;
    refreshVisibility();
    return true;
}

void Studio::pushConfigChange(std::string description, json::Value before) {
    m_configHistory.push_back(before);
    if (m_configHistory.size() > 50) m_configHistory.erase(m_configHistory.begin());
    HistoryEntry h;
    h.description = std::move(description);
    h.configBefore = std::move(before);
    h.configAfter = m_project.configJson();
    m_history.push(std::move(h));
}

bool Studio::restorePreviousConfiguration() {
    if (m_configHistory.empty()) return false;
    auto before = m_project.configJson();
    m_project.loadConfigJson(m_configHistory.back());
    m_configHistory.pop_back();
    HistoryEntry h;
    h.description = "restore configuration";
    h.configBefore = std::move(before);
    h.configAfter = m_project.configJson();
    m_history.push(std::move(h));
    applyColors();
    refreshVisibility();
    return true;
}

void Studio::refreshVisibility() {
    std::vector<int> hidden;
    for (auto& l : m_project.layers.all())
        if (!m_project.layers.isVisible(l.id) && l.tagGroup > 0) hidden.push_back(l.tagGroup);
    m_backend.setHiddenTagGroups(hidden);
}

StudioStats Studio::stats() {
    StudioStats s;
    auto& a = analysis();
    s.gameplayObjects = a.stats.gameplayObjects;
    s.structures = a.stats.structures;
    s.analysisMs = a.stats.analysisMs;
    s.perLayer = m_project.objectCountByLayer();
    for (auto& [l, n] : s.perLayer) s.generatedObjects += n;
    auto plan = CompositionPlan::build(a, m_project.settings.globalSeed, m_project.settings.globalDensity);
    double complexity = 0;
    for (auto& r : m_project.records) {
        auto& sec = plan.at(r.anchor.x);
        s.perSection[sec.index]++;
        if (auto o = LevelObject::parse(r.objectString)) complexity += 1.0 + 0.002 * o->scaleX * o->scaleY;
    }
    s.complexity = complexity;
    s.generationMs = m_lastResult.ms;
    return s;
}

}  // namespace gdds
