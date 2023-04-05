#define PI          3.1415292
#define EPSILON     0.00001

// -----------------------------------------------------------------------------
// Common Resources
// -----------------------------------------------------------------------------
struct Light
{
    float3 Position;
    float3 Color;
    float  Intensity;
};

struct SceneParameters 
{
	float4x4 ViewInverseMatrix;
	float4x4 ProjectionInverseMatrix;
	float4x4 ViewProjectionMatrix;
    float3   EyePosition;
    uint     NumLights;
    Light    Lights[8];
};

ConstantBuffer<SceneParameters> SceneParams : register(b5); // Scene params

// -----------------------------------------------------------------------------
// Ray Tracing Resources
// -----------------------------------------------------------------------------

RaytracingAccelerationStructure  Scene         : register(t0); // Acceleration structure
RWTexture2D<float4>              RenderTarget  : register(u1); // Output texture

struct Triangle {
    uint vIdx0;
    uint vIdx1;
    uint vIdx2;
};

StructuredBuffer<Triangle> Triangles[5] : register(t20); // Index buffer (4 spheres, 1 box)
StructuredBuffer<float3>   Positions[5] : register(t25); // Position buffer (4 spheres, 1 box)
StructuredBuffer<float3>   Normals[5]   : register(t30); // Normal buffer (4 spheres, 1 box)

// -----------------------------------------------------------------------------
// PBR Resources
// -----------------------------------------------------------------------------
struct MaterialParameters 
{
    float3 baseColor;
    float  roughness;
    float3 absorbColor;
};

// We'll use a StructuredBuffer here since we can't 
// set root constants per draw. 
//
StructuredBuffer<MaterialParameters> MaterialParams : register(t9); // Material params

Texture2D    IBLEnvironmentMap : register(t12);
SamplerState IBLMapSampler     : register(s14);

// -----------------------------------------------------------------------------
// Functions
// -----------------------------------------------------------------------------

// circular atan2 - converts (x,y) on a unit circle to [0, 2pi]
//
#define catan2_epsilon 0.00001
#define catan2_NAN     0.0 / 0.0 // No gaurantee this is correct

float catan2(float y, float x)
{ 
    float absx = abs(x);
    float absy = abs(y);
    if ((absx < catan2_epsilon) && (absy < catan2_epsilon)) {
        return catan2_NAN;
    }
    else if ((absx > 0) && (absy == 0.0)) {
        return 0.0;
    }
    float s = 1.5 * 3.141592;
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
float2 CartesianToSpherical(float3 pos)
{
    float absX = abs(pos.x);
    float absZ = abs(pos.z);
    // Handle pos pointing straight up or straight down
    if ((absX < 0.00001) && (absZ <= 0.00001)) {
        // Pointing straight up
        if (pos.y > 0) {
            return float2(0, 0);
        }
        // Pointing straight down
        else if (pos.y < 0) {
            return float2(0, 3.141592);
        }
        // Something went terribly wrong
        else {            
            return float2(catan2_NAN, catan2_NAN);
        }
    }
    float theta = catan2(pos.z, pos.x);
    float phi   = acos(pos.y);
    return float2(theta, phi);
}

float FresnelSchlick(float3 I, float3 N, float n1, float n2)
{
    float r0 = (n1 - n2) / (n1 + n2);
    r0 = r0 * r0;
    
    float cosX = -dot(I, N);
    if (n1 > n2) {
        float n = n1 / n2;
        float sinT2 = n * n * (1.0 - cosX * cosX);
        if (sinT2 > 1.0) {
            return 1.0; // TIR
        }
        cosX = sqrt(1.0 - sinT2);
    }
    float x = 1.0 - cosX;
    return r0 + (1.0 - r0) * x * x * x * x *x;
}

float3 GetIBLEnvironment(float3 dir, float lod)
{
    float2 uv = CartesianToSpherical(normalize(dir));
    uv.x = saturate(uv.x / (2.0 * PI));
    uv.y = saturate(uv.y / PI);
    float3 color = IBLEnvironmentMap.SampleLevel(IBLMapSampler, uv, lod).rgb;
    return color;
}

//
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
//
float3 ACESFilm(float3 x){
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

// -----------------------------------------------------------------------------
// Ray Tracing
// -----------------------------------------------------------------------------
typedef BuiltInTriangleIntersectionAttributes MyAttributes;

struct RayPayload
{
    float4 color;
    uint   rayDepth;
    uint   sampleIndex;
    uint   rayType;
};

[shader("raygeneration")]
void MyRaygenShader()
{
    const uint2  pixelCoord = (uint2)DispatchRaysIndex();
    const float2 pixelCenter = (float2)pixelCoord + float2(0.5, 0.5);
	const float2 inUV = pixelCenter/(float2)DispatchRaysDimensions();
	float2 d = inUV * 2.0 - 1.0;
    d.y = -d.y;

	float4 origin = mul(SceneParams.ViewInverseMatrix, float4(0,0,0,1));
	float4 target = mul(SceneParams.ProjectionInverseMatrix, float4(d.x, d.y, 1, 1));
	float4 direction = mul(SceneParams.ViewInverseMatrix, float4(normalize(target.xyz), 0));

    RayDesc ray;
    ray.Origin = origin.xyz;
    ray.Direction = direction.xyz;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayPayload payload = {float4(0, 0, 0, 0), 0, 0, 0};

    TraceRay(
        Scene,         // AccelerationStructure
        RAY_FLAG_NONE, // RayFlags
        ~0,            // InstanceInclusionMask
        0,             // RayContributionToHitGroupIndex
        1,             // MultiplierForGeometryContributionToHitGroupIndex
        0,             // MissShaderIndex
        ray,           // Ray
        payload);      // Payload
    
    float3 finalColor = payload.color.xyz;
    finalColor = ACESFilm(finalColor); 

    RenderTarget[pixelCoord] = float4(pow(finalColor, 1 / 2.2), 0);
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    float3 dir = WorldRayDirection();
    float3 color = GetIBLEnvironment(dir, 0);
    payload.color = float4(color, 1);
}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    uint instIdx = InstanceIndex();
    uint primIdx = PrimitiveIndex();
    Triangle tri = Triangles[instIdx][primIdx];
    
    float3 baseColor = MaterialParams[instIdx].baseColor;
    float3 absorbColor = MaterialParams[instIdx].absorbColor;

    float3 P  = WorldRayOrigin() + (RayTCurrent() * WorldRayDirection());
    float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

    float3 N0 = Normals[instIdx][tri.vIdx0];
    float3 N1 = Normals[instIdx][tri.vIdx1];
    float3 N2 = Normals[instIdx][tri.vIdx2];
    float3 N  = normalize(N0 * barycentrics.x + N1 * barycentrics.y + N2 * barycentrics.z);
    
    float3 V = normalize(SceneParams.EyePosition - P);
    float3 R = reflect(-V, N);
    
    float3 I = normalize(WorldRayDirection());
    
    bool inside  = (HitKind() == HIT_KIND_TRIANGLE_BACK_FACE);
    
    float3 reflection = 0;
    float3 refraction = 0;
    float3 absorption = 1;

    float eta1 = 1.0;
    float eta2 = 1.57;

    if (inside) {
        float temp = eta1;
        eta1 = eta2;
        eta2 = temp;
        N = -N;
    }


    float kr = FresnelSchlick(I, N, eta1, eta2); 
    kr = saturate(kr);
    kr = pow(kr, 1.4);
    float kt = 1.0 - kr;
   
    if (payload.rayDepth < 15) { 
        const float offset = 0.001;  
    
        // refraction
        if (kt > 0) {
            float3 rayDir = refract(I, N, eta1 / eta2);
            
            RayDesc ray = (RayDesc) 0;
            ray.Origin = P + offset * rayDir;
            ray.Direction = rayDir;
            ray.TMin = 0.001;
            ray.TMax = 10000.0;
            
            RayPayload thisPayload = { (float4) 0, payload.rayDepth + 1, 0, 0 };
            
            TraceRay(
            Scene,          // AccelerationStructure
            RAY_FLAG_NONE,  // RayFlags
            ~0,             // InstanceInclusionMask
            0,              // RayContributionToHitGroupIndex
            0,              // MultiplierForGeometryContributionToHitGroupIndex
            0,              // MissShaderIndex
            ray,            // Ray
            thisPayload);   // Payload   
    
            refraction += thisPayload.color.xyz;
        }
        
        if (kr > 0) {
            float3 rayDir = reflect(I, N);
            
            RayDesc ray = (RayDesc) 0;
            ray.Origin = P + offset * rayDir;
            ray.Direction = rayDir;
            ray.TMin = 0.001;
            ray.TMax = 10000.0;
            
            RayPayload thisPayload = { (float4) 0, payload.rayDepth + 1, 0, 0 };
            
            TraceRay(
            Scene,          // AccelerationStructure
            RAY_FLAG_NONE,  // RayFlags
            ~0,             // InstanceInclusionMask
            0,              // RayContributionToHitGroupIndex
            0,              // MultiplierForGeometryContributionToHitGroupIndex
            0,              // MissShaderIndex
            ray,            // Ray
            thisPayload);   // Payload   
           
            reflection += thisPayload.color.xyz;            
        }
    }
    
    if (inside) {
        float absorbDist = RayTCurrent();
        absorption = exp(-absorbColor * absorbDist);
    }
    
    float3 finalColor = baseColor * (reflection * kr + refraction * kt);
    finalColor *= absorption;
    
    payload.color = float4(finalColor, 0);
}
