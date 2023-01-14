cmake_minimum_required(VERSION 3.0)

project(0001_d3d12_basic_ray_tracing)

add_executable(
    0001_d3d12_basic_ray_tracing
    0001_d3d12_basic_ray_tracing.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
)

target_include_directories(
    0001_d3d12_basic_ray_tracing
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
)

target_link_libraries(
    0001_d3d12_basic_ray_tracing
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           d3dcompiler
           dxcompiler
)