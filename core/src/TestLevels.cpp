#include <gdds/TestLevels.hpp>

#include <algorithm>
#include <cmath>

namespace gdds {

LevelSketch::LevelSketch() { m_level.header = defaultHeader(); }

LevelSketch& LevelSketch::object(int id, double x, double y, double rotation, bool flipX, bool flipY) {
    LevelObject o;
    o.id = id;
    o.pos = {x, y};
    o.rotation = rotation;
    o.flipX = flipX;
    o.flipY = flipY;
    m_level.objects.push_back(o);
    return *this;
}

LevelSketch& LevelSketch::block(int bx, int by, int id) { return object(id, bx * 30 + 15, by * 30 + 15); }

LevelSketch& LevelSketch::rect(int bx0, int by0, int bx1, int by1, int id) {
    for (int y = by0; y <= by1; ++y)
        for (int x = bx0; x <= bx1; ++x) block(x, y, id);
    return *this;
}

LevelSketch& LevelSketch::hole(int bx0, int by0, int bx1, int by1) {
    auto& objs = m_level.objects;
    objs.erase(std::remove_if(objs.begin(), objs.end(),
                              [&](LevelObject const& o) {
                                  int bx = int(std::floor(o.pos.x / 30)), by = int(std::floor(o.pos.y / 30));
                                  return o.id == 1 && bx >= bx0 && bx <= bx1 && by >= by0 && by <= by1;
                              }),
               objs.end());
    return *this;
}

LevelSketch& LevelSketch::spike(int bx, int by, bool down) {
    return object(8, bx * 30 + 15, by * 30 + 15, down ? 180 : 0);
}

LevelSketch& LevelSketch::spikes(int bx0, int bx1, int by, bool down) {
    for (int x = bx0; x <= bx1; ++x) spike(x, by, down);
    return *this;
}

LevelSketch& LevelSketch::saw(double bx, double by) { return object(88, bx * 30 + 15, by * 30 + 15); }

LevelSketch& LevelSketch::slope(int bx, int by, bool flipX) { return object(289, bx * 30 + 15, by * 30 + 15, 0, flipX); }

LevelSketch& LevelSketch::portal(int id, double bx, double by) { return object(id, bx * 30 + 15, by * 30 + 15); }

LevelSketch& LevelSketch::orb(double bx, double by) { return object(36, bx * 30 + 15, by * 30 + 15); }

Level LevelSketch::build() const { return m_level; }

std::vector<std::string> testLevelIds() { return {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L"}; }

namespace {

Level composite() {
    LevelSketch s;
    // --- Cube part: stepped platforms, a staircase and spikes -------------
    s.rect(0, 0, 13, 0);
    s.spikes(9, 10, 1);
    s.rect(14, 0, 22, 2);                 // raised mass
    s.rect(23, 0, 27, 1);
    for (int i = 0; i < 5; ++i) s.rect(30 + i, 0, 30 + i, i);  // staircase
    s.rect(35, 0, 40, 4);
    s.spikes(41, 44, 0);                  // pit of spikes
    s.rect(45, 0, 60, 1);
    s.rect(49, 5, 56, 5);                 // floating platform
    s.spike(52, 6);
    s.rect(62, 0, 66, 3).hole(63, 2, 65, 3);  // U shape (inner corners)
    s.rect(70, 0, 80, 0);
    s.saw(75, 3);
    // --- Ship part: open space with floating obstacles ---------------------
    s.portal(13, 82, 3);
    for (int i = 0; i < 5; ++i) {
        int x = 88 + i * 9;
        int h = (i % 2) ? 6 : 2;
        s.rect(x, h, x + 2, h + 1);
        s.spikes(x, x + 2, h + 2);
    }
    s.rect(84, 11, 130, 11);              // ceiling
    // --- Wave part: tight corridor ------------------------------------------
    s.portal(660, 132, 4);
    for (int x = 134; x <= 170; ++x) {
        int wave = int(std::round(2 * std::sin((x - 134) / 4.0)));
        int floorTop = 2 + wave;
        int ceilBottom = floorTop + 4;
        s.rect(x, 0, x, floorTop);
        s.rect(x, ceilBottom, x, 11);
    }
    // --- Back to cube: open area for the artistic engine ---------------------
    s.portal(12, 172, 2);
    s.rect(172, 0, 230, 0);
    s.rect(180, 1, 184, 1);
    s.rect(195, 3, 200, 3);
    s.orb(205, 4);
    s.rect(212, 1, 220, 2);
    s.spikes(214, 216, 3);
    return s.build();
}

}  // namespace

TestLevel makeTestLevel(std::string const& id) {
    TestLevel t;
    t.id = id;
    if (id == "A") {
        t.name = "A - Simple platform";
        t.description = "One 30-block platform: must be decorated as one continuous structure.";
        t.level = LevelSketch().rect(10, 2, 39, 3).build();
    }
    else if (id == "B") {
        t.name = "B - Connected platforms";
        t.description = "Several touching platforms of different heights and a raised platform on a pillar.";
        t.level = LevelSketch().rect(5, 0, 15, 1).rect(16, 0, 25, 3).rect(26, 0, 40, 2).rect(50, 0, 51, 4).rect(45, 5, 56, 6).build();
    }
    else if (id == "C") {
        t.name = "C - Inner and outer corners";
        t.description = "A U shape, a cross and a closed ring (outer contour + cavity).";
        t.level = LevelSketch()
                      .rect(5, 0, 15, 6).hole(7, 2, 13, 6)       // U
                      .rect(22, 2, 30, 4).rect(25, 0, 27, 7)     // cross
                      .rect(36, 1, 46, 7).hole(38, 3, 44, 5)     // ring
                      .build();
    }
    else if (id == "D") {
        t.name = "D - Many spikes";
        t.description = "Spike rows on the floor and on platforms, hanging spikes and sawblades.";
        LevelSketch s;
        s.rect(0, 0, 60, 0);
        for (int i = 0; i < 6; ++i) s.spikes(4 + i * 9, 5 + i * 9, 1);
        s.rect(20, 4, 30, 4).spikes(22, 24, 5).spikes(26, 28, 3, true);
        s.saw(40, 4).saw(48, 5);
        t.level = s.build();
    }
    else if (id == "E") {
        t.name = "E - Narrow wave passage";
        t.description = "Wave portal then a zigzag corridor 4 blocks high with spikes on the walls.";
        LevelSketch s;
        s.portal(660, 3, 4);
        for (int x = 6; x <= 50; ++x) {
            int wave = int(std::round(2 * std::sin((x - 6) / 3.5)));
            int floorTop = 2 + wave;
            s.rect(x, 0, x, floorTop);
            s.rect(x, floorTop + 5, x, 12);
        }
        s.spike(20, 2 + int(std::round(2 * std::sin(14 / 3.5))) + 1);
        t.level = s.build();
    }
    else if (id == "F") {
        t.name = "F - Large open area";
        t.description = "Sparse low platforms under a lot of free space (artistic engine).";
        LevelSketch s;
        s.rect(0, 0, 80, 0);
        for (int i = 0; i < 6; ++i) s.rect(6 + i * 12, 1, 9 + i * 12, 1 + (i % 3));
        t.level = s.build();
    }
    else {
        static char const* names[] = {"G - Multiple layers and palettes", "H - Regeneration limited to a mask",
                                      "I - Locked layer during regeneration", "J - Save and reopen",
                                      "K - Manual edit then regeneration", "L - Full removal"};
        static char const* descs[] = {
            "Composite level (cube, staircase, ship, tight wave, open area) decorated with every layer.",
            "Regenerate Background Details only between X 5000 and X 8000; everything else must stay identical.",
            "Lock Base Block Design, regenerate the other layers: the locked layer must not change.",
            "Save the project and the level, reload both: layers and objects must be reconciled.",
            "Move a generated object by hand, regenerate its layer: the edited object is kept (Keep policy).",
            "Remove every generated object: the level must be byte-identical to the original gameplay."};
        int idx = std::clamp(int(id.empty() ? 'G' : id[0]) - 'G', 0, 5);
        t.name = names[idx];
        t.description = descs[idx];
        t.level = composite();
    }
    return t;
}

}  // namespace gdds
