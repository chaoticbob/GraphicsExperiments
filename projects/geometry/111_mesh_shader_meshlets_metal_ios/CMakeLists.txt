cmake_minimum_required(VERSION 3.5)

project(111_mesh_shader_meshlets_metal_ios)

list(APPEND SRC_FILES
    111_mesh_shader_meshlets_metal_ios.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/camera.h
    ${GREX_PROJECTS_COMMON_DIR}/camera.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
)

list(APPEND GLFM_APP_ASSETS
    ${GREX_ASSETS_DIR}/projects/111_mesh_shader_meshlets/shaders.msl
    ${GREX_ASSETS_DIR}/models/horse_statue_01_1k.obj
)

set(GLFM_APP_TARGET_NAME 111_mesh_shader_meshlets_metal_ios)
set(GLFM_APP_SRC ${SRC_FILES})
include(GLFMAppTarget)

set_source_files_properties(${GREX_ASSETS_DIR}/models/horse_statue_01_1k.obj PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/models")
set_source_files_properties(${GREX_ASSETS_DIR}/projects/111_mesh_shader_meshlets/shaders.msl PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/projects/111_mesh_shader_meshlets")

#set_property(SOURCE ${GREX_ASSETS_DIR}/models/horse_statue_01_1k.obj PROPERTY MACOSX_PACKAGE_LOCATION Resources/models)
#set_property(SOURCE ${GREX_ASSETS_DIR}/projects/111_mesh_shader_meshlets/shaders.msl PROPERTY MACOSX_PACKAGE_LOCATION "Resources/projects/111_mesh_shader_meshlets")


#set_target_properties(111_mesh_shader_meshlets_metal_ios PROPERTIES
#  MACOSX_BUNDLE TRUE
#  MACOSX_FRAMEWORK_IDENTIFIER com.grex.111_mesh_shader_meshlets_metal_ios
#  RESOURCE "${GREX_ASSETS_DIR}/projects/111_mesh_shader_meshlets/shaders.metal")

set_target_properties(111_mesh_shader_meshlets_metal_ios PROPERTIES FOLDER "geometry")

target_include_directories(
    111_mesh_shader_meshlets_metal_ios
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
           ${GREX_THIRD_PARTY_DIR}/metal-cpp
           ${GREX_THIRD_PARTY_DIR}/metal-cpp-extensions
           ${GREX_THIRD_PARTY_DIR}/glfm/examples/deps
)

target_link_libraries(
    111_mesh_shader_meshlets_metal_ios
    PUBLIC glfm
           meshoptimizer
)
