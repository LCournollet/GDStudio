// Integration tests of the full pipeline (analysis -> engines -> harmonizer ->
// validation -> application) on a level-string backend. They cover the test
// scenarios A..L of the specification that can be verified without the game.

#include "TestFramework.hpp"

#include <gdds/ObjectInfo.hpp>
#include <gdds/Presets.hpp>
#include <gdds/Studio.hpp>
#include <gdds/TestLevels.hpp>

#include <algorithm>
#include <map>
#include <set>

using namespace gdds;

namespace {

std::multiset<std::string> canonicalSet(std::vector<LevelObject> const& objs, std::function<bool(LevelObject const&)> keep) {
    std::multiset<std::string> out;
    for (auto& o : objs)
        if (keep(o)) out.insert(o.canonical());
    return out;
}

Project presetProject(std::string const& name = "Modern Glow") {
    Project p;
    PresetManager::apply(p, *PresetManager::builtin(name));
    return p;
}

std::string layerIdOf(Project const& p, LayerKind kind) {
    for (auto& l : p.layers.all())
        if (l.kind == kind) return l.id;
    return "";
}

std::set<int> tagSet(Project const& p) {
    std::set<int> s;
    for (auto& [g, l] : p.tagGroups()) s.insert(g);
    return s;
}

bool hasAnyTag(LevelObject const& o, std::set<int> const& tags) {
    for (int g : o.groups)
        if (tags.count(g)) return true;
    return false;
}

std::multiset<std::string> objectsOfLayer(StringLevelBackend& b, Project const& p, std::string const& layerId) {
    auto layer = p.layers.find(layerId);
    std::multiset<std::string> out;
    for (auto& o : b.level().objects)
        if (layer && o.hasGroup(layer->tagGroup)) out.insert(o.canonical());
    return out;
}

}  // namespace

TEST_CASE("generation: deterministic for identical level, config and seed") {
    auto lvl = makeTestLevel("G").level;
    StringLevelBackend b1(lvl), b2(lvl);
    Studio s1(b1), s2(b2);
    s1.attach(presetProject());
    s2.attach(presetProject());
    auto o1 = s1.run({});
    auto o2 = s2.run({});
    CHECK(o1.ok && o2.ok);
    CHECK(o1.created > 300);
    CHECK_EQ(b1.level().serialize(), b2.level().serialize());
    // A different seed changes the artistic layers.
    StringLevelBackend b3(lvl);
    Studio s3(b3);
    auto p3 = presetProject();
    p3.settings.globalSeed = 99;
    s3.attach(p3);
    s3.run({});
    CHECK(b3.level().serialize() != b1.level().serialize());
}

TEST_CASE("generation: gameplay untouched and only verified decorations added") {
    auto lvl = makeTestLevel("G").level;
    auto before = canonicalSet(lvl.objects, [](auto&) { return true; });
    StringLevelBackend b(lvl);
    Studio studio(b);
    studio.attach(presetProject());
    auto out = studio.run({});
    CHECK(out.ok);
    CHECK(out.gameplayIntact);
    auto tags = tagSet(studio.project());
    auto after = canonicalSet(b.level().objects, [&](LevelObject const& o) { return !hasAnyTag(o, tags); });
    CHECK(before == after);
    auto& table = StaticObjectTable::get();
    int generated = 0;
    for (auto& o : b.level().objects) {
        if (!hasAnyTag(o, tags)) continue;
        ++generated;
        CHECK(table.isSafeDecoration(o.id));
        CHECK_EQ(o.groups.size(), size_t(1));  // only the layer tag
    }
    CHECK_EQ(generated, out.created);
    CHECK_EQ(int(studio.project().records.size()), out.created);
}

TEST_CASE("generation A: one platform -> continuous outline inside its edge, few fill objects") {
    auto lvl = makeTestLevel("A").level;
    StringLevelBackend b(lvl);
    Studio studio(b);
    studio.attach(presetProject("Minimal"));
    auto outlineId = layerIdOf(studio.project(), LayerKind::Outline);
    auto fillId = layerIdOf(studio.project(), LayerKind::BlockFill);
    GenerationRequest req;
    req.layers = {outlineId, fillId};
    auto out = studio.run(req);
    CHECK(out.ok);
    auto& a = studio.analysis();
    CHECK_EQ(a.structures.size(), size_t(1));
    Rect platform = a.structures[0].bounds;
    int outline = 0, fill = 0;
    for (auto& r : studio.project().records) {
        auto o = LevelObject::parse(r.objectString);
        auto def = DecoCatalog::builtin().find(o->id);
        CHECK(def != nullptr);
        if (r.layerId == outlineId) ++outline;
        if (r.layerId == fillId) ++fill;
        // Every piece stays within the platform (outline drawn inside the edge).
        CHECK(platform.expanded(0.6).contains(r.anchor));
    }
    CHECK_EQ(fill, 5);      // 60x4 cells split in 12x12 max rects -> 5 objects for 30 blocks
    CHECK_EQ(outline, 12);  // 5 + 5 pieces along the long faces, 1 per short side
}

TEST_CASE("generation C: concave corners get extended joints (no notch)") {
    auto lvl = makeTestLevel("C").level;
    StringLevelBackend b(lvl);
    Studio studio(b);
    studio.attach(presetProject("Minimal"));
    auto outlineId = layerIdOf(studio.project(), LayerKind::Outline);
    GenerationRequest req;
    req.layers = {outlineId};
    CHECK(studio.run(req).ok);
    auto& a = studio.analysis();
    double T = studio.project().layers.find(outlineId)->params.knob("thickness", 3);
    // For every concave corner, some outline object must cover the T x T square
    // diagonally inside the corner.
    for (auto& c : a.corners) {
        if (c.convex || c.onGround) continue;
        Vec2 diag{(c.quadrant + 2) % 4 == 0 || (c.quadrant + 2) % 4 == 3 ? 1.0 : -1.0,
                  (c.quadrant + 2) % 4 <= 1 ? 1.0 : -1.0};
        Vec2 probe = c.pos + Vec2{diag.x * T / 2, diag.y * T / 2};
        bool covered = false;
        for (auto& r : studio.project().records) {
            auto o = LevelObject::parse(r.objectString);
            auto def = DecoCatalog::builtin().find(o->id);
            OrientedRect box{o->pos + def->visCenter().rotatedCW(o->rotation),
                             def->visWidth() * o->scaleX / 2 + 0.01, def->visHeight() * o->scaleY / 2 + 0.01, o->rotation};
            if (box.contains(probe)) { covered = true; break; }
        }
        CHECK(covered);
    }
}

TEST_CASE("generation D: thin outlines continue under spikes (regression)") {
    auto lvl = makeTestLevel("D").level;
    StringLevelBackend b(lvl);
    Studio studio(b);
    studio.attach(presetProject("Minimal"));  // 2-unit outline
    auto outlineId = layerIdOf(studio.project(), LayerKind::Outline);
    GenerationRequest req;
    req.layers = {outlineId};
    CHECK(studio.run(req).ok);
    double T = studio.project().layers.find(outlineId)->params.knob("thickness", 3);
    int checked = 0;
    for (auto& o : lvl.objects) {
        if (o.id != 8 || o.rotation != 0 || std::abs(o.pos.y - 45) > 1) continue;  // spikes standing on the floor
        Vec2 probe{o.pos.x, 30 - T / 2};
        bool covered = false;
        for (auto r : studio.project().recordsOf(outlineId)) {
            auto lo = LevelObject::parse(r->objectString);
            auto def = DecoCatalog::builtin().find(lo->id);
            OrientedRect box{lo->pos + def->visCenter().rotatedCW(lo->rotation), def->visWidth() * lo->scaleX / 2 + 0.01,
                             def->visHeight() * lo->scaleY / 2 + 0.01, lo->rotation};
            if (box.contains(probe)) { covered = true; break; }
        }
        CHECK(covered);
        ++checked;
    }
    CHECK(checked >= 10);
}

TEST_CASE("generation E: tight wave section stays readable") {
    auto lvl = makeTestLevel("E").level;
    StringLevelBackend b(lvl);
    Studio studio(b);
    studio.attach(presetProject("Neon"));
    CHECK(studio.run({}).ok);
    auto& a = studio.analysis();
    auto* wave = a.sectionAt(30 * 30);
    CHECK(wave && wave->mode == GameMode::Wave);
    CHECK(wave->tightness > 0.5);
    // No artistic/foreground object and no highlight inside the corridor.
    for (auto& r : studio.project().records) {
        auto layer = studio.project().layers.find(r.layerId);
        if (r.anchor.x < 6 * 30 || r.anchor.x > 50 * 30) continue;
        CHECK(layer->kind != LayerKind::Foreground);
        CHECK(layer->kind != LayerKind::Highlights);
        CHECK(layer->kind != LayerKind::Atmospheric);
    }
}

TEST_CASE("generation H: regenerate one layer in a mask, everything else identical") {
    auto lvl = makeTestLevel("H").level;
    StringLevelBackend b(lvl);
    Studio studio(b);
    studio.attach(presetProject());
    CHECK(studio.run({}).ok);
    auto& p = studio.project();
    auto bgId = layerIdOf(p, LayerKind::BackgroundDetails);
    // Snapshot every layer.
    std::map<std::string, std::multiset<std::string>> before;
    for (auto& l : p.layers.all()) before[l.id] = objectsOfLayer(b, p, l.id);
    auto bgBefore = studio.project().recordsOf(bgId);
    std::multiset<std::string> outsideBefore;
    Rect zone{5000, -3000, 8000, 3000};
    for (auto r : bgBefore)
        if (!zone.contains(r->anchor)) outsideBefore.insert(LevelObject::parse(r->objectString)->canonical());

    Mask m;
    m.id = "zone";
    m.rect = zone;
    p.masks.push_back(m);
    p.layers.find(bgId)->params.density = 0.9;  // change the settings of that layer only
    p.layers.find(bgId)->params.seed = 4242;
    GenerationRequest req;
    req.mode = OperationMode::Regenerate;
    req.layers = {bgId};
    req.masks = {"zone"};
    auto out = studio.run(req);
    CHECK(out.ok);
    CHECK(out.gameplayIntact);
    for (auto& l : p.layers.all())
        if (l.id != bgId) CHECK(before[l.id] == objectsOfLayer(b, p, l.id));
    std::multiset<std::string> outsideAfter, insideAfter;
    for (auto r : studio.project().recordsOf(bgId)) {
        auto c = LevelObject::parse(r->objectString)->canonical();
        (zone.contains(r->anchor) ? insideAfter : outsideAfter).insert(c);
    }
    CHECK(outsideAfter == outsideBefore);
    CHECK(!insideAfter.empty());
}

TEST_CASE("generation I: a locked layer is never modified") {
    auto lvl = makeTestLevel("I").level;
    StringLevelBackend b(lvl);
    Studio studio(b);
    studio.attach(presetProject());
    CHECK(studio.run({}).ok);
    auto& p = studio.project();
    auto fillId = layerIdOf(p, LayerKind::BlockFill);
    auto fillBefore = objectsOfLayer(b, p, fillId);
    p.layers.find(fillId)->locked = true;
    p.layers.find(fillId)->params.style = "solid";  // would change everything if regenerated
    GenerationRequest req;
    req.mode = OperationMode::Regenerate;
    auto out = studio.run(req);
    CHECK(out.ok);
    CHECK(std::find(out.skippedLocked.begin(), out.skippedLocked.end(), fillId) != out.skippedLocked.end());
    CHECK(fillBefore == objectsOfLayer(b, p, fillId));
    // Explicitly targeting the locked layer does nothing either.
    GenerationRequest only;
    only.mode = OperationMode::Remove;
    only.layers = {fillId};
    studio.run(only);
    CHECK(fillBefore == objectsOfLayer(b, p, fillId));
    // Locking a group locks its children.
    auto outlineId = layerIdOf(p, LayerKind::Outline);
    p.layers.find(fillId)->locked = false;
    p.layers.find(p.layers.find(outlineId)->parentId)->locked = true;
    CHECK(p.layers.isLocked(outlineId));
}

TEST_CASE("generation J: save and reopen keeps layers, records and editing") {
    auto lvl = makeTestLevel("J").level;
    std::string savedLevel, savedProject;
    {
        StringLevelBackend b(lvl);
        Studio studio(b);
        studio.attach(presetProject());
        CHECK(studio.run({}).ok);
        savedLevel = b.level().serialize();
        savedProject = studio.project().toJson().dump();
    }
    StringLevelBackend b(Level::parse(savedLevel));
    Studio studio(b);
    auto pj = json::parse(savedProject);
    CHECK(pj.has_value());
    auto rep = studio.attach(Project::fromJson(*pj));
    CHECK(rep.linked > 300);
    CHECK_EQ(rep.modified, 0);
    CHECK_EQ(rep.missing, 0);
    CHECK_EQ(rep.recovered, 0);
    // Editing still works after reopening: regenerate the glow layer only.
    auto glowId = layerIdOf(studio.project(), LayerKind::StructuralGlow);
    auto countBefore = studio.project().objectCountByLayer()[glowId];
    GenerationRequest req;
    req.mode = OperationMode::Regenerate;
    req.layers = {glowId};
    auto out = studio.run(req);
    CHECK(out.ok);
    CHECK_EQ(out.removed, countBefore);
    // Without the project file, layer membership is recovered from the tag groups.
    StringLevelBackend b2(Level::parse(savedLevel));
    Studio fresh(b2);
    auto p = Project::fromJson(*pj);
    p.records.clear();
    auto rep2 = fresh.attach(p);
    CHECK(rep2.recovered > 300);
}

TEST_CASE("generation K: hand-edited objects are kept (Keep) or replaced (Replace)") {
    auto lvl = makeTestLevel("K").level;
    StringLevelBackend b(lvl);
    Studio studio(b);
    studio.attach(presetProject());
    CHECK(studio.run({}).ok);
    auto outlineId = layerIdOf(studio.project(), LayerKind::Outline);
    int tag = studio.project().layers.find(outlineId)->tagGroup;
    // The user moves one outline piece by hand in the editor.
    LevelObject* edited = nullptr;
    for (auto& o : b.level().objects)
        if (o.hasGroup(tag)) { edited = &o; break; }
    CHECK(edited != nullptr);
    edited->pos.x += 7;
    std::string editedCanon = edited->canonical();
    auto rep = studio.attach(studio.project());
    CHECK_EQ(rep.modified, 1);

    GenerationRequest req;
    req.mode = OperationMode::Regenerate;
    req.layers = {outlineId};
    auto out = studio.run(req);
    CHECK(out.ok);
    CHECK_EQ(out.keptModified, 1);
    bool stillThere = false;
    for (auto& o : b.level().objects)
        if (o.canonical() == editedCanon) stillThere = true;
    CHECK(stillThere);

    // Ask: nothing happens until the user answers.
    studio.project().settings.modifiedPolicy = ModifiedPolicy::Ask;
    auto asked = studio.run(req);
    CHECK(!asked.ok);
    CHECK_EQ(asked.needsConfirmation.size(), size_t(1));
    // Answer "replace".
    req.modifiedPolicy = ModifiedPolicy::Replace;
    auto replaced = studio.run(req);
    CHECK(replaced.ok);
    stillThere = false;
    for (auto& o : b.level().objects)
        if (o.canonical() == editedCanon) stillThere = true;
    CHECK(!stillThere);
}

TEST_CASE("generation L: removing everything restores the exact gameplay") {
    auto lvl = makeTestLevel("L").level;
    auto original = canonicalSet(lvl.objects, [](auto&) { return true; });
    StringLevelBackend b(lvl);
    Studio studio(b);
    studio.attach(presetProject("Cyberpunk"));
    CHECK(studio.run({}).ok);
    GenerationRequest req;
    req.mode = OperationMode::Remove;
    auto out = studio.run(req);
    CHECK(out.ok);
    CHECK_EQ(out.created, 0);
    auto after = canonicalSet(b.level().objects, [](auto&) { return true; });
    CHECK(after == original);
    CHECK(studio.project().records.empty());
}

TEST_CASE("generation: undo / redo restore exact states, preview cancel leaves no residue") {
    auto lvl = makeTestLevel("G").level;
    StringLevelBackend b(lvl);
    Studio studio(b);
    studio.attach(presetProject());
    auto s0 = canonicalSet(b.level().objects, [](auto&) { return true; });
    CHECK(studio.run({}).ok);
    auto s1 = canonicalSet(b.level().objects, [](auto&) { return true; });
    CHECK(studio.undo());
    CHECK(canonicalSet(b.level().objects, [](auto&) { return true; }) == s0);
    CHECK(studio.redo());
    CHECK(canonicalSet(b.level().objects, [](auto&) { return true; }) == s1);
    CHECK(studio.undo());

    auto pv = studio.preview({});
    CHECK(pv.ok);
    CHECK(pv.created > 0);
    CHECK(canonicalSet(b.level().objects, [](auto&) { return true; }) == s0);  // preview does not commit
    studio.cancelPreview();
    CHECK(canonicalSet(b.level().objects, [](auto&) { return true; }) == s0);
    studio.preview({});
    auto applied = studio.applyPreview();
    CHECK(applied.ok);
    CHECK(canonicalSet(b.level().objects, [](auto&) { return true; }) == s1);
}

TEST_CASE("generation: exclusion masks keep zones free of decoration") {
    auto lvl = makeTestLevel("G").level;
    StringLevelBackend b(lvl);
    Studio studio(b);
    auto p = presetProject();
    Mask ex;
    ex.id = "wave-ex";
    ex.mode = MaskMode::Exclude;
    ex.rect = {4020, -100, 5130, 400};
    p.masks.push_back(ex);
    studio.attach(p);
    CHECK(studio.run({}).ok);
    for (auto& r : studio.project().records) CHECK(!ex.rect.contains(r.anchor));
}

TEST_CASE("generation: changing intensity only recolors channels") {
    auto lvl = makeTestLevel("G").level;
    StringLevelBackend b(lvl);
    Studio studio(b);
    studio.attach(presetProject());
    CHECK(studio.run({}).ok);
    auto objectsBefore = b.level().serialize();
    auto glowId = layerIdOf(studio.project(), LayerKind::StructuralGlow);
    auto channels = studio.project().channels.channelsOf(glowId);
    CHECK(!channels.empty());
    auto opacityOf = [&](int ch) {
        for (auto& c : b.level().header.colors())
            if (c.id == ch) return c.opacity;
        return -1.0;
    };
    double before = opacityOf(channels[0]);
    studio.project().layers.find(glowId)->params.intensity = 0.5;
    studio.applyColors();
    double after = opacityOf(channels[0]);
    CHECK(before > 0);
    CHECK_NEAR(after, std::round(before * 0.5 * 100) / 100, 0.011);
    // objects themselves are untouched
    auto objs = Level::parse(objectsBefore).objects;
    CHECK(objs.size() == b.level().objects.size());
}

TEST_CASE("generation: every built-in preset works and produces a distinct style") {
    auto lvl = makeTestLevel("G").level;
    std::map<std::string, std::set<int>> idsPerPreset;
    for (auto& preset : PresetManager::builtins()) {
        StringLevelBackend b(lvl);
        Studio studio(b);
        Project p;
        PresetManager::apply(p, preset);
        studio.attach(p);
        auto out = studio.run({});
        CHECK(out.ok);
        CHECK(out.gameplayIntact);
        CHECK(out.created > 50);
        CHECK_EQ(out.result->validation.rejected, 0);
        for (auto& r : studio.project().records) idsPerPreset[preset.name].insert(LevelObject::parse(r.objectString)->id);
    }
    CHECK_EQ(idsPerPreset.size(), size_t(7));
    for (auto& [a, ia] : idsPerPreset)
        for (auto& [bname, ib] : idsPerPreset)
            if (a < bname) CHECK(ia != ib);
}

TEST_CASE("generation: preset application keeps layers that own objects attached") {
    auto lvl = makeTestLevel("G").level;
    StringLevelBackend b(lvl);
    Studio studio(b);
    studio.attach(presetProject());
    CHECK(studio.run({}).ok);
    auto counts = studio.project().objectCountByLayer();
    PresetManager::apply(studio.project(), *PresetManager::builtin("Industrial"));
    auto after = studio.project().objectCountByLayer();
    CHECK(counts == after);  // same layer ids still own their objects
    auto exported = PresetManager::capture(studio.project(), "Mine", "test").toJson().dump();
    auto parsed = Preset::fromJson(*json::parse(exported));
    CHECK(parsed.has_value());
    CHECK_EQ(parsed->name, std::string("Mine"));
}

namespace {
// A backend that corrupts the gameplay while creating objects.
class EvilBackend : public StringLevelBackend {
public:
    using StringLevelBackend::StringLevelBackend;
    bool armed = true;
    std::vector<std::string> createObjects(std::vector<LevelObject> const& objects) override {
        auto res = StringLevelBackend::createObjects(objects);
        if (armed) {
            for (auto& o : level().objects)
                if (o.id == 8) { o.pos.y += 30; break; }  // moves a spike
            armed = false;
        }
        return res;
    }
};
}  // namespace

TEST_CASE("generation: any gameplay difference rolls the operation back") {
    auto lvl = makeTestLevel("D").level;
    EvilBackend b(lvl);
    Studio studio(b);
    studio.attach(presetProject());
    auto beforeCount = b.level().objects.size();
    auto out = studio.run({});
    CHECK(!out.ok);
    CHECK(!out.gameplayIntact);
    CHECK(studio.project().records.empty());
    // All generated objects were removed again (the corrupted spike remains for the user to see).
    CHECK_EQ(b.level().objects.size(), beforeCount);
}
