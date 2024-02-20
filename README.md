![alt text](images/screenshots/raytracing/path_trace_pbr_main_page.png?raw=true)

# Graphics Experiments (GREX)
Collection of small and mostly self contained graphics experiments for reference. 
These experiments focus on doing graphics things using the graphics API. 
The graphics API objects are not always properly cleaned up. Just keep that in 
mind if you make use of the code.

#### What kinds of experiments are in here?
Graphics experiments for rendering, [ray tracing](https://github.com/chaoticbob/GraphicsExperiments/tree/main/projects/raytracing), [PBR](https://github.com/chaoticbob/GraphicsExperiments/tree/main/projects/pbr), geometry generation, [mesh shading](https://github.com/chaoticbob/GraphicsExperiments/tree/main/projects/geometry), procedural textures, [normal mapping, parallax occlusion mapping](https://github.com/chaoticbob/GraphicsExperiments/tree/main/projects/texture), etc.
Pretty much anything that's graphically interesting. Everything can be foundin the [projects](https://github.com/chaoticbob/GraphicsExperiments/tree/main/assets/projects) directory.

# Some Stuff
1. The main idea behind these graphics experiments is to show how XYZ is done without a bunch of noise around it. 
Sometimes noise is unavoidable.
2. Experiments strives to use as little wrapper code as possible. Yes, this will mean there's some bloat due to code duplication.
3. Minimize on error handling so that it doesn't obfuscate what the experiment is trying to accomplish.

### Windowing
[glfw](https://github.com/glfw/glfw) is used to create platform windows. 

### Vulkan
Vulkan experiments use [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) 
to simplify buffer and image creation and keeps the code significantly smaller.

# Drivers
At the time of this writing, January 2023, the ray tracing experiments have been tested 
on the **public beta drivers** from AMD and NVIDIA. Sometimes things don't work correctly -
that's just life using beta drivers. Eventually the features in the beta driver will move to release.
Until that happens, some experiments may not run correctly.

For Vulkan, the drivers need to support 1.3 along with [VK_EXT_descriptor_buffer](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_EXT_descriptor_buffer.html) 
and [VK_KHR_dynamic_rendering](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_KHR_dynamic_rendering.html). 
And obviously the ray tracing related extensions.

# Dependencies
* Vulkan SDK 1.3.236.0
* Windows SDK Version 10.0.22621.0
* [DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler) v1.7.2212 (Dec 16, 2022) for DXIL.DLL (if needed - see note below)
* [glfw](https://github.com/glfw/glfw)
* [glm](https://github.com/g-truc/glm)
* [glslang](https://github.com/KhronosGroup/glslang)
* [ImGui](https://github.com/ocornut/imgui)
* [tinyobj](https://github.com/tinyobjloader/tinyobjloader)
* [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)

## DXIL.DLL
If shader compilation on D3D12 fails because DXIL.DLL can't be found, copy it from
the [DirectXShaderCompiler v1.7.2212 release](https://github.com/microsoft/DirectXShaderCompiler/releases).

# Building
## macOS
GREX development on macOS is done primarily in Xcode.

From the cloned GREX directory:
```
cmake -G Xcode -B build-xcode
```
This will generate an Xcode project in the `build-xcode` directory.

## Windows
GREX development on Windows is done in Visual Studio 2022 Professional. 

From the cloned GREX directory:
```
cmake -B build-vs2022
```
This will generate a solution for Visual Studio 2022 Professional in the `build-vs2022` directory.

