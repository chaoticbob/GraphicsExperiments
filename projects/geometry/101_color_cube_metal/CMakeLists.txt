cmake_minimum_required(VERSION 3.0)

project(101_color_cube_metal)

add_executable(
    101_color_cube_metal
    101_color_cube_metal.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/mt_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/mt_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
)

set_target_properties(101_color_cube_metal PROPERTIES FOLDER "geometry")

target_include_directories(
    101_color_cube_metal
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
           ${GREX_THIRD_PARTY_DIR}/metal-cpp
           ${GREX_THIRD_PARTY_DIR}/metal-cpp-extensions
)

FIND_LIBRARY(FOUNDATION_LIBRARY Foundation)
FIND_LIBRARY(METAL_LIBRARY Metal)
FIND_LIBRARY(METALKIT_LIBRARY MetalKit)

target_link_libraries(
    101_color_cube_metal
    PUBLIC glfw
    ${FOUNDATION_LIBRARY}
    ${METAL_LIBRARY}
    ${METALKIT_LIBRARY}
)
