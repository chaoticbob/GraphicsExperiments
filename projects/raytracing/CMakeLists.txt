cmake_minimum_required(VERSION 3.0)

# ------------------------------------------------------------------------------
# Configure directories
# ------------------------------------------------------------------------------
set(GREX_PROJECTS_COMMON_DIR "${GREX_PROJECTS_DIR}/common")

if (GREX_ENABLE_D3D12)
    add_subdirectory(000a_raygen_uv_d3d12)
    add_subdirectory(001_raytracing_basic_d3d12)
    add_subdirectory(003_basic_procedural_d3d12)
    add_subdirectory(005_sphereflake_d3d12)
    add_subdirectory(007_basic_reflection_d3d12)
    add_subdirectory(009_basic_shadow_d3d12)
    add_subdirectory(011_basic_shadow_dynamic_d3d12)
endif()


if (GREX_ENABLE_VULKAN)
    add_subdirectory(000b_raygen_uv_vulkan)
    add_subdirectory(002_raytracing_basic_vulkan)
    add_subdirectory(004_basic_procedural_vulkan)
    add_subdirectory(006_sphereflake_vulkan)
    add_subdirectory(008_basic_reflection_vulkan)
    add_subdirectory(010_basic_shadow_vulkan)
    add_subdirectory(012_basic_shadow_dynamic_vulkan)   
endif()
