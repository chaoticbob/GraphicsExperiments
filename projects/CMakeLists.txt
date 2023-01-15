cmake_minimum_required(VERSION 3.0)

# ------------------------------------------------------------------------------
# Configure directories
# ------------------------------------------------------------------------------
set(GREX_PROJECTS_COMMON_DIR "${GREX_PROJECTS_DIR}/common")

add_subdirectory(raytracing/001_raytracing_basic_d3d12)
add_subdirectory(raytracing/002_raytracing_basic_vulkan)