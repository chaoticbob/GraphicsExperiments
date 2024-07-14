cmake_minimum_required(VERSION 3.5)

project(022_raytracing_multi_geo_vulkan)

set(TARGET_NAME ${PROJECT_NAME})

add_executable(
    ${TARGET_NAME}
    ${TARGET_NAME}.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/vk_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/vk_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
    ${GREX_THIRD_PARTY_DIR}/glslang/StandAlone/resource_limits_c.cpp
    ${GREX_THIRD_PARTY_DIR}/glslang/StandAlone/ResourceLimits.cpp
)

set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "raytracing")

target_compile_definitions(
    ${TARGET_NAME}
    PUBLIC GREX_ENABLE_VULKAN
)

target_include_directories(
    ${TARGET_NAME}
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glslang # This needs to come before ${VULKAN_INCLUDE_DIR}
           ${VULKAN_INCLUDE_DIR}
           ${GREX_THIRD_PARTY_DIR}/VulkanMemoryAllocator/include
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
           ${GREX_THIRD_PARTY_DIR}/stb
)

target_link_libraries(
    ${TARGET_NAME}
    PUBLIC glfw
           glslang
	       SPIRV
)

if(WIN32)
    target_compile_definitions(
        ${TARGET_NAME}
        PUBLIC VK_USE_PLATFORM_WIN32_KHR
    )

    target_link_libraries(
        ${TARGET_NAME}
        PUBLIC "${VULKAN_LIBRARY_DIR}/vulkan-1.lib"
               "${VULKAN_LIBRARY_DIR}/dxcompiler.lib"
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

