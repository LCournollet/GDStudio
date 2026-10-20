#pragma once

#include <gdds/Analysis.hpp>
#include <gdds/Level.hpp>

#include <string>
#include <vector>

namespace gddstest {

// Builds levels on the block grid: block (bx, by) sits at (bx*30+15, by*30+15);
// by = 0 is the first row above the ground.
struct LevelBuilder {
    gdds::Level level;

    LevelBuilder() { level.header = gdds::defaultHeader(); }

    LevelBuilder& object(int id, double x, double y, double rot = 0) {
        gdds::LevelObject o;
        o.id = id;
        o.pos = {x, y};
        o.rotation = rot;
        level.objects.push_back(o);
        return *this;
    }
    LevelBuilder& block(int bx, int by, int id = 1) { return object(id, bx * 30 + 15, by * 30 + 15); }
    LevelBuilder& row(int bx0, int bx1, int by, int id = 1) {
        for (int x = bx0; x <= bx1; ++x) block(x, by, id);
        return *this;
    }
    LevelBuilder& rect(int bx0, int by0, int bx1, int by1, int id = 1) {
        for (int y = by0; y <= by1; ++y) row(bx0, bx1, y, id);
        return *this;
    }
    LevelBuilder& spike(int bx, int by) { return object(8, bx * 30 + 15, by * 30 + 15); }
    LevelBuilder& portal(int id, int bx, int by) { return object(id, bx * 30 + 15, by * 30 + 15); }

    std::unique_ptr<gdds::LevelAnalysis> analyze() const {
        gdds::Analyzer an;
        return an.analyze(gdds::Analyzer::inputsFromLevel(level), &level.header);
    }
};

inline int countCorners(gdds::LevelAnalysis const& a, bool convex) {
    int n = 0;
    for (auto& c : a.corners) if (c.convex == convex) ++n;
    return n;
}

}  // namespace gddstest
