cmake_minimum_required(VERSION 3.0)

# ------------------------------------------------------------------------------
# Configure directories
# ------------------------------------------------------------------------------
set(GREX_PROJECTS_COMMON_DIR "${GREX_PROJECTS_DIR}/common")

if (GREX_ENABLE_D3D12)
    add_subdirectory(201_pbr_spheres_d3d12)
    add_subdirectory(203_pbr_camera_d3d12)
    add_subdirectory(205_pbr_align_d3d12)
    add_subdirectory(251_pbr_explorer_d3d12)
    add_subdirectory(253_pbr_material_properties_d3d12)
    add_subdirectory(255_pbr_material_textures_d3d12)
endif()

if (GREX_ENABLE_VULKAN)
    add_subdirectory(202_pbr_spheres_vulkan)
    add_subdirectory(204_pbr_camera_vulkan)
    add_subdirectory(206_pbr_align_vulkan)
endif()
