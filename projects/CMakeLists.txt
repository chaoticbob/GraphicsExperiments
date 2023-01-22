cmake_minimum_required(VERSION 3.0)

# ------------------------------------------------------------------------------
# Configure directories
# ------------------------------------------------------------------------------
set(GREX_PROJECTS_COMMON_DIR "${GREX_PROJECTS_DIR}/common")

if (GREX_ENABLE_D3D12)
    add_subdirectory(raytracing/000a_raygen_uv_d3d12)
    add_subdirectory(raytracing/001_raytracing_basic_d3d12)
    add_subdirectory(raytracing/003_basic_procedural_d3d12)
    add_subdirectory(raytracing/005_sphereflake_d3d12)
    add_subdirectory(raytracing/007_basic_reflection_d3d12)
    add_subdirectory(raytracing/009_basic_shadow_d3d12)
    add_subdirectory(raytracing/011_basic_shadow_dynamic_d3d12)

    add_subdirectory(misc/imgui_d3d12)
endif()


if (GREX_ENABLE_VULKAN)
    add_subdirectory(raytracing/000b_raygen_uv_vulkan)
    add_subdirectory(raytracing/002_raytracing_basic_vulkan)
    add_subdirectory(raytracing/004_basic_procedural_vulkan)
    add_subdirectory(raytracing/006_sphereflake_vulkan)
    add_subdirectory(raytracing/008_basic_reflection_vulkan)
    add_subdirectory(raytracing/010_basic_shadow_vulkan)
    add_subdirectory(raytracing/012_basic_shadow_dynamic_vulkan)
    
    add_subdirectory(misc/imgui_vulkan)
endif()

add_subdirectory(misc/gen_sphereflake_coords)
add_subdirectory(misc/window_events)
