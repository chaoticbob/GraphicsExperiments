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
# Direct3D Agility SDK
# ------------------------------------------------------------------------------
if (GREX_AGILITY_SDK)

	if (GREX_ENABLE_D3D12)
		# Split the version into three parts (major.minor.patch)
		string(REGEX MATCHALL "[0-9]+" VERSION_LIST "${GREX_AGILITY_SDK}")
		list(LENGTH VERSION_LIST VERSION_LENGTH)

		if (VERSION_LENGTH EQUAL 3)
			list(GET VERSION_LIST 1 GREX_AGILITY_SDK_VERSION)
		else()
			message(FATAL_ERROR "Unknown DirectX Agility SDK Version ${GREX_AGILITY_SDK}. Should be <MAJOR.MINOR.PATCH> for the SDK version you wish to use")
		endif()

		# D3D12SDKPath needs to be a relative path from the binary to the D3D12Core.lib 
		# you want to use
		cmake_path(RELATIVE_PATH CMAKE_BINARY_DIR
			BASE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
			OUTPUT_VARIABLE BIN_TO_BUILD_PATH)


		# We need the extra leading ../ to deal with the sub-directory for the build format (Debug/Release/etc ...)
		set(GREX_AGILITY_RELATIVE_SDK_PATH "../${BIN_TO_BUILD_PATH}/packages/Microsoft.Direct3D.D3D12.${GREX_AGILITY_SDK}/build/native/bin/x64")

		# Output the results on the command line during build
		message("Generating DirectX Agility SDK Export Symbols:")
		message("  D3D12SDKVersion: ${GREX_AGILITY_SDK_VERSION}")
		message("  D3D12SDKPath:    '${GREX_AGILITY_RELATIVE_SDK_PATH}'")
		message("  NuGet PM:        NuGet\\Install-Package Microsoft.Direct3D.D3D12 -Version ${GREX_AGILITY_SDK}")
		message("")

	else()
		message("GREX_AGILITY_SDK not supported for non D3D12 platforms")
	endif()
endif()

# ------------------------------------------------------------------------------
# Graphics experiments
# ------------------------------------------------------------------------------
add_subdirectory(projects)
