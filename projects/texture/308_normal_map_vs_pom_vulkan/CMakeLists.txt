cmake_minimum_required(VERSION 3.25)

project(308_normal_map_vs_pom_vulkan)

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
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.h
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.cpp
    ${GREX_THIRD_PARTY_DIR}/MikkTSpace/mikktspace.h
    ${GREX_THIRD_PARTY_DIR}/MikkTSpace/mikktspace.c
    ${GREX_THIRD_PARTY_DIR}/glslang/StandAlone/resource_limits_c.cpp
    ${GREX_THIRD_PARTY_DIR}/glslang/StandAlone/ResourceLimits.cpp
    ${IMGUI_VULKAN_FILES}
)

set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "texture")

target_compile_definitions(
    ${TARGET_NAME}
    PUBLIC TRIMESH_USE_MIKKTSPACE
           ENABLE_IMGUI_VULKAN
		   GREX_ENABLE_VULKAN
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
           ${GREX_THIRD_PARTY_DIR}/MikkTSpace
           ${IMGUI_INC_DIR}
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

