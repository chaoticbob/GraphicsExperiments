# Ray Tracing

## raygen_uv
![alt text](../../images/screenshots/raytracing/raygen_uv.png?raw=true)

**Shows the output coordinates are the same for D3D12 and Vullkan**

Barebones experiments showing that the output image/texture coordinates are the
same for D3D12 and Vulkan. No acceleration structure used.

## raytracing_basic
![alt text](../../images/screenshots/raytracing/raytracing_basic.png?raw=true)

**Shows how to ray trace a single triangle with as little code as possible**

Super simple ray tracing experiments using D3D12 and Vulkan. These borrow from 
the [DirectX-Graphics-Samples](https://github.com/microsoft/DirectX-Graphics-Samples) 
and [SaschaWillem's Vulkan Examples](https://github.com/SaschaWillems/Vulkan). They've 
been rewritten in a way that helps me follow the code should I need a refresher.

Both versions of the raytracing_basic examples follows a similar code path. This
makes it easier to compare / contrast how something is done in each API.

Both versions also make use of run time shader compilation. D3D12 uses [DXC](https://github.com/microsoft/DirectXShaderCompiler) and Vulkan
uses [glslang](https://github.com/KhronosGroup/glslang).

D3D12 Notes:
* Does not use a root signature

Vulkan Notes:
* Uses VK_EXT_descriptor_buffer
* glslang is embedded for shader compilation

## basic_procedural
![alt text](../../images/screenshots/raytracing/basic_procedural.png?raw=true)

**Shows how to ray trace a sphere using an intersection shader**

Very basic ray tracing experiments of procedural AABB using an intersection shader. Ray traces
a hardcoded sphere. Sphere is hardcoded in the intersection shader.

This experiment builds off of **raytracing_basic**. 

D3D12 Notes:
* Does not use a root signature

## sphereflake
![alt text](../../images/screenshots/raytracing/sphereflake.png?raw=true)

**Shows how to use a storage/structured buffer as a shader record parameter**

Simple experiment that shows how to use a structured buffer as a shader record parameter
in both Vulkan and D3D12. Shader record parameter is local root signature parmeter in D3D12 speak. 

This experiment uses [Eric Haines' sphereflake algorithm from SPD](https://www.realtimerendering.com/resources/SPD/) to generate AABBs for
a sphereflake.

This experiment builds off of **basic_procedural**.
