cmake_minimum_required(VERSION 3.25)

project(window_events)

add_executable(
    window_events
    window_events.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
)

set_target_properties(window_events PROPERTIES FOLDER "misc")

target_include_directories(
    window_events
    PUBLIC  ${GREX_PROJECTS_COMMON_DIR}
)

target_link_libraries(
    window_events
    PUBLIC glfw
)
