#version 460
#extension GL_EXT_ray_tracing : enable

#define PI          3.1415292
#define EPSILON     0.00001

// -----------------------------------------------------------------------------
// Common Resources
// -----------------------------------------------------------------------------
struct Light
{
	vec3   Position;
	vec3   Color;
	vec    Intensity;
};

struct SceneParameters 
{
	mat4   ViewInverseMatrix;
	mat4   ProjectionInverseMatrix;
	mat4   ViewProjectionMatrix;
	vec3   EyePosition;
	uint   MaxSamples;
	uint   NumLights;
	Light  Lights[8];
};

layout(binding = 5) uniform buffer SceneParams;

// -----------------------------------------------------------------------------
// Ray Tracing Resources
// -----------------------------------------------------------------------------

layout(binding = 0) uniform accelerationStructureEXT Scene;	// Acceleration structure
layout(binding = 1) uniform image2D RenderTarget;		// Output texture
layout(binding = 2) uniform image2d AccumTarget			// Accumulation texture
layout(binding = 3) uniform buffer RayGenSamples;		// Ray generation samples

struct Triangle {
	uint   vIdx0;
	uint   vIdx1;
	uint   vIdx2;
};

layout(binding = 20) uniform buffer Triangle Triangles[5];  	// Index buffer (4 spheres, 1 box)
layout(binding = 25) uniform buffer vec3 Positions[5];  	// Position buffer (4 spheres, 1 box)
layout(binding = 30) uniform buffer vec3 Normals[5]; 		// Normal buffer (4 spheres, 1 box)

// -----------------------------------------------------------------------------
// Material Parameters
// -----------------------------------------------------------------------------
struct MaterialParameters 
{
	vec3   baseColor;
	vec    roughness;
	vec    metallic;
	vec    specularReflectance;
	vec    ior;
};

layout(binding = 9) uniform buffer MaterialParameters MaterialParams;  // Material params

layout(binding = 100) uniform image2D IBLEnvironmentMap;
layout(binding = 10) uniform sampler2D IBLMapSampler;

// -----------------------------------------------------------------------------
// Utility Functions
// -----------------------------------------------------------------------------

// https://www.reedbeta.com/blog/hash-functions-for-gpu-rendering/
uint pcg_hash(inout uint rngState)
{
	uint state = rngState;
	rngState = rngState * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

vec Random01(inout uint input)
{
	return pcg_hash(input) / 4294967296.0;
}

// -----------------------------------------------------------------------------
// Lighting Functions
// -----------------------------------------------------------------------------

vec3 Fresnel_SchlickRoughness(vec cosTheta, vec3 F0, vec roughness)
{
	vec3 r = (vec3)(1 - roughness);
	return F0 + (max(r, F0) - F0) * pow(1 - cosTheta, 5);
}

vec FresnelSchlickReflectionAmount(vec3 I, vec3 N, vec n1, vec n2)
{
	vec r0 = (n1 - n2) / (n1 + n2);
	r0 = r0 * r0;

	vec cosX = -dot(I, N);
	if (n1 > n2) {
		vec n = n1 / n2;
		vec sinT2 = n * n * (1.0 - cosX * cosX);
		if (sinT2 > 1.0) {
			return 1.0; // TIR
		}
		cosX = sqrt(1.0 - sinT2);
	}
	vec x = 1.0 - cosX;
	vec fr = r0 + (1.0 - r0) * x * x * x * x *x;

	return fr;
}

// circular atan2 - converts (x,y) on a unit circle to [0, 2pi]
//
#define catan2_epsilon 0.00001
#define catan2_NAN     0.0 / 0.0 // No gaurantee this is correct

vec catan2(vec y, vec x)
{ 
	vec absx = abs(x);
	vec absy = abs(y);
	if ((absx < catan2_epsilon) && (absy < catan2_epsilon)) {
		return catan2_NAN;
	}
	else if ((absx > 0) && (absy == 0.0)) {
		return 0.0;
	}
	vec s = 1.5 * 3.141592;
	if (y >= 0) {
		s = 3.141592 / 2.0;
	}
	return s - atan(x / y);
}

// Converts cartesian unit position 'pos' to (theta, phi) in
// spherical coodinates.
//
// theta is the azimuth angle between [0, 2pi].
// phi is the polar angle between [0, pi].
//
// NOTE: (0, 0, 0) will result in nan
//
vec2 CartesianToSpherical(vec3 pos)
{
	vec absX = abs(pos.x);
	vec absZ = abs(pos.z);
	// Handle pos pointing straight up or straight down
	if ((absX < 0.00001) && (absZ <= 0.00001)) {
		// Pointing straight up
		if (pos.y > 0) {
			return vec2(0, 0);
		}
		// Pointing straight down
		else if (pos.y < 0) {
			return vec2(0, 3.141592);
		}
		// Something went terribly wrong
		else {            
			return vec2(catan2_NAN, catan2_NAN);
		}
	}
	vec theta = catan2(pos.z, pos.x);
	vec phi   = acos(pos.y);
	return vec2(theta, phi);
}

vec2 Hammersley(uint i, uint N)
{
	uint bits = (i << 16u) | (i >> 16u);
	bits      = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits      = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits      = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits      = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	vec rdi = vec(bits) * 2.3283064365386963e-10f;
	return vec2(vec(i) / vec(N), rdi);
}

vec3 ImportanceSampleGGX(vec2 Xi, vec Roughness, vec3 N)
{
	vec a        = Roughness * Roughness;
	vec Phi      = 2 * PI * Xi.x;
	vec CosTheta = sqrt((1 - Xi.y) / (1 + (a * a - 1) * Xi.y));
	vec SinTheta = sqrt(1 - CosTheta * CosTheta);

	vec3 H        = (vec3)0;
	H.x             = SinTheta * cos(Phi);
	H.y             = SinTheta * sin(Phi);
	H.z             = CosTheta;
	vec3 UpVector = abs(N.y) < 0.99999f ? vec3(0, 1, 0) : vec3(1, 0, 0);
	vec3 TangentX = normalize(cross(UpVector, N));
	vec3 TangentY = cross(N, TangentX);

	// Tangent to world space
	return TangentX * H.x + TangentY * H.y + N * H.z;
}

vec3 GetIBLEnvironment(vec3 dir, vec lod)
{
	vec2 uv = CartesianToSpherical(normalize(dir));
	uv.x = saturate(uv.x / (2.0 * PI));
	uv.y = saturate(uv.y / PI);
	vec3 color = IBLEnvironmentMap.SampleLevel(IBLMapSampler, uv, lod).rgb;
	color = min(color, (vec3)100.0);
	return color;
}

// Samples hemisphere using Hammersley pattern for irradiance contribution
vec3 GenIrradianceSampleDir(uint sampleIndex, vec3 N)
{
	vec2 Xi = Hammersley(sampleIndex, SceneParams.MaxSamples);
	vec3 L  = ImportanceSampleGGX(Xi, 1.0, N);
	return L;
}

vec3 GenSpecularSampleDir(uint sampleIndex, vec3 N, vec Roughness)
{
	vec2 Xi = Hammersley(sampleIndex, SceneParams.MaxSamples);
	vec3 L  = ImportanceSampleGGX(Xi, Roughness, N);
	return L;
}

// Samples hemisphere using RNG for irradiance contribution
vec3 GenIrradianceSampleDirRNG(inout uint rngState, vec3 N)
{
	vec   u = Random01(rngState);
	vec   v = Random01(rngState);
	vec2 Xi = vec2(u, v);
	vec3 L  = ImportanceSampleGGX(Xi, 1.0, N);
	return L;
}

vec3 GenSpecularSampleDirRNG(inout uint rngState, vec3 N, vec Roughness)
{
	vec   u = Random01(rngState);
	vec   v = Random01(rngState);
	vec2 Xi = vec2(u, v);
	vec3 L  = ImportanceSampleGGX(Xi, Roughness, N);
	return L;
}

//
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
//
vec3 ACESFilm(vec3 x){
	return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

// -----------------------------------------------------------------------------
// Ray Tracing
// -----------------------------------------------------------------------------
hitAttributeEXT vec2 barycentrics;

struct RayPayload
{
	vec4 color;
	uint rayDepth;
	uint sampleIndex;
	uint rayType;
};

layout(location = 0) rayPayloadInEXT RayPayload payload;

void MyRaygenShader()
{
	uint2 rayIndex2 = DispatchRaysIndex().xy;
	uint  rayIndex = rayIndex2.y * 1920 + rayIndex2.x;
	uint  sampleCount = RayGenSamples[rayIndex];
	uint  rngState = sampleCount + rayIndex * 1943006372;

	if (sampleCount < SceneParams.MaxSamples)
	{
		vec2 Xi = Hammersley(sampleCount, SceneParams.MaxSamples);
		const vec2 pixelCenter = (vec2) rayIndex2 + vec2(0.5, 0.5) + Xi;
		const vec2 inUV = pixelCenter/(vec2)DispatchRaysDimensions();
		vec2 d = inUV * 2.0 - 1.0;
		d.y = -d.y;

		vec4 origin = mul(SceneParams.ViewInverseMatrix, vec4(0,0,0,1));
		vec4 target = mul(SceneParams.ProjectionInverseMatrix, vec4(d.x, d.y, 1, 1));
		vec4 direction = mul(SceneParams.ViewInverseMatrix, vec4(normalize(target.xyz), 0));

		vec tmin = 0.001;
		vec tmax = 10000.0;

		RayPayload payload = {vec4(0, 0, 0, 0), 0, sampleCount, 0};

		traceRayEXT(
				Scene,           // topLevel
				gl_RayFlagsOpaqueEXT,  // rayFlags
				0xff,          // cullMask
				0,            // sbtRecordOffset
				0,            // sbtRecordStride
				0,            // missIndex
				origin.xyz,        // origin
				tmin,          // Tmin
				direction.xyz,      // direction
				tmax,          // Tmax
				payload);      // Payload

		sampleCount += 1;
		AccumTarget[rayIndex2] += payload.color;
	}

	vec3 finalColor = AccumTarget[rayIndex2].xyz / (vec)sampleCount;
	finalColor = ACESFilm(finalColor); 

	RenderTarget[rayIndex2] = vec4(pow(finalColor, 1 / 2.2), 0);
	RayGenSamples[rayIndex] = sampleCount;
}

void MyMissShader()
{
	vec3 dir = WorldRayDirection();
	vec3 color = GetIBLEnvironment(dir, 0);
	payload.color = vec4(color, 1);
}

void MyClosestHitShader()
{
	uint instIdx = InstanceIndex();
	uint primIdx = PrimitiveIndex();
	Triangle tri = Triangles[instIdx][primIdx];

	vec3 P = WorldRayOrigin() + (RayTCurrent() * WorldRayDirection());
	vec3 I = normalize(WorldRayDirection());    
	vec3 V = -I;
	bool   inside = (HitKind() == HIT_KIND_TRIANGLE_BACK_FACE);       
	vec3 barycentrics = vec3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

	vec3 N0 = Normals[instIdx][tri.vIdx0];
	vec3 N1 = Normals[instIdx][tri.vIdx1];
	vec3 N2 = Normals[instIdx][tri.vIdx2];
	vec3 N  = normalize(N0 * barycentrics.x + N1 * barycentrics.y + N2 * barycentrics.z);

	// Transform normal trom object to world space
	vec3x4 m0 = ObjectToWorld3x4();
	vec3x3 m1 = vec3x3(m0._11, m0._12, m0._13,
			m0._21, m0._22, m0._23,
			m0._31, m0._32, m0._33);
	N = mul(m1, N);    

	// Material variables
	vec3 baseColor = MaterialParams[instIdx].baseColor;
	vec  roughness = MaterialParams[instIdx].roughness;
	vec  metallic  = MaterialParams[instIdx].metallic;
	vec  specularReflectance = MaterialParams[instIdx].specularReflectance;
	vec  ior = MaterialParams[instIdx].ior;

	// Remap
	roughness = roughness * roughness;

	// Calculate F0
	vec3 F0 = 0.16 * specularReflectance * specularReflectance * (1 - metallic) + baseColor * metallic;  

	vec  cosTheta = saturate(dot(N, -I));
	vec3 F = Fresnel_SchlickRoughness(cosTheta, F0, roughness);
	vec3 kD = (1.0 - F) * (1.0 - metallic);

	vec3 reflection = 0;
	vec3 refraction = 0;
	vec  kr = 1;
	vec  kt = 0;
	vec  offset = 0.001;

	vec eta1 = 1.0;
	vec eta2 = ior;

	if (inside) {
		vec temp = eta1;
		eta1 = eta2;
		eta2 = temp;
		N = -N;
	}

	if (ior > 1.0) {
		kr = saturate(FresnelSchlickReflectionAmount(I, N, eta1, eta2)); 
		kt = 1.0 - kr;
	}

	if (payload.rayDepth < 7) {
		if (kr > 0) {
			// Diffuse
			{                
				//vec L = GenIrradianceSampleDir(payload.sampleIndex, N);
				vec L = GenIrradianceSampleDirRNG(payload.rngState, N);

				vec3 rayDir = L;

				vec tmain = 0.001;
				vec tmax = 10000.0;

				RayPayload thisPayload = {(vec4)0, payload.rayDepth + 1, payload.sampleIndex, payload.rngState};

				traceRayEXT(
						Scene,           // topLevel
						gl_RayFlagsOpaqueEXT,  // rayFlags
						0xff,          // cullMask
						0,            // sbtRecordOffset
						0,            // sbtRecordStride
						0,            // missIndex
						P + offset * rayDir,  // origin
						tmin,          // Tmin
						rayDir,          // direction
						tmax,          // Tmax
						thisPayload);           // Payload

				vec3 bounceColor = thisPayload.color.xyz;
				payload.rngState = thisPayload.rngState;

				vec NoL = saturate(dot(N, L));
				reflection += kD * bounceColor * NoL;                
			}

			// Specular
			{
				//vec3 H = normalize(GenSpecularSampleDir(payload.sampleIndex, N, roughness));
				vec3 H = normalize(GenSpecularSampleDirRNG(payload.rngState, N, roughness));
				v3c3 L = 2.0 * dot(V, H) * H  - V;

				vec3 rayDir = L;

				vec tmain = 0.001;
				vec tmax = 10000.0;

				RayPayload thisPayload = {(vec4)0, payload.rayDepth + 1, payload.sampleIndex, payload.rngState};

				traceRayEXT(
						Scene,           // topLevel
						gl_RayFlagsOpaqueEXT,  // rayFlags
						0xff,          // cullMask
						0,            // sbtRecordOffset
						0,            // sbtRecordStride
						0,            // missIndex
						P + offset * rayDir,  // origin
						tmin,          // Tmin
						rayDir,          // direction
						tmax,          // Tmax
						thisPayload);           // Payload

				vec3 bounceColor = thisPayload.color.xyz;
				payload.rngState = thisPayload.rngState;

				reflection += F * bounceColor;
			}
		}

		// Refraction
		if (kt > 0) {
			vec3 rayDir = refract(I, N, eta1 / eta2);

			vec tmain = 0.001;
			vec tmax = 10000.0;

			RayPayload thisPayload = {(vec4)0, payload.rayDepth + 1, payload.sampleIndex, payload.rngState};

			traceRayEXT(
					Scene,           // topLevel
					gl_RayFlagsOpaqueEXT,  // rayFlags
					0xff,          // cullMask
					0,            // sbtRecordOffset
					0,            // sbtRecordStride
					0,            // missIndex
					P + offset * rayDir,  // origin
					tmin,          // Tmin
					rayDir,          // direction
					tmax,          // Tmax
					thisPayload);   // Payload   

			vec3 bounceColor = thisPayload.color.xyz;
			payload.rngState = thisPayload.rngState;

			refraction += bounceColor;          
		}
	}

	vec3 finalColor = (reflection * kr * baseColor) + (refraction * kt);

	payload.color = vec4(finalColor, 0);
}
