cmake_minimum_required(VERSION 3.0)

project(102_cornell_box_d3d12)

add_executable(
    102_cornell_box_d3d12
    102_cornell_box_d3d12.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
)

set_target_properties(102_cornell_box_d3d12 PROPERTIES FOLDER "geometry")

target_include_directories(
    102_cornell_box_d3d12
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
)

target_link_libraries(
    102_cornell_box_d3d12
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           d3dcompiler
           dxcompiler
)
