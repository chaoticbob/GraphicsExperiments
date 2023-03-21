cmake_minimum_required(VERSION 3.0)

project(ibl_furnace)

add_executable(
    ibl_furnace
    ibl_furnace.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
)

set_target_properties(ibl_furnace PROPERTIES FOLDER "misc")

target_include_directories(
    ibl_furnace
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/stb
)
