#include "TestFramework.hpp"
#include "TestHelpers.hpp"

using namespace gdds;
using gddstest::LevelBuilder;

TEST_CASE("analysis A: a 30-block platform is ONE structure with a clean contour") {
    LevelBuilder b;
    b.row(10, 39, 3);  // floating platform, 30 blocks
    auto a = b.analyze();
    CHECK_EQ(a->structures.size(), size_t(1));
    auto& s = a->structures[0];
    CHECK_EQ(s.cellCount, 30 * 2 * 2);
    CHECK_EQ(s.objects.size(), size_t(30));
    CHECK(s.cls == StructureClass::Platform);
    CHECK_EQ(a->contours.size(), size_t(1));
    CHECK(!a->contours[0].hole);
    CHECK_EQ(a->segments.size(), size_t(4));
    CHECK_EQ(gddstest::countCorners(*a, true), 4);
    CHECK_EQ(gddstest::countCorners(*a, false), 0);
    double top = 0;
    for (auto& seg : a->segments)
        if (seg.face == Face::Top) {
            top = seg.length;
            CHECK_NEAR(seg.p0.y, 120, 1e-9);
        }
    CHECK_NEAR(top, 900, 1e-9);
    CHECK_NEAR(a->contours[0].perimeter, 2 * (900 + 30), 1e-9);
}

TEST_CASE("analysis A2: bottom face on the ground is hidden") {
    LevelBuilder b;
    b.row(0, 9, 0);
    auto a = b.analyze();
    int ground = 0;
    for (auto& seg : a->segments) if (seg.onGround) ++ground;
    CHECK_EQ(ground, 1);
    CHECK(a->structures[0].touchesGround);
}

TEST_CASE("analysis B: adjacent platforms merge, separated ones do not") {
    LevelBuilder b;
    b.row(0, 4, 2).row(5, 9, 2);    // touching -> one
    b.row(12, 15, 2);               // gap of 2 blocks -> separate
    b.block(16, 3);                 // touches (15,2) only diagonally -> separate
    auto a = b.analyze();
    CHECK_EQ(a->structures.size(), size_t(3));
}

TEST_CASE("analysis C: L shape has 5 convex corners and 1 concave corner") {
    LevelBuilder b;
    b.row(0, 5, 1);     // horizontal arm
    b.rect(0, 2, 1, 5); // vertical arm on the left
    auto a = b.analyze();
    CHECK_EQ(a->structures.size(), size_t(1));
    CHECK_EQ(a->segments.size(), size_t(6));
    CHECK_EQ(gddstest::countCorners(*a, true), 5);
    CHECK_EQ(gddstest::countCorners(*a, false), 1);
    for (auto& c : a->corners) {
        if (c.convex) continue;
        CHECK_NEAR(c.pos.x, 60, 1e-9);
        CHECK_NEAR(c.pos.y, 60, 1e-9);
        CHECK_EQ(c.quadrant, 0);  // the empty quadrant is up-right of the inner corner
    }
}

TEST_CASE("analysis: convex corner quadrants point at the solid") {
    LevelBuilder b;
    b.block(5, 5);
    auto a = b.analyze();
    CHECK_EQ(a->corners.size(), size_t(4));
    for (auto& c : a->corners) {
        Vec2 center{165, 165};
        bool right = c.pos.x > center.x, up = c.pos.y > center.y;
        // the solid lies toward the block center from each corner
        int expected = (!right && !up) ? 0 : (right && !up) ? 1 : (right && up) ? 2 : 3;
        CHECK_EQ(c.quadrant, expected);
    }
}

TEST_CASE("analysis: rings produce an outer contour and a hole") {
    LevelBuilder b;
    b.rect(0, 2, 6, 2).rect(0, 6, 6, 6).rect(0, 3, 0, 5).rect(6, 3, 6, 5);
    auto a = b.analyze();
    CHECK_EQ(a->structures.size(), size_t(1));
    CHECK_EQ(a->contours.size(), size_t(2));
    int holes = 0;
    for (auto& c : a->contours) if (c.hole) ++holes;
    CHECK_EQ(holes, 1);
    // the hole contour is a rectangle: 4 concave corners (seen from the solid)
    for (auto& c : a->contours)
        if (c.hole) {
            for (int cid : c.corners) CHECK(!a->corners[cid].convex);
        }
}

TEST_CASE("analysis: interior depth is a Chebyshev distance") {
    LevelBuilder b;
    b.rect(10, 5, 14, 9);  // 5x5 blocks = 10x10 cells, floating
    auto a = b.analyze();
    CHECK_EQ(a->structures[0].maxDepth, 5);
    CHECK_EQ(int(a->depth.get(LevelAnalysis::cellAt({301, 151}))), 1);  // corner cell
    CHECK_EQ(int(a->depth.get(LevelAnalysis::cellAt({375, 225}))), 5);  // center
}

TEST_CASE("analysis: staircase detection") {
    LevelBuilder b;
    for (int i = 0; i < 5; ++i) b.rect(10 + i, 0, 10 + i, i);  // ascending to the right
    auto a = b.analyze();
    CHECK(!a->staircases.empty());
    CHECK(a->staircases[0].steps >= 3);
    CHECK(a->staircases[0].ascendingRight);
}

TEST_CASE("analysis: free space and vertical clearance") {
    LevelBuilder b;
    b.row(0, 20, 0);   // floor
    b.row(0, 20, 3);   // ceiling, 2 blocks of gap (rows 1-2)
    auto a = b.analyze();
    double clearance = a->verticalClearance({300, 60});
    CHECK_NEAR(clearance, 60, 1e-9);
    CHECK(a->freeDistanceAt({300, 60}) <= 30);
    CHECK(a->freeDistanceAt({300, 600}) >= 300);
}

TEST_CASE("analysis D/E: hazards, clusters, sections and tightness") {
    LevelBuilder b;
    b.row(0, 60, 0);
    for (int x = 10; x < 16; ++x) b.spike(x, 1);      // one cluster of 6 spikes
    b.spike(30, 1);                                   // isolated spike
    b.portal(660, 40, 1);                             // wave portal
    b.row(40, 60, 3);                                 // tight wave corridor: ceiling at row 3
    auto a = b.analyze();
    CHECK_EQ(a->hazardClusters.size(), size_t(2));
    CHECK_EQ(a->hazardClusters[0].objects.size(), size_t(6));
    CHECK(a->hazardClusters[0].supportStructure >= 0);
    CHECK_EQ(a->sections.size(), size_t(2));
    CHECK(a->sections[0].mode == GameMode::Cube);
    CHECK(a->sections[1].mode == GameMode::Wave);
    CHECK(a->sections[1].tightness > a->sections[0].tightness);
    CHECK(a->sections[1].tightness > 0.5);
    // the halo around hazards is marked
    CHECK(a->flags.get(LevelAnalysis::cellAt({315, 65})) & cellflag::HazardHalo);
    CHECK(!(a->flags.get(LevelAnalysis::cellAt({315, 95})) & cellflag::HazardHalo));
}

TEST_CASE("analysis: moving, hidden and no-touch blocks are not decorated as structures") {
    LevelBuilder b;
    b.row(0, 9, 0);                     // static floor -> structure
    for (int x = 20; x <= 25; ++x) {    // moving platform (group 5, moved by a trigger)
        b.block(x, 4);
        b.level.objects.back().groups = {5};
    }
    b.object(901, 300, 300);            // Move trigger targeting group 5
    b.level.objects.back().setExtra(key::TargetGroup, "5");
    b.block(40, 2);                     // hidden block
    b.level.objects.back().setExtra(135, "1");
    b.block(50, 2);                     // no-touch block
    b.level.objects.back().setExtra(121, "1");
    auto a = b.analyze();
    CHECK_EQ(a->structures.size(), size_t(1));
    CHECK(a->flags.get(LevelAnalysis::cellAt({20 * 30 + 15, 4 * 30 + 15})) & cellflag::Special);
    CHECK(!(a->flags.get(LevelAnalysis::cellAt({50 * 30 + 15, 2 * 30 + 15})) & cellflag::Solid));
}

TEST_CASE("analysis: repeated structures share a shape signature") {
    LevelBuilder b;
    b.rect(0, 2, 2, 3).rect(10, 2, 12, 3).rect(20, 2, 22, 4);
    auto a = b.analyze();
    CHECK_EQ(a->structures.size(), size_t(3));
    CHECK_EQ(a->structures[0].shapeSignature, a->structures[1].shapeSignature);
    CHECK(a->structures[0].shapeSignature != a->structures[2].shapeSignature);
    CHECK_EQ(a->structures[0].repeatCount, 2);
    CHECK(a->structures[0].signature != a->structures[1].signature);
}
