cmake_minimum_required(VERSION 3.25)

project(501_barriers_d3d12)

add_executable(
    ${PROJECT_NAME}
    ${PROJECT_NAME}.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/dx_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "api")

if (GREX_AGILITY_SDK)
	message("  ${PROJECT_NAME} using Agility SDK - Don't forget to NuGet it for this project")

	target_compile_definitions(
		${PROJECT_NAME}
		PUBLIC 
			GREX_USE_AGILITY_SDK
			GREX_AGILITY_SDK_VERSION=${GREX_AGILITY_SDK_VERSION}
			GREX_AGILITY_SDK_PATH="${GREX_AGILITY_RELATIVE_SDK_PATH}"
	)
endif()

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
)

target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC glfw
           d3d12
           dxgi
           dxguid
           d3dcompiler
           dxcompiler
)
