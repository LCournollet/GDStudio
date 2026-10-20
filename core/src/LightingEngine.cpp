// Lighting: glows and light accents. Glow layers are drawn BEHIND the
// gameplay blocks (see Generator::autoZ), so the light seems to emanate from
// the edges without ever covering blocks or hazards. Intensity is carried by
// the layer's color channels (blending + opacity), so it can be tuned later
// without regenerating anything.

#include "EngineUtil.hpp"

#include <algorithm>
#include <cmath>

namespace gdds {
namespace {

using namespace eng;

uint64_t segKey(ContourSegment const& s) { return hashOf(0x6c0, s.a.x, s.a.y, s.b.x, s.b.y, int(s.face)); }

void generateStructuralGlow(EngineContext& ctx) {
    auto& a = ctx.analysis;
    std::string style = ctx.layer.params.style.empty() ? "exposed" : ctx.layer.params.style;
    double size = ctx.layer.params.knob("size", 1.0) * kBlock * std::max(0.4, ctx.layer.params.scale);
    double minExposure = ctx.layer.params.knob("minExposure", 1.0) * kBlock;
    auto edge = ctx.pick({DecoShape::GlowEdge}, anchor::TopFace | anchor::SideFace | anchor::BottomFace, 0x9101, isStretchable);
    auto round = ctx.pick({DecoShape::GlowRound}, anchor::ConvexCorner, 0x9102);
    if (!edge) return;
    for (auto& seg : a.segments) {
        if (ctx.cancelled()) return;
        if (seg.onGround || seg.hole) continue;
        if (style == "top" && seg.face != Face::Top) continue;
        if (seg.exposure < std::min(minExposure, size)) continue;  // no room for a glow
        auto& sec = ctx.plan.at(seg.midpoint().x);
        // Variation of intensity along the level, with readability limits.
        Rng r = ctx.rng(segKey(seg));
        double variation = ctx.layer.params.knob("variation", 0.25);
        double opacity = (1.0 - variation + variation * r.uniform()) * (1.0 - 0.7 * sec.readability);
        // In tight passages the glow must leave most of the corridor clear.
        double h = std::min(size, seg.exposure * (0.8 - 0.5 * sec.readability));
        if (h < 6) continue;
        Vec2 n = faceNormal(seg.face);
        Vec2 dir = (seg.p1 - seg.p0) / seg.length;
        // Canonical glow: bright side down -> rotate so that it faces away from the surface.
        // (GD rotation is clockwise: CW 90 turns "down" into "left".)
        double rot = seg.face == Face::Top ? 0 : seg.face == Face::Bottom ? 180 : seg.face == Face::Left ? -90 : 90;
        // Split long faces into pieces (max 4 blocks) to keep objects local.
        int pieces = std::max(1, int(std::ceil(seg.length / (4 * kBlock))));
        for (int i = 0; i < pieces; ++i) {
            double s0 = seg.length * i / pieces, s1 = seg.length * (i + 1) / pieces;
            Vec2 mid = seg.p0 + dir * ((s0 + s1) / 2) + n * (h / 2);
            double along = s1 - s0 + (i + 1 < pieces ? 0.5 : 0);
            // Canonical frame: X along the face, Y away from it.
            double sx = along / edge.def->canonW(), sy = h / edge.def->canonH();
            Vec2 anchor = seg.p0 + dir * ((s0 + s1) / 2);
            if (!ctx.masks.allows(anchor)) continue;
            auto pl = ctx.place(edge, mid, rot, sx, sy, anchor, ColorRole::Glow, 0, PlacementTier::Secondary,
                                hashOf(segKey(seg), i), "edge-glow");
            pl.opacity *= opacity;
            ctx.emit(std::move(pl));
        }
    }
    // Convex corners: a round glow centred on the corner fills the gap between
    // the two perpendicular strips (the block hides the inner quarter).
    if (!round) return;
    for (auto& c : a.corners) {
        if (ctx.cancelled()) return;
        if (!c.convex || c.onGround) continue;
        auto& sIn = a.segments[c.segIn];
        auto& sOut = a.segments[c.segOut];
        if (style == "top" && sIn.face != Face::Top && sOut.face != Face::Top) continue;
        if (sIn.exposure < size * 0.5 || sOut.exposure < size * 0.5) continue;
        auto& sec = ctx.plan.at(c.pos.x);
        emitSized(ctx, round, c.pos, size * 2.0, 0, ColorRole::Glow, 1, PlacementTier::Secondary,
                  hashOf(0x9c, c.vertex.x, c.vertex.y), "corner-glow", 0.8 * (1.0 - 0.5 * sec.readability));
    }
}

void generateAmbientGlow(EngineContext& ctx) {
    auto& a = ctx.analysis;
    auto round = ctx.pick({DecoShape::GlowRound}, anchor::Any, 0xa301);
    if (!round) return;
    double minCells = ctx.layer.params.knob("minSize", 24);
    double spread = ctx.layer.params.knob("spread", 1.6) * std::max(0.4, ctx.layer.params.scale);
    for (auto& s : a.structures) {
        if (ctx.cancelled()) return;
        if (s.cellCount < minCells) continue;
        Vec2 c = s.bounds.center();
        auto& sec = ctx.plan.at(c.x);
        if (sec.readability > 0.7) continue;
        Rng r = ctx.rng(s.shapeSignature, 0xa3);  // repeated shapes get the same light
        if (!r.chance(std::clamp(ctx.density * 1.5, 0.0, 1.0))) continue;
        double size = std::clamp(std::max(s.bounds.width(), s.bounds.height()) * spread, 3 * kBlock, 24 * kBlock);
        // Long platforms: several glows along the length instead of one huge blob.
        int n = std::max(1, int(std::round(s.bounds.width() / (size * 0.9))));
        double gsize = std::max(size / n * 1.6, std::min(size, s.bounds.height() * spread * 2));
        for (int i = 0; i < n; ++i) {
            Vec2 p{s.bounds.x0 + s.bounds.width() * (i + 0.5) / n, c.y};
            emitSized(ctx, round, p, gsize, 0, ColorRole::Glow, 0, PlacementTier::Primary, hashOf(s.signature, i),
                      "ambient", ctx.layer.params.knob("opacity", 0.35));
        }
    }
}

void generateHighlights(EngineContext& ctx) {
    auto& a = ctx.analysis;
    std::string style = ctx.layer.params.style.empty() ? "corners" : ctx.layer.params.style;
    auto spark = ctx.pick({DecoShape::Ornament, DecoShape::Dot}, anchor::ConvexCorner, 0x4801,
                          [](DecoDef const& d) { return d.shape == DecoShape::Dot || d.hasTag("sparkle"); });
    auto glow = ctx.pick({DecoShape::GlowRound}, anchor::ConvexCorner, 0x4802);
    double size = ctx.layer.params.knob("size", 10) * std::max(0.4, ctx.layer.params.scale);
    if (!spark) return;
    for (auto& c : a.corners) {
        if (ctx.cancelled()) return;
        if (!c.convex || c.onGround) continue;
        // Only the upper corners catch the light.
        if (c.quadrant != 2 && c.quadrant != 3) continue;
        auto& sec = ctx.plan.at(c.pos.x);
        if (sec.readability > 0.5) continue;
        Rng r = ctx.rng(0x48, c.vertex.x, c.vertex.y);
        if (!r.chance(std::clamp(ctx.density * sec.detailBudget, 0.0, 1.0))) continue;
        if (a.hazardDistance(c.pos, 45) < 40) continue;  // never sparkle next to a hazard
        uint64_t key = hashOf(0x49, c.vertex.x, c.vertex.y);
        emitSized(ctx, spark, c.pos, size, 0, ColorRole::Accent, 1, PlacementTier::Micro, key, "highlight", 0.9);
        if (glow) emitSized(ctx, glow, c.pos, size * 3, 0, ColorRole::Glow, 0, PlacementTier::Micro, hashOf(key, 1),
                            "highlight-glow", 0.5);
    }
    if (style != "segment-ends") return;
    for (auto& seg : a.segments) {
        if (seg.face != Face::Top || seg.onGround || seg.length < 4 * kBlock) continue;
        Vec2 p = seg.midpoint() + Vec2{0, 2};
        Rng r = ctx.rng(segKey(seg), 0x4a);
        if (!r.chance(ctx.density * 0.6)) continue;
        emitSized(ctx, spark, p, size * 0.7, 0, ColorRole::Accent, 1, PlacementTier::Micro, hashOf(segKey(seg), 0x4b),
                  "highlight-mid", 0.7);
    }
}

class LightingEngine : public Engine {
public:
    void generate(EngineContext& ctx) override {
        switch (ctx.layer.kind) {
            case LayerKind::StructuralGlow: generateStructuralGlow(ctx); break;
            case LayerKind::AmbientGlow: generateAmbientGlow(ctx); break;
            case LayerKind::Highlights: generateHighlights(ctx); break;
            default: break;
        }
    }
};

}  // namespace

std::unique_ptr<Engine> makeLightingEngine() { return std::make_unique<LightingEngine>(); }

}  // namespace gdds
