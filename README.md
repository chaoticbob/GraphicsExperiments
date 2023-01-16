# Graphics Experiments
Collection of small and mostly self contained graphics experiments for reference. 
These experiments focus on doing graphics things using the graphics API. 
The graphics API objects are not always properly cleaned up. Just keep that in 
mind if you make use of the code.

#### What kinds of experiments are in here?
Whatever comes to mind. Right now there are basic ray tracing experiments for 
triangle and procedural AABB intersection.

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

# Dependencies
* Vulkan SDK 1.3.236.0
* Windows SDK Version 10.0.22621.0
* [DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler) v1.7.2212 (Dec 16, 2022) for DXIL.DLL (if needed - see note below)
* [glfw](https://github.com/glfw/glfw)
* [glm](https://github.com/g-truc/glm)
* [glslang](https://github.com/KhronosGroup/glslang)
* [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)

## DXIL.DLL
If shader compilation on D3D12 fails because DXIL.DLL can't be found, copy it from
the [DirectXShaderCompiler v1.7.2212 release](https://github.com/microsoft/DirectXShaderCompiler/releases).

