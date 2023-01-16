# Ray Tracing

### raytracing_basic
Super simple ray tracing examples using D3D12 and Vulkan. These borrow from 
the [DirectX-Graphics-Samples](https://github.com/microsoft/DirectX-Graphics-Samples) 
and [SaschaWillem's Vulkan Examples](https://github.com/SaschaWillems/Vulkan). They've 
been rewritten in away that helps me follow the code should I need a refresher.

Both versions of the raytracing_basic examples follows a similar code path. This
makes it easier to compare / contrast how something is done in each API.

Both versions also make use of run shader compilation. D3D12 uses [DXC](https://github.com/microsoft/DirectXShaderCompiler) and Vulkan
uses [glslang](https://github.com/KhronosGroup/glslang.git).