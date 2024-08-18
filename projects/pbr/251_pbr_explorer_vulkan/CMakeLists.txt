cmake_minimum_required(VERSION 3.25)

project(251_pbr_explorer_vulkan)

set(TARGET_NAME ${PROJECT_NAME})

add_executable(
    ${TARGET_NAME}
    ${PROJECT_NAME}.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/vk_renderer.h
    ${GREX_PROJECTS_COMMON_DIR}/vk_renderer.cpp
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.h
    ${GREX_PROJECTS_COMMON_DIR}/bitmap.cpp
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
    ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
    ${GREX_THIRD_PARTY_DIR}/glslang/StandAlone/resource_limits_c.cpp
    ${GREX_THIRD_PARTY_DIR}/glslang/StandAlone/ResourceLimits.cpp
    ${IMGUI_VULKAN_FILES}
)

set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "pbr")

target_compile_definitions(
    ${TARGET_NAME}
    PUBLIC ENABLE_IMGUI_VULKAN
        GREX_ENABLE_VULKAN
)

target_include_directories(
    ${TARGET_NAME}
    PUBLIC ${GREX_PROJECTS_COMMON_DIR}
           ${GREX_THIRD_PARTY_DIR}/glslang # This needs to come before ${VULKAN_INCLUDE_DIR}
		   ${VULKAN_INCLUDE_DIR}
           ${GREX_THIRD_PARTY_DIR}/VulkanMemoryAllocator/include
           ${GREX_THIRD_PARTY_DIR}/glm
           ${GREX_THIRD_PARTY_DIR}/stb
           ${GREX_THIRD_PARTY_DIR}/tinyobjloader
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

################################################################################

if (GREX_ENABLE_SLANG)
    set(TARGET_NAME "${PROJECT_NAME}_slang")

    add_executable(
        ${TARGET_NAME}
        ${PROJECT_NAME}.cpp
        ${GREX_PROJECTS_COMMON_DIR}/config.h
        ${GREX_PROJECTS_COMMON_DIR}/vk_renderer.h
        ${GREX_PROJECTS_COMMON_DIR}/vk_renderer.cpp
        ${GREX_PROJECTS_COMMON_DIR}/window.h
        ${GREX_PROJECTS_COMMON_DIR}/window.cpp
        ${GREX_PROJECTS_COMMON_DIR}/bitmap.h
        ${GREX_PROJECTS_COMMON_DIR}/bitmap.cpp
        ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.h
        ${GREX_PROJECTS_COMMON_DIR}/tri_mesh.cpp
        ${GREX_THIRD_PARTY_DIR}/glslang/StandAlone/resource_limits_c.cpp
        ${GREX_THIRD_PARTY_DIR}/glslang/StandAlone/ResourceLimits.cpp
        ${IMGUI_VULKAN_FILES}
    )

    set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "pbr")

    target_compile_definitions(
        ${TARGET_NAME}
        PUBLIC ENABLE_IMGUI_VULKAN
            GREX_ENABLE_VULKAN
    )

    target_compile_definitions(
        ${TARGET_NAME}
        PUBLIC GREX_ENABLE_SLANG
    )

    target_include_directories(
        ${TARGET_NAME}
        PUBLIC ${GREX_PROJECTS_COMMON_DIR}
               ${GREX_THIRD_PARTY_DIR}/glslang # This needs to come before ${VULKAN_INCLUDE_DIR}
		       ${VULKAN_INCLUDE_DIR}
               ${GREX_THIRD_PARTY_DIR}/VulkanMemoryAllocator/include
               ${GREX_THIRD_PARTY_DIR}/glm
               ${GREX_THIRD_PARTY_DIR}/stb
               ${GREX_THIRD_PARTY_DIR}/tinyobjloader
               ${IMGUI_INC_DIR}
               ${SLANG_INCLUDE_DIR}
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
                   "${SLANG_LIBRARY_DIR}/slang.lib"
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
                   "${SLANG_LIBRARY_DIR}/libslang.so"
        )
    endif()
endif()

