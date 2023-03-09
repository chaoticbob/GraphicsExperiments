cmake_minimum_required(VERSION 3.0)

project(107_debug_tbn_d3d12)

add_executable(
    107_debug_tbn_d3d12
    107_debug_tbn_d3d12.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
    ${IMGUI_D3D12_FILES}
    ${GREX_THIRD_PARTY_DIR}/MikkTSpace/mikktspace.h
    ${GREX_THIRD_PARTY_DIR}/MikkTSpace/mikktspace.c
)

set_target_properties(107_debug_tbn_d3d12 PROPERTIES FOLDER "geometry")

target_compile_definitions(
    107_debug_tbn_d3d12
    PUBLIC ENABLE_IMGUI_D3D12
           TRIMESH_USE_MIKKTSPACE
)

target_include_directories(
    107_debug_tbn_d3d12
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
           ${IMGUI_INC_DIR}
           ${GREX_THIRD_PARTY_DIR}/MikkTSpace
)

target_link_libraries(
    107_debug_tbn_d3d12
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           d3dcompiler
           dxcompiler
)