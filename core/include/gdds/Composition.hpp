#pragma once

// Composition planning: decisions shared by every engine BEFORE any object is
// placed, so that structural, lighting and artistic layers follow the same
// rhythm, the same density budget and the same visual hierarchy.

#include "Analysis.hpp"

#include <vector>

namespace gdds {

struct SectionPlan {
    double x0 = 0, x1 = 0;
    int index = 0;
    GameMode mode = GameMode::Cube;
    double density = 1;        // multiplier applied to every layer's density
    double detailBudget = 1;   // multiplier for micro details
    double readability = 0;    // 0 = relaxed, 1 = keep extremely clean (tight wave)
    double rhythm = 180;       // base period (units) of repeated elements
    int motifVariant = 0;      // variant of the artistic motif family
    bool transition = false;   // section starts with a game mode / speed change
};

class CompositionPlan {
public:
    static CompositionPlan build(LevelAnalysis const& analysis, uint64_t seed, double globalDensity);

    SectionPlan const& at(double x) const;
    std::vector<SectionPlan> const& sections() const { return m_sections; }
    // Phase-aligned rhythm positions: multiples of the section rhythm,
    // anchored to the section start so repeated elements line up.
    double rhythmAt(double x) const { return at(x).rhythm; }

private:
    std::vector<SectionPlan> m_sections;
    SectionPlan m_default;
};

// Blocks per second for the 5 GD speeds (index as in the level header).
double speedBlocksPerSecond(int speedIndex);

// Draw order helpers: a "rank" combines z layer and z order.
struct ZSlot {
    int zLayer = 0;
    int zOrder = 0;
};
int zRank(int zLayer, int zOrder);
ZSlot zFromRank(int rank);

}  // namespace gdds
