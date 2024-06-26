cmake_minimum_required(VERSION 3.5)

project(251_pbr_explorer_metal)

add_executable(
    251_pbr_explorer_metal
    251_pbr_explorer_metal.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer_utils.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer_utils.mm
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.h
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
    ${IMGUI_METAL_FILES}
)

set_target_properties(251_pbr_explorer_metal PROPERTIES FOLDER "pbr")

target_compile_definitions(
    251_pbr_explorer_metal
    PUBLIC ENABLE_IMGUI_METAL
    IMGUI_IMPL_METAL_CPP
)

target_include_directories(
    251_pbr_explorer_metal
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
           ${GREX_THIRD_PARTY_DIR}/metal-cpp
           ${IMGUI_INC_DIR}
)

FIND_LIBRARY(FOUNDATION_LIBRARY Foundation)
FIND_LIBRARY(METAL_LIBRARY Metal)
FIND_LIBRARY(METALKIT_LIBRARY MetalKit)

target_link_libraries(
    251_pbr_explorer_metal
    PUBLIC glfw
    ${FOUNDATION_LIBRARY}
    ${METAL_LIBRARY}
    ${METALKIT_LIBRARY}
)
