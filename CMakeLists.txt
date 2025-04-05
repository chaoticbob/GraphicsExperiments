cmake_minimum_required(VERSION 3.25)

project(GraphicsExperiments)

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------
if (APPLE)
    option(GREX_ENABLE_D3D12  "Build DirectX 12 projects" OFF)
    option(GREX_ENABLE_METAL  "Build Metal projects" ON)
    option(GREX_ENABLE_VULKAN "Build Vulkan projects" OFF)
elseif (LINUX)
    option(GREX_ENABLE_D3D12  "Build DirectX 12 projects" OFF)
    option(GREX_ENABLE_METAL  "Build Metal projects" OFF)
    option(GREX_ENABLE_VULKAN "Build Vulkan projects" ON)
else()
    option(GREX_ENABLE_D3D12  "Build DirectX 12 projects" ON)
    option(GREX_ENABLE_METAL  "Build Metal projects" OFF)
    option(GREX_ENABLE_VULKAN "Build Vulkan projects" ON)
endif()

option(GREX_ENABLE_SLANG          "Build Slang version of Vulkan projects" OFF)
option(GREX_ENABLE_MISC_PROJECTS  "Build misc projects" OFF)

# ------------------------------------------------------------------------------
# Detect Linux
# ------------------------------------------------------------------------------
if(UNIX AND APPLE)
    if (APPLE) 
        set(MACOS TRUE)       
    else()
        set(LINUX TRUE)
    endif()
endif()

if (LINUX)
    set(GREX_LINUX TRUE)
elseif (MACOS)
    set(GREX_MACOS TRUE)
elseif(WIN32)
    set(GREX_MSW TRUE)
endif()

# ------------------------------------------------------------------------------
# Global properties
# ------------------------------------------------------------------------------
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# Force C++ 20 for extra pain
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ------------------------------------------------------------------------------
# Compiler otions
# ------------------------------------------------------------------------------
if (MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
endif()

# ------------------------------------------------------------------------------
# Compile flags and definitions
# ------------------------------------------------------------------------------
if (GREX_LINUX)
    add_definitions(-DGREX_LINUX)
elseif (GREX_MACOS)
    add_compile_options("$<$<CONFIG:Debug>:-g>")
    add_compile_options("$<$<CONFIG:RelWithDebInfo>:-g>")
elseif (GREX_MSW)
    add_definitions(-DGREX_MSW)
endif()

# ------------------------------------------------------------------------------
# Configure output directories
# ------------------------------------------------------------------------------
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# ------------------------------------------------------------------------------
# Configure directories
# ------------------------------------------------------------------------------
set(GREX_DIR             "${CMAKE_CURRENT_SOURCE_DIR}")
set(GREX_PROJECTS_DIR    "${GREX_DIR}/projects")
set(GREX_INCLUDE_DIR     "${GREX_DIR}/include")
set(GREX_THIRD_PARTY_DIR "${GREX_DIR}/third_party")

# ------------------------------------------------------------------------------
# Detect Vulkan
# ------------------------------------------------------------------------------
# Determine Vulkan's include directory
if (NOT VULKAN_INCLUDE_DIR)
    if (LINUX)
        if (GGP)
            set(VULKAN_INCLUDE_DIR "${GGP_SYSROOT_PATH}/usr/local/include")
        else()
            if (DEFINED ENV{VULKAN_SDK})
                set(VULKAN_INCLUDE_DIR "$ENV{VULKAN_SDK}/include" "$ENV{VULKAN_SDK}/include/dxc")
            endif()
        endif()
    elseif (APPLE OR WIN32)
        if (DEFINED ENV{VULKAN_SDK})
            set(VULKAN_INCLUDE_DIR "$ENV{VULKAN_SDK}/Include")
        endif()
    endif()
endif()
    
# Determine Vulkan's library directory
if (NOT VULKAN_LIBRARY_DIR)
    if (LINUX)
        if (DEFINED ENV{VULKAN_SDK})
            set(VULKAN_LIBRARY_DIR "$ENV{VULKAN_SDK}/lib")
        endif()
    elseif (APPLE OR WIN32)
        if (DEFINED ENV{VULKAN_SDK})
            set(VULKAN_LIBRARY_DIR "$ENV{VULKAN_SDK}/Lib")
        endif()
    endif()
endif()
    
# Bail if Vulkan's include directory is not set
if ((NOT VULKAN_INCLUDE_DIR) AND (NOT APPLE))
    message(FATAL_ERROR "VULKAN_INCLUDE_DIR not specified and could not be determined using environment variable VULKAN_SDK")
endif()
    
if (VULKAN_INCLUDE_DIR)
    # Convert paths to something CMake can use
    file(TO_CMAKE_PATH "${VULKAN_INCLUDE_DIR}" VULKAN_INCLUDE_DIR)
    file(TO_CMAKE_PATH "${VULKAN_LIBRARY_DIR}" VULKAN_LIBRARY_DIR)

    message("Found Vulkan:")
    message("  Vulkan include directory: ${VULKAN_INCLUDE_DIR}")
    message("  Vulkan library directory: ${VULKAN_LIBRARY_DIR}")
endif()

# ------------------------------------------------------------------------------
# Slang
# ------------------------------------------------------------------------------
if (GREX_ENABLE_SLANG)
    if (NOT SLANG_DIR)
        message(FATAL_ERROR "GREX_ENABLE_SLANG requires -DSLANG_DIR=<path to Slang>")
    endif()

    set(SLANG_INCLUDE_DIR "${SLANG_DIR}/include")
    set(SLANG_LIBRARY_DIR "${SLANG_DIR}/lib")

    if (NOT EXISTS "${SLANG_DIR}/include/slang.h")
        message(FATAL_ERROR "Could not find slang.h at ${SLANG_DIR}/include/slang.h")
    endif()

    message("Found Slang:")
    message("  Slang include directory: ${SLANG_INCLUDE_DIR}")
    message("  Slang library directory: ${SLANG_LIBRARY_DIR}")

    include(cmake/slang.cmake)
endif()

# ------------------------------------------------------------------------------
# glslang
# ------------------------------------------------------------------------------
set(BUILD_SHARED_LIBS       OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL          OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_JS       OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL             OFF CACHE BOOL "" FORCE)
set(ENABLE_PCH              OFF CACHE BOOL "" FORCE)
set(ENABLE_CTEST            OFF CACHE BOOL "" FORCE)
add_subdirectory(${GREX_THIRD_PARTY_DIR}/glslang)
message("Enabled GLSL runtime shader compilation with glslang")


# ------------------------------------------------------------------------------
# Configure GLFW
# ------------------------------------------------------------------------------
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_VULKAN_STATIC  OFF CACHE BOOL "" FORCE)
add_subdirectory(${GREX_THIRD_PARTY_DIR}/glfw)

# ------------------------------------------------------------------------------
# ImGui
# ------------------------------------------------------------------------------
list(APPEND IMGUI_D3D12_FILES
    ${GREX_THIRD_PARTY_DIR}/imgui/imgui.h
    ${GREX_THIRD_PARTY_DIR}/imgui/imgui.cpp
    ${GREX_THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
    ${GREX_THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
    ${GREX_THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
    ${GREX_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_glfw.h
    ${GREX_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_glfw.h
    ${GREX_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_glfw.cpp
    ${GREX_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_dx12.h
    ${GREX_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_dx12.cpp
)

list(APPEND IMGUI_VULKAN_FILES
    ${GREX_THIRD_PARTY_DIR}/imgui/imgui.h
    ${GREX_THIRD_PARTY_DIR}/imgui/imgui.cpp
    ${GREX_THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
    ${GREX_THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
    ${GREX_THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
    ${GREX_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_glfw.h
    ${GREX_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_glfw.cpp
    ${GREX_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_vulkan.h
    ${GREX_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_vulkan.cpp
)

list(APPEND IMGUI_METAL_FILES
    ${GREX_THIRD_PARTY_DIR}/imgui/imgui.h
    ${GREX_THIRD_PARTY_DIR}/imgui/imgui.cpp
    ${GREX_THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
    ${GREX_THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
    ${GREX_THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
    ${GREX_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_glfw.h
    ${GREX_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_glfw.cpp
    ${GREX_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_metal.h
    ${GREX_THIRD_PARTY_DIR}/imgui/backends/imgui_impl_metal.mm
)

set(IMGUI_INC_DIR ${GREX_THIRD_PARTY_DIR}/imgui)

# ------------------------------------------------------------------------------
# meshoptimizer
# ------------------------------------------------------------------------------
add_subdirectory(${GREX_THIRD_PARTY_DIR}/meshoptimizer)

# ------------------------------------------------------------------------------
# Graphics experiments
# ------------------------------------------------------------------------------
add_subdirectory(projects)
