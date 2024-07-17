cmake_minimum_required(VERSION 3.25)

project(116_mesh_shader_calc_lod_d3d12)

add_executable(
    116_mesh_shader_calc_lod_d3d12
    116_mesh_shader_calc_lod_d3d12.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/camera.h
    ${GREX_PROJECTS_COMMON_DIR}/camera.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
    ${IMGUI_D3D12_FILES}
)

set_target_properties(116_mesh_shader_calc_lod_d3d12 PROPERTIES FOLDER "geometry")

target_compile_definitions(
	116_mesh_shader_calc_lod_d3d12
	PUBLIC GREX_USE_D3DX12
           ENABLE_IMGUI_D3D12
)	

target_include_directories(
    116_mesh_shader_calc_lod_d3d12	
    PUBLIC ${GREX_THIRD_PARTY_DIR}/DirectX-Headers/include
	       ${GREX_PROJECTS_COMMON_DIR}
           ${IMGUI_INC_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
)

target_link_libraries(
    116_mesh_shader_calc_lod_d3d12
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           d3dcompiler
           dxcompiler
           meshoptimizer
)
