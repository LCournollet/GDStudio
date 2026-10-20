// Compile-time cross-checks of the (work in progress) GD 2.2091 bindings
// against offsets measured independently in the installed GeometryDash.exe
// (static analysis used to build the object catalog, see docs/CATALOG.md).
// If the bindings disagree, the experimental build stops here instead of
// producing a mod that would read the wrong memory.

#include <Geode/Geode.hpp>

#include <cstddef>

#ifdef GDDS_GD_2091
#pragma warning(push)
#pragma warning(disable : 4996)  // offsetof on non-standard-layout types
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(GameObject, m_objectType) == 0x3a0, "GameObject::m_objectType offset differs from GD 2.2091");
static_assert(offsetof(GameObject, m_width) == 0x2fc, "GameObject::m_width offset differs from GD 2.2091");
static_assert(offsetof(GameObject, m_baseColor) == 0x450, "GameObject::m_baseColor offset differs from GD 2.2091");
#pragma clang diagnostic pop
#pragma warning(pop)
#endif
