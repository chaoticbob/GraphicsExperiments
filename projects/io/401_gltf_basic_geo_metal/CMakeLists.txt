cmake_minimum_required(VERSION 3.5)

project(401_gltf_basic_geo_metal)

add_executable(
    401_gltf_basic_geo_metal
    401_gltf_basic_geo_metal.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer_utils.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer_utils.mm
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/cgltf_impl.cpp
    ${GREX_THIRD_PARTY_DIR}/cgltf/cgltf.h
    ${GREX_PROJECTS_COMMON_DIR}/faux_render.h
    ${GREX_PROJECTS_COMMON_DIR}/faux_render.cpp
    ${GREX_PROJECTS_COMMON_DIR}/mtl_faux_render.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_faux_render.cpp
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.h
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.cpp
    ${GREX_THIRD_PARTY_DIR}/glslang/StandAlone/resource_limits_c.cpp
    ${GREX_THIRD_PARTY_DIR}/glslang/StandAlone/ResourceLimits.cpp
)

set_target_properties(401_gltf_basic_geo_metal PROPERTIES FOLDER "io")

target_include_directories(
    401_gltf_basic_geo_metal
    PUBLIC  ${GREX_PROJECTS_COMMON_DIR}
        ${GREX_THIRD_PARTY_DIR}/glm
        ${GREX_THIRD_PARTY_DIR}/tinyobjloader
        ${GREX_THIRD_PARTY_DIR}/cgltf
        ${GREX_THIRD_PARTY_DIR}/stb
        ${GREX_THIRD_PARTY_DIR}/MikkTSpace
        ${GREX_THIRD_PARTY_DIR}/metal-cpp
)

FIND_LIBRARY(FOUNDATION_LIBRARY Foundation)
FIND_LIBRARY(METAL_LIBRARY Metal)
FIND_LIBRARY(METALKIT_LIBRARY MetalKit)

target_link_libraries(
    401_gltf_basic_geo_metal
    PUBLIC glfw
           glslang
           ${FOUNDATION_LIBRARY}
           ${METAL_LIBRARY}
           ${METALKIT_LIBRARY}
)
