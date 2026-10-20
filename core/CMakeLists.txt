file(GLOB_RECURSE GDDS_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(gdds_core STATIC ${GDDS_CORE_SOURCES})
target_include_directories(gdds_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gdds_core PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(gdds_core PRIVATE /W4 /utf-8 /permissive- /wd4244 /wd4267)
    target_compile_definitions(gdds_core PUBLIC NOMINMAX _USE_MATH_DEFINES)
else()
    target_compile_options(gdds_core PRIVATE -Wall -Wextra)
endif()
