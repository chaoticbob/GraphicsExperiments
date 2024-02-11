cmake_minimum_required(VERSION 3.0)

project(021_raytracing_triangles_metal)

add_executable(
    ${PROJECT_NAME}
    ${PROJECT_NAME}.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer_utils.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer_utils.mm
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "raytracing")

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/metal-cpp
)

FIND_LIBRARY(FOUNDATION_LIBRARY Foundation)
FIND_LIBRARY(METAL_LIBRARY Metal)
FIND_LIBRARY(METALKIT_LIBRARY MetalKit)

target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC glfw
    ${FOUNDATION_LIBRARY}
    ${METAL_LIBRARY}
    ${METALKIT_LIBRARY}
)
