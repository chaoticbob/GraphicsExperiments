cmake_minimum_required(VERSION 3.5)

project(403_gltf_basic_material_vulkan)

add_executable(
    403_gltf_basic_material_vulkan
    403_gltf_basic_material_vulkan.cpp
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

set_target_properties(403_gltf_basic_material_vulkan PROPERTIES FOLDER "io")

target_include_directories(
    403_gltf_basic_material_vulkan
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
    403_gltf_basic_material_vulkan
    PUBLIC glfw
           glslang
           SPIRV
           dxcompiler
           ktx
)

if(WIN32)
    target_compile_definitions(
        403_gltf_basic_material_vulkan
        PUBLIC VK_USE_PLATFORM_WIN32_KHR
    )

    target_link_libraries(
        403_gltf_basic_material_vulkan
        PUBLIC "${VULKAN_LIBRARY_DIR}/vulkan-1.lib"
    )
endif()
