cmake_minimum_required(VERSION 3.0)

project(ibl_brdf_lut)

add_executable(
    ibl_brdf_lut
    ibl_brdf_lut.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
)

set_target_properties(ibl_brdf_lut PROPERTIES FOLDER "misc")

target_include_directories(
    ibl_brdf_lut
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/stb
)
