cmake_minimum_required(VERSION 3.0)

# ------------------------------------------------------------------------------
# Configure directories
# ------------------------------------------------------------------------------
set(GREX_PROJECTS_COMMON_DIR "${GREX_PROJECTS_DIR}/common")

if (GREX_ENABLE_D3D12)
    add_subdirectory(101_color_cube_d3d12)
    add_subdirectory(103_cornell_box_d3d12)
    add_subdirectory(105_cone_d3d12)
endif()

if (GREX_ENABLE_VULKAN)
endif()
