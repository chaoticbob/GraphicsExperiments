cmake_minimum_required(VERSION 3.25)

project(202_pbr_camera_metal)

add_executable(
    202_pbr_camera_metal
    202_pbr_camera_metal.cpp
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
    ${GREX_THIRD_PARTY_DIR}/MikkTSpace/mikktspace.h
    ${GREX_THIRD_PARTY_DIR}/MikkTSpace/mikktspace.c
    ${IMGUI_METAL_FILES}
)

set_target_properties(202_pbr_camera_metal PROPERTIES FOLDER "pbr")

target_compile_definitions(
    202_pbr_camera_metal
    PUBLIC TRIMESH_USE_MIKKTSPACE
    ENABLE_IMGUI_METAL
    IMGUI_IMPL_METAL_CPP
)

target_compile_definitions(
    202_pbr_camera_metal
    PUBLIC ENABLE_IMGUI_METAL
)

target_include_directories(
    202_pbr_camera_metal
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
           ${GREX_THIRD_PARTY_DIR}/metal-cpp
           ${GREX_THIRD_PARTY_DIR}/MikkTSpace
           ${IMGUI_INC_DIR}
)

FIND_LIBRARY(FOUNDATION_LIBRARY Foundation)
FIND_LIBRARY(METAL_LIBRARY Metal)
FIND_LIBRARY(METALKIT_LIBRARY MetalKit)

target_link_libraries(
    202_pbr_camera_metal
    PUBLIC glfw
    ${FOUNDATION_LIBRARY}
    ${METAL_LIBRARY}
    ${METALKIT_LIBRARY}
)
