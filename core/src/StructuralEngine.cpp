// Structural Decoration Engine: decoration that follows the gameplay geometry.
//
// Every generator works on whole structures and whole contour segments (from
// the analysis), never on individual blocks, so a 50-block platform receives
// one continuous design with consistent rhythm and clean joints.

#include "EngineUtil.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_set>

namespace gdds {
namespace {

using namespace eng;

uint64_t segKey(ContourSegment const& s) {
    return hashOf(0x5e6, s.a.x, s.a.y, s.b.x, s.b.y, int(s.face));
}

bool segmentOnSlope(LevelAnalysis const& a, ContourSegment const& s) {
    Vec2 inward = faceNormal(s.face) * -1.0;
    Vec2 dir = (s.p1 - s.p0) / std::max(1e-9, s.length);
    int samples = std::max(1, int(s.length / kCell));
    int slope = 0;
    for (int i = 0; i < samples; ++i) {
        Vec2 p = s.p0 + dir * ((i + 0.5) * kCell) + inward * (kCell * 0.5);
        if (cellIsSlope(a, p)) ++slope;
    }
    return slope * 2 > samples;
}

bool structureVisible(EngineContext const& ctx, Structure const& s) { return regionMayAllow(ctx, s.bounds); }

// -----------------------------------------------------------------------------
// Block fill

void generateFill(EngineContext& ctx) {
    auto& a = ctx.analysis;
    auto pick = ctx.pick({DecoShape::Square}, anchor::Interior, 0xf111, isStretchable);
    if (!pick) return;
    std::string style = ctx.layer.params.style.empty() ? "solid" : ctx.layer.params.style;
    int bands = style == "depth" ? int(ctx.layer.params.knob("bands", 3)) : 0;
    double shade = ctx.layer.params.knob("shade", 0.18);  // brightness drop per band
    int maxCells = int(ctx.layer.params.knob("maxRect", 12));
    for (auto& s : a.structures) {
        if (ctx.cancelled()) return;
        if (!structureVisible(ctx, s)) continue;
        std::vector<Cell> cells;
        cells.reserve(s.cells.size());
        for (auto c : s.cells)
            if (!(a.flags.get(c) & cellflag::Slope)) cells.push_back(c);
        if (cells.empty()) continue;

        auto emitCells = [&](std::vector<Cell> const& set, double brightness, uint64_t salt) {
            std::unordered_set<Cell, CellHash> inSet(set.begin(), set.end());
            for (auto& r : decomposeRects(set, maxCells, maxCells)) {
                Rect w = r.world();
                // Overlap internal edges slightly to avoid hairline seams.
                auto internal = [&](int x0, int y0, int x1, int y1) {
                    for (int y = y0; y <= y1; ++y)
                        for (int x = x0; x <= x1; ++x)
                            if (!a.isSolid({x, y})) return false;
                    return true;
                };
                double e = 0.3;
                if (internal(r.x0 - 1, r.y0, r.x0 - 1, r.y1)) w.x0 -= e;
                if (internal(r.x1 + 1, r.y0, r.x1 + 1, r.y1)) w.x1 += e;
                if (internal(r.x0, r.y0 - 1, r.x1, r.y0 - 1)) w.y0 -= e;
                if (internal(r.x0, r.y1 + 1, r.x1, r.y1 + 1)) w.y1 += e;
                std::optional<HSV> hsv;
                if (brightness < 0.999) hsv = HSV{0, 1, std::round(brightness * 100) / 100, false, false};
                emitRect(ctx, pick, w, ColorRole::Primary, 0, PlacementTier::Primary,
                         hashOf(salt, r.x0, r.y0, r.x1, r.y1), "fill", 1.0, hsv);
            }
        };

        if (bands <= 0) {
            emitCells(cells, 1.0, 0xf1);
            continue;
        }
        // Depth bands: the boundary ring keeps the primary color, deeper
        // rings get progressively darker -> inner shading.
        std::map<int, std::vector<Cell>> byBand;
        for (auto c : cells) {
            int d = std::max(1, int(a.depth.get(c)));
            int band = std::min(d, bands);
            byBand[band].push_back(c);
        }
        for (auto& [band, set] : byBand) emitCells(set, std::max(0.25, 1.0 - shade * (band - 1)), 0xf2 + band);
    }
}

// -----------------------------------------------------------------------------
// Outlines

EngineContext::Pick pickLine(EngineContext const& ctx, double thickness, uint64_t key) {
    // Prefer thin sprites for thin strokes, thick ones for bold strokes.
    auto thin = ctx.pick({DecoShape::Line}, anchor::Any, key, [&](DecoDef const& d) {
        return d.stretchable && (thickness <= 4 ? !d.hasTag("thick") : true);
    });
    if (thin) return thin;
    return ctx.pick({DecoShape::Line}, anchor::Any, key, isStretchable);
}

void generateOutline(EngineContext& ctx) {
    auto& a = ctx.analysis;
    double T = ctx.layer.params.knob("thickness", 3.0) * std::max(0.5, ctx.layer.params.scale);
    bool holes = ctx.layer.params.knob("holes", 1) > 0;
    auto line = pickLine(ctx, T, 0x0071);
    if (!line) return;
    for (auto& seg : a.segments) {
        if (ctx.cancelled()) return;
        if (seg.onGround || (seg.hole && !holes)) continue;
        if (!regionMayAllow(ctx, Rect{std::min(seg.p0.x, seg.p1.x), std::min(seg.p0.y, seg.p1.y),
                                      std::max(seg.p0.x, seg.p1.x), std::max(seg.p0.y, seg.p1.y)}))
            continue;
        if (segmentOnSlope(a, seg)) continue;
        bool convexStart = seg.cornerStart >= 0 ? a.corners[seg.cornerStart].convex : true;
        bool convexEnd = seg.cornerEnd >= 0 ? a.corners[seg.cornerEnd].convex : true;
        // Corners lying on the ground line are not visible joints.
        Vec2 dir = (seg.p1 - seg.p0) / seg.length;
        Vec2 inward = faceNormal(seg.face) * -1.0;
        double e0 = convexStart ? 0 : T, e1 = convexEnd ? 0 : T;
        Vec2 p0 = seg.p0 - dir * e0 + inward * (T / 2);
        Vec2 p1 = seg.p1 + dir * e1 + inward * (T / 2);
        emitLine(ctx, line, p0, p1, T, ColorRole::Secondary, 0, PlacementTier::Primary, segKey(seg), "outline", 1.0,
                 ctx.layer.params.knob("maxPiece", 180), seg.midpoint());
    }
    // Slopes: one diagonal stroke along each exposed slope surface.
    for (uint32_t i = 0; i < a.inputs.size(); ++i) {
        auto& fp = a.footprints[i];
        if (a.inputs[i].ignore || !fp.slope || a.inputs[i].info.kind != ObjectKind::Solid) continue;
        auto t = *fp.slope;
        Vec2 v[3] = {t.a, t.b, t.c};
        for (int k = 0; k < 3; ++k) {
            Vec2 p = v[k], q = v[(k + 1) % 3];
            Vec2 d = q - p;
            if (std::abs(d.x) < 1e-6 || std::abs(d.y) < 1e-6) continue;  // axis aligned legs
            Vec2 mid = (p + q) * 0.5;
            Vec2 n{-d.y, d.x};
            n = n / n.length();
            Vec2 third = v[(k + 2) % 3];
            if ((third - mid).dot(n) > 0) n = n * -1.0;  // n points out of the triangle
            if (a.isSolid(LevelAnalysis::cellAt(mid + n * (kCell * 0.6)))) continue;  // covered
            Vec2 off = n * (-T / 2);
            emitLine(ctx, line, p + off, q + off, T, ColorRole::Secondary, 0, PlacementTier::Primary,
                     hashOf(0x51, quantize(mid.x), quantize(mid.y)), "outline-slope", 1.0, 180, mid);
        }
    }
}

// -----------------------------------------------------------------------------
// Corners and connections

void generateCorners(EngineContext& ctx) {
    auto& a = ctx.analysis;
    std::string style = ctx.layer.params.style.empty() ? "brackets" : ctx.layer.params.style;
    double A = ctx.layer.params.knob("size", 12) * std::max(0.5, ctx.layer.params.scale);
    double inset = ctx.layer.params.knob("inset", 5);
    bool concave = ctx.layer.params.knob("concave", 1) > 0;
    double density = std::clamp(ctx.density * 1.6, 0.0, 1.0);

    auto bracket = ctx.pick({DecoShape::Corner}, anchor::ConvexCorner, 0xc0c0);
    auto square = ctx.pick({DecoShape::Square, DecoShape::Dot}, anchor::ConvexCorner, 0xc0c1,
                           [](DecoDef const& d) { return d.shape == DecoShape::Dot || d.hasTag("small"); });
    auto dot = ctx.pick({DecoShape::Dot, DecoShape::Circle}, anchor::ConcaveCorner, 0xc0c2);

    for (auto& c : a.corners) {
        if (ctx.cancelled()) return;
        if (c.onGround) continue;
        if (!ctx.masks.allows(c.pos)) continue;
        auto& sIn = a.segments[c.segIn];
        auto& sOut = a.segments[c.segOut];
        if (segmentOnSlope(a, sIn) || segmentOnSlope(a, sOut)) continue;
        double minLen = std::min(sIn.length, sOut.length);
        auto r = ctx.rng(0xc0, c.vertex.x, c.vertex.y);
        if (!r.chance(density)) continue;
        uint64_t key = hashOf(0xc1, c.vertex.x, c.vertex.y, int(c.convex));
        auto& sec = ctx.plan.at(c.pos.x);
        if (sec.readability > 0.85) continue;  // keep extremely tight passages clean

        if (c.convex) {
            if (minLen < A + inset + 4) continue;  // too short: bracket would swallow the face
            Vec2 diag = quadrantDir(c.quadrant);
            double rot = -90.0 * c.quadrant;
            if (style == "brackets" && bracket) {
                Vec2 center = c.pos + Vec2{diag.x * (inset + A / 2), diag.y * (inset + A / 2)};
                emitSized(ctx, bracket, center, A, rot, ColorRole::Accent, 0, PlacementTier::Secondary, key, "corner", 1.0,
                          false, false, c.pos);
            }
            else if (square) {
                double s = A * 0.45;
                Vec2 center = c.pos + Vec2{diag.x * (inset + s / 2), diag.y * (inset + s / 2)};
                emitSized(ctx, square, center, s, 0, ColorRole::Accent, 0, PlacementTier::Secondary, key, "corner", 1.0,
                          false, false, c.pos);
            }
        }
        else if (concave && dot) {
            if (minLen < 2 * kBlock) continue;
            // The solid occupies the three other quadrants: accent diagonally
            // opposite to the empty one, just inside the inner corner.
            Vec2 diag = quadrantDir((c.quadrant + 2) % 4);
            double s = A * 0.35;
            Vec2 center = c.pos + Vec2{diag.x * (inset + s / 2), diag.y * (inset + s / 2)};
            emitSized(ctx, dot, center, s, 0, ColorRole::Accent, 0, PlacementTier::Micro, key, "corner-inner", 0.9,
                      false, false, c.pos);
        }
    }
}

// -----------------------------------------------------------------------------
// Internal textures

void generateTexture(EngineContext& ctx) {
    auto& a = ctx.analysis;
    std::string style = ctx.layer.params.style.empty() ? "inset" : ctx.layer.params.style;
    int k = std::max(1, int(ctx.layer.params.knob("inset", 2)));  // cells kept clear from the edge
    double T = ctx.layer.params.knob("thickness", 1.5) * std::max(0.5, ctx.layer.params.scale);
    double opacity = ctx.layer.params.knob("opacity", 0.85);
    double period = ctx.layer.params.knob("period", 2) * kBlock;
    auto line = pickLine(ctx, T, 0x7e71);
    auto dot = ctx.pick({DecoShape::Dot, DecoShape::Square}, anchor::Interior, 0x7e72,
                        [](DecoDef const& d) { return d.shape == DecoShape::Dot || d.hasTag("small"); });

    for (auto& s : a.structures) {
        if (ctx.cancelled()) return;
        if (!structureVisible(ctx, s) || s.maxDepth <= k) continue;
        auto& sec = ctx.plan.at(s.bounds.center().x);
        if (sec.readability > 0.9 && style != "inset") continue;
        std::vector<Cell> interior;
        for (auto c : s.cells)
            if (a.depth.get(c) > k && !(a.flags.get(c) & cellflag::Slope)) interior.push_back(c);
        if (interior.size() < 4) continue;
        std::unordered_set<Cell, CellHash> in(interior.begin(), interior.end());
        uint64_t sk = hashOf(0x7e, s.signature);

        if (style == "inset" || style == "circuit") {
            if (!line) continue;
            for (auto& loop : traceLoops(interior)) {
                for (size_t i = 0; i < loop.segments.size(); ++i) {
                    auto& ts = loop.segments[i];
                    if (ts.face == Face::Bottom && std::max(ts.p0.y, ts.p1.y) <= 0.01) continue;  // on the ground
                    Vec2 d = ts.p1 - ts.p0;
                    double len = d.length();
                    Vec2 dir = d / len;
                    Vec2 inward = faceNormal(ts.face) * -1.0;
                    double e0 = ts.convexStart ? 0 : T, e1 = ts.convexEnd ? 0 : T;
                    Vec2 p0 = ts.p0 - dir * e0 + inward * (T / 2);
                    Vec2 p1 = ts.p1 + dir * e1 + inward * (T / 2);
                    uint64_t key = hashOf(sk, quantize(ts.p0.x), quantize(ts.p0.y), quantize(ts.p1.x), quantize(ts.p1.y));
                    emitLine(ctx, line, p0, p1, T, ColorRole::Detail, 0, PlacementTier::Secondary, key, "inset", opacity,
                             180, (ts.p0 + ts.p1) * 0.5);
                    if (style != "circuit" || !ts.convexStart || ts.face == Face::Bottom || len < 3 * kBlock) continue;
                    // Circuit stubs: short traces leaving the inset line inward, ending in a pad.
                    double stub = ctx.layer.params.knob("stub", 1) * kBlock;
                    int n = int((len - kBlock) / (2 * kBlock));
                    for (int j = 1; j <= n; ++j) {
                        auto r = ctx.rng(key, j);
                        if (!r.chance(0.45 * ctx.density * 2)) continue;
                        Vec2 base = ts.p0 + dir * (j * 2 * kBlock) + inward * T;
                        Vec2 tip = base + inward * stub;
                        bool fits = true;
                        for (double t = 0; t <= stub + kCell; t += kCell / 2)
                            if (!in.count(LevelAnalysis::cellAt(base + inward * t))) { fits = false; break; }
                        if (!fits) continue;
                        emitLine(ctx, line, base, tip, T, ColorRole::Detail, 1, PlacementTier::Micro, hashOf(key, j, 1),
                                 "circuit", opacity, 180, base);
                        if (dot) emitSized(ctx, dot, tip, T * 3, 0, ColorRole::Accent, 2, PlacementTier::Micro,
                                           hashOf(key, j, 2), "circuit-pad", 1.0, false, false, base);
                    }
                }
            }
        }
        else if (style == "grid") {
            if (!dot) continue;
            double size = ctx.layer.params.knob("dotSize", 4);
            Rect b = s.bounds;
            for (double x = std::floor(b.x0 / period) * period + period / 2; x < b.x1; x += period)
                for (double y = std::floor(b.y0 / period) * period + period / 2; y < b.y1; y += period) {
                    Vec2 p{x, y};
                    Cell c = LevelAnalysis::cellAt(p);
                    bool ok = in.count(c) && in.count({c.x + 1, c.y}) && in.count({c.x - 1, c.y}) &&
                              in.count({c.x, c.y + 1}) && in.count({c.x, c.y - 1});
                    if (!ok) continue;
                    emitSized(ctx, dot, p, size, 0, ColorRole::Detail, 0, PlacementTier::Micro,
                              hashOf(0x6d, quantize(x), quantize(y)), "grid", opacity * 0.7);
                }
        }
        else if (style == "stripes") {
            if (!line) continue;
            int every = std::max(1, int(ctx.layer.params.knob("every", 2)));
            // Diagonal strokes over 2x2-cell squares, aligned on the world grid so
            // they join into continuous stripes across the whole structure.
            for (auto c : interior) {
                if ((c.x & 1) || (c.y & 1)) continue;
                int bx = c.x / 2, by = c.y / 2;
                if (((bx - by) % every + every) % every != 0) continue;
                if (!in.count({c.x + 1, c.y}) || !in.count({c.x, c.y + 1}) || !in.count({c.x + 1, c.y + 1})) continue;
                Vec2 p0 = LevelAnalysis::vertexPos(c), p1 = LevelAnalysis::vertexPos({c.x + 2, c.y + 2});
                emitLine(ctx, line, p0, p1, T, ColorRole::Detail, 0, PlacementTier::Micro, hashOf(0x57, c.x, c.y), "stripes",
                         opacity * 0.5, 120, (p0 + p1) * 0.5);
            }
        }
        else if (style == "panels") {
            if (!line) continue;
            double P = ctx.layer.params.knob("period", 4) * kBlock;
            // Vertical seams at world-aligned positions, over full interior runs.
            for (double x = std::ceil(s.bounds.x0 / P) * P; x < s.bounds.x1; x += P) {
                int cx = int(std::round(x / kCell));
                int y0 = s.cellBounds.y0, y1 = s.cellBounds.y1;
                int runStart = INT32_MIN;
                for (int cy = y0; cy <= y1 + 1; ++cy) {
                    bool ok = cy <= y1 && in.count({cx, cy}) && in.count({cx - 1, cy});
                    if (ok && runStart == INT32_MIN) runStart = cy;
                    if (!ok && runStart != INT32_MIN) {
                        if (cy - runStart >= 2) {
                            Vec2 p0{x, runStart * kCell}, p1{x, cy * kCell};
                            emitLine(ctx, line, p0, p1, T, ColorRole::Detail, 0, PlacementTier::Secondary,
                                     hashOf(0x9a, quantize(x), runStart), "panel", opacity, 180, (p0 + p1) * 0.5);
                        }
                        runStart = INT32_MIN;
                    }
                }
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Rhythmic details along faces

void generateDetails(EngineContext& ctx) {
    auto& a = ctx.analysis;
    std::string style = ctx.layer.params.style.empty() ? "rivets" : ctx.layer.params.style;
    int faces = int(ctx.layer.params.knob("faces", 1));  // 1 top, 2 bottom, 4 sides
    double offset = ctx.layer.params.knob("offset", 9);
    double size = ctx.layer.params.knob("size", 4.5) * std::max(0.5, ctx.layer.params.scale);
    double fixedSpacing = ctx.layer.params.knob("spacing", 0) * kBlock;

    auto dot = ctx.pick({DecoShape::Dot, DecoShape::Square, DecoShape::Circle}, anchor::TopFace | anchor::SideFace, 0xde01,
                        [&](DecoDef const& d) { return d.shape != DecoShape::Square || d.hasTag("small"); });
    auto glow = ctx.pick({DecoShape::GlowRound}, anchor::Any, 0xde02);
    if (!dot) return;

    for (auto& seg : a.segments) {
        if (ctx.cancelled()) return;
        if (seg.onGround || seg.length < 2 * kBlock) continue;
        int bit = seg.face == Face::Top ? 1 : seg.face == Face::Bottom ? 2 : 4;
        if (!(faces & bit)) continue;
        if (segmentOnSlope(a, seg)) continue;
        auto& sec = ctx.plan.at(seg.midpoint().x);
        // Keep or drop a whole face so the rhythm stays regular along it.
        auto r = ctx.rng(segKey(seg), 0xd1);
        if (!r.chance(std::clamp(ctx.density * 1.4 * sec.detailBudget, 0.0, 1.0))) continue;
        double spacing = fixedSpacing > 0 ? fixedSpacing : std::max(2 * kBlock, sec.rhythm / 2);
        double margin = ctx.layer.params.knob("margin", 12);
        double usable = seg.length - 2 * margin;
        if (usable < 0) continue;
        int n = int(std::floor(usable / spacing)) + 1;
        double start = margin + (usable - (n - 1) * spacing) / 2;  // symmetric distribution
        Vec2 dir = (seg.p1 - seg.p0) / seg.length;
        Vec2 inward = faceNormal(seg.face) * -1.0;
        for (int i = 0; i < n; ++i) {
            Vec2 p = seg.p0 + dir * (start + i * spacing) + inward * offset;
            Cell c = LevelAnalysis::cellAt(p);
            if (!a.isSolid(c) || a.isHazard(c)) continue;
            uint64_t key = hashOf(segKey(seg), i);
            if (style == "lights") {
                emitSized(ctx, dot, p, size, 0, ColorRole::Accent, 1, PlacementTier::Micro, key, "light", 1.0);
                if (glow) emitSized(ctx, glow, p, size * 4.5, 0, ColorRole::Glow, 0, PlacementTier::Micro, hashOf(key, 7),
                                    "light-glow", 0.6, false, false, p);
            }
            else {
                emitSized(ctx, dot, p, size, 0, style == "studs" ? ColorRole::Detail : ColorRole::Accent, 0,
                          PlacementTier::Micro, key, style == "studs" ? "stud" : "rivet", 0.95);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Obstacle accents (behind hazards, never covering them)

void generateObstacleAccents(EngineContext& ctx) {
    auto& a = ctx.analysis;
    auto edge = ctx.pick({DecoShape::GlowEdge}, anchor::Hazard, 0x0b01, isStretchable);
    auto round = ctx.pick({DecoShape::GlowRound}, anchor::Hazard, 0x0b02);
    double height = ctx.layer.params.knob("height", 0.7) * kBlock;
    double opacity = ctx.layer.params.knob("opacity", 0.55);
    for (auto& hc : a.hazardClusters) {
        if (ctx.cancelled()) return;
        Vec2 center = hc.bounds.center();
        if (!ctx.masks.allows(center)) continue;
        auto& sec = ctx.plan.at(center.x);
        double op = opacity * (1.0 - 0.4 * sec.readability);
        if (hc.spikes > 0 && edge && hc.supportStructure >= 0) {
            double y = hc.bounds.y0;
            Rect strip{hc.bounds.x0 - 4, y, hc.bounds.x1 + 4, y + height};
            emitRect(ctx, edge, strip, ColorRole::Glow, 0, PlacementTier::Secondary, hashOf(0x0b, quantize(center.x), quantize(y)),
                     "spike-base-glow", op, std::nullopt, center);
        }
        if (hc.saws > 0 && round) {
            for (auto idx : hc.objects) {
                if (a.inputs[idx].info.shape != HitboxShape::Circle) continue;
                auto& fp = a.footprints[idx];
                double size = std::max(fp.visual.width(), fp.visual.height()) * 1.35;
                emitSized(ctx, round, fp.visual.center(), size, 0, ColorRole::Glow, 0, PlacementTier::Secondary,
                          hashOf(0x0c, quantize(fp.visual.center().x), quantize(fp.visual.center().y)), "saw-halo", op * 0.7);
            }
        }
    }
}

class StructuralEngine : public Engine {
public:
    void generate(EngineContext& ctx) override {
        switch (ctx.layer.kind) {
            case LayerKind::BlockFill: generateFill(ctx); break;
            case LayerKind::Outline: generateOutline(ctx); break;
            case LayerKind::Corners: generateCorners(ctx); break;
            case LayerKind::InternalTexture: generateTexture(ctx); break;
            case LayerKind::StructuralDetails: generateDetails(ctx); break;
            case LayerKind::ObstacleAccents: generateObstacleAccents(ctx); break;
            default: break;
        }
    }
};

}  // namespace

std::unique_ptr<Engine> makeStructuralEngine() { return std::make_unique<StructuralEngine>(); }

}  // namespace gdds
