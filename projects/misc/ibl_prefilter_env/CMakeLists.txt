cmake_minimum_required(VERSION 3.25)

project(ibl_prefilter_env)

add_executable(
    ibl_prefilter_env
    ibl_prefilter_env.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.h
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
)

set_target_properties(ibl_prefilter_env PROPERTIES FOLDER "misc")

target_include_directories(
    ibl_prefilter_env
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/stb
           ${GREX_THIRD_PARTY_DIR}/glfw/include
           ${GREX_THIRD_PARTY_DIR}/pcg32
)

target_link_libraries(
    ibl_prefilter_env
    PUBLIC glfw
)
