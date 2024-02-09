cmake_minimum_required(VERSION 3.5)

project(119_mesh_shader_bary_d3d12)

set(SHADER_FILES ${GREX_ASSETS_DIR}/projects/119_mesh_shader_bary/shaders.hlsl)

add_executable(
    ${PROJECT_NAME}
    ${PROJECT_NAME}.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/camera.h
    ${GREX_PROJECTS_COMMON_DIR}/camera.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
    ${SHADER_FILES}
)

set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "geometry")

set_property(SOURCE ${SHADER_FILES} PROPERTY VS_SETTINGS "ExcludedFromBuild=true")

target_compile_definitions(
	${PROJECT_NAME}
	PUBLIC GREX_USE_D3DX12
)	

target_include_directories(
    ${PROJECT_NAME}	
    PUBLIC ${GREX_THIRD_PARTY_DIR}/DirectX-Headers/include
	       ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
)

target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           d3dcompiler
           dxcompiler
           meshoptimizer
)
