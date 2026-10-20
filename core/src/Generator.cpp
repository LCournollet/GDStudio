#include <gdds/Generator.hpp>
#include <gdds/ObjectInfo.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <unordered_map>

namespace gdds {

namespace {

bool engineEnabled(EngineKind e, GenerationSettings const& s) {
    switch (e) {
        case EngineKind::Structural: return s.structural;
        case EngineKind::Lighting: return s.lighting;
        case EngineKind::Artistic: return s.artistic;
        default: return false;
    }
}

// Is the layer drawn above the hazards (so it must never cover them)?
bool drawnAboveHazards(Placement const& p, LevelAnalysis const& a) {
    return zRank(p.object.zLayer, p.object.zOrder) >= a.hazardBottomRank;
}

bool footprintTouchesHazard(Rect const& r, LevelAnalysis const& a, double shrink = 1.0) {
    // Shrink each axis independently: a thin line keeps its length.
    Rect s = r;
    if (s.width() > 2 * shrink + 0.01) { s.x0 += shrink; s.x1 -= shrink; }
    if (s.height() > 2 * shrink + 0.01) { s.y0 += shrink; s.y1 -= shrink; }
    // Upper bounds are exclusive: a footprint ending exactly on a cell border
    // does not touch the next cell.
    Cell c0 = LevelAnalysis::cellAt({s.x0, s.y0});
    Cell c1 = LevelAnalysis::cellAt({std::max(s.x0, s.x1 - 1e-6), std::max(s.y0, s.y1 - 1e-6)});
    if (int64_t(c1.x - c0.x + 1) * (c1.y - c0.y + 1) > 200000) return true;  // absurd footprint: refuse
    for (int y = c0.y; y <= c1.y; ++y)
        for (int x = c0.x; x <= c1.x; ++x)
            if (a.isHazard({x, y})) return true;
    return false;
}

// Per-object rules of the layer that are independent of the engine logic:
// probability of appearance, scale range and minimum spacing. Deterministic
// (decisions derive from the placement key).
void applyObjectRules(std::vector<Placement>& placements, Layer const& layer, DecoCatalog const& catalog) {
    if (layer.objects.empty()) return;
    std::unordered_map<int, ObjectRule const*> rules;
    for (auto& r : layer.objects) rules[r.id] = &r;
    std::vector<size_t> order(placements.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (placements[a].priority != placements[b].priority) return placements[a].priority > placements[b].priority;
        return placements[a].key < placements[b].key;
    });
    std::vector<char> drop(placements.size(), 0);
    std::unordered_map<int, std::unordered_map<Cell, std::vector<Vec2>, CellHash>> accepted;  // id -> bins
    for (size_t idx : order) {
        auto& p = placements[idx];
        auto it = rules.find(p.object.id);
        if (it == rules.end()) continue;
        auto& rule = *it->second;
        Rng r(hashOf(p.key, 0x7a1e));
        if (rule.probability < 1 && r.uniform() >= rule.probability) { drop[idx] = 1; continue; }
        // Scale range: only for free-standing elements (geometry-bound objects keep their size).
        auto def = catalog.find(p.object.id);
        bool freeElement = p.tier != PlacementTier::Primary && def && !def->stretchable &&
                           std::abs(def->visCenter().x) < 0.5 && std::abs(def->visCenter().y) < 0.5;
        if (freeElement && (rule.minScale != 1 || rule.maxScale != 1)) {
            double s = rule.minScale + (rule.maxScale - rule.minScale) * r.uniform();
            p.object.scaleX = std::round(p.object.scaleX * s * 10000) / 10000;
            p.object.scaleY = std::round(p.object.scaleY * s * 10000) / 10000;
            Vec2 c = p.footprint.center();
            p.footprint = Rect::fromCenter(c, p.footprint.width() * s, p.footprint.height() * s);
        }
        if (rule.spacing > 0) {
            Vec2 c = p.footprint.center();
            double bin = std::max(30.0, rule.spacing);
            Cell b{int(std::floor(c.x / bin)), int(std::floor(c.y / bin))};
            auto& bins = accepted[p.object.id];
            bool tooClose = false;
            for (int dy = -1; dy <= 1 && !tooClose; ++dy)
                for (int dx = -1; dx <= 1 && !tooClose; ++dx) {
                    auto bi = bins.find({b.x + dx, b.y + dy});
                    if (bi == bins.end()) continue;
                    for (auto& q : bi->second)
                        if ((q - c).length() < rule.spacing) { tooClose = true; break; }
                }
            if (tooClose) { drop[idx] = 1; continue; }
            bins[b].push_back(c);
        }
    }
    std::vector<Placement> kept;
    kept.reserve(placements.size());
    for (size_t i = 0; i < placements.size(); ++i)
        if (!drop[i]) kept.push_back(std::move(placements[i]));
    placements = std::move(kept);
}

}  // namespace

// ---------------------------------------------------------------------------

ZSlot Generator::autoZ(LayerKind kind, LevelAnalysis const& a) {
    int solidTop = a.solidTopRank;
    int hazardBottom = a.hazardBottomRank;
    auto aboveSolids = [&](int k) { return zFromRank(solidTop + k); };
    switch (kind) {
        case LayerKind::BlockFill: return aboveSolids(1);
        case LayerKind::InternalTexture: return aboveSolids(2);
        case LayerKind::Outline: return aboveSolids(4);
        case LayerKind::Corners: return aboveSolids(5);
        case LayerKind::StructuralDetails: return aboveSolids(6);
        case LayerKind::Highlights: return aboveSolids(7);
        case LayerKind::ObstacleAccents: {
            // Right below the hazards, but never below the block design.
            int r = std::max(hazardBottom - 1, solidTop + 8);
            if (r >= hazardBottom) r = std::min(zRank(zlayer::B3, 40), solidTop - 1);
            return zFromRank(r);
        }
        case LayerKind::StructuralGlow: return zFromRank(std::min(zRank(zlayer::B3, 20), solidTop - 50));
        case LayerKind::AmbientGlow: return zFromRank(std::min(zRank(zlayer::B4, 10), solidTop - 100));
        case LayerKind::BackgroundStructures: return {zlayer::B4, -40};
        case LayerKind::BackgroundDetails: return {zlayer::B4, -20};
        case LayerKind::Atmospheric: return zFromRank(std::min(zRank(zlayer::B3, -10), solidTop - 80));
        case LayerKind::Foreground: return {zlayer::T3, 0};
        case LayerKind::Group: break;
    }
    return {zlayer::B1, 0};
}

std::vector<std::string> Generator::resolveLayers(Project const& project, GenerationRequest const& request,
                                                  std::vector<std::string>* skippedLocked) const {
    std::vector<std::string> ids;
    auto& stack = project.layers;
    if (request.layers.empty()) {
        for (auto& id : stack.displayOrder()) {
            auto l = stack.find(id);
            bool parentsEnabled = true;
            for (auto p = l; p && !p->parentId.empty(); p = stack.find(p->parentId))
                if (auto pp = stack.find(p->parentId); pp && !pp->enabled) parentsEnabled = false;
            if (!l->enabled || !parentsEnabled || l->kind == LayerKind::Group) continue;
            if (!engineEnabled(engineOf(l->kind), project.settings)) continue;
            ids.push_back(id);
        }
    }
    else {
        for (auto& req : request.layers)
            for (auto& id : stack.subtree(req)) {
                auto l = stack.find(id);
                if (l->kind == LayerKind::Group) continue;
                if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
            }
    }
    std::vector<std::string> out;
    for (auto& id : ids) {
        if (stack.isLocked(id)) {
            if (skippedLocked) skippedLocked->push_back(id);
            continue;
        }
        out.push_back(id);
    }
    return out;
}

GenerationResult Generator::run(Project const& project, LevelAnalysis const& analysis, GenerationRequest const& request,
                                std::atomic<bool> const* cancel) const {
    auto t0 = std::chrono::steady_clock::now();
    GenerationResult result;
    auto layerIds = resolveLayers(project, request, &result.skippedLocked);
    auto plan = CompositionPlan::build(analysis, project.settings.globalSeed, project.settings.globalDensity);
    ModifiedPolicy policy = request.modifiedPolicy.value_or(project.settings.modifiedPolicy);

    auto structural = makeStructuralEngine();
    auto lighting = makeLightingEngine();
    auto artistic = makeArtisticEngine();

    for (auto& layerId : layerIds) {
        if (cancel && cancel->load()) { result.cancelled = true; break; }
        auto lt0 = std::chrono::steady_clock::now();
        Layer const& layer = *project.layers.find(layerId);
        LayerStats ls;
        ls.layerId = layerId;
        // One predicate for removal and generation (see MaskSet).
        MaskSet masks = MaskSet::build(project.masks, layerId, request.masks, &analysis, layer.params.boundary);

        // ---- objects to remove + modified-object policy -------------------
        // Removal covers the operation region (whole layer without operation
        // masks) regardless of include/exclude masks, so that the zone ends up
        // reflecting the current configuration.
        std::set<uint64_t> keptKeys;
        for (auto rec : project.recordsOf(layerId)) {
            if (!masks.inOperationRegion(rec->anchor)) continue;
            if (rec->state == RecordState::Modified) {
                if (policy == ModifiedPolicy::Keep) {
                    result.keptModified.push_back(rec->uid);
                    keptKeys.insert(rec->placementKey);
                    ls.keptModified++;
                    continue;
                }
                if (policy == ModifiedPolicy::Ask) {
                    result.askModified.push_back(rec->uid);
                    keptKeys.insert(rec->placementKey);
                    continue;
                }
            }
            result.removeRecords.push_back(rec->uid);
            ls.removed++;
        }
        if (request.mode == OperationMode::Remove) {
            result.layerStats.push_back(ls);
            continue;
        }

        // ---- engine ----------------------------------------------------------
        std::vector<Placement> placements;
        EngineContext ctx{analysis, m_catalog, layer, plan, masks};
        ctx.seed = hashOf(project.settings.globalSeed, layer.params.seed, hashString(layerKindName(layer.kind)));
        ctx.z = layer.params.autoZ || layer.params.zLayer == 0 ? autoZ(layer.kind, analysis)
                                                                 : ZSlot{layer.params.zLayer, layer.params.zOrder};
        ctx.density = std::clamp(layer.params.density, 0.0, 1.0);
        ctx.cancel = cancel;
        ctx.out = &placements;
        switch (engineOf(layer.kind)) {
            case EngineKind::Structural: structural->generate(ctx); break;
            case EngineKind::Lighting: lighting->generate(ctx); break;
            case EngineKind::Artistic: artistic->generate(ctx); break;
            default: break;
        }
        // Final mask filter (anchor based) and kept-modified skip.
        size_t before = placements.size();
        placements.erase(std::remove_if(placements.begin(), placements.end(),
                                        [&](Placement const& p) {
                                            return !masks.allows(p.anchor) || keptKeys.count(p.key) != 0;
                                        }),
                         placements.end());
        applyObjectRules(placements, layer, m_catalog);
        ls.rejected += int(before - placements.size());
        ls.placed = int(placements.size());
        ls.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - lt0).count();
        result.layerStats.push_back(ls);
        for (auto& p : placements) result.placements.push_back(std::move(p));
    }

    if (request.mode != OperationMode::Remove && !result.cancelled) {
        result.harmony = harmonize(result.placements, analysis, plan, project);
        result.validation = validatePlacements(result.placements, analysis, m_catalog, project);

        // Global cap: drop the least important objects first.
        int cap = std::max(100, project.settings.maxObjectsPerOperation);
        if (int(result.placements.size()) > cap) {
            std::stable_sort(result.placements.begin(), result.placements.end(),
                             [](Placement const& a, Placement const& b) { return a.priority > b.priority; });
            result.warnings.push_back("Object limit reached: " + std::to_string(result.placements.size() - size_t(cap)) +
                                      " low-priority objects were dropped (limit " + std::to_string(cap) + ").");
            result.placements.resize(size_t(cap));
        }

        for (auto& p : result.placements) {
            auto& sec = plan.at(p.anchor.x);
            result.objectsPerSection[sec.index]++;
        }
    }
    result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return result;
}

void finalizePlacements(Project& project, GenerationResult& result) {
    if (result.finalized) return;
    result.finalized = true;
    bool warned = false;
    for (auto& p : result.placements) {
        Layer* layer = project.layers.find(p.layerId);
        if (!layer) continue;
        int bucket = opacityBucket(p.opacity);
        int ch = project.channels.channelFor(p.layerId, p.role, bucket);
        if (ch == 0 && !warned) {
            result.warnings.push_back("No free color channel left: some objects keep their default color.");
            warned = true;
        }
        p.object.mainColor = ch;
        if (p.detailRole) p.object.detailColor = project.channels.channelFor(p.layerId, *p.detailRole, bucket);
        if (project.settings.useTagGroups && layer->tagGroup > 0) p.object.addGroup(layer->tagGroup);
        ColorAdjust adj = resolveAdjust(project.palette, layer->palette);
        if (adj.variation > 0) {
            Rng r(hashOf(p.key, 0x4a5));
            HSV hsv;
            hsv.h = std::round(r.range(-12, 12) * adj.variation);
            hsv.v = std::round((1.0 + r.range(-0.18, 0.12) * adj.variation) * 100) / 100;
            if (!hsv.isIdentity()) p.object.mainHSV = hsv;
        }
    }
}

// ---------------------------------------------------------------------------
// Harmonizer

HarmonizeReport harmonize(std::vector<Placement>& placements, LevelAnalysis const& analysis, CompositionPlan const& plan,
                          Project const& project) {
    HarmonizeReport rep;
    std::vector<char> drop(placements.size(), 0);

    // 1. Readability: nothing drawn above hazards may cover them; foreground
    //    stays away from the gameplay.
    for (size_t i = 0; i < placements.size(); ++i) {
        auto& p = placements[i];
        auto layer = project.layers.find(p.layerId);
        if (drawnAboveHazards(p, analysis) && footprintTouchesHazard(p.footprint, analysis)) {
            drop[i] = 1;
            rep.issues.push_back({IssueSeverity::Info, "hazard-cover", "Placement would cover a hazard; removed",
                                  p.anchor, p.layerId});
            continue;
        }
        if (layer && layer->kind == LayerKind::Foreground) {
            double fd = analysis.freeDistanceAt(p.footprint.center());
            if (!analysis.isAreaFree(p.footprint, 2 * kBlock) || fd < 3 * kBlock) {
                drop[i] = 1;
                continue;
            }
        }
    }

    // 2. Overlaps between secondary / micro elements of the same layer.
    std::unordered_map<Cell, std::vector<size_t>, CellHash> bins;
    auto binOf = [](Vec2 p) { return Cell{int(std::floor(p.x / 90)), int(std::floor(p.y / 90))}; };
    std::vector<size_t> order(placements.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (placements[a].priority != placements[b].priority) return placements[a].priority > placements[b].priority;
        return placements[a].key < placements[b].key;
    });
    for (size_t idx : order) {
        if (drop[idx]) continue;
        auto& p = placements[idx];
        // Primary forms tile on purpose; additive glows are meant to overlap.
        if (p.tier == PlacementTier::Primary || p.role == ColorRole::Glow) continue;
        Cell b = binOf(p.footprint.center());
        bool conflict = false;
        for (int dy = -1; dy <= 1 && !conflict; ++dy)
            for (int dx = -1; dx <= 1 && !conflict; ++dx) {
                auto it = bins.find({b.x + dx, b.y + dy});
                if (it == bins.end()) continue;
                for (size_t other : it->second) {
                    auto& q = placements[other];
                    if (q.layerId != p.layerId) continue;
                    Rect inter = p.footprint.intersection(q.footprint);
                    if (inter.isEmpty()) continue;
                    double smaller = std::min(p.footprint.area(), q.footprint.area());
                    if (smaller > 0 && inter.area() > 0.35 * smaller) { conflict = true; break; }
                }
            }
        if (conflict) {
            drop[idx] = 1;
            rep.overlapsRemoved++;
            continue;
        }
        bins[b].push_back(idx);
    }

    // 3. Density budget for optional elements (5x5 block bins).
    std::unordered_map<Cell, std::vector<size_t>, CellHash> densityBins;
    for (size_t idx : order) {
        if (drop[idx] || placements[idx].tier == PlacementTier::Primary) continue;
        Vec2 c = placements[idx].footprint.center();
        densityBins[{int(std::floor(c.x / 150)), int(std::floor(c.y / 150))}].push_back(idx);
    }
    for (auto& [bin, list] : densityBins) {
        double x = (bin.x + 0.5) * 150;
        auto& sec = plan.at(x);
        int maxObjects = std::max(2, int(std::round(22 * std::max(0.15, sec.detailBudget))));
        if (int(list.size()) > maxObjects) {
            rep.crowdedZones++;
            // `list` follows the priority order: trim the tail.
            for (size_t k = size_t(maxObjects); k < list.size(); ++k) {
                drop[list[k]] = 1;
                rep.densityTrimmed++;
            }
        }
    }

    // 4. Excessive repetition in artistic layers: vary rotation/flip.
    std::map<std::string, std::vector<size_t>> perLayer;
    for (size_t i = 0; i < placements.size(); ++i)
        if (!drop[i]) perLayer[placements[i].layerId].push_back(i);
    for (auto& [layerId, list] : perLayer) {
        auto layer = project.layers.find(layerId);
        if (!layer || engineOf(layer->kind) != EngineKind::Artistic) continue;
        std::sort(list.begin(), list.end(), [&](size_t a, size_t b) { return placements[a].anchor.x < placements[b].anchor.x; });
        int run = 1;
        for (size_t k = 1; k < list.size(); ++k) {
            auto& a = placements[list[k - 1]].object;
            auto& b = placements[list[k]].object;
            bool same = a.id == b.id && std::abs(a.scaleX - b.scaleX) < 0.05 && std::abs(a.rotation - b.rotation) < 1;
            run = same ? run + 1 : 1;
            if (run >= 4) {
                b.flipX = !b.flipX;
                rep.repetitionsVaried++;
                run = 1;
            }
        }
    }

    // 5. Empty zones: open sections that received no optional decoration.
    for (auto& sec : plan.sections()) {
        int count = 0;
        for (size_t i = 0; i < placements.size(); ++i)
            if (!drop[i] && placements[i].anchor.x >= sec.x0 && placements[i].anchor.x < sec.x1) ++count;
        if (count == 0 && sec.x1 - sec.x0 > 20 * kBlock && sec.readability < 0.5) rep.emptyZones++;
    }

    // 6. Color sanity (per layer palette).
    {
        auto fill = resolveRole(project.palette, {}, ColorRole::Primary).color;
        auto outline = resolveRole(project.palette, {}, ColorRole::Secondary).color;
        auto bg = resolveRole(project.palette, {}, ColorRole::Background).color;
        if (contrastRatio(fill, outline) < 1.6)
            rep.issues.push_back({IssueSeverity::Warning, "low-contrast-outline",
                                  "Outline and block fill colors are very close: contours will be hard to read", {}, ""});
        if (contrastRatio(fill, bg) < 1.15)
            rep.issues.push_back({IssueSeverity::Warning, "low-contrast-background",
                                  "Block fill and background colors are almost identical: platforms may blend into the background",
                                  {}, ""});
    }

    std::vector<Placement> kept;
    kept.reserve(placements.size());
    for (size_t i = 0; i < placements.size(); ++i)
        if (!drop[i]) kept.push_back(std::move(placements[i]));
    placements = std::move(kept);
    return rep;
}

// ---------------------------------------------------------------------------
// Safety validation

ValidationReport validatePlacements(std::vector<Placement>& placements, LevelAnalysis const& analysis,
                                    DecoCatalog const& catalog, Project const& project) {
    ValidationReport rep;
    rep.limits = {
        "Gameplay objects are never modified: the level's non-generated objects are compared before/after every operation.",
        "Only catalog objects verified as decoration (no collision, not a trigger/portal/orb/pad) can be generated.",
        "Readability is protected by conservative rules (hazard margins, corridor tightness, draw order); the player's "
        "exact trajectory is NOT simulated.",
    };
    auto& table = StaticObjectTable::get();
    auto tags = project.tagGroups();
    std::vector<Placement> ok;
    ok.reserve(placements.size());
    for (auto& p : placements) {
        rep.checked++;
        auto def = catalog.find(p.object.id);
        std::string problem;
        if (!def) problem = "object id not in the decoration catalog";
        else if (catalog.isDisabled(p.object.id)) problem = "object id disabled after live verification";
        else if (table.isKnown(p.object.id) && table.info(p.object.id).kind != ObjectKind::Decoration)
            problem = "object id is not a decoration in the game's object table";
        for (int g : p.object.groups)
            if (!tags.count(g)) problem = "unexpected group on a generated object";
        if (!p.object.extra.empty()) problem = "unexpected properties on a generated object";
        if (problem.empty() && drawnAboveHazards(p, analysis) && footprintTouchesHazard(p.footprint, analysis))
            problem = "would be drawn over a hazard";
        if (!problem.empty()) {
            rep.rejected++;
            rep.issues.push_back({IssueSeverity::Error, "rejected", problem, p.anchor, p.layerId});
            continue;
        }
        ok.push_back(std::move(p));
    }
    placements = std::move(ok);
    return rep;
}

}  // namespace gdds
