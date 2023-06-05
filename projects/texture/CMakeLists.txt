cmake_minimum_required(VERSION 3.0)

# ------------------------------------------------------------------------------
# Configure directories
# ------------------------------------------------------------------------------
set(GREX_PROJECTS_COMMON_DIR "${GREX_PROJECTS_DIR}/common")

if (GREX_ENABLE_D3D12)
    add_subdirectory(301_textured_cube_d3d12)
    add_subdirectory(303_lambert_textured_cube_d3d12)
    add_subdirectory(305_phong_textured_cube_d3d12)
    add_subdirectory(307_normal_map_d3d12)
    add_subdirectory(309_normal_map_explorer_d3d12)
    add_subdirectory(311_parallax_occlusion_map_d3d12)
    add_subdirectory(313_parallax_occlusion_map_explorer_d3d12)
    add_subdirectory(315_normal_map_vs_pom_d3d12)
endif()


if (GREX_ENABLE_VULKAN)
    add_subdirectory(302_textured_cube_vulkan)
endif()
