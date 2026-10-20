// Artistic Decoration Engine: background compositions, atmosphere and
// foreground. Placement is driven by the composition plan (rhythm, density
// budget, readability of each section) and by the free-space analysis, not by
// uniform random scattering.

#include "EngineUtil.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace gdds {
namespace {

using namespace eng;

// Vertical extent of what the camera shows around the gameplay, per column.
class Band {
public:
    explicit Band(LevelAnalysis const& a) {
        a.flags.forEach([&](int x, int y, uint8_t f) {
            if (!(f & (cellflag::Solid | cellflag::Hazard | cellflag::Special))) return;
            int bx = int(std::floor(x * kCell / kBlock));
            auto [it, inserted] = m_top.emplace(bx, y);
            if (!inserted) it->second = std::max(it->second, y);
        });
    }
    // Highest gameplay point (units) within +-window blocks of x.
    double gameplayTop(double x, int window = 12) const {
        int bx = int(std::floor(x / kBlock));
        int best = -1;
        for (int i = -window; i <= window; ++i) {
            auto it = m_top.find(bx + i);
            if (it != m_top.end()) best = std::max(best, it->second);
        }
        return best < 0 ? 4 * kBlock : (best + 1) * kCell;
    }
    // [bottom, top] of the visible band at x.
    std::pair<double, double> at(double x) const {
        double top = std::max(11 * kBlock, gameplayTop(x) + 6 * kBlock);
        return {0.0, top};
    }

private:
    std::unordered_map<int, int> m_top;
};

double sectionStart(EngineContext const& ctx, double x) { return std::max(ctx.analysis.levelBounds.x0, ctx.plan.at(x).x0); }

// Horizontal span of the composition. It deliberately ignores the masks:
// sequential layouts (pillars, skylines) must start from the same origin
// whether the whole level or one zone is generated, otherwise a local
// regeneration would shift every element. Masks only filter the result.
Rect levelSpan(EngineContext const& ctx) {
    Rect lb = ctx.analysis.levelBounds;
    if (lb.isEmpty()) return {0, 0, 0, 0};
    return {std::floor((lb.x0 - 5 * kBlock) / kBlock) * kBlock, 0, lb.x1 + 5 * kBlock, 0};
}

// Cheap skip for grid-aligned loops (no sequential state involved).
bool columnOutside(EngineContext const& ctx, double x, double w) {
    Rect m = ctx.masks.bounds();
    return x + w < m.x0 - kBlock || x > m.x1 + kBlock;
}

// -----------------------------------------------------------------------------
// Background structures

void pillars(EngineContext& ctx, Band const& band, bool skyline) {
    auto fill = ctx.pick({DecoShape::Square}, anchor::FreeSpace, 0xb501, isStretchable);
    auto line = ctx.pick({DecoShape::Line}, anchor::FreeSpace, 0xb502, isStretchable);
    auto window = ctx.pick({DecoShape::Square, DecoShape::Dot}, anchor::FreeSpace, 0xb503,
                           [](DecoDef const& d) { return d.hasTag("small") || d.shape == DecoShape::Dot; });
    if (!fill) return;
    Rect span = levelSpan(ctx);
    double scale = std::max(0.4, ctx.layer.params.scale);
    double x = std::floor(span.x0 / kBlock) * kBlock;
    int k = 0;
    while (x < span.x1) {
        if (ctx.cancelled()) return;
        auto& sec = ctx.plan.at(x);
        double period = ctx.layer.params.knob("period", 0) > 0 ? ctx.layer.params.knob("period", 0) * kBlock
                                                                : sec.rhythm * (skyline ? 0.8 : 1.5);
        auto [b0, b1] = band.at(x);
        double bandH = b1 - b0;
        Rng r = ctx.rng(0xb5, int64_t(std::round(x / kBlock)));
        double width, gap;
        if (skyline) {
            width = std::round(r.range(2, 5) * scale) * kBlock;
            gap = r.chance(0.35) ? kBlock : 0;
        }
        else {
            width = std::max(kBlock, std::round(ctx.layer.params.knob("width", 2.5) * scale * 2) / 2 * kBlock);
            gap = std::max(0.0, period - width);
        }
        double n = valueNoise(ctx.seed ^ 0xb5, x / (kBlock * 9));
        double h = bandH * (skyline ? (0.25 + 0.45 * n) : (0.35 + 0.5 * n));
        if (!skyline && (k % 2)) h *= 0.72;  // tall / short alternation gives the rhythm
        h = std::round(h / kHalfBlock) * kHalfBlock;
        double density = sec.density * (1.0 - 0.6 * sec.readability);
        bool keep = r.chance(std::clamp(ctx.density * 1.6 * density, 0.0, 1.0));
        if (keep && h >= 2 * kBlock) {
            Rect body{x, b0 - kBlock, x + width, b0 + h};
            uint64_t key = hashOf(0xb6, int64_t(std::round(x)));
            double op = ctx.layer.params.knob("opacity", 1.0) * (1.0 - 0.35 * sec.readability);
            emitRect(ctx, fill, body, ColorRole::Background, 0, PlacementTier::Primary, key, skyline ? "skyline" : "pillar", op);
            if (line && !skyline) {
                double t = ctx.layer.params.knob("capThickness", 2);
                emitLine(ctx, line, {body.x0, body.y1 - t / 2}, {body.x1, body.y1 - t / 2}, t, ColorRole::Detail, 1,
                         PlacementTier::Secondary, hashOf(key, 1), "pillar-cap", 0.7, 180, body.center());
                if (ctx.layer.params.complexity > 0.45 && width >= 2 * kBlock) {
                    double ix = body.x0 + width * 0.25, ix2 = body.x1 - width * 0.25;
                    for (double xx : {ix, ix2})
                        emitLine(ctx, line, {xx, b0}, {xx, body.y1 - 12}, 1.5, ColorRole::Detail, 1, PlacementTier::Micro,
                                 hashOf(key, int64_t(xx)), "pillar-inset", 0.35, 180, body.center());
                }
            }
            if (skyline && window && ctx.layer.params.complexity > 0.4) {
                // A few lit windows; regular grid so it reads as architecture.
                for (double wy = b0 + kBlock; wy < body.y1 - kBlock; wy += kBlock)
                    for (double wx = body.x0 + kBlock * 0.5; wx < body.x1 - kBlock * 0.25; wx += kBlock) {
                        Rng wr = ctx.rng(key, int64_t(wx), int64_t(wy));
                        if (!wr.chance(0.28)) continue;
                        emitSized(ctx, window, {wx + 7.5, wy + 7.5}, 6, 0, ColorRole::Detail, 1, PlacementTier::Micro,
                                  hashOf(key, int64_t(wx), int64_t(wy)), "window", 0.45);
                    }
            }
        }
        x += width + gap;
        ++k;
    }
}

void geometric(EngineContext& ctx, Band const& band) {
    auto shapes = ctx.candidates({DecoShape::Circle, DecoShape::Ring, DecoShape::Square, DecoShape::OutlineShape,
                                  DecoShape::Polygon, DecoShape::Triangle, DecoShape::Diamond},
                                 anchor::FreeSpace,
                                 [](DecoDef const& d) { return !d.hasTag("small") || d.shape == DecoShape::Square; });
    if (shapes.empty()) return;
    Rect span = levelSpan(ctx);
    double scale = std::max(0.4, ctx.layer.params.scale);
    double x = std::floor(span.x0 / kBlock) * kBlock;
    while (x < span.x1) {
        if (ctx.cancelled()) return;
        auto& sec = ctx.plan.at(x);
        double period = sec.rhythm * 2;
        auto [b0, b1] = band.at(x);
        Rng r = ctx.rng(0x9e, int64_t(std::round(x / kBlock)));
        double cy = b0 + (b1 - b0) * (0.45 + 0.25 * (valueNoise(ctx.seed ^ 0x9e, x / (kBlock * 7)) - 0.5));
        Vec2 c{x + period / 2, cy};
        double free = ctx.analysis.freeDistanceAt(c);
        // Size follows the free space around the anchor, within sane bounds:
        // large enough to structure the background, never a wall of color.
        double size = std::clamp(free * 1.2, 2.5 * kBlock, ctx.layer.params.knob("maxSize", 8) * kBlock) * scale;
        if (r.chance(std::clamp(ctx.density * 1.4 * sec.density, 0.0, 1.0)) && sec.readability < 0.8) {
            // One family per section (motif variant) keeps the composition coherent.
            size_t family = size_t(sec.motifVariant) % shapes.size();
            auto main = shapes[(family + size_t(r.rangeInt(0, 1))) % shapes.size()];
            double rot = main.def->shape == DecoShape::Square || main.def->shape == DecoShape::OutlineShape ? 45 : 0;
            uint64_t key = hashOf(0x9f, int64_t(std::round(c.x)));
            // Shapes behind dense gameplay are dimmed so they never compete with it.
            double behind = ctx.analysis.solidCoverage(Rect::fromCenter(c, size, size));
            double op = ctx.layer.params.knob("opacity", 0.5) * (1.0 - 0.5 * behind) * (1.0 - 0.4 * sec.readability);
            emitSized(ctx, main, c, size, rot, ColorRole::Background, 0, PlacementTier::Primary, key, "geo-main", op);
            if (ctx.layer.params.complexity > 0.35) {
                // Secondary concentric outline, larger and fainter (hierarchy).
                auto ring = ctx.pick({DecoShape::Ring, DecoShape::OutlineShape}, anchor::FreeSpace, key ^ 0x1);
                if (ring)
                    emitSized(ctx, ring, c, size * 1.4, rot, ColorRole::Background, -1, PlacementTier::Secondary,
                              hashOf(key, 2), "geo-ring", op * 0.5);
            }
            if (ctx.layer.params.complexity > 0.65) {
                // Small satellite shapes on the diagonal.
                for (int i = 0; i < 2; ++i) {
                    Vec2 off{(i ? 1 : -1) * size * 0.75, (i ? -1 : 1) * size * 0.45};
                    emitSized(ctx, main, c + off, size * 0.22, rot, ColorRole::Detail, 1, PlacementTier::Micro,
                              hashOf(key, 3 + i), "geo-satellite", op * 0.6);
                }
            }
        }
        x += period;
    }
}

void girders(EngineContext& ctx, Band const& band) {
    auto line = ctx.pick({DecoShape::Line}, anchor::FreeSpace, 0x6201, isStretchable);
    if (!line) return;
    Rect span = levelSpan(ctx);
    double t = ctx.layer.params.knob("thickness", 3);
    double x = std::floor(span.x0 / (4 * kBlock)) * 4 * kBlock;
    while (x < span.x1) {
        if (ctx.cancelled()) return;
        auto& sec = ctx.plan.at(x);
        double P = std::max(4 * kBlock, sec.rhythm);
        auto [b0, b1] = band.at(x);
        double h0 = b0 + (b1 - b0) * 0.55, h1 = h0 + 2 * kBlock;
        if (sec.readability < 0.75 && ctx.density > 0.05) {
            uint64_t key = hashOf(0x62, int64_t(std::round(x)));
            double op = ctx.layer.params.knob("opacity", 0.7) * (1 - 0.4 * sec.readability);
            emitLine(ctx, line, {x, h0}, {x + P, h0}, t, ColorRole::Background, 0, PlacementTier::Primary, key, "girder", op);
            emitLine(ctx, line, {x, h1}, {x + P, h1}, t, ColorRole::Background, 0, PlacementTier::Primary, hashOf(key, 1),
                     "girder", op);
            emitLine(ctx, line, {x, b0}, {x, h1}, t, ColorRole::Background, 0, PlacementTier::Primary, hashOf(key, 2), "post", op);
            emitLine(ctx, line, {x, h0}, {x + P, h1}, t * 0.7, ColorRole::Detail, 0, PlacementTier::Secondary, hashOf(key, 3),
                     "brace", op * 0.8);
        }
        x += P;
    }
}

void generateBackgroundStructures(EngineContext& ctx) {
    Band band(ctx.analysis);
    std::string style = ctx.layer.params.style.empty() ? "pillars" : ctx.layer.params.style;
    if (style == "skyline") pillars(ctx, band, true);
    else if (style == "geometric") geometric(ctx, band);
    else if (style == "girders") girders(ctx, band);
    else pillars(ctx, band, false);
}

// -----------------------------------------------------------------------------
// Background details: blue-noise scattering (jittered grid, local & deterministic)

void generateBackgroundDetails(EngineContext& ctx) {
    Band band(ctx.analysis);
    std::string style = ctx.layer.params.style.empty() ? "scatter" : ctx.layer.params.style;
    auto shapes = ctx.candidates({DecoShape::Dot, DecoShape::Circle, DecoShape::Ring, DecoShape::Square, DecoShape::Diamond,
                                  DecoShape::Polygon, DecoShape::OutlineShape, DecoShape::Triangle, DecoShape::Ornament},
                                 anchor::FreeSpace);
    auto line = ctx.pick({DecoShape::Line}, anchor::FreeSpace, 0xbd01, isStretchable);
    if (shapes.empty()) return;
    Rect span = levelSpan(ctx);
    double scale = std::max(0.4, ctx.layer.params.scale);
    double cell = ctx.layer.params.knob("spacing", 3) * kBlock;
    double clearance = ctx.layer.params.knob("clearance", 1) * kBlock;
    for (double gx = std::floor(span.x0 / cell) * cell; gx < span.x1; gx += cell) {
        if (ctx.cancelled()) return;
        if (columnOutside(ctx, gx, cell)) continue;
        auto& sec = ctx.plan.at(gx);
        auto [b0, b1] = band.at(gx);
        for (double gy = std::floor(b0 / cell) * cell; gy < b1; gy += cell) {
            Rng r = ctx.rng(0xbd, int64_t(std::round(gx / cell)), int64_t(std::round(gy / cell)));
            double p = std::clamp(ctx.density * 1.2 * sec.density * (1 - 0.7 * sec.readability), 0.0, 1.0);
            if (!r.chance(p)) continue;
            Vec2 c{gx + r.range(0.2, 0.8) * cell, gy + r.range(0.2, 0.8) * cell};
            if (c.y < 0) continue;
            double free = ctx.analysis.freeDistanceAt(c);
            if (free < clearance) continue;
            uint64_t key = hashOf(0xbe, quantize(c.x), quantize(c.y));
            if (style == "lines" && line) {
                double len = std::round(r.range(2, 6)) * kBlock * scale;
                emitLine(ctx, line, {c.x - len / 2, c.y}, {c.x + len / 2, c.y}, 1.5, ColorRole::Detail, 0,
                         PlacementTier::Micro, key, "data-line", r.range(0.2, 0.5), 180, c);
                continue;
            }
            // Coherent vocabulary: two shape families per section.
            size_t fam = size_t(sec.motifVariant * 2 + r.rangeInt(0, 1)) % shapes.size();
            auto pick = shapes[fam];
            double size = r.range(0.3, 0.8) * kBlock * scale;
            if (style == "grid-dots") {
                Vec2 q{std::round(c.x / kBlock) * kBlock, std::round(c.y / kBlock) * kBlock};
                c = q;
                size = 0.2 * kBlock * scale;
            }
            double depth = size / (0.8 * kBlock * scale);  // smaller = farther = dimmer
            double rot = (pick.def->shape == DecoShape::Square || pick.def->shape == DecoShape::OutlineShape) && r.chance(0.5) ? 45 : 0;
            if (pick.rule && !pick.rule->rotations.empty())
                rot = pick.rule->rotations[size_t(r.rangeInt(0, int(pick.rule->rotations.size()) - 1))];
            emitSized(ctx, pick, c, size, rot, r.chance(0.7) ? ColorRole::Detail : ColorRole::Background, 0,
                      PlacementTier::Micro, key, "bg-detail", 0.25 + 0.45 * depth);
        }
    }
}

// -----------------------------------------------------------------------------
// Atmosphere

void generateAtmospheric(EngineContext& ctx) {
    auto& a = ctx.analysis;
    Band band(a);
    std::string style = ctx.layer.params.style.empty() ? "particles" : ctx.layer.params.style;
    double scale = std::max(0.4, ctx.layer.params.scale);
    if (style == "chains") {
        auto chain = ctx.pick({DecoShape::Chain}, anchor::BottomFace, 0xa701);
        auto end = ctx.pick({DecoShape::Dot, DecoShape::Diamond, DecoShape::Triangle}, anchor::BottomFace, 0xa702);
        if (!chain) return;
        for (auto& seg : a.segments) {
            if (ctx.cancelled()) return;
            if (seg.face != Face::Bottom || seg.onGround || seg.exposure < 3 * kBlock || seg.length < 2 * kBlock) continue;
            auto& sec = ctx.plan.at(seg.midpoint().x);
            if (sec.readability > 0.5) continue;
            double spacing = std::max(3 * kBlock, sec.rhythm);
            int n = std::max(1, int(seg.length / spacing));
            for (int i = 0; i < n; ++i) {
                Vec2 top = {std::min(seg.p0.x, seg.p1.x) + seg.length * (i + 0.5) / n, seg.p0.y};
                Rng r = ctx.rng(0xa7, quantize(top.x), quantize(top.y));
                if (!r.chance(std::clamp(ctx.density * 1.5, 0.0, 1.0))) continue;
                double maxLen = std::min(seg.exposure - 1.5 * kBlock, 3 * kBlock * scale);
                double len = std::round(r.range(0.4, 1.0) * maxLen / kHalfBlock) * kHalfBlock;
                if (len < kBlock) continue;
                double w = chain.def->canonW() * (len / chain.def->canonH());
                (void)w;
                // Canonical chain stands on its mount: flip vertically to hang it.
                Vec2 c{top.x, top.y - len / 2};
                double s = len / chain.def->canonH();
                Placement pl = ctx.place(chain, c, 0, s, -s, top, ColorRole::Detail, 0, PlacementTier::Secondary,
                                         hashOf(0xa8, quantize(top.x)), "chain");
                if (!ctx.masks.allows(top)) continue;
                ctx.emit(pl);
                if (end) emitSized(ctx, end, {top.x, top.y - len - 4}, 9 * scale, 180, ColorRole::Accent, 1,
                                   PlacementTier::Micro, hashOf(0xa9, quantize(top.x)), "chain-end", 0.9, false, false, top);
            }
        }
        return;
    }
    auto particle = ctx.pick({DecoShape::Dot, DecoShape::Ornament, DecoShape::GlowRound}, anchor::FreeSpace, 0xa601,
                             [&](DecoDef const& d) {
                                 return style == "dust" ? d.shape == DecoShape::GlowRound : d.shape != DecoShape::GlowRound;
                             });
    if (!particle) return;
    Rect span = levelSpan(ctx);
    double cell = (style == "dust" ? 6 : 2.5) * kBlock;
    for (double gx = std::floor(span.x0 / cell) * cell; gx < span.x1; gx += cell) {
        if (ctx.cancelled()) return;
        if (columnOutside(ctx, gx, cell)) continue;
        auto& sec = ctx.plan.at(gx);
        if (sec.readability > 0.65) continue;
        auto [b0, b1] = band.at(gx);
        for (double gy = std::floor(b0 / cell) * cell; gy < b1; gy += cell) {
            Rng r = ctx.rng(0xa6, int64_t(std::round(gx / cell)), int64_t(std::round(gy / cell)));
            Vec2 c{gx + r.uniform() * cell, gy + r.uniform() * cell};
            if (c.y < 8) continue;
            double free = a.freeDistanceAt(c);
            // More atmosphere where there is room; none hugging the gameplay.
            double openness = std::clamp((free - kBlock) / (5 * kBlock), 0.0, 1.0);
            if (!r.chance(std::clamp(ctx.density * openness * sec.detailBudget * 1.3, 0.0, 1.0))) continue;
            double size = style == "dust" ? r.range(3, 7) * kBlock * scale : r.range(2.5, 6) * scale;
            double op = style == "dust" ? r.range(0.08, 0.2) : r.range(0.25, 0.7);
            emitSized(ctx, particle, c, size, r.range(0, 90), style == "dust" ? ColorRole::Glow : ColorRole::Accent, 0,
                      PlacementTier::Micro, hashOf(0xa5, quantize(c.x), quantize(c.y)), style == "dust" ? "dust" : "particle", op);
        }
    }
}

// -----------------------------------------------------------------------------
// Foreground: few large silhouettes far from the gameplay

void generateForeground(EngineContext& ctx) {
    auto& a = ctx.analysis;
    Band band(a);
    auto fill = ctx.pick({DecoShape::Square, DecoShape::Triangle, DecoShape::Circle}, anchor::FreeSpace, 0xf001,
                         [](DecoDef const& d) { return d.shape != DecoShape::Square || d.stretchable; });
    if (!fill) return;
    Rect span = levelSpan(ctx);
    double scale = std::max(0.4, ctx.layer.params.scale);
    double x = std::floor(span.x0 / kBlock) * kBlock;
    while (x < span.x1) {
        if (ctx.cancelled()) return;
        auto& sec = ctx.plan.at(x);
        double period = sec.rhythm * 3;
        Rng r = ctx.rng(0xf0, int64_t(std::round(x / kBlock)));
        if (sec.readability < 0.35 && r.chance(std::clamp(ctx.density * 1.2, 0.0, 1.0))) {
            auto [b0, b1] = band.at(x);
            double w = std::round(r.range(1.5, 3.5) * scale) * kBlock;
            double h = std::round(r.range(2, 4) * scale) * kBlock;
            // Hang from the top of the visible band.
            Rect body{x, b1 - h, x + w, b1 + 2 * kBlock};
            Vec2 c = body.center();
            if (a.isAreaFree(body, 3 * kBlock) && a.freeDistanceAt({c.x, body.y0}) >= 3 * kBlock) {
                if (fill.def->shape == DecoShape::Square)
                    emitRect(ctx, fill, body, ColorRole::Foreground, 0, PlacementTier::Primary, hashOf(0xf1, int64_t(x)),
                             "fg-silhouette", ctx.layer.params.knob("opacity", 0.95));
                else
                    emitSized(ctx, fill, c, std::max(w, h), 180, ColorRole::Foreground, 0, PlacementTier::Primary,
                              hashOf(0xf2, int64_t(x)), "fg-silhouette", ctx.layer.params.knob("opacity", 0.95));
            }
        }
        x += period;
    }
}

class ArtisticEngine : public Engine {
public:
    void generate(EngineContext& ctx) override {
        switch (ctx.layer.kind) {
            case LayerKind::BackgroundStructures: generateBackgroundStructures(ctx); break;
            case LayerKind::BackgroundDetails: generateBackgroundDetails(ctx); break;
            case LayerKind::Atmospheric: generateAtmospheric(ctx); break;
            case LayerKind::Foreground: generateForeground(ctx); break;
            default: break;
        }
    }
};

}  // namespace

std::unique_ptr<Engine> makeArtisticEngine() { return std::make_unique<ArtisticEngine>(); }

}  // namespace gdds
