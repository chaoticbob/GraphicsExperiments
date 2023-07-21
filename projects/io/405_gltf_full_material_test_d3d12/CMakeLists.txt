cmake_minimum_required(VERSION 3.0)

project(405_gltf_full_material_test_d3d12)

add_executable(
    405_gltf_full_material_test_d3d12
    405_gltf_full_material_test_d3d12.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/cgltf_impl.cpp
	${GREX_THIRD_PARTY_DIR}/cgltf/cgltf.h
    ${GREX_PROJECTS_COMMON_DIR}/faux_render.h
    ${GREX_PROJECTS_COMMON_DIR}/faux_render.cpp
    ${GREX_PROJECTS_COMMON_DIR}/dx_faux_render.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_faux_render.cpp
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.h
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.cpp
)

set_target_properties(405_gltf_full_material_test_d3d12 PROPERTIES FOLDER "io")

target_include_directories(
    405_gltf_full_material_test_d3d12
    PUBLIC  ${GREX_PROJECTS_COMMON_DIR}
            ${GREX_THIRD_PARTY_DIR}/glm
            ${GREX_THIRD_PARTY_DIR}/tinyobjloader
            ${GREX_THIRD_PARTY_DIR}/cgltf
            ${GREX_THIRD_PARTY_DIR}/stb
)

target_link_libraries(
    405_gltf_full_material_test_d3d12
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           dxcompiler
           ktx
)
