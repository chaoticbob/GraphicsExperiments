cmake_minimum_required(VERSION 3.0)

project(201_pbr_spheres_d3d12)

add_executable(
    201_pbr_spheres_d3d12
    201_pbr_spheres_d3d12.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.h
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
    ${IMGUI_D3D12_FILES}
)

set_target_properties(201_pbr_spheres_d3d12 PROPERTIES FOLDER "pbr")

target_compile_definitions(
    201_pbr_spheres_d3d12
    PUBLIC ENABLE_IMGUI_D3D12
)

target_include_directories(
    201_pbr_spheres_d3d12
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/stb
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${IMGUI_INC_DIR}
)

target_link_libraries(
    201_pbr_spheres_d3d12
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           d3dcompiler
           dxcompiler
)