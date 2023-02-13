# PBR (Physically Based Rendering)

## pbr_spheres
![alt text](../../images/screenshots/pbr/pbr_spheres.png?raw=true)

**Basic PBR implementation with direct and indirect lighting**

Renders the typical sphere grid of metalness on X-axis and roughness on Y-axis. 
Uses IBL lighting by default, point lights can be turned optionally turned on.

HDRI images for IBL are from [Poly Haven](https://polyhaven.com/hdris), which are generously provided under [CC0](https://polyhaven.com/license).

Notes:
* Spheres can spin.
* Lighting is done in world space.
* IBL uses rectangular images and not cube maps. There are some artifacts and side effects, such as the pinch at the top and bottom of the diffuse indirect lighting. Haven't found a good way to solve this yet.

## pbr_camera
![alt text](../../images/screenshots/pbr/pbr_camera.png?raw=true)

**Slightly more sophisticated PBR implementation with normal map**

PBR render of a [camera model](https://sketchfab.com/3d-models/dae-bilora-bella-46-camera-game-ready-asset-eeb9d9f0627f4783b5d16a8732f0d1a4)
with normal mapping. Tangent and bitangent are calculated using [MikkTSpace](https://github.com/mmikk/MikkTSpace).

IBL uses rectangular images and not cube maps. There are some artifacts and side effects, such as the 
pinch at the top and bottom of the diffuse indirect lighting. Haven't found a good way to solve
this yet.

HDRI images for IBL are from [Poly Haven](https://polyhaven.com/hdris), which are generously provided under [CC0](https://polyhaven.com/license).

Notes:
* Model can be spin.
* Lighting is done in world space.
* IBL uses rectangular images and not cube maps. There are some artifacts and side effects, such as the pinch at the top and bottom of the diffuse indirect lighting. Haven't found a good way to solve this yet.

## pbr_explorer
![alt text](../../images/screenshots/pbr/pbr_explorer.png?raw=true)

**Exploring the parameters of PBR**

Toy program to explore the various Distribution, Fresnel, and Geometry functions for implementing
PBR. Knob is borrowed from [Google Filament](https://github.com/google/filament) and monkey is generated from [Blender](https://www.blender.org/).

HDRI images for IBL are from [Poly Haven](https://polyhaven.com/hdris), which are generously provided under [CC0](https://polyhaven.com/license).

Notes:
* The view camera can spin around the scene.
* Lighting is done in world space.
* IBL uses rectangular images and not cube maps. There are some artifacts and side effects, such as the pinch at the top and bottom of the diffuse indirect lighting. Haven't found a good way to solve this yet.

