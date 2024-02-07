cmake_minimum_required(VERSION 3.5)

project(normal_map_convert)

add_executable(
    normal_map_convert
    normal_map_convert.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.h
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_THIRD_PARTY_DIR}/miniz-3.0.2/miniz.h
    ${GREX_THIRD_PARTY_DIR}/miniz-3.0.2/miniz.c
)

set_target_properties(normal_map_convert PROPERTIES FOLDER "misc")

target_compile_definitions(
    normal_map_convert
    PUBLIC GREX_ENABLE_EXR
)

target_include_directories(
    normal_map_convert
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/stb
           ${GREX_THIRD_PARTY_DIR}/tinyexr
           ${GREX_THIRD_PARTY_DIR}/miniz-3.0.2
           ${GREX_THIRD_PARTY_DIR}/pcg32
)

target_link_libraries(
    normal_map_convert
    PUBLIC glfw
)
