cmake_minimum_required(VERSION 3.0)

project(034_raytracing_path_trace_pbr_vulkan)

add_executable(
    034_raytracing_path_trace_pbr_vulkan
    034_raytracing_path_trace_pbr_vulkan.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/vk_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/vk_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.h
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
    #${IMGUI_VULKAN_FILES}
)

set_target_properties(034_raytracing_path_trace_pbr_vulkan PROPERTIES FOLDER "raytracing")

#target_compile_definitions(
#    034_raytracing_path_trace_pbr_vulkan
#    PUBLIC ENABLE_IMGUI_VULKAN
#)

target_include_directories(
    034_raytracing_path_trace_pbr_vulkan
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
		   ${VULKAN_INCLUDE_DIR}
           ${GREX_THIRD_PARTY_DIR}/VulkanMemoryAllocator/include
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
           ${IMGUI_INC_DIR}
)

target_link_libraries(
    034_raytracing_path_trace_pbr_vulkan
    PUBLIC glfw
           dxcompiler
)

if(WIN32)
    target_compile_definitions(
        034_raytracing_path_trace_pbr_vulkan
        PUBLIC VK_USE_PLATFORM_WIN32_KHR
    )

    target_link_libraries(
        034_raytracing_path_trace_pbr_vulkan
        PUBLIC "${VULKAN_LIBRARY_DIR}/vulkan-1.lib"
    )
endif()