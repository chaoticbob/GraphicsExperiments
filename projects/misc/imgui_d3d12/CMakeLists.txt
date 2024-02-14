cmake_minimum_required(VERSION 3.5)

project(test_app_imgui_d3d12)

add_executable(
    test_app_imgui_d3d12
    test_app_imgui_d3d12.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.cpp
    ${IMGUI_D3D12_FILES}
)

set_target_properties(test_app_imgui_d3d12 PROPERTIES FOLDER "misc")

target_compile_definitions(
    test_app_imgui_d3d12
    PUBLIC ENABLE_IMGUI_D3D12
)

target_include_directories(
    test_app_imgui_d3d12
    PUBLIC  ${GREX_PROJECTS_COMMON_DIR}
            ${IMGUI_INC_DIR}
)

target_link_libraries(
    test_app_imgui_d3d12
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           d3dcompiler
           dxcompiler
)
