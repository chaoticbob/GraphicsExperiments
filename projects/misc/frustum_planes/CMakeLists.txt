cmake_minimum_required(VERSION 3.25)

project(frustum_planes)

add_executable(
    frustum_planes
    frustum_planes.cpp
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
    ${GREX_PROJECTS_COMMON_DIR}/camera.h
    ${GREX_PROJECTS_COMMON_DIR}/camera.cpp
    ${IMGUI_D3D12_FILES}
    ${GREX_THIRD_PARTY_DIR}/imGuIZMO.quat/imGuIZMO.quat/imGuIZMOquat.h
    ${GREX_THIRD_PARTY_DIR}/imGuIZMO.quat/imGuIZMO.quat/imGuIZMOquat.cpp
)

set_target_properties(frustum_planes PROPERTIES FOLDER "misc")

target_compile_definitions(
    frustum_planes
    PUBLIC ENABLE_IMGUI_D3D12
           IMGUIZMO_IMGUI_FOLDER=${IMGUI_INC_DIR}/
           VGIZMO_USES_GLM
)

target_include_directories(
    frustum_planes
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${IMGUI_INC_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/pcg32
           ${GREX_THIRD_PARTY_DIR}/imGuIZMO.quat/imGuIZMO.quat
)

target_link_libraries(
    frustum_planes
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           d3dcompiler
           dxcompiler
)
