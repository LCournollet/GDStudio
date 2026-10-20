#pragma once

// Generation masks. Masks work in real level coordinates and can be combined:
// a point is allowed for a layer when it lies in at least one applicable
// include mask (or no include mask applies) and in no applicable exclusion.
//
// Membership is always decided on a placement's ANCHOR point, with the same
// predicate for generation and removal. This is what makes "Regenerate in
// mask" exact: everything whose anchor is outside the mask is left untouched,
// and what is regenerated inside is exactly what was removed. How structural
// decorations crossing the mask border are cut is controlled by the layer's
// BoundaryPolicy (see Layer.hpp).

#include "Geometry.hpp"
#include "Json.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gdds {

class LevelAnalysis;

enum class MaskShape { Rectangle, Polygon, Brush, Section, Structure };
enum class MaskMode { Include, Exclude };

// How structural decorations crossing a mask border are handled.
enum class BoundaryPolicy : uint8_t {
    Extend,      // a structure touched by the mask is processed entirely
    Clip,        // elements are cut into grid-aligned pieces, each piece decided on its own
    InsideOnly,  // only structures entirely inside the mask
};
char const* boundaryPolicyName(BoundaryPolicy p);
BoundaryPolicy boundaryPolicyFromName(std::string const& n);

char const* maskShapeName(MaskShape s);
MaskShape maskShapeFromName(std::string const& n);

struct BrushStroke {
    Vec2 center;
    double radius = 30;
};

struct Mask {
    std::string id;
    std::string name;
    MaskShape shape = MaskShape::Rectangle;
    MaskMode mode = MaskMode::Include;
    bool enabled = true;
    // Layers this mask applies to. Empty = every layer.
    std::vector<std::string> layers;

    Rect rect;                          // Rectangle
    Polygon polygon;                    // Polygon
    std::vector<BrushStroke> strokes;   // Brush (union of disks)
    double sectionX0 = 0, sectionX1 = 0;  // Section (vertical band)
    // Structure masks remember the structures' identity signatures and their
    // bounds, so the selection survives re-analysis (structures are matched
    // by signature first, then by best overlap).
    std::vector<uint64_t> structureSignatures;
    std::vector<Rect> structureBounds;
    double structureMargin = 30;        // halo around the selected structures

    bool appliesTo(std::string const& layerId) const;
    Rect bounds() const;
    // Geometric containment (ignores enabled/mode). Structure masks need the
    // analysis (pass nullptr to use the stored bounds).
    bool contains(Vec2 p, LevelAnalysis const* analysis = nullptr) const;

    void translate(Vec2 d);
    // Scales the mask around `origin` (used by the resize handles).
    void scale(Vec2 origin, double sx, double sy);

    json::Value toJson() const;
    static Mask fromJson(json::Value const& v);
};

// Evaluates the combination of masks for one layer and one operation.
//
// Include and operation masks honour the layer's BoundaryPolicy: with Extend,
// an anchor belongs to the mask when the structure it decorates touches the
// mask; with InsideOnly, when that structure lies entirely inside. Exclusion
// masks are always strict (anchor based). The SAME predicate decides what is
// removed and what is generated, which keeps local operations exact.
class MaskSet {
public:
    MaskSet() = default;

    // `masks`: all project masks; `layerId`: the layer being generated;
    // `operationMasks`: masks explicitly chosen for a local operation
    // ("Generate in mask"); they act as additional mandatory includes.
    static MaskSet build(std::vector<Mask> const& masks, std::string const& layerId,
                         std::vector<std::string> const& operationMasks, LevelAnalysis const* analysis,
                         BoundaryPolicy policy = BoundaryPolicy::Extend);

    bool allows(Vec2 p) const;
    // Only the operation masks (used to select what a local operation removes).
    bool inOperationRegion(Vec2 p) const;
    bool unrestricted() const { return m_includes.empty() && m_excludes.empty() && m_required.empty(); }
    bool hasOperationMasks() const { return !m_required.empty(); }
    // Conservative bounds of the allowed region (infinite rect if unrestricted).
    Rect bounds() const;

private:
    bool inMask(Mask const* m, Vec2 p) const;
    int structureNear(Vec2 p) const;

    std::vector<Mask const*> m_includes;
    std::vector<Mask const*> m_excludes;
    std::vector<Mask const*> m_required;  // operation masks: point must be in one of them
    LevelAnalysis const* m_analysis = nullptr;
    BoundaryPolicy m_policy = BoundaryPolicy::Extend;
    mutable std::unordered_map<uint64_t, bool> m_structureCache;
};

}  // namespace gdds
