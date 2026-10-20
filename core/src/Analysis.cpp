#include <gdds/Analysis.hpp>
#include <gdds/Random.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace gdds {

char const* faceName(Face f) {
    switch (f) {
        case Face::Top: return "top";
        case Face::Bottom: return "bottom";
        case Face::Left: return "left";
        case Face::Right: return "right";
    }
    return "?";
}

Vec2 faceNormal(Face f) {
    switch (f) {
        case Face::Top: return {0, 1};
        case Face::Bottom: return {0, -1};
        case Face::Left: return {-1, 0};
        case Face::Right: return {1, 0};
    }
    return {0, 1};
}

char const* structureClassName(StructureClass c) {
    switch (c) {
        case StructureClass::Block: return "block";
        case StructureClass::Platform: return "platform";
        case StructureClass::Wall: return "wall";
        case StructureClass::Pillar: return "pillar";
        case StructureClass::Mass: return "mass";
        case StructureClass::Thin: return "thin";
        case StructureClass::Staircase: return "staircase";
    }
    return "?";
}

ObjectFootprint computeFootprint(LevelObject const& obj, ObjectInfo const& info) {
    ObjectFootprint fp;
    double sx = std::abs(obj.scaleX), sy = std::abs(obj.scaleY);
    Vec2 off{info.hitOffX * sx * (obj.flipX ? -1 : 1), info.hitOffY * sy * (obj.flipY ? -1 : 1)};
    Vec2 center = obj.pos + off.rotatedCW(obj.rotation);

    switch (info.shape) {
        case HitboxShape::Box:
            fp.hitbox = {center, info.hitW * sx / 2, info.hitH * sy / 2, obj.rotation};
            break;
        case HitboxShape::Circle: {
            double r = info.hitW * std::max(sx, sy) / 2;
            fp.hitbox = {center, r, r, 0};
            break;
        }
        case HitboxShape::Slope: {
            // Default slope: solid below the diagonal, rising to the right.
            double hw = info.hitW * sx / 2, hh = info.hitH * sy / 2;
            Vec2 local[3] = {{-hw, -hh}, {hw, -hh}, {hw, hh}};
            Vec2 world[3];
            for (int i = 0; i < 3; ++i) {
                Vec2 l = local[i];
                if (obj.flipX) l.x = -l.x;
                if (obj.flipY) l.y = -l.y;
                world[i] = obj.pos + l.rotatedCW(obj.rotation);
            }
            fp.slope = Triangle{world[0], world[1], world[2]};
            fp.hitbox = {obj.pos, hw, hh, obj.rotation};
            break;
        }
        case HitboxShape::None:
            fp.hitbox = {obj.pos, 0, 0, 0};
            break;
    }
    OrientedRect vis{obj.pos, info.visW * sx / 2, info.visH * sy / 2, obj.rotation};
    fp.visual = vis.bounds();
    return fp;
}

// ---------------------------------------------------------------------------
// LevelAnalysis queries

double LevelAnalysis::freeDistanceAt(Vec2 p) const {
    Cell c = cellAt(p);
    if (isBlocked(c)) return 0;
    uint8_t v = freeDist.get(c);
    if (v == 0) return kFreeDistCap * kCell;
    return (v - 1) * kCell;
}

double LevelAnalysis::verticalClearance(Vec2 p, double cap) const {
    Cell c = cellAt(p);
    if (isSolid(c)) return 0;
    int capCells = int(cap / kCell) + 1;
    int up = 0, down = 0;
    while (up < capCells && !isSolid({c.x, c.y + up + 1})) ++up;
    while (down < capCells && c.y - down - 1 >= 0 && !isSolid({c.x, c.y - down - 1})) ++down;
    return std::min(cap, (up + down + 1) * kCell);
}

GameplaySection const* LevelAnalysis::sectionAt(double x) const {
    if (sections.empty()) return nullptr;
    for (auto& s : sections) if (x >= s.x0 && x < s.x1) return &s;
    return x < sections.front().x0 ? &sections.front() : &sections.back();
}

bool LevelAnalysis::isAreaFree(Rect const& r, double margin) const {
    Rect m = r.expanded(margin);
    Cell a = cellAt({m.x0, m.y0}), b = cellAt({m.x1 - 1e-6, m.y1 - 1e-6});
    for (int y = a.y; y <= b.y; ++y)
        for (int x = a.x; x <= b.x; ++x)
            if (isBlocked({x, y})) return false;
    return true;
}

double LevelAnalysis::solidCoverage(Rect const& r) const {
    Cell a = cellAt({r.x0 + 1e-6, r.y0 + 1e-6}), b = cellAt({r.x1 - 1e-6, r.y1 - 1e-6});
    int total = 0, solid = 0;
    for (int y = a.y; y <= b.y; ++y)
        for (int x = a.x; x <= b.x; ++x) {
            ++total;
            if (isSolid({x, y})) ++solid;
        }
    return total ? double(solid) / total : 0;
}

double LevelAnalysis::hazardDistance(Vec2 p, double cap) const {
    Cell c = cellAt(p);
    int r = int(cap / kCell) + 1;
    double best = cap;
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            if (!isHazard({c.x + dx, c.y + dy})) continue;
            double d = std::max(0.0, std::max(std::abs(dx), std::abs(dy)) - 0.5) * kCell;
            best = std::min(best, d);
        }
    return best;
}

std::string LevelAnalysis::summary() const {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "objects=%d gameplay=%d (solid=%d hazard=%d unknown=%d) deco=%d ignored=%d | structures=%d "
                  "contours=%d segments=%d corners=%d stairs=%d hazardClusters=%d sections=%d | %.1f ms",
                  stats.objectsTotal, stats.gameplayObjects, stats.solidObjects, stats.hazardObjects,
                  stats.unknownObjects, stats.decorationObjects, stats.ignoredObjects, stats.structures,
                  stats.contours, stats.segments, stats.corners, stats.staircases, stats.hazardClusters,
                  stats.sections, stats.analysisMs);
    return buf;
}

// ---------------------------------------------------------------------------
// Analyzer

namespace {

int drawRank(int zLayer, int zOrder) {
    return zlayer::rank(zLayer) * 20000 + std::clamp(zOrder, -9999, 9999) + 10000;
}

Dir leftOf(Dir d) { return Dir((int(d) + 1) % 4); }
Dir rightOf(Dir d) { return Dir((int(d) + 3) % 4); }
Dir opposite(Dir d) { return Dir((int(d) + 2) % 4); }
Cell step(Cell c, Dir d) {
    switch (d) {
        case Dir::East: return {c.x + 1, c.y};
        case Dir::North: return {c.x, c.y + 1};
        case Dir::West: return {c.x - 1, c.y};
        case Dir::South: return {c.x, c.y - 1};
    }
    return c;
}
Face faceOfTravel(Dir d) {
    switch (d) {
        case Dir::East: return Face::Bottom;
        case Dir::North: return Face::Right;
        case Dir::West: return Face::Top;
        case Dir::South: return Face::Left;
    }
    return Face::Top;
}
int quadrantOf(Dir h, Dir v) {
    // h is East/West, v is North/South (order-insensitive)
    if (h == Dir::North || h == Dir::South) std::swap(h, v);
    bool east = h == Dir::East, north = v == Dir::North;
    if (east && north) return 0;
    if (!east && north) return 1;
    if (!east && !north) return 2;
    return 3;
}

template <class Pred>
void rasterize(Rect const& bounds, Pred&& inside, bool conservative, std::vector<Cell>& out) {
    out.clear();
    if (bounds.isEmpty() && !(bounds.width() == 0 && bounds.height() == 0)) return;
    Cell a = LevelAnalysis::cellAt({bounds.x0, bounds.y0});
    Cell b = LevelAnalysis::cellAt({bounds.x1, bounds.y1});
    // Guard against absurd footprints (scaled helper objects)
    if (int64_t(b.x - a.x + 1) * int64_t(b.y - a.y + 1) > 400000) return;
    for (int y = a.y; y <= b.y; ++y) {
        for (int x = a.x; x <= b.x; ++x) {
            Cell c{x, y};
            if (conservative) {
                Rect cr{x * kCell, y * kCell, (x + 1) * kCell, (y + 1) * kCell};
                // overlap test with a small inset so touching edges do not count
                if (cr.expanded(-1.0).intersects(bounds)) out.push_back(c);
            }
            else if (inside(LevelAnalysis::cellCenter(c))) {
                out.push_back(c);
            }
        }
    }
    if (out.empty()) out.push_back(LevelAnalysis::cellAt(bounds.center()));
}

}  // namespace

std::vector<AnalysisInput> Analyzer::inputsFromLevel(Level const& level) {
    std::vector<AnalysisInput> out;
    out.reserve(level.objects.size());
    auto& table = StaticObjectTable::get();
    for (auto& o : level.objects) out.push_back({o, table.info(o.id), false});
    refineInputs(out);
    return out;
}

void Analyzer::refineInputs(std::vector<AnalysisInput>& inputs) {
    // Groups targeted by triggers (target group 51, center/second group 71).
    std::unordered_set<int> animated;
    for (auto& in : inputs) {
        if (in.ignore || in.info.kind != ObjectKind::Trigger) continue;
        for (int k : {key::TargetGroup, key::CenterGroup}) {
            int g = in.object.extraInt(k, 0);
            if (g > 0) animated.insert(g);
        }
    }
    for (auto& in : inputs) {
        if (in.ignore) continue;
        auto& kind = in.info.kind;
        bool noTouch = in.object.extraInt(121, 0) != 0;
        bool hidden = in.object.extraInt(135, 0) != 0;
        bool dynamic = false;
        for (int g : in.object.groups)
            if (animated.count(g)) { dynamic = true; break; }
        if (kind == ObjectKind::Solid) {
            if (noTouch) kind = ObjectKind::Decoration;
            else if (hidden || dynamic) kind = ObjectKind::Special;
        }
        else if (kind == ObjectKind::Hazard && noTouch) {
            kind = ObjectKind::Special;
        }
    }
}

std::unique_ptr<LevelAnalysis> Analyzer::analyze(std::vector<AnalysisInput> inputs, LevelHeader const* header) const {
    auto t0 = std::chrono::steady_clock::now();
    auto A = std::make_unique<LevelAnalysis>();
    A->inputs = std::move(inputs);
    auto& in = A->inputs;
    A->footprints.reserve(in.size());

    // ---- 1. Footprints and rasterization ---------------------------------
    std::vector<Cell> cells;
    int solidTop = INT32_MIN, hazardBottom = INT32_MAX, hazardTop = INT32_MIN;
    std::vector<uint32_t> hazardObjects;
    std::vector<std::pair<uint32_t, Cell>> solidObjectCells;

    for (uint32_t i = 0; i < in.size(); ++i) {
        auto& rec = in[i];
        auto fp = computeFootprint(rec.object, rec.info);
        A->footprints.push_back(fp);
        A->stats.objectsTotal++;
        if (rec.ignore) { A->stats.ignoredObjects++; continue; }

        int zl = rec.object.zLayer ? rec.object.zLayer : rec.info.defaultZLayer;
        int zo = rec.object.hasZOrder ? rec.object.zOrder : rec.info.defaultZOrder;

        switch (rec.info.kind) {
            case ObjectKind::Solid: {
                A->stats.solidObjects++;
                A->stats.gameplayObjects++;
                solidTop = std::max(solidTop, drawRank(zl, zo));
                if (fp.slope) {
                    auto tri = *fp.slope;
                    rasterize(tri.bounds(), [&](Vec2 p) { return tri.contains(p); }, false, cells);
                    for (auto c : cells) A->flags.at(c.x, c.y) |= cellflag::Solid | cellflag::Slope;
                }
                else {
                    auto box = fp.hitbox;
                    rasterize(box.bounds(), [&](Vec2 p) { return box.contains(p); }, false, cells);
                    for (auto c : cells) A->flags.at(c.x, c.y) |= cellflag::Solid;
                }
                if (!cells.empty()) solidObjectCells.emplace_back(i, cells[cells.size() / 2]);
                A->levelBounds = A->levelBounds.united(fp.hitbox.bounds());
                A->gameplayBounds = A->gameplayBounds.united(fp.hitbox.bounds());
                break;
            }
            case ObjectKind::Hazard: {
                A->stats.hazardObjects++;
                A->stats.gameplayObjects++;
                hazardBottom = std::min(hazardBottom, drawRank(zl, zo));
                hazardTop = std::max(hazardTop, drawRank(zl, zo));
                // Hazards are rasterized with their VISIBLE footprint: what matters
                // for decoration is never covering what the player must see.
                Rect vis = fp.visual.expanded(-1.5);
                if (vis.isEmpty()) vis = fp.visual;
                rasterize(vis, [](Vec2) { return true; }, true, cells);
                for (auto c : cells) A->flags.at(c.x, c.y) |= cellflag::Hazard;
                hazardObjects.push_back(i);
                A->levelBounds = A->levelBounds.united(fp.visual);
                A->gameplayBounds = A->gameplayBounds.united(fp.visual);
                break;
            }
            case ObjectKind::Decoration: {
                A->stats.decorationObjects++;
                rasterize(fp.visual.expanded(-2), [](Vec2) { return true; }, true, cells);
                for (auto c : cells) A->flags.at(c.x, c.y) |= cellflag::Deco;
                break;
            }
            case ObjectKind::Trigger:
                // Triggers are invisible during gameplay and take no space.
                A->stats.gameplayObjects++;
                break;
            case ObjectKind::Portal: {
                A->stats.gameplayObjects++;
                A->portals.push_back({rec.object.pos.x, rec.object.pos.y, rec.info.portal, rec.info.portalValue, rec.object.id});
                [[fallthrough]];
            }
            case ObjectKind::Pad:
            case ObjectKind::Orb:
            case ObjectKind::Collectible:
            case ObjectKind::Special:
            case ObjectKind::Unknown:
            case ObjectKind::StartPos: {
                if (rec.info.kind != ObjectKind::Portal) A->stats.gameplayObjects++;
                if (rec.info.kind == ObjectKind::Unknown) A->stats.unknownObjects++;
                if (rec.info.kind == ObjectKind::StartPos) break;
                rasterize(fp.visual.expanded(-1.5), [](Vec2) { return true; }, true, cells);
                for (auto c : cells) A->flags.at(c.x, c.y) |= cellflag::Special;
                A->levelBounds = A->levelBounds.united(fp.visual);
                if (rec.info.kind == ObjectKind::Portal || rec.info.kind == ObjectKind::Orb || rec.info.kind == ObjectKind::Pad)
                    A->gameplayBounds = A->gameplayBounds.united(fp.visual);
                break;
            }
        }
    }
    A->solidTopRank = solidTop == INT32_MIN ? drawRank(zlayer::B2, 0) : solidTop;
    A->hazardBottomRank = hazardBottom == INT32_MAX ? drawRank(zlayer::B1, 0) : hazardBottom;
    A->hazardTopRank = hazardTop == INT32_MIN ? drawRank(zlayer::B1, 0) : hazardTop;

    // Hazard halo (readability margin)
    {
        int m = int(std::ceil(m_opts.hazardHaloMargin / kCell));
        std::vector<Cell> hazardCells;
        A->flags.forEach([&](int x, int y, uint8_t f) { if (f & cellflag::Hazard) hazardCells.push_back({x, y}); });
        for (auto c : hazardCells)
            for (int dy = -m; dy <= m; ++dy)
                for (int dx = -m; dx <= m; ++dx) A->flags.at(c.x + dx, c.y + dy) |= cellflag::HazardHalo;
    }

    // ---- 2. Connected components of solid cells ---------------------------
    std::vector<Cell> solidCells;
    A->flags.forEach([&](int x, int y, uint8_t f) {
        if (f & cellflag::Solid) {
            solidCells.push_back({x, y});
            A->cellBounds.include(x, y);
        }
    });
    std::sort(solidCells.begin(), solidCells.end(), [](Cell a, Cell b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });

    for (auto start : solidCells) {
        if (A->structureOf.get(start) != -1) continue;
        Structure s;
        s.id = int(A->structures.size());
        std::deque<Cell> queue{start};
        A->structureOf.set(start, s.id);
        while (!queue.empty()) {
            Cell c = queue.front();
            queue.pop_front();
            s.cells.push_back(c);
            s.cellBounds.include(c.x, c.y);
            if (A->flags.get(c) & cellflag::Slope) s.hasSlopes = true;
            for (Dir d : {Dir::East, Dir::North, Dir::West, Dir::South}) {
                Cell n = step(c, d);
                if (A->isSolid(n) && A->structureOf.get(n) == -1) {
                    A->structureOf.set(n, s.id);
                    queue.push_back(n);
                }
            }
        }
        std::sort(s.cells.begin(), s.cells.end(), [](Cell a, Cell b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
        s.cellCount = int(s.cells.size());
        s.bounds = {s.cellBounds.x0 * kCell, s.cellBounds.y0 * kCell, (s.cellBounds.x1 + 1) * kCell,
                    (s.cellBounds.y1 + 1) * kCell};
        s.touchesGround = m_opts.groundIsSolid && s.cellBounds.y0 <= 0;
        uint64_t sig = 0x5157, shape = 0x5348;
        for (auto c : s.cells) {
            sig = hashCombine(sig, (uint64_t(uint32_t(c.x)) << 32) | uint32_t(c.y));
            shape = hashCombine(shape, (uint64_t(uint32_t(c.x - s.cellBounds.x0)) << 32) | uint32_t(c.y - s.cellBounds.y0));
        }
        s.signature = sig;
        s.shapeSignature = shape;
        A->structures.push_back(std::move(s));
    }
    for (auto& [idx, c] : solidObjectCells) {
        int sid = A->structureOf.get(c);
        if (sid >= 0) A->structures[sid].objects.push_back(idx);
    }
    {
        std::unordered_map<uint64_t, int> shapeCounts;
        for (auto& s : A->structures) shapeCounts[s.shapeSignature]++;
        for (auto& s : A->structures) s.repeatCount = shapeCounts[s.shapeSignature];
    }

    // ---- 3. Interior depth (Chebyshev distance to the outside) ------------
    {
        auto solidOrGround = [&](Cell c) { return A->isSolid(c) || (m_opts.groundIsSolid && c.y < 0); };
        std::deque<Cell> queue;
        for (auto c : solidCells) {
            bool boundary = false;
            for (int dy = -1; dy <= 1 && !boundary; ++dy)
                for (int dx = -1; dx <= 1 && !boundary; ++dx)
                    if ((dx || dy) && !solidOrGround({c.x + dx, c.y + dy})) boundary = true;
            if (boundary) {
                A->depth.set(c, 1);
                queue.push_back(c);
            }
        }
        while (!queue.empty()) {
            Cell c = queue.front();
            queue.pop_front();
            uint8_t d = A->depth.get(c);
            if (d >= 254) continue;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    Cell n{c.x + dx, c.y + dy};
                    if (A->isSolid(n) && A->depth.get(n) == 0) {
                        A->depth.set(n, uint8_t(d + 1));
                        queue.push_back(n);
                    }
                }
        }
        for (auto& s : A->structures)
            for (auto c : s.cells) s.maxDepth = std::max<int>(s.maxDepth, A->depth.get(c));
    }

    // ---- 4. Free-space distance field -------------------------------------
    {
        std::deque<Cell> queue;
        // Seeds: empty cells adjacent to blocked cells get distance 1.
        std::vector<Cell> blocked;
        A->flags.forEach([&](int x, int y, uint8_t f) {
            if (f & (cellflag::Solid | cellflag::Hazard | cellflag::Special)) blocked.push_back({x, y});
        });
        for (auto c : blocked) {
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    Cell n{c.x + dx, c.y + dy};
                    if (A->isBlocked(n) || A->freeDist.get(n) != 0) continue;
                    A->freeDist.set(n, 2);  // stored as distance + 1
                    queue.push_back(n);
                }
        }
        while (!queue.empty()) {
            Cell c = queue.front();
            queue.pop_front();
            uint8_t v = A->freeDist.get(c);
            if (v > LevelAnalysis::kFreeDistCap) continue;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    Cell n{c.x + dx, c.y + dy};
                    if (A->isBlocked(n) || A->freeDist.get(n) != 0) continue;
                    A->freeDist.set(n, uint8_t(v + 1));
                    queue.push_back(n);
                }
        }
    }

    // ---- 5. Contours, segments and corners ---------------------------------
    for (auto& s : A->structures) {
        // Directed unit edges with the structure on the left.
        struct Edge { Cell from; Dir dir; };
        std::unordered_map<Cell, std::vector<int>, CellHash> outgoing;
        std::vector<Edge> edges;
        auto inS = [&](Cell c) { return A->structureOf.get(c) == s.id; };
        for (auto c : s.cells) {
            if (!inS({c.x, c.y - 1})) edges.push_back({{c.x, c.y}, Dir::East});
            if (!inS({c.x + 1, c.y})) edges.push_back({{c.x + 1, c.y}, Dir::North});
            if (!inS({c.x, c.y + 1})) edges.push_back({{c.x + 1, c.y + 1}, Dir::West});
            if (!inS({c.x - 1, c.y})) edges.push_back({{c.x, c.y + 1}, Dir::South});
        }
        for (int e = 0; e < int(edges.size()); ++e) outgoing[edges[e].from].push_back(e);
        std::vector<char> used(edges.size(), 0);

        // Deterministic start order: lowest vertex first.
        std::vector<int> order(edges.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            auto& ea = edges[a].from; auto& eb = edges[b].from;
            if (ea.y != eb.y) return ea.y < eb.y;
            if (ea.x != eb.x) return ea.x < eb.x;
            return int(edges[a].dir) < int(edges[b].dir);
        });

        for (int startEdge : order) {
            if (used[startEdge]) continue;
            // Walk the loop
            std::vector<int> loop;
            int e = startEdge;
            while (e >= 0 && !used[e]) {
                used[e] = 1;
                loop.push_back(e);
                Cell end = step(edges[e].from, edges[e].dir);
                auto it = outgoing.find(end);
                int next = -1;
                if (it != outgoing.end()) {
                    Dir d = edges[e].dir;
                    // Prefer left turn (keeps diagonal-touching cells separated), then straight, then right.
                    for (Dir want : {leftOf(d), d, rightOf(d)}) {
                        for (int cand : it->second)
                            if (!used[cand] && edges[cand].dir == want) { next = cand; break; }
                        if (next >= 0) break;
                    }
                }
                e = next;
            }
            if (loop.size() < 4) continue;

            // Rotate so the loop starts at a direction change (a corner).
            size_t n = loop.size();
            size_t rot = 0;
            for (size_t k = 0; k < n; ++k) {
                if (edges[loop[k]].dir != edges[loop[(k + n - 1) % n]].dir) { rot = k; break; }
            }
            std::rotate(loop.begin(), loop.begin() + long(rot), loop.end());

            Contour contour;
            contour.id = int(A->contours.size());
            contour.structure = s.id;

            // Signed area via vertices to detect holes.
            double area2 = 0;
            for (size_t k = 0; k < n; ++k) {
                Cell a = edges[loop[k]].from, b = step(a, edges[loop[k]].dir);
                area2 += double(a.x) * b.y - double(b.x) * a.y;
            }
            contour.hole = area2 < 0;

            // Merge collinear unit edges into segments.
            size_t k = 0;
            while (k < n) {
                Dir d = edges[loop[k]].dir;
                Cell a = edges[loop[k]].from;
                size_t j = k;
                while (j + 1 < n && edges[loop[j + 1]].dir == d) ++j;
                Cell b = step(edges[loop[j]].from, d);
                ContourSegment seg;
                seg.id = int(A->segments.size());
                seg.contour = contour.id;
                seg.structure = s.id;
                seg.a = a;
                seg.b = b;
                seg.p0 = LevelAnalysis::vertexPos(a);
                seg.p1 = LevelAnalysis::vertexPos(b);
                seg.travel = d;
                seg.face = faceOfTravel(d);
                seg.length = (seg.p1 - seg.p0).length();
                seg.hole = contour.hole;
                seg.onGround = m_opts.groundIsSolid && seg.face == Face::Bottom && a.y <= 0;
                contour.segments.push_back(seg.id);
                contour.perimeter += seg.length;
                A->segments.push_back(seg);
                k = j + 1;
            }
            // Corners between consecutive segments
            size_t m = contour.segments.size();
            for (size_t i2 = 0; i2 < m; ++i2) {
                auto& sIn = A->segments[contour.segments[i2]];
                auto& sOut = A->segments[contour.segments[(i2 + 1) % m]];
                Corner c;
                c.id = int(A->corners.size());
                c.contour = contour.id;
                c.structure = s.id;
                c.vertex = sIn.b;
                c.pos = sIn.p1;
                c.segIn = sIn.id;
                c.segOut = sOut.id;
                c.faceIn = sIn.face;
                c.faceOut = sOut.face;
                c.convex = sOut.travel == leftOf(sIn.travel);
                c.quadrant = c.convex ? quadrantOf(opposite(sIn.travel), leftOf(sIn.travel))
                                      : quadrantOf(opposite(sIn.travel), rightOf(sIn.travel));
                c.onGround = sIn.onGround || sOut.onGround;
                sIn.cornerEnd = c.id;
                sOut.cornerStart = c.id;
                contour.corners.push_back(c.id);
                A->corners.push_back(c);
            }
            s.contours.push_back(contour.id);
            A->contours.push_back(std::move(contour));
        }
    }

    // Segment exposure and hazard contact
    for (auto& seg : A->segments) {
        Vec2 nrm = faceNormal(seg.face);
        Vec2 dir = (seg.p1 - seg.p0) / std::max(seg.length, 1e-9);
        int samples = std::max(1, int(seg.length / kCell));
        std::vector<double> free;
        int hazardHits = 0;
        for (int i = 0; i < samples; ++i) {
            Vec2 p = seg.p0 + dir * ((i + 0.5) * kCell) + nrm * (kCell * 0.5);
            Cell c = LevelAnalysis::cellAt(p);
            if (A->isHazard(c)) ++hazardHits;
            int dist = 0;
            Cell cur = c;
            while (dist < 16 && !A->isBlocked(cur) && !(m_opts.groundIsSolid && cur.y < 0)) {
                ++dist;
                cur = LevelAnalysis::cellAt(p + nrm * (kCell * dist));
            }
            free.push_back(dist * kCell);
        }
        std::sort(free.begin(), free.end());
        seg.exposure = free[free.size() / 2];
        seg.hazardCoverage = double(hazardHits) / samples;
        auto& st = A->structures[seg.structure];
        if (seg.onGround) continue;
        if (seg.face == Face::Top) st.topLength += seg.length;
        else if (seg.face == Face::Bottom) st.bottomLength += seg.length;
        else st.sideLength += seg.length;
    }

    // ---- 6. Structure classification and staircases -----------------------
    for (auto& s : A->structures) {
        double w = s.bounds.width(), h = s.bounds.height();
        double fill = double(s.cellCount) / std::max(1, s.cellBounds.width() * s.cellBounds.height());
        if (std::min(w, h) <= kCell) s.cls = StructureClass::Thin;
        else if (s.cellCount <= 16 && fill > 0.9) s.cls = StructureClass::Block;
        else if (w >= 3 * h && h <= 2 * kBlock) s.cls = StructureClass::Platform;
        else if (h >= 3 * w) s.cls = w <= 2 * kBlock ? StructureClass::Pillar : StructureClass::Wall;
        else s.cls = StructureClass::Mass;
    }
    for (auto& contour : A->contours) {
        if (contour.hole) continue;
        auto const& segs = contour.segments;
        size_t m = segs.size();
        if (m < 6) continue;
        // Look for runs alternating a horizontal top face and a vertical riser,
        // all short and all risers facing the same way.
        size_t i = 0;
        while (i < m) {
            size_t j = i;
            Face riser = Face::Top;
            int steps = 0;
            double sumW = 0, sumH = 0;
            while (j + 1 < m + i) {
                auto& h = A->segments[segs[j % m]];
                auto& v = A->segments[segs[(j + 1) % m]];
                bool ok = h.face == Face::Top && (v.face == Face::Left || v.face == Face::Right) &&
                          h.length <= 3 * kBlock && v.length <= 3 * kBlock;
                if (ok && steps > 0 && v.face != riser) ok = false;
                if (!ok) break;
                riser = v.face;
                ++steps;
                sumW += h.length;
                sumH += v.length;
                j += 2;
                if (steps > int(m)) break;
            }
            if (steps >= 3) {
                Staircase st;
                st.structure = contour.structure;
                st.steps = steps;
                st.stepW = sumW / steps;
                st.stepH = sumH / steps;
                st.ascendingRight = riser == Face::Left;
                auto& first = A->segments[segs[i % m]];
                auto& last = A->segments[segs[(i + 2 * steps - 1) % m]];
                st.start = first.p0;
                st.end = last.p1;
                if (st.start.y > st.end.y) std::swap(st.start, st.end);
                A->staircases.push_back(st);
                if (steps * 2 >= int(m) / 2) A->structures[contour.structure].cls = StructureClass::Staircase;
                i += size_t(2 * steps);
            }
            else ++i;
        }
    }

    // ---- 7. Hazard clusters ------------------------------------------------
    {
        std::vector<int> parent(hazardObjects.size());
        std::iota(parent.begin(), parent.end(), 0);
        std::function<int(int)> find = [&](int x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };
        std::unordered_map<Cell, std::vector<int>, CellHash> buckets;
        auto bucketOf = [](Vec2 p) { return Cell{int(std::floor(p.x / 60)), int(std::floor(p.y / 60))}; };
        for (int k = 0; k < int(hazardObjects.size()); ++k)
            buckets[bucketOf(A->footprints[hazardObjects[k]].visual.center())].push_back(k);
        for (int k = 0; k < int(hazardObjects.size()); ++k) {
            Rect rk = A->footprints[hazardObjects[k]].visual.expanded(kBlock);
            Cell b = bucketOf(A->footprints[hazardObjects[k]].visual.center());
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx) {
                    auto it = buckets.find({b.x + dx, b.y + dy});
                    if (it == buckets.end()) continue;
                    for (int o : it->second)
                        if (o != k && rk.intersects(A->footprints[hazardObjects[o]].visual)) parent[find(k)] = find(o);
                }
        }
        std::map<int, int> rootToCluster;
        for (int k = 0; k < int(hazardObjects.size()); ++k) {
            int r = find(k);
            auto [it, inserted] = rootToCluster.emplace(r, int(A->hazardClusters.size()));
            if (inserted) {
                HazardCluster hc;
                hc.id = it->second;
                hc.bounds = Rect::empty();
                A->hazardClusters.push_back(hc);
            }
            auto& hc = A->hazardClusters[it->second];
            uint32_t idx = hazardObjects[k];
            hc.objects.push_back(idx);
            hc.bounds = hc.bounds.united(A->footprints[idx].visual);
            if (in[idx].info.shape == HitboxShape::Circle) hc.saws++;
            else if (in[idx].info.visH <= 30 && in[idx].info.hitW < 20) hc.spikes++;
            else hc.others++;
        }
        for (auto& hc : A->hazardClusters) {
            Cell below = LevelAnalysis::cellAt({hc.bounds.center().x, hc.bounds.y0 - kCell * 0.5});
            hc.supportStructure = A->structureOf.get(below);
        }
    }

    // ---- 8. Gameplay timeline ----------------------------------------------
    {
        GameMode mode = header ? header->startMode() : GameMode::Cube;
        int speed = header ? header->startSpeed() : 0;
        bool flipped = header ? header->startFlipped() : false;
        bool mini = header ? header->startMini() : false;
        bool dual = header ? header->startDual() : false;
        auto portals = A->portals;
        std::stable_sort(portals.begin(), portals.end(), [](auto& a, auto& b) { return a.x < b.x; });
        double startX = std::min(0.0, A->levelBounds.isEmpty() ? 0.0 : A->levelBounds.x0);
        double endX = A->levelBounds.isEmpty() ? 0.0 : A->levelBounds.x1 + 10 * kBlock;
        GameplaySection cur{startX, endX, mode, speed, flipped, mini, dual};
        for (auto& p : portals) {
            GameplaySection next = cur;
            switch (p.type) {
                case PortalType::GameMode: next.mode = GameMode(p.value); break;
                case PortalType::Speed: next.speed = p.value; break;
                case PortalType::Gravity: next.flipped = p.value == 2 ? !cur.flipped : p.value == 1; break;
                case PortalType::Size: next.mini = p.value != 0; break;
                case PortalType::Dual: next.dual = p.value != 0; break;
                default: continue;
            }
            bool changed = next.mode != cur.mode || next.speed != cur.speed || next.flipped != cur.flipped ||
                           next.mini != cur.mini || next.dual != cur.dual;
            if (!changed) continue;
            cur.x1 = p.x;
            if (cur.x1 > cur.x0) A->sections.push_back(cur);
            next.x0 = p.x;
            cur = next;
        }
        cur.x1 = std::max(cur.x0 + kBlock, endX);
        A->sections.push_back(cur);

        // Per-section corridor tightness and densities.
        for (auto& sec : A->sections) {
            double len = std::max(kBlock, sec.x1 - sec.x0);
            int hazards = 0;
            for (auto idx : hazardObjects) {
                double x = in[idx].object.pos.x;
                if (x >= sec.x0 && x < sec.x1) ++hazards;
            }
            sec.hazardDensity = hazards / (len / kBlock);
            // Column scan for the largest bounded empty gap in each column.
            std::vector<double> gaps;
            int cx0 = int(std::floor(sec.x0 / kCell)), cx1 = int(std::floor(sec.x1 / kCell));
            int solidCount = 0;
            Cell bandLo = LevelAnalysis::cellAt({0, A->gameplayBounds.isEmpty() ? 0 : A->gameplayBounds.y0});
            Cell bandHi = LevelAnalysis::cellAt({0, A->gameplayBounds.isEmpty() ? 300 : A->gameplayBounds.y1});
            // For every column: the largest vertical gap the player could be in.
            // A column open to the sky (nothing above its last obstacle within
            // the gameplay band) is unconstrained.
            constexpr double kOpen = 20 * kBlock;
            for (int cx = std::max(cx0, A->cellBounds.x0 - 1); cx <= std::min(cx1, A->cellBounds.x1 + 1); ++cx) {
                int lastBlocked = -1;  // ground acts as a floor
                double best = -1;
                for (int cy = 0; cy <= bandHi.y + 1; ++cy) {
                    bool blocked = A->isSolid({cx, cy}) || A->isHazard({cx, cy});
                    if (blocked) {
                        ++solidCount;
                        int gap = cy - lastBlocked - 1;
                        if (gap > 0) best = std::max(best, gap * kCell);
                        lastBlocked = cy;
                    }
                }
                (void)bandLo;
                bool openSky = lastBlocked < bandHi.y + 1 && (bandHi.y + 1 - lastBlocked) * kCell >= 3 * kBlock;
                gaps.push_back(openSky ? kOpen : (best > 0 ? best : kOpen));
            }
            sec.solidDensity = solidCount / (len / kBlock);
            if (!gaps.empty()) {
                std::sort(gaps.begin(), gaps.end());
                sec.minClearance = gaps[gaps.size() / 10];
            }
            else sec.minClearance = 20 * kBlock;
            double modeWeight = 0.6;
            switch (sec.mode) {
                case GameMode::Wave: modeWeight = 1.0; break;
                case GameMode::Ship: case GameMode::Swing: modeWeight = 0.85; break;
                case GameMode::Ufo: case GameMode::Spider: modeWeight = 0.75; break;
                default: break;
            }
            // 2 blocks of clearance or less = fully tight, 8 blocks or more = relaxed.
            double clearanceTight = std::clamp((8 * kBlock - sec.minClearance) / (6 * kBlock), 0.0, 1.0);
            double hazardTight = std::clamp(sec.hazardDensity / 1.5, 0.0, 1.0);
            double speedTight = sec.speed >= 3 ? 0.15 : 0.0;
            sec.tightness = std::clamp(modeWeight * std::max(clearanceTight, 0.6 * hazardTight) + speedTight + (sec.mini ? 0.1 : 0), 0.0, 1.0);
        }
    }

    // ---- Stats --------------------------------------------------------------
    A->stats.structures = int(A->structures.size());
    A->stats.contours = int(A->contours.size());
    A->stats.segments = int(A->segments.size());
    A->stats.corners = int(A->corners.size());
    A->stats.staircases = int(A->staircases.size());
    A->stats.hazardClusters = int(A->hazardClusters.size());
    A->stats.sections = int(A->sections.size());
    A->stats.analysisMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return A;
}

}  // namespace gdds
