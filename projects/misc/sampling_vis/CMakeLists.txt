cmake_minimum_required(VERSION 3.25)

project(sampling_vis)

add_executable(
    sampling_vis
    sampling_vis.cpp
    sampling.h
    sampling.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/dx_draw_context.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_draw_context.cpp
    ${GREX_PROJECTS_COMMON_DIR}/line_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/line_mesh.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
    ${IMGUI_D3D12_FILES}
)

set_target_properties(sampling_vis PROPERTIES FOLDER "misc")

target_compile_definitions(
    sampling_vis
    PUBLIC ENABLE_IMGUI_D3D12
)

target_include_directories(
    sampling_vis
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${IMGUI_INC_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/pcg32
)

target_link_libraries(
    sampling_vis
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           d3dcompiler
           dxcompiler
)
