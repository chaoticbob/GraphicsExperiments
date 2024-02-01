cmake_minimum_required(VERSION 3.5)

project(114_mesh_shader_culling_metal_ios)

list(APPEND SRC_FILES
    114_mesh_shader_culling_metal_ios.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/camera.h
    ${GREX_PROJECTS_COMMON_DIR}/camera.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
    #${IMGUI_METAL_FILES}
)

list(APPEND GLFM_APP_ASSETS
    ${GREX_ASSETS_DIR}/projects/114_mesh_shader_culling/shaders.msl
    ${GREX_ASSETS_DIR}/models/horse_statue_01_1k.obj
)

set(GLFM_APP_TARGET_NAME 114_mesh_shader_culling_metal_ios)
set(GLFM_APP_SRC ${SRC_FILES})
include(GLFMAppTarget)

set_source_files_properties(${GREX_ASSETS_DIR}/models/horse_statue_01_1k.obj PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/models")
set_source_files_properties(${GREX_ASSETS_DIR}/projects/114_mesh_shader_culling/shaders.msl PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/projects/114_mesh_shader_culling")

set_target_properties(114_mesh_shader_culling_metal_ios PROPERTIES FOLDER "geometry")

#target_compile_definitions(
#    114_mesh_shader_culling_metal_ios
#    PUBLIC ENABLE_IMGUI_METAL
#	IMGUI_IMPL_METAL_CPP
#)

target_include_directories(
    114_mesh_shader_culling_metal_ios
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
           ${GREX_THIRD_PARTY_DIR}/metal-cpp
           ${GREX_THIRD_PARTY_DIR}/metal-cpp-extensions
           ${GREX_THIRD_PARTY_DIR}/glfm/examples/deps
           #${IMGUI_INC_DIR}
)

target_link_libraries(
    114_mesh_shader_culling_metal_ios
    PUBLIC glfm
    meshoptimizer
)
