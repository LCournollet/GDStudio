cmake_minimum_required(VERSION 3.21)
# Geode v5 requires C++23 (the core library itself only needs C++20).
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

project(GDDecorationStudio VERSION 0.1.0 LANGUAGES C CXX)

# The decoration engine lives in a pure C++ library (core/) with no Geode
# dependency, so it can be unit-tested and driven from the command line.
# The Geode mod (mod/) is a thin adapter between that library and the editor.
option(GDDS_BUILD_MOD   "Build the Geode mod (requires GEODE_SDK)" ON)
option(GDDS_BUILD_TESTS "Build the core unit tests"                OFF)
option(GDDS_BUILD_TOOLS "Build the command line tools"             OFF)

add_subdirectory(core)

if (GDDS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if (GDDS_BUILD_TOOLS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/cli/CMakeLists.txt)
    add_subdirectory(tools/cli)
endif()

if (GDDS_BUILD_MOD)
    add_subdirectory(mod)
endif()
