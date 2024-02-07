cmake_minimum_required(VERSION 3.5)

project(110_mesh_shader_triangle_d3d12)

add_executable(
    110_mesh_shader_triangle_d3d12
    110_mesh_shader_triangle_d3d12.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
)

set_target_properties(110_mesh_shader_triangle_d3d12 PROPERTIES FOLDER "geometry")

target_compile_definitions(
	110_mesh_shader_triangle_d3d12
	PUBLIC GREX_USE_D3DX12
)	

target_include_directories(
    110_mesh_shader_triangle_d3d12	
    PUBLIC ${GREX_THIRD_PARTY_DIR}/DirectX-Headers/include
	       ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
)

target_link_libraries(
    110_mesh_shader_triangle_d3d12
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           d3dcompiler
           dxcompiler
)
