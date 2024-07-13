#include "window.h"

#include "mtl_renderer.h"

#include "tri_mesh.h"

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
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 720;
static bool     gEnableDebug  = true;

void CreateBLAS(
    MetalRenderer*        pRenderer,
    uint32_t              indexCount,
    MetalBuffer*          pIndexBuffer,
    uint32_t              vertexCount,
    MetalBuffer*          pPositionBuffer,
    std::vector<MetalAS>& BLAS);

void CreateTLAS(
    MetalRenderer*        pRenderer,
    std::vector<MetalAS>& BLAS,
    MetalAS*              pTLAS);

void CreateGeometry(
    MetalRenderer* pRenderer,
    uint32_t*      pIndexCount,
    MetalBuffer*   pIndexBuffer,
    uint32_t*      pVertexCount,
    MetalBuffer*   pPositionBuffer,
    MetalBuffer*   pNormalBuffer);

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
    auto source = LoadString("projects/021_raytracing_triangles/shaders.metal");
    assert((!source.empty()) && "no shader source!");
    
    NS::Error* pError  = nullptr;
    auto       library = NS::TransferPtr(renderer->Device->newLibrary(
        NS::String::string(source.c_str(), NS::UTF8StringEncoding),
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
    // Ray trace pipeline
    // *************************************************************************
    NS::SharedPtr<MTL::ComputePipelineState> rayTracePipeline;
    {
        MTL::ComputePipelineDescriptor* rayTracePipelineDesc = MTL::ComputePipelineDescriptor::alloc()->init();
        rayTracePipelineDesc->setComputeFunction(rayTraceShader.Function.get());

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
    // Create geometry
    // *************************************************************************
    uint32_t    indexCount  = 0;
    uint32_t    vertexCount = 0;
    MetalBuffer indexBuffer;
    MetalBuffer positionBuffer;
    MetalBuffer normalBuffer;
    CreateGeometry(
        renderer.get(),
        &indexCount,
        &indexBuffer,
        &vertexCount,
        &positionBuffer,
        &normalBuffer);

    // *************************************************************************
    // Bottom level acceleration structure
    // *************************************************************************
    std::vector<MetalAS> blasBuffer;
    CreateBLAS(
        renderer.get(),
        indexCount,
        &indexBuffer,
        vertexCount,
        &positionBuffer,
        blasBuffer);

    // *************************************************************************
    // Top level acceleration structure
    // *************************************************************************
    MetalAS tlasBuffer;
    CreateTLAS(renderer.get(), blasBuffer, &tlasBuffer);

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
    if (!InitSwapchain(renderer.get(), window->GetNativeWindow(), window->GetWidth(), window->GetHeight(), 2, MTL::PixelFormatDepth32Float))
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
        pComputeEncoder->setBuffer(indexBuffer.Buffer.get(), 0, 2);
        pComputeEncoder->setBuffer(normalBuffer.Buffer.get(), 0, 3);
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
        };

        Camera camera      = {};
        
        camera.projInverse = glm::inverse(glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 512.0f));
        camera.viewInverse = glm::inverse(glm::translate(glm::mat4(1), glm::vec3(0.0f, 0.0f, -2.5f)));
        
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

void CreateBLAS(
    MetalRenderer*        pRenderer,
    uint32_t              indexCount,
    MetalBuffer*          pIndexBuffer,
    uint32_t              vertexCount,
    MetalBuffer*          pPositionBuffer,
    std::vector<MetalAS>& BLAS)
{
    NS::AutoreleasePool* pPoolAllocator = NS::AutoreleasePool::alloc()->init();

    MTL::AccelerationStructureTriangleGeometryDescriptor* triGeoDesc = MTL::AccelerationStructureTriangleGeometryDescriptor::alloc()->init();
    
    triGeoDesc->setIndexType(MTL::IndexTypeUInt32);
    triGeoDesc->setIndexBuffer(pIndexBuffer->Buffer.get());
    
    triGeoDesc->setVertexBuffer(pPositionBuffer->Buffer.get());
    triGeoDesc->setVertexFormat(MTL::AttributeFormatFloat3);
    triGeoDesc->setVertexStride(12);
    triGeoDesc->setTriangleCount(indexCount/3);

    auto asDesc = NS::TransferPtr(MTL::PrimitiveAccelerationStructureDescriptor::alloc()->init());
    auto triGeoDescArray = NS::TransferPtr(NS::Array::array((NS::Object**)&triGeoDesc, 1));
    asDesc->setGeometryDescriptors(triGeoDescArray.get());

    MetalAS accelStructure;
    CHECK_CALL(CreateAccelerationStructure(pRenderer, asDesc.get(), &accelStructure));

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

void CreateGeometry(
    MetalRenderer* pRenderer,
    uint32_t*      pIndexCount,
    MetalBuffer*   pIndexBuffer,
    uint32_t*      pVertexCount,
    MetalBuffer*   pPositionBuffer,
    MetalBuffer*   pNormalBuffer)
{
    TriMesh::Options options;
    options.enableNormals   = true;
 
    TriMesh mesh = TriMesh::Sphere(1.0f, 16, 8, options);

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetTriangles()),
        DataPtr(mesh.GetTriangles()),
        pIndexBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetPositions()),
        DataPtr(mesh.GetPositions()),
        pPositionBuffer));

    CHECK_CALL(CreateBuffer(
        pRenderer,
        SizeInBytes(mesh.GetNormals()),
        DataPtr(mesh.GetNormals()),
        pNormalBuffer));

    *pIndexCount  = 3 * mesh.GetNumTriangles();
    *pVertexCount = mesh.GetNumVertices();
}
