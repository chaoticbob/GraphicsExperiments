cmake_minimum_required(VERSION 3.0)

project(006_basic_shadow_dynamic_metal)

add_executable(
    006_basic_shadow_dynamic_metal
    006_basic_shadow_dynamic_metal.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer_utils.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer_utils.mm
    ${GREX_PROJECTS_COMMON_DIR}/sphereflake.h
    ${GREX_PROJECTS_COMMON_DIR}/sphereflake.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
)

set_target_properties(006_basic_shadow_dynamic_metal PROPERTIES FOLDER "raytracing")

target_include_directories(
    006_basic_shadow_dynamic_metal
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/metal-cpp
)

FIND_LIBRARY(FOUNDATION_LIBRARY Foundation)
FIND_LIBRARY(METAL_LIBRARY Metal)
FIND_LIBRARY(METALKIT_LIBRARY MetalKit)

target_link_libraries(
    006_basic_shadow_dynamic_metal
    PUBLIC glfw
    ${FOUNDATION_LIBRARY}
    ${METAL_LIBRARY}
    ${METALKIT_LIBRARY}
)