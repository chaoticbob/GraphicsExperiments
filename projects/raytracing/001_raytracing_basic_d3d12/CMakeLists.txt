cmake_minimum_required(VERSION 3.25)

project(001_raytracing_basic_d3d12)

add_executable(
    001_raytracing_basic_d3d12
    001_raytracing_basic_d3d12.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
)

set_target_properties(001_raytracing_basic_d3d12 PROPERTIES FOLDER "raytracing")

target_include_directories(
    001_raytracing_basic_d3d12
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
)

target_link_libraries(
    001_raytracing_basic_d3d12
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           d3dcompiler
           dxcompiler
)
