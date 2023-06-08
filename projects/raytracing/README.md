# Ray Tracing

## raygen_uv
![alt text](../../images/screenshots/raytracing/raygen_uv.png?raw=true)

**Shows the output coordinates are the same for D3D12 and Vullkan**

Barebones experiments showing that the output image/texture coordinates are the
same for D3D12 and Vulkan. 

Notes:
* Does not use acceleration structures
* Does not use a local root signature or shader record parmeters

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
* Does not use a local root signature

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
* Does not use a local root signature

## sphereflake
![alt text](../../images/screenshots/raytracing/sphereflake.png?raw=true)

**Shows how to use a storage/structured buffer as a shader record parameter**

Simple experiment that shows how to use a structured buffer as a shader record parameter
in both Vulkan and D3D12. Shader record parameter is local root signature parmeter in D3D12 speak. 

This experiment uses [Eric Haines' sphereflake algorithm from SPD](https://www.realtimerendering.com/resources/SPD/) to generate AABBs for
a sphereflake.

This experiment builds off of **basic_procedural**.

D3D12 Notes:
* Uses local root signature and parameters

Vulkan Notes:
* Uses shader record parameters

## basic_reflection
![alt text](../../images/screenshots/raytracing/basic_reflection.png?raw=true)

**Shows basic reflection via closest hit shader**

Building from the **spherefalke** experiment, basic reflection is added to the closest hit shader.
The key thing to keep an eye on in this experiment is how similar/different the ray payload is handled
in D3D12 vs Vulkan. 

The shading in this experiment is really hacky, just tuned enough to let the reflection come through.

This experiment uses [Eric Haines' sphereflake algorithm from SPD](https://www.realtimerendering.com/resources/SPD/) to generate AABBs for
a sphereflake.

This experiment builds off of **sphereflake**.

D3D12 Notes:
* Uses local root signature and parameters

Vulkan Notes:
* Uses shader record parameters

## basic_shadow
![alt text](../../images/screenshots/raytracing/basic_shadow.png?raw=true)

**Shows basic shadow using a miss shadow shader and skipping the closest hit shader if there's a hit**

Building from the **basic_reflection** experiment, basic shadow is added to the closest hit shader.
The key thing to keep an eye on in this experiment is the use of the shadow miss shader and the flags 
called in the closest hit shader to end traces on first hit and skip the closeset hit shader.

The shading in this experiment is really hacky, just tuned enough to let the shadows come through.

This experiment uses [Eric Haines' sphereflake algorithm from SPD](https://www.realtimerendering.com/resources/SPD/) to generate AABBs for
a sphereflake.

This experiment builds off of **basic_reflection**.

D3D12 Notes:
* Uses local root signature and parameters
* Uses TraceRay's MissShaderIndex function parameter to reference the two miss shaders.

Vulkan Notes:
* Uses shader record parameters
* Uses traceRayExt's missIndex function parameter to reference the two miss shaders.

## basic_shadow_dynamic
![alt text](../../images/screenshots/raytracing/basic_shadow_dynamic.png?raw=true)

**Shows basic shadow with moving light**

Same as **basic_shadow** except with the light position updated via the constant/uniform buffer.

## raytracing_triangles
![alt text](../../images/screenshots/raytracing/raytracing_triangles.png?raw=true)

**Shows basic ray intersection using triangle geometry**

Same as **basic_procedural** except with triangles. 

The purpose of this experiment is to show how to use HLSL's `PrimitiveIndex()` to look up the vertex
indices for an intersected triangle. These vertex indices then can be used to look up vertex attributes
such as normals for shading. 

Shaders are found in the [assets/projects/021_raytracing_triangles_d3d12](https://github.com/chaoticbob/GraphicsExperiments/tree/main/assets/projects/021_raytracing_triangles_d3d12) directory.

## raytracing_multi_geo
![alt text](../../images/screenshots/raytracing/raytracing_multi_geo.png?raw=true)

**Shows basic ray intersection using multiple triangle geomtry descriptions**

The purpose of this experiment is to show how to use HLSL's `GeometryIndex()` to look up
the intersected geometry and the corresponding material. Once the geometry has been 
identified, `PrimitiveIndex()` is used to look up the vertex indices for an intersected triangle. These 
vertex indices then can be used to look up vertex attributes such as normals for shading. 

A single BLAS is used to contain 3 difference pieces of geometry: cube, sphere, and cone.

Note the argument value for  `TraceRay()`'s `MultiplierForGeometryContributionToHitGroupIndex` is set to 0.
This is necessary for `GeometryIndex()` to work correctly.

Shaders are found in the [assets/projects/023_raytracing_multi_geo_d3d12](https://github.com/chaoticbob/GraphicsExperiments/tree/main/assets/projects/023_raytracing_multi_geo_d3d12) directory.

## raytracing_multi_instance
![alt text](../../images/screenshots/raytracing/raytracing_multi_instance.png?raw=true)

**Shows basic ray intersection using multiple triangle geomtry descriptions**

The purpose of this experiment is to show how to use HLSL's `InstanceIndex()` to look up
the intersected geometry and the corresponding material. Once the geometry has been 
identified, `PrimitiveIndex()` is used to look up the vertex indices for an intersected triangle. These 
vertex indices then can be used to look up vertex attributes such as normals for shading. 

There are 3 BLASes in this experiment - each containing a different geometry.  
The TLAS has 3 instance descriptions - each pointing to one of the 3 BLASes.

Shaders are found in the [assets/projects/025_raytracing_multi_instance_d3d12](https://github.com/chaoticbob/GraphicsExperiments/tree/main/assets/projects/025_raytracing_multi_instance_d3d12) directory.

## pbr_spheres
![alt text](../../images/screenshots/raytracing/pbr_spheres.png?raw=true)

**Shows ray tracing and PBR using same technique as raster version**

The purpose of this experiment is to render typical roughness / metallic grid using ray tracing
and the typical PBR approach (with LUT, irradiance, and environment textures). It's very slow
since there's a large number of samples for both the diffuse and specular calculation.

There are no punctual lights in this sample, all the lighting is done via IBL. Punctual lights might
get added later.

Shaders are found in the [assets/projects/027_28_raytracing_pbr_spheres](https://github.com/chaoticbob/GraphicsExperiments/tree/main/assets/projects/027_28_raytracing_pbr_spheres) directory.

HDRI images for IBL are from [Poly Haven](https://polyhaven.com/hdris), which are generously provided under [CC0](https://polyhaven.com/license).

## refract
![alt text](../../images/screenshots/raytracing/refract.png?raw=true)

**Demonstrates recursive ray tracing and ray splitting for refraction/reflection**

The purpose of this experiment is to understand how recursive ray tracing works when doing a ray split
for refraction and reflection. Refraction implementation is pretty naive and may need some fixes/tweaks
to achieve the look you want. 

There are no punctual lights in this sample, all the lighting is done via IBL. Punctual lights might
get added later.

Shaders are found in the [assets/projects/029_raytracing_refract_d3d12](https://github.com/chaoticbob/GraphicsExperiments/tree/main/assets/projects/029_raytracing_refract_d3d12) directory.

HDRI images for IBL are from [Poly Haven](https://polyhaven.com/hdris), which are generously provided under [CC0](https://polyhaven.com/license).

## path_trace
![alt text](../../images/screenshots/raytracing/path_trace.png?raw=true)

**Naive/fun path tracing experiment**

Inspired by [https://github.com/TheRealMJP/DXRPathTracer]. This experiments shows a very naive and simplistic path tracer.
Shading doesn't use PBR yet. The main idea for this one was to incorporate the recursive ray tracing and refraction from the
earlier experiment with an increasing sample count. Sampling mechanism is pretty naive and there's definitely some artifacts.
But fun to see it starting to work. Sample count get reset if camera is move using mouse drag.

There are no punctual lights in this sample, all the lighting is done via IBL. Punctual lights might
get added later.

Shaders are found in the [assets/projects/031_032_raytracing_path_trace](https://github.com/chaoticbob/GraphicsExperiments/tree/main/assets/projects/031_032_raytracing_path_trace) directory.

This experiment uses [Nathan Reed's PCG has function](https://www.reedbeta.com/blog/hash-functions-for-gpu-rendering/)

HDRI images for IBL are from [Poly Haven](https://polyhaven.com/hdris), which are generously provided under [CC0](https://polyhaven.com/license).

## path_trace_pbr
![alt text](../../images/screenshots/raytracing/path_trace_pbr.png?raw=true)

**Naive/fun path tracing with PBR experiment**

This experiment builds upon **path_trace** adding PBR as well a slightly more complex set of geometries. I found
[Thorsten Thormählen's lecture on image based lighting](https://www.mathematik.uni-marburg.de/~thormae/lectures/graphics1/graphics_10_2_eng_web.html#1) helpful
in getting an idea of how the IBL contribution should work for indirect lighting. The change of variable substitution
was the key. It wasn't obvious to me in the beginning how the D(h) - the normal distribution function - was suppose
to be handled. 

The diffuse component doesn't use cosine weighted sampling. The weighting towards the middle of hemisphere pulls too much on the structured
of the IBL and diffuse began to look more like specular - which wasn't desirable.

There are no punctual lights in this sample, all the lighting is done via IBL. Punctual lights might
get added later.

Shaders are found in the [assets/projects/031_032_raytracing_path_trace](https://github.com/chaoticbob/GraphicsExperiments/tree/main/assets/projects/031_032_raytracing_path_trace) directory.

HDRI images for IBL are from [Poly Haven](https://polyhaven.com/hdris), which are generously provided under [CC0](https://polyhaven.com/license).






