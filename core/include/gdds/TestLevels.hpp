#pragma once

// Gameplay-only test levels (A..L) used by the unit tests, the CLI and the
// offline renderer. They contain only native GD gameplay objects.

#include "Level.hpp"

#include <string>
#include <vector>

namespace gdds {

struct TestLevel {
    std::string id;           // "A".."L"
    std::string name;
    std::string description;
    Level level;
};

std::vector<std::string> testLevelIds();
TestLevel makeTestLevel(std::string const& id);

// Small builder on the block grid: block (bx, by) is centered at
// (bx*30+15, by*30+15); by = 0 is the first row above the ground.
class LevelSketch {
public:
    LevelSketch();
    LevelSketch& object(int id, double x, double y, double rotation = 0, bool flipX = false, bool flipY = false);
    LevelSketch& block(int bx, int by, int id = 1);
    LevelSketch& rect(int bx0, int by0, int bx1, int by1, int id = 1);
    LevelSketch& hole(int bx0, int by0, int bx1, int by1);  // removes blocks
    LevelSketch& spike(int bx, int by, bool down = false);
    LevelSketch& spikes(int bx0, int bx1, int by, bool down = false);
    LevelSketch& saw(double bx, double by);
    LevelSketch& slope(int bx, int by, bool flipX = false);
    LevelSketch& portal(int id, double bx, double by);
    LevelSketch& orb(double bx, double by);
    Level build() const;

private:
    Level m_level;
};

}  // namespace gdds
