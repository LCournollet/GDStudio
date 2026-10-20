# Geode mod target. The engine itself is the gdds_core library (../core).

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
endif()

set(GDDS_GD_VERSION "2.2081" CACHE STRING "Geometry Dash version targeted: 2.2081 (stable Geode) or 2.2091 (experimental bindings)")
set_property(CACHE GDDS_GD_VERSION PROPERTY STRINGS "2.2081" "2.2091")
if (GDDS_GD_VERSION STREQUAL "2.2091")
    message(WARNING "Building against the EXPERIMENTAL GD 2.2091 bindings (work in progress upstream).")
    set(GEODE_GD_VERSION 2.2091 CACHE INTERNAL "Geode GD Version")
    set(GEODE_COMP_GD_VERSION 22091 CACHE INTERNAL "Geode Comparable GD Version")
endif()

# Geode reads mod.json from this directory: generate it for the chosen GD
# version and for the version of the SDK in use.
file(READ "$ENV{GEODE_SDK}/VERSION" GDDS_GEODE_VERSION)
string(STRIP "${GDDS_GEODE_VERSION}" GDDS_GEODE_VERSION)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/mod.json.in ${CMAKE_CURRENT_SOURCE_DIR}/mod.json @ONLY)

file(GLOB_RECURSE GDDS_MOD_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(GDDecorationStudio SHARED ${GDDS_MOD_SOURCES})
target_include_directories(GDDecorationStudio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(GDDecorationStudio)
target_link_libraries(GDDecorationStudio gdds_core)
if (GDDS_GD_VERSION STREQUAL "2.2091")
    target_compile_definitions(GDDecorationStudio PRIVATE GDDS_GD_2091=1)
endif()
