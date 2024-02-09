cmake_minimum_required(VERSION 3.5)

project(112_mesh_shader_amplification_d3d12)

add_executable(
    112_mesh_shader_amplification_d3d12
    112_mesh_shader_amplification_d3d12.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/camera.h
    ${GREX_PROJECTS_COMMON_DIR}/camera.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
)

set_target_properties(112_mesh_shader_amplification_d3d12 PROPERTIES FOLDER "geometry")

target_compile_definitions(
	112_mesh_shader_amplification_d3d12
	PUBLIC GREX_USE_D3DX12
)	

target_include_directories(
    112_mesh_shader_amplification_d3d12	
    PUBLIC ${GREX_THIRD_PARTY_DIR}/DirectX-Headers/include
	       ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
)

target_link_libraries(
    112_mesh_shader_amplification_d3d12
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           d3dcompiler
           dxcompiler
           meshoptimizer
)
