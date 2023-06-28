cmake_minimum_required(VERSION 3.0)

project(306_parallax_occlusion_map_d3d12)

add_executable(
    306_parallax_occlusion_map_d3d12
    306_parallax_occlusion_map_d3d12.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.h
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.cpp
    ${GREX_THIRD_PARTY_DIR}/MikkTSpace/mikktspace.h
    ${GREX_THIRD_PARTY_DIR}/MikkTSpace/mikktspace.c
)

set_target_properties(306_parallax_occlusion_map_d3d12 PROPERTIES FOLDER "texture")

target_compile_definitions(
    306_parallax_occlusion_map_d3d12
    PUBLIC TRIMESH_USE_MIKKTSPACE
)

target_include_directories(
    306_parallax_occlusion_map_d3d12
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
           ${GREX_THIRD_PARTY_DIR}/MikkTSpace
)

target_link_libraries(
    306_parallax_occlusion_map_d3d12
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           d3dcompiler
           dxcompiler
)
