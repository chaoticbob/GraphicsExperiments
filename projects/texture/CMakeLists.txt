cmake_minimum_required(VERSION 3.25)

# ------------------------------------------------------------------------------
# Configure directories
# ------------------------------------------------------------------------------
set(GREX_PROJECTS_COMMON_DIR "${GREX_PROJECTS_DIR}/common")

if (GREX_ENABLE_D3D12)
    add_subdirectory(301_textured_cube_d3d12)
    add_subdirectory(302_lambert_textured_cube_d3d12)
    add_subdirectory(303_phong_textured_cube_d3d12)
    add_subdirectory(304_normal_map_d3d12)
    add_subdirectory(305_normal_map_explorer_d3d12)
    add_subdirectory(306_parallax_occlusion_map_d3d12)
    add_subdirectory(307_parallax_occlusion_map_explorer_d3d12)
    add_subdirectory(308_normal_map_vs_pom_d3d12)
endif()


if (GREX_ENABLE_VULKAN)
    add_subdirectory(301_textured_cube_vulkan)
    add_subdirectory(302_lambert_textured_cube_vulkan)    
    add_subdirectory(303_phong_textured_cube_vulkan)
    add_subdirectory(304_normal_map_vulkan)
    add_subdirectory(305_normal_map_explorer_vulkan)
    add_subdirectory(306_parallax_occlusion_map_vulkan)
    add_subdirectory(307_parallax_occlusion_map_explorer_vulkan)
    add_subdirectory(308_normal_map_vs_pom_vulkan)
endif()

if (GREX_ENABLE_METAL)
    add_subdirectory(301_textured_cube_metal)
    add_subdirectory(302_lambert_textured_cube_metal)
    add_subdirectory(303_phong_textured_cube_metal)
    add_subdirectory(304_normal_map_metal)
    add_subdirectory(305_normal_map_explorer_metal)
    add_subdirectory(306_parallax_occlusion_map_metal)
    add_subdirectory(307_parallax_occlusion_map_explorer_metal)
    add_subdirectory(308_normal_map_vs_pom_metal)
endif()
