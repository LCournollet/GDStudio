#include <gdds/Analysis.hpp>
#include <gdds/Mask.hpp>

#include <algorithm>

namespace gdds {

char const* maskShapeName(MaskShape s) {
    switch (s) {
        case MaskShape::Rectangle: return "rectangle";
        case MaskShape::Polygon: return "polygon";
        case MaskShape::Brush: return "brush";
        case MaskShape::Section: return "section";
        case MaskShape::Structure: return "structure";
    }
    return "rectangle";
}

MaskShape maskShapeFromName(std::string const& n) {
    if (n == "polygon") return MaskShape::Polygon;
    if (n == "brush") return MaskShape::Brush;
    if (n == "section") return MaskShape::Section;
    if (n == "structure") return MaskShape::Structure;
    return MaskShape::Rectangle;
}

bool Mask::appliesTo(std::string const& layerId) const {
    return layers.empty() || std::find(layers.begin(), layers.end(), layerId) != layers.end();
}

Rect Mask::bounds() const {
    switch (shape) {
        case MaskShape::Rectangle: return rect;
        case MaskShape::Polygon: return polygon.bounds();
        case MaskShape::Brush: {
            Rect r = Rect::empty();
            for (auto& s : strokes) r = r.united(Rect::fromCenter(s.center, s.radius * 2, s.radius * 2));
            return r;
        }
        case MaskShape::Section: return {std::min(sectionX0, sectionX1), -1e7, std::max(sectionX0, sectionX1), 1e7};
        case MaskShape::Structure: {
            Rect r = Rect::empty();
            for (auto& b : structureBounds) r = r.united(b.expanded(structureMargin));
            return r;
        }
    }
    return rect;
}

bool Mask::contains(Vec2 p, LevelAnalysis const* analysis) const {
    switch (shape) {
        case MaskShape::Rectangle: return rect.contains(p);
        case MaskShape::Polygon: return polygon.points.size() >= 3 && polygon.contains(p);
        case MaskShape::Brush:
            for (auto& s : strokes) {
                Vec2 d = p - s.center;
                if (d.x * d.x + d.y * d.y <= s.radius * s.radius) return true;
            }
            return false;
        case MaskShape::Section: return p.x >= std::min(sectionX0, sectionX1) && p.x <= std::max(sectionX0, sectionX1);
        case MaskShape::Structure: {
            if (analysis) {
                // Points inside or near one of the selected structures.
                int m = int(std::ceil(structureMargin / kCell));
                Cell c = LevelAnalysis::cellAt(p);
                for (int dy = -m; dy <= m; ++dy)
                    for (int dx = -m; dx <= m; ++dx) {
                        int sid = analysis->structureAt({c.x + dx, c.y + dy});
                        if (sid < 0) continue;
                        uint64_t sig = analysis->structures[sid].signature;
                        if (std::find(structureSignatures.begin(), structureSignatures.end(), sig) != structureSignatures.end())
                            return true;
                        // Structure edited since the mask was made: fall back to bounds overlap.
                        auto const& sb = analysis->structures[sid].bounds;
                        for (auto& b : structureBounds) {
                            Rect inter = b.intersection(sb);
                            if (!inter.isEmpty() && inter.area() >= 0.5 * std::min(b.area(), sb.area())) return true;
                        }
                    }
                return false;
            }
            for (auto& b : structureBounds)
                if (b.expanded(structureMargin).contains(p)) return true;
            return false;
        }
    }
    return false;
}

void Mask::translate(Vec2 d) {
    rect = {rect.x0 + d.x, rect.y0 + d.y, rect.x1 + d.x, rect.y1 + d.y};
    for (auto& p : polygon.points) p = p + d;
    for (auto& s : strokes) s.center = s.center + d;
    sectionX0 += d.x;
    sectionX1 += d.x;
    // Structure masks are tied to the gameplay and do not move.
}

void Mask::scale(Vec2 o, double sx, double sy) {
    auto tf = [&](Vec2 p) { return Vec2{o.x + (p.x - o.x) * sx, o.y + (p.y - o.y) * sy}; };
    Vec2 a = tf({rect.x0, rect.y0}), b = tf({rect.x1, rect.y1});
    rect = {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
    for (auto& p : polygon.points) p = tf(p);
    for (auto& s : strokes) {
        s.center = tf(s.center);
        s.radius *= std::sqrt(std::abs(sx * sy));
    }
    sectionX0 = o.x + (sectionX0 - o.x) * sx;
    sectionX1 = o.x + (sectionX1 - o.x) * sx;
}

json::Value Mask::toJson() const {
    json::Value v = json::Value::object();
    v["id"] = id;
    v["name"] = name;
    v["shape"] = maskShapeName(shape);
    v["mode"] = mode == MaskMode::Include ? "include" : "exclude";
    v["enabled"] = enabled;
    json::Value ls = json::Value::array();
    for (auto& l : layers) ls.push(l);
    v["layers"] = ls;
    switch (shape) {
        case MaskShape::Rectangle:
            v["rect"] = json::Array{rect.x0, rect.y0, rect.x1, rect.y1};
            break;
        case MaskShape::Polygon: {
            json::Value pts = json::Value::array();
            for (auto& p : polygon.points) pts.push(json::Array{p.x, p.y});
            v["points"] = pts;
            break;
        }
        case MaskShape::Brush: {
            json::Value st = json::Value::array();
            for (auto& s : strokes) st.push(json::Array{s.center.x, s.center.y, s.radius});
            v["strokes"] = st;
            break;
        }
        case MaskShape::Section:
            v["x0"] = sectionX0;
            v["x1"] = sectionX1;
            break;
        case MaskShape::Structure: {
            json::Value sigs = json::Value::array();
            for (auto s : structureSignatures) sigs.push(std::to_string(s));
            v["signatures"] = sigs;
            json::Value bs = json::Value::array();
            for (auto& b : structureBounds) bs.push(json::Array{b.x0, b.y0, b.x1, b.y1});
            v["bounds"] = bs;
            v["margin"] = structureMargin;
            break;
        }
    }
    return v;
}

Mask Mask::fromJson(json::Value const& v) {
    Mask m;
    m.id = v.str("id");
    m.name = v.str("name");
    m.shape = maskShapeFromName(v.str("shape", "rectangle"));
    m.mode = v.str("mode") == "exclude" ? MaskMode::Exclude : MaskMode::Include;
    m.enabled = v.boolean("enabled", true);
    for (auto& l : v.get("layers").items()) m.layers.push_back(l.asString());
    auto rectFrom = [](json::Value const& a) {
        auto& it = a.items();
        if (it.size() < 4) return Rect{};
        return Rect{it[0].asDouble(), it[1].asDouble(), it[2].asDouble(), it[3].asDouble()};
    };
    m.rect = rectFrom(v.get("rect"));
    for (auto& p : v.get("points").items())
        if (p.size() >= 2) m.polygon.points.push_back({p.items()[0].asDouble(), p.items()[1].asDouble()});
    for (auto& s : v.get("strokes").items())
        if (s.size() >= 3) m.strokes.push_back({{s.items()[0].asDouble(), s.items()[1].asDouble()}, s.items()[2].asDouble()});
    m.sectionX0 = v.num("x0");
    m.sectionX1 = v.num("x1");
    for (auto& s : v.get("signatures").items()) m.structureSignatures.push_back(s.asUInt64());
    for (auto& b : v.get("bounds").items()) m.structureBounds.push_back(rectFrom(b));
    m.structureMargin = v.num("margin", 30);
    return m;
}

// ---------------------------------------------------------------------------

char const* boundaryPolicyName(BoundaryPolicy p) {
    switch (p) {
        case BoundaryPolicy::Extend: return "extend";
        case BoundaryPolicy::Clip: return "clip";
        case BoundaryPolicy::InsideOnly: return "inside-only";
    }
    return "extend";
}

BoundaryPolicy boundaryPolicyFromName(std::string const& n) {
    if (n == "clip") return BoundaryPolicy::Clip;
    if (n == "inside-only") return BoundaryPolicy::InsideOnly;
    return BoundaryPolicy::Extend;
}

MaskSet MaskSet::build(std::vector<Mask> const& masks, std::string const& layerId,
                       std::vector<std::string> const& operationMasks, LevelAnalysis const* analysis,
                       BoundaryPolicy policy) {
    MaskSet set;
    set.m_analysis = analysis;
    set.m_policy = policy;
    for (auto& m : masks) {
        bool isOp = std::find(operationMasks.begin(), operationMasks.end(), m.id) != operationMasks.end();
        if (isOp) {
            // An operation mask is used even if disabled for global generation,
            // but an exclusion mask chosen for an operation stays an exclusion.
            if (m.mode == MaskMode::Exclude) set.m_excludes.push_back(&m);
            else set.m_required.push_back(&m);
            continue;
        }
        if (!m.enabled || !m.appliesTo(layerId)) continue;
        if (m.mode == MaskMode::Include) set.m_includes.push_back(&m);
        else set.m_excludes.push_back(&m);
    }
    return set;
}

int MaskSet::structureNear(Vec2 p) const {
    if (!m_analysis) return -1;
    Cell c = LevelAnalysis::cellAt(p);
    int sid = m_analysis->structureAt(c);
    if (sid >= 0) return sid;
    // Anchors of contour decorations sit on the boundary: look one cell around.
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            sid = m_analysis->structureAt({c.x + dx, c.y + dy});
            if (sid >= 0) return sid;
        }
    return -1;
}

bool MaskSet::inMask(Mask const* m, Vec2 p) const {
    if (m_policy == BoundaryPolicy::Clip || !m_analysis) return m->contains(p, m_analysis);
    int sid = structureNear(p);
    if (sid < 0) return m->contains(p, m_analysis);
    uint64_t key = (uint64_t(reinterpret_cast<uintptr_t>(m)) << 20) ^ uint64_t(sid);
    if (auto it = m_structureCache.find(key); it != m_structureCache.end()) return it->second;
    auto const& s = m_analysis->structures[size_t(sid)];
    bool result;
    if (!s.bounds.intersects(m->bounds().expanded(1))) result = false;
    else if (m_policy == BoundaryPolicy::Extend) {
        result = false;
        for (auto c : s.cells)
            if (m->contains(LevelAnalysis::cellCenter(c), m_analysis)) { result = true; break; }
    }
    else {
        result = true;
        for (auto c : s.cells)
            if (!m->contains(LevelAnalysis::cellCenter(c), m_analysis)) { result = false; break; }
    }
    m_structureCache[key] = result;
    return result;
}

bool MaskSet::inOperationRegion(Vec2 p) const {
    if (m_required.empty()) return true;
    for (auto m : m_required)
        if (inMask(m, p)) return true;
    return false;
}

bool MaskSet::allows(Vec2 p) const {
    for (auto m : m_excludes) if (m->contains(p, m_analysis)) return false;
    if (!inOperationRegion(p)) return false;
    if (m_includes.empty()) return true;
    for (auto m : m_includes) if (inMask(m, p)) return true;
    return false;
}

Rect MaskSet::bounds() const {
    Rect inf{-1e9, -1e9, 1e9, 1e9};
    Rect r = inf;
    if (!m_required.empty()) {
        Rect u = Rect::empty();
        for (auto m : m_required) u = u.united(m->bounds());
        r = r.intersection(u);
    }
    if (!m_includes.empty()) {
        Rect u = Rect::empty();
        for (auto m : m_includes) u = u.united(m->bounds());
        r = r.intersection(u);
    }
    return r;
}

}  // namespace gdds
