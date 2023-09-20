cmake_minimum_required(VERSION 3.5)

project(raygen)

add_executable(
    raygen
    raygen.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
)

target_include_directories(
    raygen
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
)

target_link_libraries(
    raygen
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           d3dcompiler
           dxcompiler
)
