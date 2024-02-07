cmake_minimum_required(VERSION 3.5)

project(118_mesh_shader_vertex_attrs_vulkan)

set(SHADER_FILES ${GREX_ASSETS_DIR}/projects/118_mesh_shader_vertex_attrs/shaders.hlsl)

add_executable(
    ${PROJECT_NAME}
    ${PROJECT_NAME}.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/vk_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/vk_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/camera.h
    ${GREX_PROJECTS_COMMON_DIR}/camera.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
    ${GREX_THIRD_PARTY_DIR}/glslang/StandAlone/resource_limits_c.cpp
    ${GREX_THIRD_PARTY_DIR}/glslang/StandAlone/ResourceLimits.cpp
    ${SHADER_FILES}
)

set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "geometry")

set_property(SOURCE ${SHADER_FILES} PROPERTY VS_SETTINGS "ExcludedFromBuild=true")

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glslang # This needs to come before ${VULKAN_INCLUDE_DIR}
           ${VULKAN_INCLUDE_DIR}
           ${GREX_THIRD_PARTY_DIR}/VulkanMemoryAllocator/include
           ${GREX_THIRD_PARTY_DIR}/glm
		   ${GREX_THIRD_PARTY_DIR}/tinyobjloader
		   ${GREX_THIRD_PARTY_DIR}/stb
)

target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC glfw
           glslang
           SPIRV
		   dxcompiler
           meshoptimizer
)

if(WIN32)
    target_compile_definitions(
        ${PROJECT_NAME}
        PUBLIC VK_USE_PLATFORM_WIN32_KHR
    )

    target_link_libraries(
        ${PROJECT_NAME}
        PUBLIC "${VULKAN_LIBRARY_DIR}/vulkan-1.lib"
    )
endif()
