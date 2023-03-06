struct CameraProperties {
	float4x4 ViewInverse;
	float4x4 ProjInverse;
};

RaytracingAccelerationStructure  Scene        : register(t0); // Acceleration structure
RWTexture2D<float4>              RenderTarget : register(u1); // Output textures
ConstantBuffer<CameraProperties> Cam          : register(b2); // Constant buffer

struct Triangle {
    uint vIdx0;
    uint vIdx1;
    uint vIdx2;
};

StructuredBuffer<Triangle> Triangles : register(t3); // Index buffer
StructuredBuffer<float3>   Positions : register(t4); // Position buffer
StructuredBuffer<float3>   Normals   : register(t4); // Normal buffer

typedef BuiltInTriangleIntersectionAttributes MyAttributes;

struct RayPayload
{
    float4 color;
};

[shader("raygeneration")]
void MyRaygenShader()
{
	const float2 pixelCenter = (float2)DispatchRaysIndex() + float2(0.5, 0.5);
	const float2 inUV = pixelCenter/(float2)DispatchRaysDimensions();
	float2 d = inUV * 2.0 - 1.0;
    d.y = -d.y;

	float4 origin = mul(Cam.ViewInverse, float4(0,0,0,1));
	float4 target = mul(Cam.ProjInverse, float4(d.x, d.y, 1, 1));
	float4 direction = mul(Cam.ViewInverse, float4(normalize(target.xyz), 0));

    RayDesc ray;
    ray.Origin = origin.xyz;
    ray.Direction = direction.xyz;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayPayload payload = {float4(0, 0, 0, 0)};

    TraceRay(
        Scene,                 // AccelerationStructure
        RAY_FLAG_FORCE_OPAQUE, // RayFlags
        ~0,                    // InstanceInclusionMask
        0,                     // RayContributionToHitGroupIndex
        1,                     // MultiplierForGeometryContributionToHitGroupIndex
        0,                     // MissShaderIndex
        ray,                   // Ray
        payload);              // Payload

    RenderTarget[DispatchRaysIndex().xy] = payload.color;
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    payload.color = float4(0, 0, 0, 1);
}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    uint primIdx = PrimitiveIndex();
    Triangle tri = Triangles[primIdx];

    float3 hitPosition  = WorldRayOrigin() + (RayTCurrent() * WorldRayDirection());
    float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

    float3 N0 = Normals[tri.vIdx0];
    float3 N1 = Normals[tri.vIdx1];
    float3 N2 = Normals[tri.vIdx2];
    float3 N  = N0 * barycentrics.x + N1 * barycentrics.y + N2 * barycentrics.z;

    //// Lambert shading
    float3 lightPos = float3(2, 5, 5);
    float3 L = normalize(lightPos - hitPosition);
    float d = 0.8 * saturate(dot(L, N));
    float a = 0.2;

    float3 color = (float3)(d + a);

    payload.color = float4(color, 1);
}
