#pragma once

// Helpers shared by the engine implementations (not part of the public API).

#include <gdds/Engine.hpp>

#include <cmath>
#include <optional>

namespace gdds::eng {

using Pick = EngineContext::Pick;

inline bool isStretchable(DecoDef const& d) { return d.stretchable; }

// Quadrant diagonal (0 = NE, 1 = NW, 2 = SW, 3 = SE).
inline Vec2 quadrantDir(int q) {
    return {(q == 0 || q == 3) ? 1.0 : -1.0, (q == 0 || q == 1) ? 1.0 : -1.0};
}

// Region pre-check: can anything anchored inside `r` survive the masks?
inline bool regionMayAllow(EngineContext const& ctx, Rect const& r) {
    Rect b = ctx.masks.bounds();
    return b.intersects(r.expanded(kBlock));
}

// A stretched object covering `r` exactly (Square shape).
inline void emitRect(EngineContext const& ctx, Pick const& p, Rect r, ColorRole role, int zOff, PlacementTier tier,
                     uint64_t key, char const* src, double opacity = 1.0, std::optional<HSV> hsv = std::nullopt,
                     std::optional<Vec2> anchorOverride = std::nullopt) {
    if (!p || r.isEmpty()) return;
    double sx = r.width() / p.def->canonW();
    double sy = r.height() / p.def->canonH();
    Vec2 c = r.center();
    Vec2 anchor = anchorOverride.value_or(c);
    if (!ctx.masks.allows(anchor)) return;
    auto pl = ctx.place(p, c, 0, sx, sy, anchor, role, zOff, tier, key, src);
    pl.opacity *= opacity;
    if (hsv && !hsv->isIdentity()) pl.object.mainHSV = hsv;
    ctx.emit(std::move(pl));
}

// Line pieces along a -> b, centered on the segment, thickness t.
// Extend policy: pieces evenly split (max `maxPiece`); Clip policy: pieces cut
// at world-aligned multiples of `clipChunk` so each piece is decided alone.
inline void emitLine(EngineContext const& ctx, Pick const& p, Vec2 a, Vec2 b, double t, ColorRole role, int zOff,
                     PlacementTier tier, uint64_t key, char const* src, double opacity = 1.0, double maxPiece = 180,
                     std::optional<Vec2> anchorOverride = std::nullopt) {
    if (!p) return;
    Vec2 d = b - a;
    double len = d.length();
    if (len < 0.5) return;
    Vec2 u = d / len;
    double angle = -std::atan2(u.y, u.x) * 180.0 / kPi;  // GD rotation is clockwise
    std::vector<std::pair<double, double>> pieces;
    if (ctx.layer.params.boundary == BoundaryPolicy::Clip && (std::abs(u.x) < 1e-9 || std::abs(u.y) < 1e-9)) {
        double chunk = ctx.layer.params.knob("clipChunk", 60);
        bool horizontal = std::abs(u.y) < 1e-9;
        double s0 = horizontal ? a.x : a.y, s1 = horizontal ? b.x : b.y;
        double lo = std::min(s0, s1), hi = std::max(s0, s1);
        double cur = lo;
        while (cur < hi - 1e-6) {
            double next = std::min(hi, (std::floor(cur / chunk + 1e-9) + 1) * chunk);
            double from = (s0 <= s1) ? cur - lo : hi - next;
            pieces.push_back({from, from + (next - cur)});
            cur = next;
        }
    }
    else {
        int n = std::max(1, int(std::ceil(len / maxPiece - 1e-9)));
        for (int i = 0; i < n; ++i) pieces.push_back({len * i / n, len * (i + 1) / n});
    }
    double sy = t / p.def->canonH();
    for (size_t i = 0; i < pieces.size(); ++i) {
        // tiny overlap hides seams between pieces
        double s0 = pieces[i].first - (i > 0 ? 0.25 : 0), s1 = pieces[i].second + (i + 1 < pieces.size() ? 0.25 : 0);
        Vec2 c = a + u * ((s0 + s1) / 2);
        Vec2 anchor = (anchorOverride && ctx.layer.params.boundary != BoundaryPolicy::Clip) ? *anchorOverride : c;
        if (!ctx.masks.allows(anchor)) continue;
        double sx = (s1 - s0) / p.def->canonW();
        auto pl = ctx.place(p, c, angle, sx, sy, anchor, role, zOff, tier, hashOf(key, i), src);
        pl.opacity *= opacity;
        ctx.emit(std::move(pl));
    }
}

// Uniformly scaled object with its visual size `size` (largest canonical side).
inline void emitSized(EngineContext const& ctx, Pick const& p, Vec2 center, double size, double rotation, ColorRole role,
                      int zOff, PlacementTier tier, uint64_t key, char const* src, double opacity = 1.0, bool flipX = false,
                      bool flipY = false, std::optional<Vec2> anchorOverride = std::nullopt) {
    if (!p) return;
    double s = size / std::max(p.def->canonW(), p.def->canonH());
    Vec2 anchor = anchorOverride.value_or(center);
    if (!ctx.masks.allows(anchor)) return;
    auto pl = ctx.place(p, center, rotation, flipX ? -s : s, flipY ? -s : s, anchor, role, zOff, tier, key, src);
    pl.opacity *= opacity;
    ctx.emit(std::move(pl));
}

// Smooth 1D value noise in [0, 1].
inline double valueNoise(uint64_t seed, double x) {
    double xi = std::floor(x);
    double t = x - xi;
    auto h = [&](double v) { return double(hashOf(seed, int64_t(v)) >> 11) * (1.0 / 9007199254740992.0); };
    double a = h(xi), b = h(xi + 1);
    double s = t * t * (3 - 2 * t);
    return a + (b - a) * s;
}

inline bool cellIsSlope(LevelAnalysis const& a, Vec2 p) { return a.flags.get(LevelAnalysis::cellAt(p)) & cellflag::Slope; }

}  // namespace gdds::eng
