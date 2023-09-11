cmake_minimum_required(VERSION 3.0)

# ------------------------------------------------------------------------------
# Configure directories
# ------------------------------------------------------------------------------
set(GREX_PROJECTS_COMMON_DIR "${GREX_PROJECTS_DIR}/common")

if (GREX_ENABLE_D3D12)
    add_subdirectory(401_gltf_basic_geo_d3d12)
    add_subdirectory(402_gltf_basic_texture_d3d12)
    add_subdirectory(403_gltf_basic_material_d3d12)
    add_subdirectory(404_gltf_basic_material_texture_d3d12)
    add_subdirectory(405_gltf_full_material_test_d3d12)
endif()

if (GREX_ENABLE_VULKAN)
    add_subdirectory(401_gltf_basic_geo_vulkan)
    add_subdirectory(402_gltf_basic_texture_vulkan)
    add_subdirectory(403_gltf_basic_material_vulkan)
    add_subdirectory(404_gltf_basic_material_texture_vulkan)
    add_subdirectory(405_gltf_full_material_test_vulkan)
endif()

if (GREX_ENABLE_METAL)
    add_subdirectory(401_gltf_basic_geo_metal)
    add_subdirectory(402_gltf_basic_texture_metal)
    add_subdirectory(403_gltf_basic_material_metal)
    add_subdirectory(404_gltf_basic_material_texture_metal)
endif()
