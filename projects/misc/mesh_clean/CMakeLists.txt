cmake_minimum_required(VERSION 3.25)

project(mesh_clean)

add_executable(
    mesh_clean
    mesh_clean.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
)

set_target_properties(mesh_clean PROPERTIES FOLDER "misc")

target_include_directories(
    mesh_clean
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
)

target_link_libraries(
    mesh_clean
    PUBLIC meshoptimizer
)