cmake_minimum_required(VERSION 3.0)

project(imgui_vulkan)

add_executable(
    test_app_imgui_vulkan
    test_app_imgui_vulkan.cpp
    ${GREX_PROJECTS_COMMON_DIR}/config.h
    ${GREX_PROJECTS_COMMON_DIR}/window.h
    ${GREX_PROJECTS_COMMON_DIR}/window.cpp
    ${IMGUI_VULKAN_FILES}
)

set_target_properties(test_app_imgui_vulkan PROPERTIES FOLDER "misc")

target_compile_definitions(
    test_app_imgui_vulkan
    PUBLIC ENABLE_IMGUI_VULKAN
)

target_include_directories(
    test_app_imgui_vulkan
    PUBLIC  ${GREX_PROJECTS_COMMON_DIR}
            ${GREX_THIRD_PARTY_DIR}/glslang # This needs to come before ${VULKAN_INCLUDE_DIR}
            ${VULKAN_INCLUDE_DIR}
            ${GREX_THIRD_PARTY_DIR}/VulkanMemoryAllocator/include
            ${IMGUI_INC_DIR}
)

target_link_libraries(
    test_app_imgui_vulkan
    PUBLIC glfw
           glslang
           SPIRV
)

if(WIN32)
    target_compile_definitions(
        test_app_imgui_vulkan
        PUBLIC VK_USE_PLATFORM_WIN32_KHR
               ENABLE_IMGUI_VULKAN
    )

    target_link_libraries(
        test_app_imgui_vulkan
        PUBLIC "${VULKAN_LIBRARY_DIR}/vulkan-1.lib"
    )
endif()