cmake_minimum_required(VERSION 3.5)

project(202_pbr_camera_d3d12)

add_executable(
    202_pbr_camera_d3d12
    202_pbr_camera_d3d12.cpp
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
    ${IMGUI_D3D12_FILES}
)

set_target_properties(202_pbr_camera_d3d12 PROPERTIES FOLDER "pbr")

target_compile_definitions(
    202_pbr_camera_d3d12
    PUBLIC TRIMESH_USE_MIKKTSPACE
)

target_compile_definitions(
    202_pbr_camera_d3d12
    PUBLIC ENABLE_IMGUI_D3D12
)

target_include_directories(
    202_pbr_camera_d3d12
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
           ${GREX_THIRD_PARTY_DIR}/MikkTSpace
           ${IMGUI_INC_DIR}
)

target_link_libraries(
    202_pbr_camera_d3d12
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           d3dcompiler
           dxcompiler
)
