cmake_minimum_required(VERSION 3.0)

project(316_normal_map_vs_pom_vulkan)

add_executable(
    316_normal_map_vs_pom_vulkan
    316_normal_map_vs_pom_vulkan.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/vk_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/vk_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.h
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.cpp
    ${GREX_THIRD_PARTY_DIR}/MikkTSpace/mikktspace.h
    ${GREX_THIRD_PARTY_DIR}/MikkTSpace/mikktspace.c
    ${GREX_THIRD_PARTY_DIR}/glslang/StandAlone/resource_limits_c.cpp
    ${GREX_THIRD_PARTY_DIR}/glslang/StandAlone/ResourceLimits.cpp
    ${IMGUI_VULKAN_FILES}
)

set_target_properties(316_normal_map_vs_pom_vulkan PROPERTIES FOLDER "texture")

target_compile_definitions(
    316_normal_map_vs_pom_vulkan
    PUBLIC TRIMESH_USE_MIKKTSPACE
           ENABLE_IMGUI_VULKAN
)

target_include_directories(
    316_normal_map_vs_pom_vulkan
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glslang # This needs to come before ${VULKAN_INCLUDE_DIR}
		   ${VULKAN_INCLUDE_DIR}
           ${GREX_THIRD_PARTY_DIR}/VulkanMemoryAllocator/include
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
           ${GREX_THIRD_PARTY_DIR}/MikkTSpace
           ${IMGUI_INC_DIR}
)

target_link_libraries(
    316_normal_map_vs_pom_vulkan
    PUBLIC glfw
           glslang
           SPIRV
           dxcompiler
)

if(WIN32)
    target_compile_definitions(
        316_normal_map_vs_pom_vulkan
        PUBLIC VK_USE_PLATFORM_WIN32_KHR
    )

    target_link_libraries(
        316_normal_map_vs_pom_vulkan
        PUBLIC "${VULKAN_LIBRARY_DIR}/vulkan-1.lib"
    )
endif()
