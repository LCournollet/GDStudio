#include "TestFramework.hpp"

#include <gdds/Json.hpp>
#include <gdds/Layer.hpp>
#include <gdds/Level.hpp>
#include <gdds/LevelObject.hpp>
#include <gdds/Mask.hpp>
#include <gdds/Palette.hpp>
#include <gdds/Random.hpp>

using namespace gdds;

TEST_CASE("json: parse and dump round trip") {
    auto v = json::parse(R"({"a": 1, "b": [true, null, "x\"y"], "c": {"d": -2.5e1}, "u": "é"})");
    CHECK(v.has_value());
    CHECK_EQ(v->integer("a"), 1);
    CHECK(v->get("b").items()[0].asBool());
    CHECK(v->get("b").items()[1].isNull());
    CHECK_EQ(v->get("b").items()[2].asString(), std::string("x\"y"));
    CHECK_NEAR(v->get("c").num("d"), -25.0, 1e-9);
    CHECK_EQ(v->str("u"), std::string("\xc3\xa9"));
    auto again = json::parse(v->dump());
    CHECK(again.has_value());
    CHECK_EQ(again->dump(0), v->dump(0));
    json::ParseError err;
    CHECK(!json::parse("{\"a\": }", &err).has_value());
    CHECK(!json::parse("[1, 2", &err).has_value());
}

TEST_CASE("json: copy on write") {
    json::Value a = json::Value::object();
    a["x"] = 1;
    json::Value b = a;
    b["x"] = 2;
    CHECK_EQ(a.integer("x"), 1);
    CHECK_EQ(b.integer("x"), 2);
}

TEST_CASE("object: parse / serialize round trip keeps unknown keys") {
    auto o = LevelObject::parse("1,211,2,45,3,105.5,6,90,21,998,24,5,25,12,57,4.9999,128,2.5,129,0.5,43,10a0.5a1a1a0,41,1,155,3,108,77");
    CHECK(o.has_value());
    CHECK_EQ(o->id, 211);
    CHECK_NEAR(o->pos.x, 45, 1e-9);
    CHECK_NEAR(o->pos.y, 105.5, 1e-9);
    CHECK_NEAR(o->rotation, 90, 1e-9);
    CHECK_EQ(o->mainColor, 998);
    CHECK_EQ(o->zLayer, 5);
    CHECK_EQ(o->zOrder, 12);
    CHECK(o->hasGroup(4));
    CHECK(o->hasGroup(9999));
    CHECK_NEAR(o->scaleX, 2.5, 1e-9);
    CHECK_NEAR(o->scaleY, 0.5, 1e-9);
    CHECK(o->mainHSV.has_value());
    CHECK_NEAR(o->mainHSV->h, 10, 1e-9);
    CHECK(o->mainHSV->sAdd);
    CHECK_EQ(o->extraInt(155), 3);
    CHECK_EQ(o->extraInt(108), 77);
    auto again = LevelObject::parse(o->serialize());
    CHECK(again.has_value());
    CHECK_EQ(again->canonical(), o->canonical());
    CHECK_EQ(again->fingerprint(), o->fingerprint());
}

TEST_CASE("object: canonical form ignores key order and number formatting") {
    auto a = LevelObject::parse("1,1,2,15,3,15,57,3.1");
    auto b = LevelObject::parse("3,15.000,57,1.3,2,15,1,1");
    CHECK(a && b);
    CHECK_EQ(a->canonical(), b->canonical());
    CHECK(a->fingerprint() != LevelObject::parse("1,1,2,15,3,16")->fingerprint());
    // Ignoring groups (studio tag groups) must make them equal again.
    auto c = LevelObject::parse("1,1,2,15,3,15");
    CHECK(c->fingerprint(true) == a->fingerprint(true));
}

TEST_CASE("object: the volatile key 155 is kept but not part of the fingerprint") {
    // Strings as GD 2.2091 wrote them for the same object, at creation and after reload.
    auto created = LevelObject::parse("1,1764,2,356.25,3,51,21,996,57,9995,128,0.563,129,0.563,155,34");
    auto reloaded = LevelObject::parse("1,1764,2,356.25,3,51,57,9995,155,7,21,996,128,0.563,129,0.563");
    CHECK(created && reloaded);
    CHECK_EQ(created->fingerprint(), reloaded->fingerprint());
    CHECK(created->serialize().find("155,34") != std::string::npos);  // preserved verbatim
    CHECK(created->fingerprint() != LevelObject::parse("1,1764,2,356.25,3,52,21,996,57,9995,155,34")->fingerprint());
}

TEST_CASE("object: legacy uniform scale is mapped to X/Y") {
    auto o = LevelObject::parse("1,1,2,0,3,0,32,1.5");
    CHECK_NEAR(o->scaleX, 1.5, 1e-9);
    CHECK_NEAR(o->scaleY, 1.5, 1e-9);
}

TEST_CASE("level: header, colors and objects") {
    Level lvl;
    lvl.header = defaultHeader();
    auto cols = lvl.header.colors();
    CHECK(cols.size() >= 4);
    ColorChannel extra;
    extra.id = 998;
    extra.r = 10; extra.g = 20; extra.b = 30;
    extra.opacity = 0.5;
    extra.blending = true;
    cols.push_back(extra);
    lvl.header.setColors(cols);
    lvl.objects.push_back(*LevelObject::parse("1,1,2,15,3,15"));
    auto parsed = Level::parse(lvl.serialize());
    CHECK_EQ(parsed.objects.size(), size_t(1));
    auto pc = parsed.header.colors();
    bool found = false;
    for (auto& c : pc)
        if (c.id == 998) {
            found = true;
            CHECK_EQ(c.g, 20);
            CHECK(c.blending);
            CHECK_NEAR(c.opacity, 0.5, 1e-9);
        }
    CHECK(found);
}

TEST_CASE("random: deterministic and order independent") {
    Rng a(hashOf(42, 7, 3)), b(hashOf(42, 7, 3)), c(hashOf(42, 7, 4));
    for (int i = 0; i < 100; ++i) CHECK_EQ(a.next(), b.next());
    CHECK(Rng(hashOf(42, 7, 3)).next() != c.next());
    Rng w(5);
    int counts[3] = {0, 0, 0};
    for (int i = 0; i < 3000; ++i) counts[w.weighted({1, 0, 3})]++;
    CHECK_EQ(counts[1], 0);
    CHECK(counts[2] > counts[0] * 2);
}

TEST_CASE("palette: hex, hsv, contrast and adjustments") {
    auto c = Color3::fromHex("#FF5AC8");
    CHECK(c.has_value());
    CHECK_EQ(c->toHex(), std::string("#FF5AC8"));
    double h, s, v;
    c->toHSV(h, s, v);
    auto back = Color3::fromHSV(h, s, v);
    CHECK(std::abs(back.r - c->r) <= 1 && std::abs(back.g - c->g) <= 1 && std::abs(back.b - c->b) <= 1);
    CHECK(contrastRatio({0, 0, 0}, {255, 255, 255}) > 20);
    Palette p = Palette::defaultPalette();
    PaletteOverride ov;
    ColorAdjust adj;
    adj.glowIntensity = 0.5;
    ov.adjust = adj;
    auto glow = resolveRole(p, ov, ColorRole::Glow);
    CHECK_NEAR(glow.opacity, p.role(ColorRole::Glow).opacity * 0.5, 1e-9);
    auto prim = resolveRole(p, ov, ColorRole::Primary);
    CHECK_NEAR(prim.opacity, 1.0, 1e-9);  // glow intensity only affects blending roles
    auto j = Palette::fromJson(p.toJson());
    CHECK_EQ(j.role(ColorRole::Secondary).color.toHex(), p.role(ColorRole::Secondary).color.toHex());
}

TEST_CASE("palette: channel allocation avoids level channels and is stable") {
    ChannelAllocator alloc;
    alloc.setUsedByLevel({999, 998, 5});
    int a = alloc.channelFor("L1", ColorRole::Glow);
    int b = alloc.channelFor("L1", ColorRole::Primary);
    int a2 = alloc.channelFor("L1", ColorRole::Glow);
    CHECK_EQ(a, 997);
    CHECK_EQ(a2, a);
    CHECK(b != a && b != 999 && b != 998);
    // The level starts using our channel: we must move away from it.
    alloc.setUsedByLevel({999, 998, 5, 997});
    int a3 = alloc.channelFor("L1", ColorRole::Glow);
    CHECK(a3 != 997);
    alloc.releaseLayer("L1");
    CHECK(alloc.channelsOf("L1").empty());
}

TEST_CASE("layers: tree operations") {
    LayerStack st;
    Layer g; g.name = "Structural"; g.kind = LayerKind::Group;
    auto gid = st.add(g).id;
    Layer a; a.name = "Outlines"; a.kind = LayerKind::Outline; a.parentId = gid;
    auto aid = st.add(a).id;
    Layer b; b.name = "Corners"; b.kind = LayerKind::Corners; b.parentId = gid;
    auto bid = st.add(b).id;
    Layer c; c.name = "Background"; c.kind = LayerKind::BackgroundStructures;
    auto cid = st.add(c).id;

    CHECK_EQ(st.childrenOf(gid).size(), size_t(2));
    CHECK_EQ(st.subtree(gid).size(), size_t(3));
    CHECK(st.move(bid, -1));
    CHECK_EQ(st.childrenOf(gid)[0], bid);
    CHECK(!st.reparent(gid, aid));  // cycle refused
    st.find(gid)->locked = true;
    CHECK(st.isLocked(aid));
    CHECK(!st.isLocked(cid));
    auto dup = st.duplicate(gid);
    CHECK(!dup.empty());
    CHECK_EQ(st.childrenOf(dup).size(), size_t(2));
    CHECK_EQ(st.all().size(), size_t(7));
    auto removed = st.remove(gid);
    CHECK_EQ(removed.size(), size_t(3));
    CHECK_EQ(st.all().size(), size_t(4));
    LayerStack copy;
    copy.loadJson(st.toJson());
    CHECK_EQ(copy.all().size(), st.all().size());
    CHECK(copy.displayOrder() == st.displayOrder());
}

TEST_CASE("masks: shapes and combination") {
    Mask r; r.id = "r"; r.shape = MaskShape::Rectangle; r.rect = {5000, -1000, 8000, 1000};
    Mask ex; ex.id = "ex"; ex.shape = MaskShape::Rectangle; ex.mode = MaskMode::Exclude; ex.rect = {6000, 0, 6500, 300};
    Mask poly; poly.id = "p"; poly.shape = MaskShape::Polygon; poly.polygon.points = {{0, 0}, {100, 0}, {50, 100}};
    Mask brush; brush.id = "b"; brush.shape = MaskShape::Brush; brush.strokes = {{{0, 0}, 30}, {{60, 0}, 30}};
    Mask sec; sec.id = "s"; sec.shape = MaskShape::Section; sec.sectionX0 = 100; sec.sectionX1 = 200;

    CHECK(poly.contains({50, 40}));
    CHECK(!poly.contains({5, 90}));
    CHECK(brush.contains({25, 5}));
    CHECK(!brush.contains({30, 10}));  // 31.6 units from both centers
    CHECK(!brush.contains({30, 40}));
    CHECK(sec.contains({150, 99999}));

    std::vector<Mask> masks{r, ex};
    auto set = MaskSet::build(masks, "any-layer", {}, nullptr);
    CHECK(set.allows({5500, 100}));
    CHECK(!set.allows({6200, 100}));   // excluded
    CHECK(!set.allows({4000, 100}));   // outside include
    // Layer-specific mask does not affect other layers.
    masks[1].layers = {"glow"};
    auto set2 = MaskSet::build(masks, "outline", {}, nullptr);
    CHECK(set2.allows({6200, 100}));
    // Operation masks are mandatory includes.
    auto set3 = MaskSet::build({r, poly}, "x", {"p"}, nullptr);
    CHECK(!set3.allows({5500, 100}));
    auto rt = Mask::fromJson(poly.toJson());
    CHECK(rt.contains({50, 40}));
    Mask moved = r;
    moved.translate({100, 0});
    CHECK(moved.contains({8050, 0}));
}
