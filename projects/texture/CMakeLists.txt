cmake_minimum_required(VERSION 3.0)

# ------------------------------------------------------------------------------
# Configure directories
# ------------------------------------------------------------------------------
set(GREX_PROJECTS_COMMON_DIR "${GREX_PROJECTS_DIR}/common")

if (GREX_ENABLE_D3D12)
    add_subdirectory(301_textured_cube_d3d12)
    add_subdirectory(303_lambert_textured_cube_d3d12)
    add_subdirectory(305_phong_textured_cube_d3d12)
endif()


if (GREX_ENABLE_VULKAN)
endif()
