cmake_minimum_required(VERSION 3.5)

project(001_raytracing_basic_vulkan)

set(TARGET_NAME ${PROJECT_NAME})

add_executable(
    001_raytracing_basic_vulkan
    001_raytracing_basic_vulkan.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/vk_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/vk_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_THIRD_PARTY_DIR}/glslang/StandAlone/resource_limits_c.cpp
    ${GREX_THIRD_PARTY_DIR}/glslang/StandAlone/ResourceLimits.cpp
)

set_target_properties(001_raytracing_basic_vulkan PROPERTIES FOLDER "raytracing")

target_compile_definitions(
    ${TARGET_NAME}
    PUBLIC GREX_ENABLE_VULKAN
)

target_include_directories(
    001_raytracing_basic_vulkan
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glslang # This needs to come before ${VULKAN_INCLUDE_DIR}
           ${VULKAN_INCLUDE_DIR}
           ${GREX_THIRD_PARTY_DIR}/VulkanMemoryAllocator/include
           ${GREX_THIRD_PARTY_DIR}/glm
)

target_link_libraries(
    001_raytracing_basic_vulkan
    PUBLIC glfw
           glslang
           SPIRV
           dxcompiler
)

if(WIN32)
    target_compile_definitions(
        001_raytracing_basic_vulkan
        PUBLIC VK_USE_PLATFORM_WIN32_KHR
    )

    target_link_libraries(
        001_raytracing_basic_vulkan
        PUBLIC "${VULKAN_LIBRARY_DIR}/vulkan-1.lib"
    )
elseif(LINUX)
    target_compile_definitions(
        ${TARGET_NAME}
		PUBLIC VK_USE_PLATFORM_XCB_KHR
    )

    target_link_libraries(
        ${TARGET_NAME}
        PUBLIC "${VULKAN_LIBRARY_DIR}/libvulkan.so"
        	   "${VULKAN_LIBRARY_DIR}/libdxcompiler.so"
    )
endif()
