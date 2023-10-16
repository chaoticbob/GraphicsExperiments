#include <metal_stdlib>

using namespace metal;
using namespace raytracing;

struct CameraProperties {
	float4x4 ViewInverse;
	float4x4 ProjInverse;
};

struct Triangle {
	uint vIdx0;
	uint vIdx1;
	uint vIdx2;
};

struct RayPayload
{
    float4 color;
};

void TraceRay(
            instance_acceleration_structure Scene,
	device	float3*							Normals,
            ray                             ray,
    thread  RayPayload&                     payload);

// [shader("raygeneration")]
kernel void MyRayGen(
             uint2                           DispatchRaysIndex         [[thread_position_in_grid]],
             uint2                           DispatchRaysDimensions    [[threads_per_grid]],
             instance_acceleration_structure Scene                     [[buffer(0)]],
	constant CameraProperties&				 Cam					   [[buffer(1)]],
	device	 float3*						 Normals				   [[buffer(2)]],
             texture2d<float, access::write> RenderTarget              [[texture(0)]])
{
    const float2 pixelCenter = (float2)DispatchRaysIndex + float2(0.5, 0.5);
    const float2 inUV = pixelCenter/(float2)DispatchRaysDimensions;
	float2 d = inUV * 2.0 - 1.0;
    d.y = -d.y;

    float4 origin = (Cam.ViewInverse * float4(0,0,0,1));
    float4 target = (Cam.ProjInverse * float4(d.x, d.y, 1, 1));
    float4 direction = (Cam.ViewInverse * float4(normalize(target.xyz), 0));

    ray ray;
    ray.origin = origin.xyz;
    ray.direction = direction.xyz;
    ray.min_distance = 0.001;
    ray.max_distance = 10000.0;

    RayPayload payload = {float4(0,0,0,0)};

    TraceRay(
        Scene,      // AccelerationStructure
		Normals,	// Per Vertex Normals
        ray,        // Ray
        payload);   // Ray payload

    RenderTarget.write(payload.color, DispatchRaysIndex);
}

// [shader("miss")]
void MyMissShader(thread  RayPayload& payload)
{
    payload.color = float4(0, 0, 0, 1);
}

// [shader("closesthit")]
void MyClosestHitShader(
             intersector<triangle_data, instancing>::result_type intersection,
	device	 float3*								 		Normals,
             ray                                     		WorldRay,
    thread   RayPayload&                             		payload)
{
	Triangle tri = *(const device Triangle*)intersection.primitive_data;

    float3 hitPosition = WorldRay.origin + intersection.distance * WorldRay.direction;
	float3 barycentrics = float3(
		1 - intersection.triangle_barycentric_coord.x - intersection.triangle_barycentric_coord.y,
		intersection.triangle_barycentric_coord.x,
		intersection.triangle_barycentric_coord.y);

    float3 N0 = Normals[tri.vIdx0];
    float3 N1 = Normals[tri.vIdx1];
    float3 N2 = Normals[tri.vIdx2];
    float3 N  = N0 * barycentrics.x + N1 * barycentrics.y + N2 * barycentrics.z;

    // Lambert shading
    float3 lightPos = float3(2, 5, 5);
    float3 L = normalize(lightPos - hitPosition);
    float d = 0.8 * saturate(dot(L, N));
    float a = 0.2;

    float3 color = (float3)(d + a);

    payload.color = float4(color, 1);
}

void TraceRay(
             instance_acceleration_structure         Scene,
	device   float3*								 Normals,
             ray                                     ray,
    thread   RayPayload&                             payload)
{
    intersector<triangle_data, instancing>                intersector;
    ::intersector<triangle_data, instancing>::result_type intersection;

    intersection = intersector.intersect(ray, Scene, 1);

    if (intersection.type == intersection_type::none) {
        MyMissShader(payload);

    } else if (intersection.type == intersection_type::triangle) {

        MyClosestHitShader(
            intersection,
			Normals,
            ray,
            payload);
    }
}

struct VSOutput {
    float4 Position [[position]];
    float2 TexCoord;
};

vertex VSOutput vsmain(unsigned short id [[vertex_id]])
{
    VSOutput result;

    // Clip space position
    result.Position.x = (float)(id / 2) * 4.0 - 1.0;
    result.Position.y = (float)(id % 2) * 4.0 - 1.0;
    result.Position.z = 0.0;
    result.Position.w = 1.0;

    // Texture coordinates
    result.TexCoord.x = (float)(id / 2) * 2.0;
    result.TexCoord.y = 1.0 - (float)(id % 2) * 2.0;

    return result;
}

fragment float4 psmain(VSOutput input [[stage_in]], texture2d<float> Tex0)
{
    constexpr sampler Sampler0(min_filter::nearest, mag_filter::nearest, mip_filter::none);
    return Tex0.sample(Sampler0, input.TexCoord);
}


