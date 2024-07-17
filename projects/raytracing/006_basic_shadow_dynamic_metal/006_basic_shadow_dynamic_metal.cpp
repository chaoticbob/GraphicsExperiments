#include "window.h"

#include "mtl_renderer.h"

#include "sphereflake.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#define CHECK_CALL(FN)                                                               \
    {                                                                                \
        NS::Error* pError = FN;                                                      \
        if (pError != nullptr)                                                       \
        {                                                                            \
            std::stringstream ss;                                                    \
            ss << "\n";                                                              \
            ss << "*** FUNCTION CALL FAILED *** \n";                                 \
            ss << "FUNCTION: " << #FN << "\n";                                       \
            ss << "Error: " << pError->localizedDescription()->utf8String() << "\n"; \
            ss << "\n";                                                              \
            GREX_LOG_ERROR(ss.str().c_str());                                        \
            assert(false);                                                           \
        }                                                                            \
    }

// =============================================================================
// Shader code
// =============================================================================
const char* gShaders = R"(
#include <metal_stdlib>

using namespace metal;
using namespace raytracing;

struct CameraProperties {
	float4x4 ViewInverse;
	float4x4 ProjInverse;
    float3   EyePosition;
    float3   LightPosition;
};

struct Sphere {
	float minX;
	float minY;
	float minZ;
	float maxX;
	float maxY;
	float maxZ;
};

struct RayPayload
{
    float4 color;
    uint   recursionDepth;
};

struct ShadowPayload
{
    bool hit;
};

// Return the type for a bounding box intersection function.
struct BoundingBoxIntersection {
    bool  accept   [[accept_intersection]];    // Whether to accept or reject the intersection
    float distance [[distance]];             // Distance from the ray origin to the intersection point
};

// -----------------------------------------------------------------------------
// Function Prototypes

void TraceRay(
             instance_acceleration_structure         Scene,
             intersection_function_table<instancing> intersectionFunctionTable,
    constant CameraProperties&                       Cam,
             ray                                     ray,
    thread   RayPayload&                             payload);

void TraceShadowRay(
             instance_acceleration_structure         Scene,
             intersection_function_table<instancing> intersectionFunctionTable,
             ray                                     ray,
    thread   ShadowPayload&                          payload);

// -----------------------------------------------------------------------------

// [shader("raygeneration")]
kernel void MyRayGen(
             uint2                                   DispatchRaysIndex         [[thread_position_in_grid]],
             uint2                                   DispatchRaysDimensions    [[threads_per_grid]],
             instance_acceleration_structure         Scene                     [[buffer(0)]],
    constant CameraProperties&                       Cam                       [[buffer(1)]],
             intersection_function_table<instancing> intersectionFunctionTable [[buffer(2)]],
             texture2d<float, access::write>         RenderTarget              [[texture(0)]])
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

    RayPayload payload = { float4(0,0,0,0), 0 };

    TraceRay(
        Scene,                      // AccelerationStructure
        intersectionFunctionTable,  // Intersection Functions
        Cam,
        ray,                        // Ray
        payload);                   // Ray payload

    RenderTarget.write(payload.color, DispatchRaysIndex);
}

// -----------------------------------------------------------------------------

float3 CubicBezier(float t, float3 P0, float3 P1, float3 P2, float3 P3)
{
    float s = (1 - t);
    float a = s * s * s;
    float b = 3 * s * s * t;
    float c = 3 * s * t * t;
    float d = t * t * t;
    return a * P0 + b * P1 + c * P2 + d * P3;
}

// [shader("miss")]
void MyMissShader(
            ray         WorldRay,
    thread  RayPayload& payload)
{
    float3 P = normalize(WorldRay.direction);
    float  t = (P.y + 1) / 2;
    
    float3 C0 = float3(0.010, 0.010, 0.020);
    float3 C1 = float3(0.920, 0.920, 0.990);
    float3 C2 = float3(0.437, 0.609, 0.747);
    float3 C3 = float3(0.190, 0.312, 0.579);
    float3 C = CubicBezier(t, C0, C1, C2, C3);
    
    payload.color = float4(C, 1);
}

// -----------------------------------------------------------------------------

// [shader("miss")]
void MyMissShadowShader(thread ShadowPayload& payload)
{
    payload.hit = false;
}

// -----------------------------------------------------------------------------

// Fresnel reflectance - schlick approximation.
float3 FresnelReflectanceSchlick(float3 I, float3 N, float3 f0)
{
    float cosi = saturate(dot(-I, N));
    return f0 + (1 - f0)*pow(1 - cosi, 5);
}

// [shader("closesthit")]
void MyClosestHitShader(
             instance_acceleration_structure         Scene,
    constant CameraProperties&                       Cam,
             intersection_function_table<instancing> intersectionFunctionTable,
             intersector<instancing>::result_type    intersection,
             ray                                     WorldRay,
    thread   RayPayload&                             payload)
{
    float3 GROUND = float3(0.980, 0.863, 0.596);
    float3 SPHERE = float3(0.549, 0.556, 0.554);

    Sphere sphere = *(const device Sphere*)intersection.primitive_data;

    float3 sphereMin = float3(sphere.minX, sphere.minY, sphere.minZ);
    float3 sphereMax = float3(sphere.maxX, sphere.maxY, sphere.maxZ);
    float3 sphereCenter = 0.5 * (sphereMax - sphereMin) + sphereMin;

    float3 hitPosition = WorldRay.origin + intersection.distance * WorldRay.direction;
    float3 hitNormal = normalize(hitPosition - sphereCenter);

    uint currentRecursionDepth = payload.recursionDepth + 1;

    // Diffuse
    float3 lightPos = Cam.LightPosition;
    float3 lightDir = normalize(lightPos - hitPosition);
    float d = saturate(dot(lightDir, hitNormal));

    // Shadow
    float shadow = 0;
    if (currentRecursionDepth < 5) {
        ray ray;
        ray.origin = hitPosition + 0.001 * hitNormal;
        ray.direction = lightDir;
        ray.min_distance = 0.001;
        ray.max_distance = 10000.0;
    
        ShadowPayload shadowPayload = {true};
    
        TraceShadowRay(
            Scene,                      // AccelerationStructure
            intersectionFunctionTable,  // Intersection Functions
            ray,                        // Ray
            shadowPayload);             // Payload
    
        shadow = shadowPayload.hit ? 1.0 : 0.0;
    }   

    if (intersection.primitive_id > 0) {
        float3 reflectedColor = (float3)0;

        if (currentRecursionDepth < 5) {
            ray ray;
            ray.origin = hitPosition + 0.001 * hitNormal;
            ray.direction = reflect(WorldRay.direction, hitNormal);
            ray.min_distance = 0.001;
            ray.max_distance = 10000.0;

            RayPayload subPayload = { float4(0,0,0,0), currentRecursionDepth };

            TraceRay(
                Scene,                      // AccelerationStructure
                intersectionFunctionTable,  // Intersection Functions
                Cam,
                ray,                        // Ray
                subPayload);                // Payload

            float3 fresnelR = FresnelReflectanceSchlick(WorldRay.direction, hitNormal, SPHERE);
            reflectedColor = 0.95 * fresnelR * subPayload.color.xyz;
        }

        float3 V = normalize(Cam.EyePosition - hitPosition);
        float3 R = reflect(-lightDir, hitNormal);
        float  RdotV = saturate(dot(R, V));
        float  s = pow(RdotV, 30.0);

        const float kD = 0.8;
        const float kS = 0.5;

        float3 color = ((kD * d + kS * s) * SPHERE) + reflectedColor * (1 - 0.2 * shadow);
        payload.color = float4(color, 0);                 
    }
    else {
        payload.color = float4(d * GROUND * (1 - 0.4 * shadow), 0);
    }
}

// -----------------------------------------------------------------------------

//
// Based on:
//   https://github.com/georgeouzou/vk_exp/blob/master/shaders/sphere.rint
//
// this method is documented in raytracing gems book
float2 gems_intersections(float3 orig, float3 dir, float3 center, float radius)
{
	float3 f = orig - center;
	float  a = dot(dir, dir);
	float  bi = dot(-f, dir);
	float  c = dot(f, f) - radius * radius;
	float3 s = f + (bi/a)*dir;
	float  discr = radius * radius - dot(s, s);

	float2 t = float2(-1.0, -1.0);
	if (discr >= 0) {
		float q = bi + sign(bi) * sqrt(a*discr);
		float t1 = c / q;
		float t2 = q / a;
		t = float2(t1, t2);
	}
	return t;
}

// [shader("intersection")]
[[intersection(bounding_box, instancing)]]
BoundingBoxIntersection  MyIntersectionShader(
                 float3 orig             [[origin]],
                 float3 dir              [[direction]],
                 float  minDistance      [[min_distance]],
                 float  maxDistance      [[max_distance]],
    const device void*  perPrimitiveData [[primitive_data]])
{
    Sphere sphere = *(const device Sphere*)perPrimitiveData;
    
	float3 aabb_min = float3(sphere.minX, sphere.minY, sphere.minZ);
	float3 aabb_max = float3(sphere.maxX, sphere.maxY, sphere.maxZ);

	float3 center = (aabb_max + aabb_min) / (float3)2.0;
	float radius = (aabb_max.x - aabb_min.x) / 2.0;

    // Might be some wonky behavior if inside sphere
	float2 t = gems_intersections(orig, dir, center, radius);

    // Keep the smallest non-negative value
    float minT = any( t < 0 ) ? max(t.x, t.y) : min(t.x, t.y);

    BoundingBoxIntersection ret;

    if (minT < 0) {
        ret.accept = false;
    }
    else {
        ret.distance = minT;
        ret.accept = ret.distance >= minDistance  && ret.distance <= maxDistance;
    }

   return ret;
}

void TraceRay(
             instance_acceleration_structure         Scene,
             intersection_function_table<instancing> intersectionFunctionTable,
    constant CameraProperties&                       Cam,
             ray                                     ray,
    thread   RayPayload&                             payload)
{
    intersector<instancing>                intersector;
    ::intersector<instancing>::result_type intersection;

    intersection = intersector.intersect(ray, Scene, 1, intersectionFunctionTable);

    if (intersection.type == intersection_type::none) {
        MyMissShader(ray, payload);

    } else if (intersection.type == intersection_type::bounding_box) {

        MyClosestHitShader(
            Scene,
            Cam,
            intersectionFunctionTable,
            intersection,
            ray,
            payload);
    }
}

void TraceShadowRay(
             instance_acceleration_structure         Scene,
             intersection_function_table<instancing> intersectionFunctionTable,
             ray                                     ray,
    thread   ShadowPayload&                          payload)
{
    intersector<instancing>                intersector;
    ::intersector<instancing>::result_type intersection;

    //
    // These flags are important
    //
    intersector.accept_any_intersection(true);
  
    intersection = intersector.intersect(ray, Scene, 1, intersectionFunctionTable);

    if (intersection.type == intersection_type::none) {
        MyMissShadowShader(payload);
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

)";

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 720;
static bool     gEnableDebug  = true;

void CreateSphereBuffer(
    MetalRenderer* pRenderer,
    uint32_t*      pNumSpheres,
    MetalBuffer*   pBuffer);

void CreateBLAS(
    MetalRenderer*        pRenderer,
    uint32_t              numSpheres,
    MetalBuffer*          pSphereBuffer,
    std::vector<MetalAS>& BLAS);

void CreateTLAS(
    MetalRenderer*        pRenderer,
    std::vector<MetalAS>& BLAS,
    MetalAS*              pTLAS);

NS::SharedPtr<MTL::IntersectionFunctionTable> CreateIntersectionFunctionTable(
    MetalRenderer*             pRenderer,
    MTL::Library*              pLibrary,
    MTL::ComputePipelineState* pRaytracingPipeline,
    MetalBuffer*               pSphereBuffer);

// =============================================================================
// main()
// =============================================================================
int main(int argc, char** argv)
{
    std::unique_ptr<MetalRenderer> renderer = std::make_unique<MetalRenderer>();

    if (!InitMetal(renderer.get(), gEnableDebug))
    {
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Compile shaders
    // *************************************************************************
    NS::Error* pError  = nullptr;
    auto       library = NS::TransferPtr(renderer->Device->newLibrary(
        NS::String::string(gShaders, NS::UTF8StringEncoding),
        nullptr,
        &pError));

    if (library.get() == nullptr)
    {
        std::stringstream ss;
        ss << "\n"
           << "Shader compiler error (VS): " << pError->localizedDescription()->utf8String() << "\n";
        GREX_LOG_ERROR(ss.str().c_str());
        assert(false);
        return EXIT_FAILURE;
    }

    MetalShader rayTraceShader;
    rayTraceShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("MyRayGen", NS::UTF8StringEncoding)));
    if (rayTraceShader.Function.get() == nullptr)
    {
        assert(false && "VS Shader MTL::Library::newFunction() failed for raygen");
        return EXIT_FAILURE;
    }

    MetalShader rayTraceIntersectionShader;
    rayTraceIntersectionShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("MyIntersectionShader", NS::UTF8StringEncoding)));
    if (rayTraceIntersectionShader.Function.get() == nullptr)
    {
        assert(false && "VS Shader MTL::Library::newFunction() failed for raygen");
        return EXIT_FAILURE;
    }

    MetalShader vsShader;
    vsShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("vsmain", NS::UTF8StringEncoding)));
    if (vsShader.Function.get() == nullptr)
    {
        assert(false && "VS Shader MTL::Library::newFunction() failed for vertex shader");
        return EXIT_FAILURE;
    }

    MetalShader psShader;
    psShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("psmain", NS::UTF8StringEncoding)));
    if (psShader.Function.get() == nullptr)
    {
        assert(false && "VS Shader MTL::Library::newFunction() failed for fragment shader");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Sphere buffer
    // *************************************************************************
    uint32_t    numSpheres = 0;
    MetalBuffer sphereBuffer;
    CreateSphereBuffer(renderer.get(), &numSpheres, &sphereBuffer);

    // *************************************************************************
    // Ray trace pipeline
    // *************************************************************************
    NS::SharedPtr<MTL::ComputePipelineState> rayTracePipeline;
    {
        MTL::ComputePipelineDescriptor* rayTracePipelineDesc = MTL::ComputePipelineDescriptor::alloc()->init();
        rayTracePipelineDesc->setComputeFunction(rayTraceShader.Function.get());
        rayTracePipelineDesc->setMaxCallStackDepth(5);

        std::vector<MTL::Function*> linkedFunctionsVector;
        linkedFunctionsVector.push_back(rayTraceIntersectionShader.Function.get());

        NS::Array* linkedFunctionsArray = (NS::Array*)CFArrayCreate(
            kCFAllocatorDefault,
            (const void**)linkedFunctionsVector.data(),
            linkedFunctionsVector.size(),
            &kCFTypeArrayCallBacks);

        MTL::LinkedFunctions* linkedFunctions = MTL::LinkedFunctions::alloc()->init();
        linkedFunctions->setFunctions(linkedFunctionsArray);

        rayTracePipelineDesc->setLinkedFunctions(linkedFunctions);

        NS::Error* error;
        rayTracePipeline = NS::TransferPtr(renderer->Device->newComputePipelineState(rayTracePipelineDesc, 0, nullptr, &error));
    }

    // *************************************************************************
    // Copy pipeline
    // *************************************************************************
    NS::SharedPtr<MTL::RenderPipelineState> copyPipeline;
    {
        auto pipelineDesc = NS::TransferPtr(MTL::RenderPipelineDescriptor::alloc()->init());
        pipelineDesc->setVertexFunction(vsShader.Function.get());
        pipelineDesc->setFragmentFunction(psShader.Function.get());
        pipelineDesc->colorAttachments()->object(0)->setPixelFormat(GREX_DEFAULT_RTV_FORMAT);

        NS::Error* error;
        copyPipeline = NS::TransferPtr(renderer->Device->newRenderPipelineState(pipelineDesc.get(), &error));
    }

    // *************************************************************************
    // Bottom level acceleration structure
    // *************************************************************************
    std::vector<MetalAS> blasBuffer;
    CreateBLAS(renderer.get(), numSpheres, &sphereBuffer, blasBuffer);

    // *************************************************************************
    // Top level acceleration structure
    // *************************************************************************
    MetalAS tlasBuffer;
    CreateTLAS(renderer.get(), blasBuffer, &tlasBuffer);

    // *************************************************************************
    // Intersection Function Table
    // *************************************************************************
    NS::SharedPtr<MTL::IntersectionFunctionTable> intersectionFunctionTable =
        CreateIntersectionFunctionTable(
            renderer.get(),
            library.get(),
            rayTracePipeline.get(),
            &sphereBuffer);

    // *************************************************************************
    // Ray trace ouput texture
    // *************************************************************************
    MetalTexture outputTex;
    {
        CHECK_CALL(CreateRWTexture(
            renderer.get(),
            gWindowWidth,
            gWindowHeight,
            MTL::PixelFormatRGBA32Float,
            &outputTex));
    }

    // *************************************************************************
    // Render Pass Description
    // *************************************************************************
    MTL::RenderPassDescriptor* pRenderPassDescriptor = MTL::RenderPassDescriptor::renderPassDescriptor();

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, GREX_BASE_FILE_NAME());
    if (!window)
    {
        assert(false && "GrexWindow::Create failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetNativeWindowHandle(), window->GetWidth(), window->GetHeight(), 2, MTL::PixelFormatDepth32Float))
    {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Main loop
    // *************************************************************************
    MTL::ClearColor clearColor(0.23f, 0.23f, 0.31f, 0);
    uint32_t        frameIndex = 0;

    while (window->PollEvents())
    {
        CA::MetalDrawable* pDrawable = renderer->pSwapchain->nextDrawable();
        assert(pDrawable != nullptr);

        MTL::CommandBuffer* pCommandBuffer = renderer->Queue->commandBuffer();

        MTL::ComputeCommandEncoder* pComputeEncoder = pCommandBuffer->computeCommandEncoder();
        pComputeEncoder->setComputePipelineState(rayTracePipeline.get());
        pComputeEncoder->setAccelerationStructure(tlasBuffer.AS.get(), 0);
        pComputeEncoder->setIntersectionFunctionTable(intersectionFunctionTable.get(), 2);
        pComputeEncoder->setTexture(outputTex.Texture.get(), 0);

        // Add a useResource() call for every BLAS used by the TLAS
        for (int blasIndex = 0; blasIndex < blasBuffer.size(); blasIndex++)
        {
            pComputeEncoder->useResource(blasBuffer[blasIndex].AS.get(), MTL::ResourceUsageRead);
        }

        struct Camera
        {
            glm::mat4 viewInverse;
            glm::mat4 projInverse;
            glm::vec3 eyePosition;
            uint32_t  _pad0;
            glm::vec3 lightPosition;
            uint32_t  _pad1;
        };

        Camera camera      = {};
        camera.eyePosition = vec3(0, 2.5f, 3.5f);

        camera.projInverse = glm::inverse(glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 512.0f));
        auto mat           = glm::lookAt(camera.eyePosition, vec3(0, 1, 0), vec3(0, 1, 0));
        camera.viewInverse = glm::inverse(mat);

        // Update light position
        {
            float t = static_cast<float>(glfwGetTime());
            float r = 15.0f;

            camera.lightPosition = vec3(r * cos(t), 25, r * sin(t));
        }

        pComputeEncoder->setBytes(&camera, sizeof(Camera), 1);

        {
            MTL::Size threadsPerThreadgroup = {8, 8, 1};
            MTL::Size threadsPerGrid        = {
                (gWindowWidth + threadsPerThreadgroup.width - 1) / threadsPerThreadgroup.width,
                (gWindowHeight + threadsPerThreadgroup.height - 1) / threadsPerThreadgroup.height,
                1};

            pComputeEncoder->dispatchThreadgroups(threadsPerGrid, threadsPerThreadgroup);
        }
        pComputeEncoder->endEncoding();

        auto colorTargetDesc = NS::TransferPtr(MTL::RenderPassColorAttachmentDescriptor::alloc()->init());
        colorTargetDesc->setClearColor(clearColor);
        colorTargetDesc->setTexture(pDrawable->texture());
        colorTargetDesc->setLoadAction(MTL::LoadActionClear);
        colorTargetDesc->setStoreAction(MTL::StoreActionStore);
        pRenderPassDescriptor->colorAttachments()->setObject(colorTargetDesc.get(), 0);

        MTL::RenderCommandEncoder* pRenderEncoder = pCommandBuffer->renderCommandEncoder(pRenderPassDescriptor);
        pRenderEncoder->setRenderPipelineState(copyPipeline.get());
        pRenderEncoder->setFragmentTexture(outputTex.Texture.get(), 0);
        pRenderEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::Integer(0), 6);
        pRenderEncoder->endEncoding();

        pCommandBuffer->presentDrawable(pDrawable);
        pCommandBuffer->commit();
    }

    return 0;
}

void CreateSphereBuffer(
    MetalRenderer* pRenderer,
    uint32_t*      pNumSpheres,
    MetalBuffer*   pBuffer)
{
    std::vector<SphereFlake> spheres;

    SphereFlake sphere = {};

    // Ground plane sphere
    float groundSize = 1000.0f;
    sphere.aabbMin   = (groundSize * vec3(-1, -1, -1)) - vec3(0, groundSize, 0);
    sphere.aabbMax   = (groundSize * vec3(1, 1, 1)) - vec3(0, groundSize, 0);
    spheres.push_back(sphere);

    // Initial sphere
    float radius   = 1;
    sphere.aabbMin = (radius * vec3(-1, -1, -1)) + vec3(0, radius, 0);
    sphere.aabbMax = (radius * vec3(1, 1, 1)) + vec3(0, radius, 0);
    spheres.push_back(sphere);

    GenerateSphereFlake(0, 5, radius / 3.0f, radius, vec3(0, radius, 0), vec3(0, 1, 0), spheres);

    *pNumSpheres = CountU32(spheres);

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(spheres),
        DataPtr(spheres),
        pBuffer));

    GREX_LOG_INFO("Num spheres: " << *pNumSpheres);
}

void CreateBLAS(
    MetalRenderer*        pRenderer,
    uint32_t              numSpheres,
    MetalBuffer*          pSphereBuffer,
    std::vector<MetalAS>& BLAS)
{
    NS::AutoreleasePool* pPoolAllocator = NS::AutoreleasePool::alloc()->init();

    MTL::AccelerationStructureBoundingBoxGeometryDescriptor* aabbGeoDesc = MTL::AccelerationStructureBoundingBoxGeometryDescriptor::alloc()->init();

    aabbGeoDesc->setBoundingBoxBuffer(pSphereBuffer->Buffer.get());
    aabbGeoDesc->setBoundingBoxCount(numSpheres);
    aabbGeoDesc->setBoundingBoxStride(sizeof(SphereFlake));
    aabbGeoDesc->setIntersectionFunctionTableOffset(0);

    aabbGeoDesc->setPrimitiveDataBuffer(pSphereBuffer->Buffer.get());
    aabbGeoDesc->setPrimitiveDataStride(sizeof(SphereFlake));
    aabbGeoDesc->setPrimitiveDataElementSize(sizeof(SphereFlake));

    MTL::PrimitiveAccelerationStructureDescriptor* asDesc           = MTL::PrimitiveAccelerationStructureDescriptor::alloc()->init();
    NS::Array*                                     aabbGeoDescArray = (NS::Array*)CFArrayCreate(kCFAllocatorDefault, (const void**)&aabbGeoDesc, 1, &kCFTypeArrayCallBacks);
    asDesc->setGeometryDescriptors(aabbGeoDescArray);

    MetalAS accelStructure;
    CHECK_CALL(CreateAccelerationStructure(pRenderer, asDesc, &accelStructure));

    BLAS.push_back(accelStructure);
}

void CreateTLAS(
    MetalRenderer*        pRenderer,
    std::vector<MetalAS>& BLAS,
    MetalAS*              pTLAS)
{
    // clang-format off
    float transformMatrix[3][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f} 
    };
    // clang-format on

    NS::AutoreleasePool* pPoolAllocator = NS::AutoreleasePool::alloc()->init();

    // Allocate a buffer of acceleration structure instance descriptors. Each descriptor represents
    // an instance of one of the primitive acceleration structures created above, with its own
    // transformation matrix.
    MTL::Buffer* instanceBuffer = pRenderer->Device->newBuffer(sizeof(MTL::AccelerationStructureInstanceDescriptor), MTL::ResourceStorageModeShared);

    MTL::AccelerationStructureInstanceDescriptor* instanceDescriptors = (MTL::AccelerationStructureInstanceDescriptor*)instanceBuffer->contents();

    // Fill out instance descriptors.
    uint32_t instanceIndex = 0;
    uint32_t instanceCount = 1;

    // Map the instance to its acceleration structure.
    instanceDescriptors[instanceIndex].accelerationStructureIndex = 0;

    // Mark the instance as opaque if it doesn't have an intersection function so that the
    // ray intersector doesn't attempt to execute a function that doesn't exist.
    // instanceDescriptors[instanceIndex].options = MTL::AccelerationStructureInstanceOptionOpaque;

    // Metal adds the geometry intersection function table offset and instance intersection
    // function table offset together to determine which intersection function to execute.
    // The sample mapped geometries directly to their intersection functions above, so it
    // sets the instance's table offset to 0.
    instanceDescriptors[instanceIndex].intersectionFunctionTableOffset = 0;

    // Set the instance mask, which the sample uses to filter out intersections between rays
    // and geometry. For example, it uses masks to prevent light sources from being visible
    // to secondary rays, which would result in their contribution being double-counted.
    instanceDescriptors[instanceIndex].mask = 1;

    // Copy the first three rows of the instance transformation matrix. Metal
    // assumes that the bottom row is (0, 0, 0, 1), which allows the renderer to
    // tightly pack instance descriptors in memory.
    for (int column = 0; column < 4; column++)
    {
        for (int row = 0; row < 3; row++)
        {
            instanceDescriptors[instanceIndex].transformationMatrix.columns[column][row] = transformMatrix[row][column];
        }
    }

    std::vector<MTL::AccelerationStructure*> blasAS;
    for (int32_t primitiveIndex = 0; primitiveIndex < BLAS.size(); primitiveIndex++)
    {
        blasAS.push_back(BLAS[primitiveIndex].AS.get());
    }

    NS::Array* blasASArray = (NS::Array*)CFArrayCreate(
        kCFAllocatorDefault,
        (const void**)blasAS.data(),
        blasAS.size(),
        &kCFTypeArrayCallBacks);

    // Create an instance acceleration structure descriptor.
    MTL::InstanceAccelerationStructureDescriptor* accelDescriptor = MTL::InstanceAccelerationStructureDescriptor::alloc()->init();

    accelDescriptor->setInstancedAccelerationStructures(blasASArray);
    accelDescriptor->setInstanceCount(instanceCount);
    accelDescriptor->setInstanceDescriptorBuffer(instanceBuffer);

    CHECK_CALL(CreateAccelerationStructure(pRenderer, accelDescriptor, pTLAS));
}

NS::SharedPtr<MTL::IntersectionFunctionTable> CreateIntersectionFunctionTable(
    MetalRenderer*             pRenderer,
    MTL::Library*              pLibrary,
    MTL::ComputePipelineState* pRaytracingPipeline,
    MetalBuffer*               pSphereBuffer)
{
    NS::AutoreleasePool* pPoolAllocator = NS::AutoreleasePool::alloc()->init();

    // Create an intersection function table
    MTL::IntersectionFunctionTableDescriptor* intersectionFunctionTableDesc =
        MTL::IntersectionFunctionTableDescriptor::alloc()->init();

    intersectionFunctionTableDesc->setFunctionCount(1);

    MTL::IntersectionFunctionTable* intersectionFunctionTable = pRaytracingPipeline->newIntersectionFunctionTable(
        intersectionFunctionTableDesc);

    // Get the intersection function from the Metal shader file
    MTL::Function* intersectionFunction = pLibrary->newFunction(
        NS::String::string("MyIntersectionShader", NS::UTF8StringEncoding));

    // Create a function handle from the function
    MTL::FunctionHandle* intersectionFunctionHandle = pRaytracingPipeline->functionHandle(intersectionFunction);

    // Put the newly created function handle into the table
    intersectionFunctionTable->setFunction(intersectionFunctionHandle, 0);

    // Add the sphere flake buffer into the per-primitive data
    intersectionFunctionTable->setBuffer(pSphereBuffer->Buffer.get(), 0, 0);

    return NS::TransferPtr(intersectionFunctionTable);
}
