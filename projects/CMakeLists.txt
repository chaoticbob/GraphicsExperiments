cmake_minimum_required(VERSION 3.5)

# ------------------------------------------------------------------------------
# Configure directories
# ------------------------------------------------------------------------------
set(GREX_PROJECTS_COMMON_DIR "${GREX_PROJECTS_DIR}/common")

add_subdirectory(geometry)
add_subdirectory(io)
add_subdirectory(pbr)
add_subdirectory(raytracing)
add_subdirectory(texture)

if (GREX_ENABLE_D3D12)
    add_subdirectory(misc/imgui_d3d12)
endif()


if (GREX_ENABLE_VULKAN)  
    # Waiting for ImGui to support VK_KHR_dynamic_rendering
    #add_subdirectory(misc/imgui_vulkan)
endif()

add_subdirectory(misc/gen_sphereflake_coords)
add_subdirectory(misc/window_events)
add_subdirectory(misc/ibl_brdf_lut)
add_subdirectory(misc/ibl_furnace)
add_subdirectory(misc/ibl_prefilter_env)
add_subdirectory(misc/normal_map_convert)
add_subdirectory(misc/sampling_vis)
add_subdirectory(misc/frustum_planes)
