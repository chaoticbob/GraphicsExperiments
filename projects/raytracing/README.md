# Ray Tracing

### raytracing_basic
Super simple ray tracing examples using D3D12 and Vulkan. These borrow from 
the [DirectX-Graphics-Samples](https://github.com/microsoft/DirectX-Graphics-Samples) 
and [SaschaWillem's Vulkan Examples](https://github.com/SaschaWillems/Vulkan). They've 
been rewritten in a way that helps me follow the code should I need a refresher.

Both versions of the raytracing_basic examples follows a similar code path. This
makes it easier to compare / contrast how something is done in each API.

Both versions also make use of run time shader compilation. D3D12 uses [DXC](https://github.com/microsoft/DirectXShaderCompiler) and Vulkan
uses [glslang](https://github.com/KhronosGroup/glslang).

### basic procedural
Very back ray tracing example of procedural using an intersetion shader. Ray traces
a hard coded sphere. Sphere is hard coded in the intersection shader.

This experiment builds off of **raytracing_basic**. 