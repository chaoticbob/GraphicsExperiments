cmake_minimum_required(VERSION 3.0)

# ------------------------------------------------------------------------------
# Configure directories
# ------------------------------------------------------------------------------
set(GREX_PROJECTS_COMMON_DIR "${GREX_PROJECTS_DIR}/common")

add_subdirectory(raytracing/000a_raygen_uv_d3d12)
add_subdirectory(raytracing/000b_raygen_uv_vulkan)

add_subdirectory(raytracing/001_raytracing_basic_d3d12)
add_subdirectory(raytracing/002_raytracing_basic_vulkan)
add_subdirectory(raytracing/003_basic_procedural_d3d12)
add_subdirectory(raytracing/004_basic_procedural_vulkan)
add_subdirectory(raytracing/005_sphereflake_d3d12)
add_subdirectory(raytracing/006_sphereflake_vulkan)

add_subdirectory(misc/gen_sphereflake_coords)
