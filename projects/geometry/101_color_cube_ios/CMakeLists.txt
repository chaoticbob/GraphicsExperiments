cmake_minimum_required(VERSION 3.5)

project(101_color_cube_ios)

FILE(GLOB iosFiles 
	${GREX_PROJECTS_COMMON_DIR}/ios/*.h
	${GREX_PROJECTS_COMMON_DIR}/ios/*.m
	${GREX_PROJECTS_COMMON_DIR}/ios/UIKit/*.h
	${GREX_PROJECTS_COMMON_DIR}/ios/UIKit/*.m
)

add_executable(
    101_color_cube_ios
    101_color_cube_ios.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer_utils.h
    ${GREX_PROJECTS_COMMON_DIR}/mtl_renderer_utils.mm
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.mm
	${iosFiles}
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
)

set_target_properties(101_color_cube_ios PROPERTIES FOLDER "geometry")

#set(CMAKE_SYSTEM_NAME "iOS")
#set(CMAKE_OSX_SYSROOT "iOS")
#set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0")

#SET(CMAKE_SYSTEM_NAME Darwin)
#SET(CMAKE_SYSTEM_VERSION 13)
#SET(DARWIN_TARGET_OS_NAME ios)

target_compile_definitions(
	101_color_cube_ios
    PUBLIC TARGET_IOS
)

target_include_directories(
    101_color_cube_ios
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
           ${GREX_THIRD_PARTY_DIR}/metal-cpp
)

target_link_libraries(
	101_color_cube_ios
	PUBLIC 
		"-framework Metal"
		"-framework UIKit"
)

