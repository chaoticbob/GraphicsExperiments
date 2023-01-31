cmake_minimum_required(VERSION 3.0)

project(103_cornell_box_d3d12)

add_executable(
    103_cornell_box_d3d12
    103_cornell_box_d3d12.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
)

set_target_properties(103_cornell_box_d3d12 PROPERTIES FOLDER "geometry")

target_include_directories(
    103_cornell_box_d3d12
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
)

target_link_libraries(
    103_cornell_box_d3d12
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           d3dcompiler
           dxcompiler
)