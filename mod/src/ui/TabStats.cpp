// Stats tab: analysis, generation and project statistics.

#include "StudioPopup.hpp"

using namespace geode::prelude;

void tabs::buildStats(StudioPopup* popup, ui::Column& col) {
    auto s = popup->session();
    col.buttons({{"Re-analyse the level", [popup, s] {
                     s->studio().invalidateAnalysis();
                     s->studio().analysis();
                     popup->refresh();
                 }}},
                0.4f);
    auto st = s->studio().stats();
    auto& a = s->studio().analysis();
    col.title("Level analysis");
    col.text(fmt::format("Gameplay objects analysed: {}   (solids {}, hazards {}, unknown {})", a.stats.gameplayObjects,
                         a.stats.solidObjects, a.stats.hazardObjects, a.stats.unknownObjects), 0.3f, ui::kText);
    col.text(fmt::format("Structures: {}   contours: {}   corners: {}   staircases: {}   hazard clusters: {}", a.stats.structures,
                         a.stats.contours, a.stats.corners, a.stats.staircases, a.stats.hazardClusters), 0.3f, ui::kText);
    col.text(fmt::format("Analysis time: {:.1f} ms", a.stats.analysisMs), 0.3f, ui::kText);
    col.title("Sections");
    for (size_t i = 0; i < a.sections.size(); ++i) {
        auto& sec = a.sections[i];
        col.text(fmt::format("#{}  X {:.0f}..{:.0f}  {}  speed {}  tightness {:.2f}  objects {}", i, sec.x0, sec.x1,
                             gdds::gameModeName(sec.mode), sec.speed, sec.tightness, st.perSection[int(i)]),
                 0.28f);
    }
    col.title("Generated decoration");
    col.text(fmt::format("Objects: {}   last generation: {:.1f} ms   estimated complexity: {:.0f}", st.generatedObjects,
                         st.generationMs, st.complexity), 0.3f, ui::kText);
    for (auto& [lid, n] : st.perLayer) {
        auto l = s->project().layers.find(lid);
        col.text(fmt::format("  {}: {}", l ? l->name : lid, n), 0.28f);
    }
    auto& rep = s->lastReconcile();
    col.title("Project");
    col.text(fmt::format("File: {}", s->projectPath().filename().string()), 0.28f);
    col.text(fmt::format("At load: {} linked, {} modified by hand, {} deleted by hand, {} recovered from tag groups",
                         rep.linked, rep.modified, rep.missing, rep.recovered), 0.28f);
    col.text(fmt::format("Color channels used by the studio: {}   operations logged: {}",
                         s->project().channels.allocations().size(), s->project().operations.size()), 0.28f);
}
