#include <gdds/DecoCatalog.hpp>

#include <algorithm>
#include <cctype>
#include <set>

namespace gdds {

namespace {
struct ShapeName { DecoShape shape; char const* name; };
constexpr ShapeName kShapeNames[] = {
    {DecoShape::Square, "square"},       {DecoShape::Line, "line"},           {DecoShape::Corner, "corner"},
    {DecoShape::Dot, "dot"},             {DecoShape::Circle, "circle"},       {DecoShape::Ring, "ring"},
    {DecoShape::Triangle, "triangle"},   {DecoShape::Diamond, "diamond"},     {DecoShape::Polygon, "polygon"},
    {DecoShape::OutlineShape, "outline-shape"}, {DecoShape::GlowEdge, "glow-edge"}, {DecoShape::GlowCorner, "glow-corner"},
    {DecoShape::GlowRound, "glow-round"}, {DecoShape::Chain, "chain"},        {DecoShape::Ornament, "ornament"},
    {DecoShape::Large, "large"},
};

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct BuiltinRow {
    int id;
    char const* name;
    char const* frame;
    char const* category;
    DecoShape shape;
    float x0, y0, x1, y1;
    float baseRotation;
    float thickness;
    bool colorable, hasDetail, stretchable, blending;
    char const* tags;  // comma separated
};

using S = DecoShape;
constexpr BuiltinRow kBuiltin[] = {
#include "generated/DecoCatalogData.inc"
};
}  // namespace

char const* decoShapeName(DecoShape s) {
    for (auto& sn : kShapeNames) if (sn.shape == s) return sn.name;
    return "ornament";
}

std::optional<DecoShape> decoShapeFromName(std::string const& n) {
    for (auto& sn : kShapeNames) if (n == sn.name) return sn.shape;
    return std::nullopt;
}

bool DecoDef::hasTag(std::string const& t) const { return std::find(tags.begin(), tags.end(), t) != tags.end(); }

json::Value DecoDef::toJson() const {
    json::Value v = json::Value::object();
    v["id"] = id;
    v["name"] = name;
    v["frame"] = frame;
    v["category"] = category;
    v["shape"] = decoShapeName(shape);
    v["visual"] = json::Array{visX0, visY0, visX1, visY1};
    v["thickness"] = thickness;
    v["colorable"] = colorable;
    v["hasDetail"] = hasDetail;
    v["stretchable"] = stretchable;
    v["blending"] = blending;
    json::Value t = json::Value::array();
    for (auto& tag : tags) t.push(tag);
    v["tags"] = t;
    return v;
}

DecoDef DecoDef::fromJson(json::Value const& v) {
    DecoDef d;
    d.id = v.integer("id");
    d.name = v.str("name");
    d.frame = v.str("frame");
    d.category = v.str("category");
    d.shape = decoShapeFromName(v.str("shape")).value_or(DecoShape::Ornament);
    auto& vis = v.get("visual").items();
    if (vis.size() == 4) {
        d.visX0 = vis[0].asDouble();
        d.visY0 = vis[1].asDouble();
        d.visX1 = vis[2].asDouble();
        d.visY1 = vis[3].asDouble();
    }
    d.thickness = v.num("thickness");
    d.colorable = v.boolean("colorable", true);
    d.hasDetail = v.boolean("hasDetail", false);
    d.stretchable = v.boolean("stretchable", false);
    d.blending = v.boolean("blending", false);
    for (auto& t : v.get("tags").items()) d.tags.push_back(t.asString());
    return d;
}

DecoCatalog const& DecoCatalog::builtin() {
    static DecoCatalog const cat = [] {
        DecoCatalog c;
        for (auto& row : kBuiltin) {
            DecoDef d;
            d.id = row.id;
            d.name = row.name;
            d.frame = row.frame;
            d.category = row.category;
            d.shape = row.shape;
            d.visX0 = row.x0;
            d.visY0 = row.y0;
            d.visX1 = row.x1;
            d.visY1 = row.y1;
            d.baseRotation = row.baseRotation;
            d.thickness = row.thickness;
            d.colorable = row.colorable;
            d.hasDetail = row.hasDetail;
            d.stretchable = row.stretchable;
            d.blending = row.blending;
            std::string tags = row.tags;
            size_t start = 0;
            while (start < tags.size()) {
                size_t end = tags.find(',', start);
                if (end == std::string::npos) end = tags.size();
                if (end > start) d.tags.push_back(tags.substr(start, end - start));
                start = end + 1;
            }
            c.add(std::move(d));
        }
        return c;
    }();
    return cat;
}

void DecoCatalog::add(DecoDef def) {
    for (auto& d : m_defs)
        if (d.id == def.id) { d = std::move(def); return; }
    m_defs.push_back(std::move(def));
}

DecoDef const* DecoCatalog::find(int id) const {
    for (auto& d : m_defs) if (d.id == id) return &d;
    return nullptr;
}

std::vector<DecoDef const*> DecoCatalog::byShape(DecoShape s) const {
    std::vector<DecoDef const*> out;
    for (auto& d : m_defs) if (d.shape == s && !isDisabled(d.id)) out.push_back(&d);
    return out;
}

std::vector<DecoDef const*> DecoCatalog::byCategory(std::string const& c) const {
    std::vector<DecoDef const*> out;
    for (auto& d : m_defs) if (d.category == c && !isDisabled(d.id)) out.push_back(&d);
    return out;
}

std::vector<std::string> DecoCatalog::categories() const {
    std::vector<std::string> out;
    for (auto& d : m_defs)
        if (std::find(out.begin(), out.end(), d.category) == out.end()) out.push_back(d.category);
    return out;
}

std::vector<DecoDef const*> DecoCatalog::search(std::string const& query) const {
    std::vector<DecoDef const*> out;
    std::string q = lower(query);
    for (auto& d : m_defs) {
        if (q.empty() || std::to_string(d.id) == q || lower(d.name).find(q) != std::string::npos ||
            lower(d.frame).find(q) != std::string::npos || lower(d.category).find(q) != std::string::npos) {
            out.push_back(&d);
            continue;
        }
        for (auto& t : d.tags)
            if (lower(t).find(q) != std::string::npos) { out.push_back(&d); break; }
    }
    return out;
}

void DecoCatalog::disable(int id) {
    if (!isDisabled(id)) m_disabled.push_back(id);
}

bool DecoCatalog::isDisabled(int id) const {
    return std::find(m_disabled.begin(), m_disabled.end(), id) != m_disabled.end();
}

json::Value DecoCatalog::toJson() const {
    json::Value arr = json::Value::array();
    for (auto& d : m_defs) arr.push(d.toJson());
    return arr;
}

void DecoCatalog::loadJson(json::Value const& v) {
    m_defs.clear();
    for (auto& d : v.items()) add(DecoDef::fromJson(d));
}

}  // namespace gdds
