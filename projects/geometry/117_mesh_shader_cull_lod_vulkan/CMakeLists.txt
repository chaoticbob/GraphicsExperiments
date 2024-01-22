cmake_minimum_required(VERSION 3.5)

project(117_mesh_shader_cull_lod_vulkan)

add_executable(
    117_mesh_shader_cull_lod_vulkan
    117_mesh_shader_cull_lod_vulkan.cpp
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
    ${IMGUI_VULKAN_FILES}
)

set_target_properties(117_mesh_shader_cull_lod_vulkan PROPERTIES FOLDER "geometry")

target_compile_definitions(
	117_mesh_shader_cull_lod_vulkan
	PUBLIC ENABLE_IMGUI_VULKAN
)

target_include_directories(
    117_mesh_shader_cull_lod_vulkan
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glslang # This needs to come before ${VULKAN_INCLUDE_DIR}
           ${VULKAN_INCLUDE_DIR}
           ${IMGUI_INC_DIR}
           ${GREX_THIRD_PARTY_DIR}/VulkanMemoryAllocator/include
           ${GREX_THIRD_PARTY_DIR}/glm
		   ${GREX_THIRD_PARTY_DIR}/tinyobjloader
		   ${GREX_THIRD_PARTY_DIR}/stb
)

target_link_libraries(
    117_mesh_shader_cull_lod_vulkan
    PUBLIC glfw
           glslang
           SPIRV
		   dxcompiler
           meshoptimizer
)

if(WIN32)
    target_compile_definitions(
        117_mesh_shader_cull_lod_vulkan
        PUBLIC VK_USE_PLATFORM_WIN32_KHR
    )

    target_link_libraries(
        117_mesh_shader_cull_lod_vulkan
        PUBLIC "${VULKAN_LIBRARY_DIR}/vulkan-1.lib"
    )
endif()
