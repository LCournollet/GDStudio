#include <gdds/Project.hpp>
#include <gdds/Placement.hpp>
#include <gdds/Random.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace gdds {

char const* modifiedPolicyName(ModifiedPolicy p) {
    switch (p) {
        case ModifiedPolicy::Keep: return "keep";
        case ModifiedPolicy::Replace: return "replace";
        case ModifiedPolicy::Ask: return "ask";
    }
    return "keep";
}

ModifiedPolicy modifiedPolicyFromName(std::string const& n) {
    if (n == "replace") return ModifiedPolicy::Replace;
    if (n == "ask") return ModifiedPolicy::Ask;
    return ModifiedPolicy::Keep;
}

// ---------------------------------------------------------------------------
// JSON helpers

json::Value GenerationSettings::toJson() const {
    json::Value v = json::Value::object();
    v["structural"] = structural;
    v["lighting"] = lighting;
    v["artistic"] = artistic;
    v["modifiedPolicy"] = modifiedPolicyName(modifiedPolicy);
    v["useTagGroups"] = useTagGroups;
    v["maxObjectsPerOperation"] = maxObjectsPerOperation;
    v["globalDensity"] = globalDensity;
    v["globalSeed"] = std::to_string(globalSeed);
    v["presetName"] = presetName;
    return v;
}

GenerationSettings GenerationSettings::fromJson(json::Value const& v) {
    GenerationSettings s;
    s.structural = v.boolean("structural", true);
    s.lighting = v.boolean("lighting", true);
    s.artistic = v.boolean("artistic", true);
    s.modifiedPolicy = modifiedPolicyFromName(v.str("modifiedPolicy", "keep"));
    s.useTagGroups = v.boolean("useTagGroups", true);
    s.maxObjectsPerOperation = v.integer("maxObjectsPerOperation", 25000);
    s.globalDensity = v.num("globalDensity", 1);
    s.globalSeed = v.get("globalSeed").asUInt64(1);
    s.presetName = v.str("presetName");
    return s;
}

namespace {
char const* stateName(RecordState s) {
    switch (s) {
        case RecordState::Linked: return "linked";
        case RecordState::Modified: return "modified";
        case RecordState::Missing: return "missing";
    }
    return "linked";
}
RecordState stateFromName(std::string const& n) {
    if (n == "modified") return RecordState::Modified;
    if (n == "missing") return RecordState::Missing;
    return RecordState::Linked;
}
json::Value stringArray(std::vector<std::string> const& v) {
    json::Value a = json::Value::array();
    for (auto& s : v) a.push(s);
    return a;
}
std::vector<std::string> stringsFrom(json::Value const& v) {
    std::vector<std::string> out;
    for (auto& s : v.items()) out.push_back(s.asString());
    return out;
}
}  // namespace

json::Value GeneratedRecord::toJson() const {
    json::Value v = json::Value::object();
    v["uid"] = std::to_string(uid);
    v["layer"] = layerId;
    v["op"] = operationId;
    v["key"] = std::to_string(placementKey);
    v["fp"] = std::to_string(fingerprint);
    v["obj"] = objectString;
    v["ax"] = anchor.x;
    v["ay"] = anchor.y;
    v["state"] = stateName(state);
    return v;
}

GeneratedRecord GeneratedRecord::fromJson(json::Value const& v) {
    GeneratedRecord r;
    r.uid = v.get("uid").asUInt64();
    r.layerId = v.str("layer");
    r.operationId = v.str("op");
    r.placementKey = v.get("key").asUInt64();
    r.fingerprint = v.get("fp").asUInt64();
    r.objectString = v.str("obj");
    // The object string is the source of truth: recompute the fingerprint so
    // projects saved before a change of the canonical form still match.
    if (auto o = LevelObject::parse(r.objectString)) r.fingerprint = o->fingerprint();
    r.anchor = {v.num("ax"), v.num("ay")};
    r.state = stateFromName(v.str("state"));
    return r;
}

json::Value Operation::toJson() const {
    json::Value v = json::Value::object();
    v["id"] = id;
    v["type"] = type;
    v["layers"] = stringArray(layers);
    v["masks"] = stringArray(masks);
    v["time"] = std::to_string(timestamp);
    v["created"] = created;
    v["removed"] = removed;
    v["note"] = note;
    return v;
}

Operation Operation::fromJson(json::Value const& v) {
    Operation o;
    o.id = v.str("id");
    o.type = v.str("type");
    o.layers = stringsFrom(v.get("layers"));
    o.masks = stringsFrom(v.get("masks"));
    o.timestamp = int64_t(v.get("time").asUInt64());
    o.created = v.integer("created");
    o.removed = v.integer("removed");
    o.note = v.str("note");
    return o;
}

// ---------------------------------------------------------------------------
// Registry

GeneratedRecord* Project::findRecord(uint64_t uid) {
    for (auto& r : records) if (r.uid == uid) return &r;
    return nullptr;
}

std::vector<GeneratedRecord const*> Project::recordsOf(std::string const& layerId) const {
    std::vector<GeneratedRecord const*> out;
    for (auto& r : records) if (r.layerId == layerId) out.push_back(&r);
    return out;
}

uint64_t Project::addRecord(GeneratedRecord rec) {
    for (auto& r : records) m_nextUid = std::max(m_nextUid, r.uid + 1);
    rec.uid = m_nextUid++;
    if (rec.fingerprint == 0) {
        if (auto o = LevelObject::parse(rec.objectString)) rec.fingerprint = o->fingerprint();
    }
    records.push_back(std::move(rec));
    return records.back().uid;
}

void Project::removeRecords(std::vector<uint64_t> const& uids) {
    std::set<uint64_t> s(uids.begin(), uids.end());
    records.erase(std::remove_if(records.begin(), records.end(), [&](GeneratedRecord const& r) { return s.count(r.uid); }),
                  records.end());
}

std::map<std::string, int> Project::objectCountByLayer() const {
    std::map<std::string, int> out;
    for (auto& r : records)
        if (r.state != RecordState::Missing) out[r.layerId]++;
    return out;
}

std::map<int, std::string> Project::tagGroups() const {
    std::map<int, std::string> out;
    for (auto& l : layers.all())
        if (l.tagGroup > 0) out[l.tagGroup] = l.id;
    return out;
}

void Project::allocateTagGroups(std::set<int> const& usedGroups) {
    std::set<int> taken = usedGroups;
    for (auto& l : layers.all())
        if (l.tagGroup > 0) taken.insert(l.tagGroup);
    for (auto const& l : layers.all()) {
        if (l.tagGroup > 0 || l.kind == LayerKind::Group) continue;
        auto* layer = layers.find(l.id);
        for (int g = 9999; g >= 1000; --g) {
            if (taken.count(g)) continue;
            layer->tagGroup = g;
            taken.insert(g);
            break;
        }
    }
}

bool Project::isGenerated(LevelObject const& obj) const { return generatedIndex().contains(obj); }

GeneratedIndex::GeneratedIndex(std::vector<GeneratedRecord> const& records, std::map<int, std::string> const& tags) {
    for (auto& r : records)
        if (r.state != RecordState::Missing) m_fingerprints.insert(r.fingerprint);
    for (auto& [g, l] : tags) m_tags.insert(g);
}

bool GeneratedIndex::contains(LevelObject const& obj) const {
    return contains(obj, m_fingerprints.empty() ? 0 : obj.fingerprint());
}

bool GeneratedIndex::contains(LevelObject const& obj, uint64_t fp) const {
    for (int g : obj.groups)
        if (m_tags.count(g)) return true;
    return fp != 0 && m_fingerprints.count(fp) != 0;
}

ReconcileReport Project::reconcile(std::vector<LevelObject> const& objects) {
    ReconcileReport rep;
    std::unordered_multimap<uint64_t, size_t> byFp;  // fingerprint -> record index
    for (size_t i = 0; i < records.size(); ++i) byFp.emplace(records[i].fingerprint, i);
    std::vector<char> claimed(records.size(), 0);
    std::vector<char> liveMatched(objects.size(), 0);

    // 1. exact matches
    for (size_t li = 0; li < objects.size(); ++li) {
        auto range = byFp.equal_range(objects[li].fingerprint());
        for (auto it = range.first; it != range.second; ++it) {
            if (claimed[it->second]) continue;
            claimed[it->second] = 1;
            liveMatched[li] = 1;
            records[it->second].state = RecordState::Linked;
            rep.liveToRecord[li] = records[it->second].uid;
            rep.linked++;
            break;
        }
    }

    // 2. tagged objects without an exact match: modified by the user
    auto tags = tagGroups();
    std::vector<GeneratedRecord> recovered;
    for (size_t li = 0; li < objects.size(); ++li) {
        if (liveMatched[li]) continue;
        auto const& o = objects[li];
        std::string layerId;
        for (auto& [g, lid] : tags)
            if (o.hasGroup(g)) { layerId = lid; break; }
        if (layerId.empty()) continue;
        // closest unclaimed record of the same layer and object id
        size_t best = SIZE_MAX;
        double bestD = 4 * 30.0;
        for (size_t ri = 0; ri < records.size(); ++ri) {
            if (claimed[ri] || records[ri].layerId != layerId) continue;
            auto ro = LevelObject::parse(records[ri].objectString);
            if (!ro || ro->id != o.id) continue;
            double d = (ro->pos - o.pos).length();
            if (d < bestD) { bestD = d; best = ri; }
        }
        if (best != SIZE_MAX) {
            claimed[best] = 1;
            auto& r = records[best];
            r.state = RecordState::Modified;
            r.objectString = o.serialize();
            r.fingerprint = o.fingerprint();
            rep.liveToRecord[li] = r.uid;
            rep.modified++;
        }
        else {
            GeneratedRecord r;
            r.layerId = layerId;
            r.operationId = "recovered";
            r.objectString = o.serialize();
            r.fingerprint = o.fingerprint();
            r.anchor = o.pos;
            r.state = RecordState::Modified;
            recovered.push_back(std::move(r));
            rep.recovered++;
            rep.liveToRecord[li] = 0;  // fixed below
        }
        liveMatched[li] = 1;
    }
    // 3. records without object: deleted by the user (or undone in GD)
    for (size_t ri = 0; ri < records.size(); ++ri) {
        if (claimed[ri]) continue;
        records[ri].state = RecordState::Missing;
        rep.missing++;
    }
    for (auto& r : recovered) {
        uint64_t uid = addRecord(r);
        for (auto& [li, ruid] : rep.liveToRecord)
            if (ruid == 0 && objects[li].fingerprint() == r.fingerprint) { ruid = uid; break; }
    }
    // Missing records carry no information worth keeping long term.
    records.erase(std::remove_if(records.begin(), records.end(),
                                 [](GeneratedRecord const& r) { return r.state == RecordState::Missing; }),
                  records.end());
    return rep;
}

std::vector<ColorChannel> Project::channelDefinitions() const {
    std::vector<ColorChannel> out;
    for (auto& a : channels.allocations()) {
        auto layer = layers.find(a.layer);
        PaletteOverride none;
        RoleColor rc = resolveRole(palette, layer ? layer->palette : none, a.role);
        double layerOpacity = layer ? layer->params.opacity : 1.0;
        double intensity = (layer && rc.blending) ? layer->params.intensity : 1.0;
        ColorChannel c;
        c.id = a.channel;
        c.r = rc.color.r;
        c.g = rc.color.g;
        c.b = rc.color.b;
        c.blending = rc.blending;
        c.opacity = std::clamp(rc.opacity * layerOpacity * intensity * (a.opacityBucket / 10.0), 0.0, 1.0);
        c.opacity = std::round(c.opacity * 100) / 100;
        out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.id < b.id; });
    return out;
}

Operation& Project::beginOperation(std::string type, std::vector<std::string> ls, std::vector<std::string> ms) {
    Operation op;
    auto now = std::chrono::system_clock::now().time_since_epoch();
    op.timestamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    op.id = "op-" + std::to_string(operations.size() + 1) + "-" + std::to_string(op.timestamp % 100000);
    op.type = std::move(type);
    op.layers = std::move(ls);
    op.masks = std::move(ms);
    operations.push_back(std::move(op));
    if (operations.size() > 500) operations.erase(operations.begin(), operations.begin() + 100);
    return operations.back();
}

json::Value Project::configJson() const {
    json::Value v = json::Value::object();
    v["palette"] = palette.toJson();
    v["layers"] = layers.toJson();
    json::Value ms = json::Value::array();
    for (auto& m : masks) ms.push(m.toJson());
    v["masks"] = ms;
    v["settings"] = settings.toJson();
    return v;
}

void Project::loadConfigJson(json::Value const& v) {
    if (v.contains("palette")) palette = Palette::fromJson(v.get("palette"));
    if (v.contains("layers")) layers.loadJson(v.get("layers"));
    if (v.contains("masks")) {
        masks.clear();
        for (auto& m : v.get("masks").items()) masks.push_back(Mask::fromJson(m));
    }
    if (v.contains("settings")) settings = GenerationSettings::fromJson(v.get("settings"));
}

json::Value Project::toJson() const {
    json::Value v = json::Value::object();
    v["format"] = "gd-decoration-studio-project";
    v["formatVersion"] = formatVersion;
    v["projectId"] = projectId;
    v["levelName"] = levelName;
    v["gameplaySignature"] = std::to_string(levelGameplaySignature);
    v["config"] = configJson();
    v["channels"] = channels.toJson();
    json::Value recs = json::Value::array();
    for (auto& r : records) recs.push(r.toJson());
    v["records"] = recs;
    json::Value ops = json::Value::array();
    for (auto& o : operations) ops.push(o.toJson());
    v["operations"] = ops;
    return v;
}

Project Project::fromJson(json::Value const& v) {
    Project p;
    p.formatVersion = v.integer("formatVersion", 1);
    p.projectId = v.str("projectId");
    p.levelName = v.str("levelName");
    p.levelGameplaySignature = v.get("gameplaySignature").asUInt64();
    p.loadConfigJson(v.get("config"));
    p.channels.loadJson(v.get("channels"));
    for (auto& r : v.get("records").items()) p.records.push_back(GeneratedRecord::fromJson(r));
    for (auto& o : v.get("operations").items()) p.operations.push_back(Operation::fromJson(o));
    for (auto& r : p.records) p.m_nextUid = std::max(p.m_nextUid, r.uid + 1);
    return p;
}

// ---------------------------------------------------------------------------
// Snapshots / used ids

GameplaySnapshot GameplaySnapshot::take(std::vector<LevelObject> const& objects, Project const& project) {
    GameplaySnapshot s;
    auto index = project.generatedIndex();
    for (auto& o : objects) {
        uint64_t fp = o.fingerprint();
        if (index.contains(o, fp)) continue;
        s.counts[fp]++;
        s.total++;
    }
    return s;
}

SnapshotDiff compareSnapshots(GameplaySnapshot const& before, GameplaySnapshot const& after) {
    SnapshotDiff d;
    for (auto& [fp, n] : before.counts) {
        auto it = after.counts.find(fp);
        int m = it == after.counts.end() ? 0 : it->second;
        if (m < n) d.missing += size_t(n - m);
    }
    for (auto& [fp, n] : after.counts) {
        auto it = before.counts.find(fp);
        int m = it == before.counts.end() ? 0 : it->second;
        if (n > m) d.added += size_t(n - m);
    }
    return d;
}

std::set<int> collectUsedGroups(std::vector<LevelObject> const& objects, std::set<int> const& ignoreGroups) {
    std::set<int> used;
    for (auto& o : objects) {
        for (int g : o.groups)
            if (!ignoreGroups.count(g)) used.insert(g);
        // Conservative: any integer value in a trigger/special key could be a
        // group reference (target group, center group, spawn ids...). Only
        // values in the group id range matter.
        if (!o.extra.empty()) {
            for (auto& [k, v] : o.extra) {
                if (v.empty() || v.size() > 5) continue;
                bool digits = std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; });
                if (!digits) continue;
                int n = std::stoi(v);
                if (n >= 1 && n <= 9999 && !ignoreGroups.count(n)) used.insert(n);
            }
        }
    }
    return used;
}

std::set<int> collectUsedChannels(std::vector<LevelObject> const& objects, std::vector<ColorChannel> const& headerColors,
                                  std::set<int> const& studioChannels) {
    std::set<int> used;
    for (auto& o : objects) {
        if (o.mainColor && !studioChannels.count(o.mainColor)) used.insert(o.mainColor);
        if (o.detailColor && !studioChannels.count(o.detailColor)) used.insert(o.detailColor);
        if (auto t = o.extraValue(key::TargetColor)) {
            int ch = std::atoi(t->c_str());
            if (ch > 0) used.insert(ch);
        }
    }
    for (auto& c : headerColors) {
        if (studioChannels.count(c.id)) continue;
        // A channel defined in the header with a non-default color counts as used.
        bool isDefault = c.r == 255 && c.g == 255 && c.b == 255 && c.opacity >= 1 && !c.blending && c.copyId == 0 &&
                         c.playerColor <= 0;
        if (!isDefault && c.id >= 1 && c.id <= 999) used.insert(c.id);
        if (c.copyId > 0) used.insert(c.copyId);
    }
    return used;
}

// ---------------------------------------------------------------------------
// History

void History::push(HistoryEntry e) {
    m_entries.resize(m_index);  // drop the redo branch
    m_entries.push_back(std::move(e));
    if (m_entries.size() > kMaxEntries) m_entries.erase(m_entries.begin());
    m_index = m_entries.size();
}

}  // namespace gdds
