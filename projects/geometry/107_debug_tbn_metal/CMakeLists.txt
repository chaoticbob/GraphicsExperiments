cmake_minimum_required(VERSION 3.0)

project(107_debug_tbn_metal)

add_executable(
    107_debug_tbn_metal
    107_debug_tbn_metal.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer_utils.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer_utils.mm
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
    ${IMGUI_METAL_FILES}
    ${GREX_THIRD_PARTY_DIR}/MikkTSpace/mikktspace.h
    ${GREX_THIRD_PARTY_DIR}/MikkTSpace/mikktspace.c
)

set_target_properties(107_debug_tbn_metal PROPERTIES FOLDER "geometry")

target_compile_definitions(
    107_debug_tbn_metal
    PUBLIC ENABLE_IMGUI_METAL
	IMGUI_IMPL_METAL_CPP
    TRIMESH_USE_MIKKTSPACE
)

target_include_directories(
    107_debug_tbn_metal
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
           ${GREX_THIRD_PARTY_DIR}/metal-cpp
           ${IMGUI_INC_DIR}
           ${GREX_THIRD_PARTY_DIR}/MikkTSpace
)

FIND_LIBRARY(FOUNDATION_LIBRARY Foundation)
FIND_LIBRARY(METAL_LIBRARY Metal)
FIND_LIBRARY(METALKIT_LIBRARY MetalKit)

target_link_libraries(
    107_debug_tbn_metal
    PUBLIC glfw
    ${FOUNDATION_LIBRARY}
    ${METAL_LIBRARY}
    ${METALKIT_LIBRARY}
)
