# Graphics Experiments
Collection of small and mostly self contained graphics experiments for reference. 
These experiments focus on doing graphics things using the graphics API. 
The graphics API objects are not always properly cleaned up. Just keep that in 
mind if you make use of the code.

#### What kinds of experiments are in here?
Whatever comes to mind. Right now there is only a raytracing example.

# Some Stuff
1. The main idea behind these graphics experiments is to show how XYZ is done without a bunch of noise around it. 
Sometimes noise can't be avoided.
2. Use as little wrapper code as possible. Yes, this will mean there's some bloat due to code duplication.
3. Minimize on error handling so that it doesn't obfuscate what the experiment is trying to accomplish.

### Windowing
[glfw](https://github.com/glfw/glfw) is used to create platform windows. 

### Vulkan
Vulkan experiments will use [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) 
to simplify buffer and image creation and keeps the code significantly smaller.

# Dependencies
* [glfw](https://github.com/glfw/glfw)
* [glm](https://github.com/g-truc/glm)
* [glslang](https://github.com/KhronosGroup/glslang)
* [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)