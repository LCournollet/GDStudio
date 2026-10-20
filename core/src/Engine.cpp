#include <gdds/Engine.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace gdds {

std::vector<EngineContext::Pick> EngineContext::candidates(std::initializer_list<DecoShape> shapes, uint32_t anchors,
                                                           std::function<bool(DecoDef const&)> const& filter) const {
    std::vector<Pick> out;
    auto shapeOk = [&](DecoShape s) { return std::find(shapes.begin(), shapes.end(), s) != shapes.end(); };
    if (!layer.objects.empty()) {
        for (auto& rule : layer.objects) {
            if (!rule.enabled || rule.weight <= 0) continue;
            auto def = catalog.find(rule.id);
            if (!def || catalog.isDisabled(rule.id)) continue;
            if (!shapeOk(def->shape)) continue;
            if ((rule.anchors & anchors) == 0) continue;
            if (filter && !filter(*def)) continue;
            out.push_back({def, &rule});
        }
        return out;
    }
    for (auto& def : catalog.all()) {
        if (!def.hasTag("default") || catalog.isDisabled(def.id)) continue;
        if (!shapeOk(def.shape)) continue;
        if (filter && !filter(def)) continue;
        out.push_back({&def, nullptr});
    }
    return out;
}

EngineContext::Pick EngineContext::pick(std::initializer_list<DecoShape> shapes, uint32_t anchors, uint64_t key,
                                        std::function<bool(DecoDef const&)> const& filter) const {
    auto cands = candidates(shapes, anchors, filter);
    if (cands.empty()) return {};
    std::vector<double> weights;
    for (auto& c : cands) weights.push_back(c.rule ? c.rule->weight : 1.0);
    Rng r(hashOf(seed, 0x9e11, key));
    int idx = r.weighted(weights);
    return idx < 0 ? Pick{} : cands[size_t(idx)];
}

Rect EngineContext::visualBounds(DecoDef const& def, Vec2 visualCenter, double rotation, double scaleX, double scaleY) {
    // scaleX/scaleY and rotation are given in the canonical frame.
    OrientedRect r{visualCenter, def.canonW() * std::abs(scaleX) / 2, def.canonH() * std::abs(scaleY) / 2, rotation};
    return r.bounds();
}

Placement EngineContext::place(Pick const& p, Vec2 visualCenter, double rotation, double scaleX, double scaleY,
                               Vec2 anchorPoint, ColorRole role, int zOffset, PlacementTier tier, uint64_t key,
                               char const* source) const {
    auto const& def = *p.def;
    Placement pl;
    LevelObject o;
    o.id = def.id;
    // Convert canonical-frame scale/flip into the sprite's native frame.
    bool qt = def.quarterTurned();
    bool cfx = scaleX < 0, cfy = scaleY < 0;
    double nsx = std::abs(qt ? scaleY : scaleX), nsy = std::abs(qt ? scaleX : scaleY);
    bool flipX = qt ? cfy : cfx, flipY = qt ? cfx : cfy;
    double total = rotation + def.baseRotation;
    Vec2 vc{def.visCenter().x * nsx * (flipX ? -1 : 1), def.visCenter().y * nsy * (flipY ? -1 : 1)};
    o.pos = visualCenter - vc.rotatedCW(total);
    o.pos = {std::round(o.pos.x * 1000) / 1000, std::round(o.pos.y * 1000) / 1000};
    double rot = std::fmod(total, 360.0);
    if (rot < 0) rot += 360;
    if (std::abs(rot - 360) < 1e-6) rot = 0;
    o.rotation = std::round(rot * 1000) / 1000;
    o.scaleX = std::round(nsx * 10000) / 10000;
    o.scaleY = std::round(nsy * 10000) / 10000;
    o.flipX = flipX;
    o.flipY = flipY;
    double sx = std::abs(scaleX), sy = std::abs(scaleY);
    ZSlot zs = zFromRank(zRank(z.zLayer, z.zOrder) + zOffset);
    o.zLayer = zs.zLayer;
    o.zOrder = zs.zOrder;
    o.hasZOrder = true;
    pl.object = std::move(o);
    pl.layerId = layer.id;
    pl.role = role;
    if (p.rule && !p.rule->colors.empty()) {
        Rng r(hashOf(seed, 0xc010, key));
        pl.role = p.rule->colors[size_t(r.rangeInt(0, int(p.rule->colors.size()) - 1))];
    }
    pl.opacity = p.rule ? p.rule->opacity : 1.0;
    pl.anchor = anchorPoint;
    pl.footprint = visualBounds(def, visualCenter, rotation, sx, sy);
    pl.tier = tier;
    pl.priority = tier == PlacementTier::Primary ? 300 : tier == PlacementTier::Secondary ? 200 : 100;
    pl.key = hashOf(seed, key);
    pl.source = source;
    return pl;
}

// ---------------------------------------------------------------------------

std::vector<CellRect> decomposeRects(std::vector<Cell> cells, int maxW, int maxH) {
    std::sort(cells.begin(), cells.end(), [](Cell a, Cell b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
    std::unordered_set<Cell, CellHash> remaining(cells.begin(), cells.end());
    std::vector<CellRect> out;
    for (auto c : cells) {
        if (!remaining.count(c)) continue;
        int x1 = c.x;
        while (x1 + 1 - c.x + 1 <= maxW && remaining.count({x1 + 1, c.y})) ++x1;
        int y1 = c.y;
        while (y1 + 1 - c.y + 1 <= maxH) {
            bool full = true;
            for (int x = c.x; x <= x1 && full; ++x) full = remaining.count({x, y1 + 1}) != 0;
            if (!full) break;
            ++y1;
        }
        for (int y = c.y; y <= y1; ++y)
            for (int x = c.x; x <= x1; ++x) remaining.erase({x, y});
        out.push_back({c.x, c.y, x1, y1});
    }
    return out;
}

namespace {
Cell stepDir(Cell c, Dir d) {
    switch (d) {
        case Dir::East: return {c.x + 1, c.y};
        case Dir::North: return {c.x, c.y + 1};
        case Dir::West: return {c.x - 1, c.y};
        case Dir::South: return {c.x, c.y - 1};
    }
    return c;
}
Face faceOf(Dir d) {
    switch (d) {
        case Dir::East: return Face::Bottom;
        case Dir::North: return Face::Right;
        case Dir::West: return Face::Top;
        case Dir::South: return Face::Left;
    }
    return Face::Top;
}
}  // namespace

std::vector<TracedLoop> traceLoops(std::vector<Cell> const& cells) {
    std::unordered_set<Cell, CellHash> set(cells.begin(), cells.end());
    struct Edge { Cell from; Dir dir; };
    std::vector<Edge> edges;
    for (auto c : cells) {
        if (!set.count({c.x, c.y - 1})) edges.push_back({{c.x, c.y}, Dir::East});
        if (!set.count({c.x + 1, c.y})) edges.push_back({{c.x + 1, c.y}, Dir::North});
        if (!set.count({c.x, c.y + 1})) edges.push_back({{c.x + 1, c.y + 1}, Dir::West});
        if (!set.count({c.x - 1, c.y})) edges.push_back({{c.x, c.y + 1}, Dir::South});
    }
    std::unordered_map<Cell, std::vector<int>, CellHash> outgoing;
    for (int i = 0; i < int(edges.size()); ++i) outgoing[edges[i].from].push_back(i);
    std::vector<int> order(edges.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        auto& ea = edges[a].from; auto& eb = edges[b].from;
        if (ea.y != eb.y) return ea.y < eb.y;
        if (ea.x != eb.x) return ea.x < eb.x;
        return int(edges[a].dir) < int(edges[b].dir);
    });
    std::vector<char> used(edges.size(), 0);
    std::vector<TracedLoop> loops;
    auto left = [](Dir d) { return Dir((int(d) + 1) % 4); };
    auto right = [](Dir d) { return Dir((int(d) + 3) % 4); };
    for (int start : order) {
        if (used[start]) continue;
        std::vector<int> loop;
        int e = start;
        while (e >= 0 && !used[e]) {
            used[e] = 1;
            loop.push_back(e);
            Cell end = stepDir(edges[e].from, edges[e].dir);
            int next = -1;
            auto it = outgoing.find(end);
            if (it != outgoing.end()) {
                for (Dir want : {left(edges[e].dir), edges[e].dir, right(edges[e].dir)}) {
                    for (int cand : it->second)
                        if (!used[cand] && edges[cand].dir == want) { next = cand; break; }
                    if (next >= 0) break;
                }
            }
            e = next;
        }
        if (loop.size() < 4) continue;
        size_t n = loop.size(), rot = 0;
        for (size_t k = 0; k < n; ++k)
            if (edges[loop[k]].dir != edges[loop[(k + n - 1) % n]].dir) { rot = k; break; }
        std::rotate(loop.begin(), loop.begin() + long(rot), loop.end());
        TracedLoop tl;
        double area2 = 0;
        for (size_t k = 0; k < n; ++k) {
            Cell a = edges[loop[k]].from, b = stepDir(a, edges[loop[k]].dir);
            area2 += double(a.x) * b.y - double(b.x) * a.y;
        }
        tl.hole = area2 < 0;
        std::vector<Dir> dirs;
        size_t k = 0;
        while (k < n) {
            Dir d = edges[loop[k]].dir;
            Cell a = edges[loop[k]].from;
            size_t j = k;
            while (j + 1 < n && edges[loop[j + 1]].dir == d) ++j;
            Cell b = stepDir(edges[loop[j]].from, d);
            tl.segments.push_back({LevelAnalysis::vertexPos(a), LevelAnalysis::vertexPos(b), faceOf(d)});
            dirs.push_back(d);
            k = j + 1;
        }
        size_t m = tl.segments.size();
        for (size_t i = 0; i < m; ++i) {
            bool convex = dirs[(i + 1) % m] == left(dirs[i]);
            tl.segments[i].convexEnd = convex;
            tl.segments[(i + 1) % m].convexStart = convex;
        }
        loops.push_back(std::move(tl));
    }
    return loops;
}

}  // namespace gdds
