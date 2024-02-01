cmake_minimum_required(VERSION 3.5)

project(110_mesh_shader_triangle_metal_ios)

list(APPEND SRC_FILES
    110_mesh_shader_triangle_metal_ios.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer.cpp
)

set(GLFM_APP_TARGET_NAME 110_mesh_shader_triangle_metal_ios)
set(GLFM_APP_SRC ${SRC_FILES})
include(GLFMAppTarget)

set_target_properties(110_mesh_shader_triangle_metal_ios PROPERTIES FOLDER "geometry")

target_include_directories(
    110_mesh_shader_triangle_metal_ios
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
           ${GREX_THIRD_PARTY_DIR}/metal-cpp
           ${GREX_THIRD_PARTY_DIR}/metal-cpp-extensions
)

target_link_libraries(
    110_mesh_shader_triangle_metal_ios
    PUBLIC glfm
)
