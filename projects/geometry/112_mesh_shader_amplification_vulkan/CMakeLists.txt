cmake_minimum_required(VERSION 3.5)

project(112_mesh_shader_amplification_vulkan)

add_executable(
    112_mesh_shader_amplification_vulkan
    112_mesh_shader_amplification_vulkan.cpp
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
)

set_target_properties(112_mesh_shader_amplification_vulkan PROPERTIES FOLDER "geometry")

target_include_directories(
    112_mesh_shader_amplification_vulkan
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glslang # This needs to come before ${VULKAN_INCLUDE_DIR}
           ${VULKAN_INCLUDE_DIR}
           ${GREX_THIRD_PARTY_DIR}/VulkanMemoryAllocator/include
           ${GREX_THIRD_PARTY_DIR}/glm
		   ${GREX_THIRD_PARTY_DIR}/tinyobjloader
		   ${GREX_THIRD_PARTY_DIR}/stb
)

target_link_libraries(
    112_mesh_shader_amplification_vulkan
    PUBLIC glfw
           glslang
           SPIRV
		   dxcompiler
           meshoptimizer
)

target_compile_definitions(
    112_mesh_shader_amplification_vulkan
    PUBLIC GREX_ENABLE_VULKAN
)

if(WIN32)
    target_compile_definitions(
        112_mesh_shader_amplification_vulkan
        PUBLIC VK_USE_PLATFORM_WIN32_KHR
    )

    target_link_libraries(
        112_mesh_shader_amplification_vulkan
        PUBLIC "${VULKAN_LIBRARY_DIR}/vulkan-1.lib"
    )
elseif(LINUX)
    target_compile_definitions(
        112_mesh_shader_amplification_vulkan
        PUBLIC VK_USE_PLATFORM_XCB_KHR
    )
    target_link_libraries(
        112_mesh_shader_amplification_vulkan
        PUBLIC vulkan
    )
endif()
