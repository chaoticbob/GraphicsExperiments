# Material Rendering Variable Names

Names for material and rendering variables. These names are a guideline not a rule.

## Material Variables
| Name      | Type   | Range  | Description|
|-----------|--------|--------|------------|
|`baseColor`|`float3`| | Base color of material, aka `albedo`|
|`roughness`|`float` | [0, 1] | Roughness for both dielectric and metals, aka `perceptualRoughness`|
|`metallic` |`float` | [0, 1] | 1 = metal material, 0 = dielectric material |
|`specular` |`float` | [0, 1] | Specular reflection strength for rough dielectric materials, use `0.5` if in doubt|
|`ior` |`float` || Index of refraction|

## Rendering Variables
| Name       | Type  | Range  | Description|
|------------|-------|--------|------------|
|`P`         |`float3`|| Surface position|
|`N`         |`float3`|| Normalized surface normal|
|`E`         |`float3`|| Eye position|
|`V`         |`float3`|| Normalized view vector: `V = normalize(E - P)` |
|`I`         |`float3`|| Normalized incident vector: `I = -V`|
|`R`         |`float3`|| Normalized reflection vector: `R = reflect(-V, N)`|
|`H`         |`float3`|| Normalized half vector: `H = normalize(L + V)`|
|`L`         |`float3`|| Normalized light vector: `L = normalized(Lp - P)`|
|`Lp`        |`float3`|| Light position|
|`Lc`        |`float3`|| Light color|
|`Ls`        |`float` || Light intensity|
|`Rd`        |`float3`|| Diffuse bidirectional reflectance|
|`Rs`        |`float3`|| Specular bidirectional reflectance|
|`Cd`        |`float3`|| Diffuse color remapped from `baseColor` (or `albedo`): `Cd = baseColor * dieletric`|
|`F0`        |`float3`|| Fresnel at 0 degrees: `F0 = (0.16 * specular * specular * dieletric) + (baseColor * metallic)`|
|`alpha`     |`float` || Squared value of  `roughness` (or `perceptualRoughness`) from material: `alpha = roughness * roughness`|
|`dieletric` |`float` || 1 = dielectric material, 0 = metal material: `dielectric = (1 = metallic)`|