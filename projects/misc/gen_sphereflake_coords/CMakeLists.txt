cmake_minimum_required(VERSION 3.5)

project(gen_sphereflake_coords)

add_executable(
    gen_sphereflake_coords
    gen_sphereflake_coords.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
)

set_target_properties(gen_sphereflake_coords PROPERTIES FOLDER "misc")

target_include_directories(
    gen_sphereflake_coords
    PUBLIC  ${GREX_PROJECTS_COMMON_DIR}
            ${GREX_THIRD_PARTY_DIR}/glm
)
