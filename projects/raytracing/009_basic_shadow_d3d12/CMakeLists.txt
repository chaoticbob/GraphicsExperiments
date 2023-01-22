cmake_minimum_required(VERSION 3.0)

project(009_basic_shadow_d3d12)

add_executable(
    009_basic_shadow_d3d12
    009_basic_shadow_d3d12.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/sphereflake.h
    ${GREX_PROJECTS_COMMON_DIR}/sphereflake.cpp
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
)

set_target_properties(009_basic_shadow_d3d12 PROPERTIES FOLDER "raytracing")

target_include_directories(
    009_basic_shadow_d3d12
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
)

target_link_libraries(
    009_basic_shadow_d3d12
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           d3dcompiler
           dxcompiler
)