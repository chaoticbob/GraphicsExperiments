cmake_minimum_required(VERSION 3.0)

# ------------------------------------------------------------------------------
# Configure directories
# ------------------------------------------------------------------------------
set(GREX_PROJECTS_COMMON_DIR "${GREX_PROJECTS_DIR}/common")

if (GREX_ENABLE_D3D12)
    add_subdirectory(101_color_cube_d3d12)
    add_subdirectory(102_cornell_box_d3d12)
    add_subdirectory(103_cone_d3d12)
    add_subdirectory(104_debug_tbn_d3d12)
endif()

if (GREX_ENABLE_METAL)
	add_subdirectory(101_color_cube_metal)
    add_subdirectory(102_cornell_box_metal)
    add_subdirectory(103_cone_metal)
endif()

if (GREX_ENABLE_VULKAN)
    add_subdirectory(101_color_cube_vulkan)
    add_subdirectory(102_cornell_box_vulkan)
    add_subdirectory(103_cone_vulkan)
    add_subdirectory(104_debug_tbn_vulkan)
endif()
