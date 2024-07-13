cmake_minimum_required(VERSION 3.5)

project(gltf_d3d12)

add_executable(
    gltf_d3d12
    gltf_d3d12.cpp
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

set_target_properties(gltf_d3d12 PROPERTIES FOLDER "misc")

target_include_directories(
    gltf_d3d12
    PUBLIC  ${GREX_PROJECTS_COMMON_DIR}
            ${GREX_THIRD_PARTY_DIR}/glm
            ${GREX_THIRD_PARTY_DIR}/tinyobjloader
            ${GREX_THIRD_PARTY_DIR}/cgltf
            ${GREX_THIRD_PARTY_DIR}/stb
)

target_link_libraries(
    gltf_d3d12
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           dxcompiler
)
