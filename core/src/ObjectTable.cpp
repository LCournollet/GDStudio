#include <gdds/ObjectInfo.hpp>

#include <algorithm>
#include <unordered_map>

namespace gdds {

// Row format of the generated table (see tools/catalog/gen_object_table.py).
struct StaticObjectTable::Entry {
    int id;
    ObjectKind kind;
    PortalType portal;
    int portalValue;
    HitboxShape shape;
    float hitW, hitH, hitOffX, hitOffY;
    float visW, visH;
    int zLayer, zOrder;
    bool safeDeco;
    char const* frame;
};

namespace {

using K = ObjectKind;
using P = PortalType;
using H = HitboxShape;

std::vector<StaticObjectTable::Entry> const& table() {
    static std::vector<StaticObjectTable::Entry> const rows = {
#include "generated/ObjectTableData.inc"
    };
    return rows;
}

std::unordered_map<int, size_t> const& index() {
    static std::unordered_map<int, size_t> const idx = [] {
        std::unordered_map<int, size_t> m;
        auto& rows = table();
        for (size_t i = 0; i < rows.size(); ++i) m.emplace(rows[i].id, i);
        return m;
    }();
    return idx;
}

}  // namespace

StaticObjectTable::StaticObjectTable() : m_entries(&table()) {}

StaticObjectTable const& StaticObjectTable::get() {
    static StaticObjectTable const instance;
    return instance;
}

bool StaticObjectTable::isKnown(int id) const { return index().count(id) != 0; }

ObjectInfo StaticObjectTable::info(int id) const {
    ObjectInfo info;
    auto it = index().find(id);
    if (it == index().end()) return info;  // Unknown: conservative
    auto& e = (*m_entries)[it->second];
    info.kind = e.kind;
    info.portal = e.portal;
    info.portalValue = e.portalValue;
    info.shape = e.shape;
    info.hitW = e.hitW;
    info.hitH = e.hitH;
    info.hitOffX = e.hitOffX;
    info.hitOffY = e.hitOffY;
    info.visW = e.visW;
    info.visH = e.visH;
    info.defaultZLayer = e.zLayer;
    info.defaultZOrder = e.zOrder;
    info.name = e.frame;
    return info;
}

bool StaticObjectTable::isSafeDecoration(int id) const {
    auto it = index().find(id);
    return it != index().end() && (*m_entries)[it->second].safeDeco &&
           (*m_entries)[it->second].kind == ObjectKind::Decoration;
}

char const* StaticObjectTable::frameName(int id) const {
    auto it = index().find(id);
    return it == index().end() ? "" : (*m_entries)[it->second].frame;
}

}  // namespace gdds
