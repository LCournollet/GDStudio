#include <gdds/Composition.hpp>
#include <gdds/Random.hpp>

#include <algorithm>

namespace gdds {

double speedBlocksPerSecond(int speedIndex) {
    switch (speedIndex) {
        case 1: return 8.37;   // 0.5x
        case 2: return 12.96;  // 2x
        case 3: return 15.6;   // 3x
        case 4: return 19.27;  // 4x
        default: return 10.39; // 1x
    }
}

namespace {
constexpr int kLayerRanks[] = {zlayer::B5, zlayer::B4, zlayer::B3, zlayer::B2, zlayer::B1, zlayer::T1, zlayer::T2, zlayer::T3, zlayer::T4};
}

int zRank(int zLayer, int zOrder) {
    return zlayer::rank(zLayer) * 20000 + std::clamp(zOrder, -9999, 9999) + 10000;
}

ZSlot zFromRank(int rank) {
    int r = std::clamp(rank / 20000, 0, 8);
    int order = rank - r * 20000 - 10000;
    return {kLayerRanks[r], std::clamp(order, -9999, 9999)};
}

CompositionPlan CompositionPlan::build(LevelAnalysis const& analysis, uint64_t seed, double globalDensity) {
    CompositionPlan plan;
    int index = 0;
    GameMode prevMode = GameMode::Cube;
    int prevSpeed = -1;
    for (auto& sec : analysis.sections) {
        SectionPlan sp;
        sp.x0 = sec.x0;
        sp.x1 = sec.x1;
        sp.index = index;
        sp.mode = sec.mode;
        sp.readability = sec.tightness;
        // Tight sections keep their structural design but lose most of the
        // optional decoration; relaxed ones get the full budget.
        sp.density = globalDensity * (1.0 - 0.65 * sec.tightness);
        sp.detailBudget = globalDensity * (1.0 - 0.85 * sec.tightness);
        // One beat every ~0.55 s of travel: faster sections get longer periods
        // so the perceived rhythm stays constant.
        double bps = speedBlocksPerSecond(sec.speed);
        sp.rhythm = std::clamp(std::round(bps * 0.55 * 2) / 2, 4.0, 12.0) * kBlock;
        // Motif variants follow the game mode so each gameplay part gets its
        // own flavour while staying in the same family.
        sp.motifVariant = int(hashOf(seed, int(sec.mode), sec.speed) % 3);
        sp.transition = index > 0 && (sec.mode != prevMode || sec.speed != prevSpeed);
        prevMode = sec.mode;
        prevSpeed = sec.speed;
        plan.m_sections.push_back(sp);
        ++index;
    }
    if (plan.m_sections.empty()) {
        SectionPlan sp;
        sp.x0 = -1e9;
        sp.x1 = 1e9;
        sp.density = globalDensity;
        sp.detailBudget = globalDensity;
        sp.rhythm = 6 * kBlock;
        plan.m_sections.push_back(sp);
    }
    plan.m_default = plan.m_sections.front();
    return plan;
}

SectionPlan const& CompositionPlan::at(double x) const {
    for (auto& s : m_sections)
        if (x >= s.x0 && x < s.x1) return s;
    if (!m_sections.empty()) return x < m_sections.front().x0 ? m_sections.front() : m_sections.back();
    return m_default;
}

}  // namespace gdds
