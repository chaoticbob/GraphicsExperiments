cmake_minimum_required(VERSION 3.0)

project(405_gltf_full_material_test_vulkan)

add_executable(
    405_gltf_full_material_test_vulkan
    405_gltf_full_material_test_vulkan.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/vk_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/vk_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/cgltf_impl.cpp
    ${GREX_THIRD_PARTY_DIR}/cgltf/cgltf.h
    ${GREX_PROJECTS_COMMON_DIR}/faux_render.h
    ${GREX_PROJECTS_COMMON_DIR}/faux_render.cpp
    ${GREX_PROJECTS_COMMON_DIR}/vk_faux_render.h
    ${GREX_PROJECTS_COMMON_DIR}/vk_faux_render.cpp
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.h
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.cpp
    ${GREX_THIRD_PARTY_DIR}/glslang/StandAlone/resource_limits_c.cpp
    ${GREX_THIRD_PARTY_DIR}/glslang/StandAlone/ResourceLimits.cpp
)

set_target_properties(405_gltf_full_material_test_vulkan PROPERTIES FOLDER "io")

target_include_directories(
    405_gltf_full_material_test_vulkan
    PUBLIC  ${GREX_PROJECTS_COMMON_DIR}
        ${GREX_THIRD_PARTY_DIR}/glm # This needs to come before ${VULKAN_INCLUDE_DIR}
        ${GREX_THIRD_PARTY_DIR}/glslang # This needs to come before ${VULKAN_INCLUDE_DIR}
        ${VULKAN_INCLUDE_DIR}
        ${GREX_THIRD_PARTY_DIR}/VulkanMemoryAllocator/include
        ${GREX_THIRD_PARTY_DIR}/tinyobjloader
        ${GREX_THIRD_PARTY_DIR}/cgltf
        ${GREX_THIRD_PARTY_DIR}/stb
)

target_link_libraries(
    405_gltf_full_material_test_vulkan
    PUBLIC glfw
           glslang
           SPIRV
           dxcompiler
           ktx
)

if(WIN32)
    target_compile_definitions(
        405_gltf_full_material_test_vulkan
        PUBLIC VK_USE_PLATFORM_WIN32_KHR
    )

    target_link_libraries(
        405_gltf_full_material_test_vulkan
        PUBLIC "${VULKAN_LIBRARY_DIR}/vulkan-1.lib"
    )
endif()
