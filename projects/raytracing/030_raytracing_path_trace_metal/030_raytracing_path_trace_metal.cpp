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
// Macros, enums, and constants
// =============================================================================
//const uint32_t kOutputResourcesOffset = 0;
//const uint32_t kGeoBuffersOffset      = 20;
//const uint32_t kIBLTextureOffset      = 3;

const uint32_t kGeometryArgBufferParamIndex = 6;

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 720;
static bool     gEnableDebug  = true;

static float gTargetAngle = 0.0f;
static float gAngle       = 0.0f;

static bool     gResetRayGenSamples = true;
static uint32_t gMaxSamples         = 5120;
static uint32_t gCurrentMaxSamples  = 0;

struct Light
{
    vec3  Position;
    float pad0;
    vec3  Color;
    float Intensity;
};

struct SceneParameters
{
    mat4  ViewInverseMatrix;
    mat4  ProjectionInverseMatrix;
    mat4  ViewProjectionMatrix;
    vec3  EyePosition;
    uint  MaxSamples;
    uint  NumLights;
    uint  _pad0[3];
    Light Lights[8];
};

struct Geometry
{
    uint32_t    indexCount;
    MetalBuffer indexBuffer;
    uint32_t    vertexCount;
    MetalBuffer positionBuffer;
    MetalBuffer normalBuffer;
};

struct IBLTextures
{
    MetalTexture irrTexture;
    MetalTexture envTexture;
    uint32_t     envNumLevels;
};

struct MaterialParameters
{
    vec3  baseColor;
    float roughness;
    float metallic;
    float specularReflectance;
    float ior;
    uint  _pad0;
};

// =============================================================================
// Forward declarations
// =============================================================================
void CreateGeometries(
    MetalRenderer* pRenderer,
    Geometry&      outSphereGeometry,
    Geometry&      outBoxGeometry);

void CreateBLASes(
    MetalRenderer*   pRenderer,
    const Geometry&  sphereGeometry,
    const Geometry&  boxGeometry,
    MetalAS*         pSphereBLAS,
    MetalAS*         pBoxBLAS);

void CreateTLAS(
    MetalRenderer*                   pRenderer,
    const MetalAS*                   pSphereBLAS,
    const MetalAS*                   pBoxBLAS,
    MetalAS*                         pTLAS,
    MetalBuffer*                     pInstanceBuffer,
    std::vector<MaterialParameters>& outMaterialParams);
    
void CreateIBLTextures(
    MetalRenderer* pRenderer,
    IBLTextures&   outIBLTextures);

// =============================================================================
// Input functions
// =============================================================================
void MouseMove(int x, int y, int buttons)
{
    static int prevX = x;
    static int prevY = y;

    if (buttons & MOUSE_BUTTON_LEFT)
    {
        int dx = x - prevX;
        int dy = y - prevY;

        gTargetAngle += 0.25f * dx;

        gResetRayGenSamples = true;
    }

    prevX = x;
    prevY = y;
}

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
    auto source = LoadString("projects/030_raytracing_path_trace/shaders.metal");
    assert((!source.empty()) && "no shader source!");
    
    auto compileOptions = NS::TransferPtr(MTL::CompileOptions::alloc()->init());
    compileOptions->setLanguageVersion(MTL::LanguageVersion3_1);
    compileOptions->setFastMathEnabled(false);
    compileOptions->setOptimizationLevel(MTL::LibraryOptimizationLevelDefault);
    
    NS::Error* pError  = nullptr;
    //
    auto library = NS::TransferPtr(renderer->Device->newLibrary(
        NS::String::string(source.c_str(), NS::UTF8StringEncoding),
        compileOptions.get(),
        &pError));

    if (library.get() == nullptr)
    {
        std::stringstream ss;
        ss << "\n"
           << "Shader compiler error: " << pError->localizedDescription()->utf8String() << "\n";
        GREX_LOG_ERROR(ss.str().c_str());
        assert(false);
        return EXIT_FAILURE;
    }

    MetalShader rayTraceShader;
    rayTraceShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("MyRayGen", NS::UTF8StringEncoding)));
    if (rayTraceShader.Function.get() == nullptr)
    {
        assert(false && "Shader MTL::Library::newFunction() failed for raygen");
        return EXIT_FAILURE;
    }

    MetalShader clearShader;
    clearShader.Function = NS::TransferPtr(library->newFunction(NS::String::string("Clear", NS::UTF8StringEncoding)));
    if (clearShader.Function.get() == nullptr)
    {
        assert(false && "Shader MTL::Library::newFunction() failed for clear shader");
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
        auto pipelineDesc = NS::TransferPtr(MTL::ComputePipelineDescriptor::alloc()->init());
        pipelineDesc->setComputeFunction(rayTraceShader.Function.get());
        pipelineDesc->setMaxCallStackDepth(5);

        NS::Error* error;
        rayTracePipeline = NS::TransferPtr(renderer->Device->newComputePipelineState(pipelineDesc.get(), 0, nullptr, &error));
    }

    // *************************************************************************
    // Clear pipeline
    // *************************************************************************
    NS::SharedPtr<MTL::ComputePipelineState> clearPipeline;
    {
        auto pipelineDesc = NS::TransferPtr(MTL::ComputePipelineDescriptor::alloc()->init());
        pipelineDesc->setComputeFunction(clearShader.Function.get());

        NS::Error* error;
        clearPipeline = NS::TransferPtr(renderer->Device->newComputePipelineState(pipelineDesc.get(), 0, nullptr, &error));
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
    Geometry sphereGeometry;
    Geometry boxGeometry;
    CreateGeometries(
        renderer.get(),
        sphereGeometry,
        boxGeometry);        

    // *************************************************************************
    // Geometry argument buffer
    // *************************************************************************
    NS::SharedPtr<MTL::Buffer> geometryArgBuffer;
    {
        auto argEncoder = NS::TransferPtr(rayTraceShader.Function->newArgumentEncoder(kGeometryArgBufferParamIndex));
        
        geometryArgBuffer = NS::TransferPtr(renderer->Device->newBuffer(argEncoder->encodedLength(), MTL::ResourceStorageModeManaged));
        argEncoder->setArgumentBuffer(geometryArgBuffer.get(), 0);
        
        for (uint i = 0; i < 4; ++i) {
            argEncoder->setBuffer(sphereGeometry.indexBuffer.Buffer.get(),    0, 0 + i);
            argEncoder->setBuffer(sphereGeometry.positionBuffer.Buffer.get(), 0, 5 + i);
            argEncoder->setBuffer(sphereGeometry.normalBuffer.Buffer.get(),   0, 10 + i);
        }
        argEncoder->setBuffer(boxGeometry.indexBuffer.Buffer.get(),    0, 0 + 4);
        argEncoder->setBuffer(boxGeometry.positionBuffer.Buffer.get(), 0, 5 + 4);
        argEncoder->setBuffer(boxGeometry.normalBuffer.Buffer.get(),   0, 10 + 4);
        
        geometryArgBuffer->didModifyRange(NS::Range::Make(0, geometryArgBuffer->length()));
    }

    // *************************************************************************
    // Bottom level acceleration structure
    // *************************************************************************
    MetalAS sphereBLAS;
    MetalAS boxBLAS;
    CreateBLASes(renderer.get(), sphereGeometry, boxGeometry, &sphereBLAS, &boxBLAS);

    // *************************************************************************
    // Top level acceleration structure
    // *************************************************************************
    MetalAS                         TLAS;
    MetalBuffer                     instanceBuffer;
    std::vector<MaterialParameters> materialParams;
    CreateTLAS(renderer.get(), &sphereBLAS, &boxBLAS, &TLAS, &instanceBuffer, materialParams);

    // *************************************************************************
    // Material params buffer
    // *************************************************************************
    MetalBuffer materialParamsBuffer;
    CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(materialParams), DataPtr(materialParams), &materialParamsBuffer));
    materialParamsBuffer.Buffer->setLabel(NS::String::string("Material Params Buffer", NS::UTF8StringEncoding));

    // *************************************************************************
    // Ray trace ouput texture
    // *************************************************************************
    MetalTexture outputTexture;
    MetalTexture accumTexture;
    {
        CHECK_CALL(CreateRWTexture(renderer.get(), gWindowWidth, gWindowHeight, MTL::PixelFormatRGBA8Unorm, &outputTexture));
        CHECK_CALL(CreateRWTexture(renderer.get(), gWindowWidth, gWindowHeight, MTL::PixelFormatRGBA32Float, &accumTexture));
    }
    
    // *************************************************************************
    // Ray gen samples buffer
    // *************************************************************************s
    MetalBuffer rayGenSamplesBuffer;
    CHECK_CALL(CreateBuffer(renderer.get(), (gWindowWidth * gWindowHeight * sizeof(uint32_t)), nullptr, &rayGenSamplesBuffer));

    // *************************************************************************
    // IBL txtures
    // *************************************************************************
    IBLTextures iblTextures = {};
    CreateIBLTextures(
        renderer.get(),
        iblTextures);

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
    window->AddMouseMoveCallbacks(MouseMove);

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetNativeWindow(), window->GetWidth(), window->GetHeight(), 2, MTL::PixelFormatDepth32Float))
    {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Imgui
    // *************************************************************************
    if (!window->InitImGuiForMetal(renderer.get())) {
        assert(false && "GrexWindow::InitImGuiForMetal failed");
        return EXIT_FAILURE;
    }
    
    // *************************************************************************
    // Scene parameters
    // *************************************************************************
    //SceneParameters sceneParams = {};
    MetalBuffer sceneParamsBuffer;
    CHECK_CALL(CreateBuffer(renderer.get(), sizeof(SceneParameters), nullptr, MTL::ResourceStorageModeShared, &sceneParamsBuffer));
    SceneParameters* pSceneParams = (SceneParameters*)sceneParamsBuffer.Buffer->contents();
    
    // *************************************************************************
    // Misc vars
    // *************************************************************************
    uint32_t sampleCount     = 0;
    float    rayGenStartTime = 0;    
    
    // *************************************************************************
    // Main loop
    // *************************************************************************
    uint32_t frameIndex = 0;

    while (window->PollEvents())
    {
        window->ImGuiNewFrameMetal(pRenderPassDescriptor);

        if (ImGui::Begin("Scene")) {
            ImGui::SliderInt("Max Samples Per Pixel", reinterpret_cast<int*>(&gMaxSamples), 1, 16384);

            ImGui::Separator();

            float progress = sampleCount / static_cast<float>(gMaxSamples);
            char  buf[256] = {};
            sprintf(buf, "%d/%d Samples", sampleCount, gMaxSamples);
            ImGui::ProgressBar(progress, ImVec2(-1, 0), buf);

            ImGui::Separator();

            static float elapsedTime = 0;
            if (sampleCount < gMaxSamples) {
                float currentTime = static_cast<float>(glfwGetTime());
                elapsedTime       = currentTime - rayGenStartTime;
            }

            ImGui::Text("Render time: %0.3f seconds", elapsedTime);
        }
        ImGui::End();
        
        // ---------------------------------------------------------------------

        if (gCurrentMaxSamples != gMaxSamples)
        {
            gCurrentMaxSamples  = gMaxSamples;
            gResetRayGenSamples = true;
        }

        // Smooth out the rotation on Y
        gAngle += (gTargetAngle - gAngle) * 0.25f;
        // Keep resetting until the angle is somewhat stable
        if (fabs(gTargetAngle - gAngle) > 0.1f)
        {
            gResetRayGenSamples = true;
        }

        // Camera matrices
        mat4 transformEyeMat     = glm::rotate(glm::radians(-gAngle), vec3(0, 1, 0));
        vec3 startingEyePosition = vec3(0, 4.0f, 8.5f);
        vec3 eyePosition         = transformEyeMat * vec4(startingEyePosition, 1);
        mat4 viewMat             = glm::lookAt(eyePosition, vec3(0, 3, 0), vec3(0, 1, 0));
        mat4 projMat             = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);

        // Set constant buffer values
        pSceneParams->ViewInverseMatrix       = glm::inverse(viewMat);
        pSceneParams->ProjectionInverseMatrix = glm::inverse(projMat);
        pSceneParams->EyePosition             = eyePosition;
        pSceneParams->MaxSamples              = gCurrentMaxSamples;

        // ---------------------------------------------------------------------
    
        CA::MetalDrawable* pDrawable = renderer->pSwapchain->nextDrawable();
        assert(pDrawable != nullptr);

        auto commandBufferDescriptor = NS::TransferPtr(MTL::CommandBufferDescriptor::alloc()->init());
        commandBufferDescriptor->setErrorOptions(MTL::CommandBufferErrorOptionEncoderExecutionStatus);
        
        MTL::CommandBuffer* pCommandBuffer = renderer->Queue->commandBuffer(commandBufferDescriptor.get());

        // Reset ray gen samples
        if (gResetRayGenSamples)
        {
            sampleCount     = 0;
            rayGenStartTime = static_cast<float>(glfwGetTime());
            
            MTL::ComputeCommandEncoder* pComputeEncoder = pCommandBuffer->computeCommandEncoder();

            pComputeEncoder->setComputePipelineState(clearPipeline.get());
            pComputeEncoder->setTexture(accumTexture.Texture.get(), 0);
            pComputeEncoder->setBuffer(rayGenSamplesBuffer.Buffer.get(), 0, 0);
            
            MTL::Size threadsPerThreadgroup = {8, 8, 1};
            MTL::Size threadsPerGrid        = {
                (gWindowWidth + threadsPerThreadgroup.width - 1) / threadsPerThreadgroup.width,
                (gWindowHeight + threadsPerThreadgroup.height - 1) / threadsPerThreadgroup.height,
                1};

            pComputeEncoder->useResource(accumTexture.Texture.get(), MTL::ResourceUsageWrite);
            pComputeEncoder->useResource(rayGenSamplesBuffer.Buffer.get(), MTL::ResourceUsageWrite);
            pComputeEncoder->dispatchThreadgroups(threadsPerGrid, threadsPerThreadgroup);
            
            pComputeEncoder->endEncoding();
            
            gResetRayGenSamples = false;
        }

        // Ray trace
        {
            MTL::ComputeCommandEncoder* pComputeEncoder = pCommandBuffer->computeCommandEncoder();
            
            pComputeEncoder->setComputePipelineState(rayTracePipeline.get());
            pComputeEncoder->setAccelerationStructure(TLAS.AS.get(), 0);
            pComputeEncoder->setBuffer(instanceBuffer.Buffer.get(), 0, 1);
            pComputeEncoder->setBuffer(sceneParamsBuffer.Buffer.get(), 0, 2);
            pComputeEncoder->setBuffer(geometryArgBuffer.get(), 0, kGeometryArgBufferParamIndex);
            pComputeEncoder->setBuffer(materialParamsBuffer.Buffer.get(), 0, 4);
            pComputeEncoder->setBuffer(rayGenSamplesBuffer.Buffer.get(), 0, 5);
            pComputeEncoder->setTexture(iblTextures.envTexture.Texture.get(), 3);
            pComputeEncoder->setTexture(outputTexture.Texture.get(), 0);
            pComputeEncoder->setTexture(accumTexture.Texture.get(), 1);
            
            pComputeEncoder->useResource(materialParamsBuffer.Buffer.get(), MTL::ResourceUsageRead);
            pComputeEncoder->useResource(sphereGeometry.indexBuffer.Buffer.get(), MTL::ResourceUsageRead);
            pComputeEncoder->useResource(sphereGeometry.normalBuffer.Buffer.get(), MTL::ResourceUsageRead);
            pComputeEncoder->useResource(boxGeometry.indexBuffer.Buffer.get(), MTL::ResourceUsageRead);
            pComputeEncoder->useResource(boxGeometry.normalBuffer.Buffer.get(), MTL::ResourceUsageRead);
            
            pComputeEncoder->useResource(accumTexture.Texture.get(), MTL::ResourceUsageRead);
            pComputeEncoder->useResource(rayGenSamplesBuffer.Buffer.get(), MTL::ResourceUsageRead);
            
            // Add a useResource() call for every BLAS used by the TLAS
            pComputeEncoder->useResource(sphereBLAS.AS.get(), MTL::ResourceUsageRead);
            pComputeEncoder->useResource(boxBLAS.AS.get(), MTL::ResourceUsageRead);

            // Dispatch
            {
                MTL::Size threadsPerThreadgroup = {8, 8, 1};
                MTL::Size threadsPerGrid        = {
                    (gWindowWidth + threadsPerThreadgroup.width - 1) / threadsPerThreadgroup.width,
                    (gWindowHeight + threadsPerThreadgroup.height - 1) / threadsPerThreadgroup.height,
                    1};

                pComputeEncoder->dispatchThreadgroups(threadsPerGrid, threadsPerThreadgroup);
            }
            pComputeEncoder->endEncoding();
        }
        
        // Copy to swapchain image
        {
            auto colorTargetDesc = NS::TransferPtr(MTL::RenderPassColorAttachmentDescriptor::alloc()->init());
            colorTargetDesc->setTexture(pDrawable->texture());
            colorTargetDesc->setLoadAction(MTL::LoadActionLoad);
            colorTargetDesc->setStoreAction(MTL::StoreActionStore);
            pRenderPassDescriptor->colorAttachments()->setObject(colorTargetDesc.get(), 0);

            MTL::RenderCommandEncoder* pRenderEncoder = pCommandBuffer->renderCommandEncoder(pRenderPassDescriptor);
            pRenderEncoder->setRenderPipelineState(copyPipeline.get());
            pRenderEncoder->setFragmentTexture(outputTexture.Texture.get(), 0);
            
            pRenderEncoder->useResource(outputTexture.Texture.get(), MTL::ResourceUsageRead);
            
            pRenderEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::Integer(0), 6);
        
            // Draw ImGui
            window->ImGuiRenderDrawData(renderer.get(), pCommandBuffer, pRenderEncoder);
            
            pRenderEncoder->endEncoding();
        }

        pCommandBuffer->presentDrawable(pDrawable);
        pCommandBuffer->commit();
        pCommandBuffer->waitUntilCompleted();
        
        // Update sample count
        if (sampleCount < gMaxSamples) {
            ++sampleCount;
        }        
    }

    return 0;
}

void CreateGeometries(
    MetalRenderer* pRenderer,
    Geometry&      outSphereGeometry,
    Geometry&      outBoxGeometryy)
{
    TriMesh::Options triMeshOptions = {};
    triMeshOptions.enableNormals    = true;

    // Geometry
    {
        // Sphere
        TriMesh mesh = TriMesh::Sphere(1.0f, 32, 32, triMeshOptions);

        Geometry& geo = outSphereGeometry;

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            &geo.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            &geo.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            &geo.normalBuffer));

        geo.indexCount  = 3 * mesh.GetNumTriangles();
        geo.vertexCount = mesh.GetNumVertices();
        
        geo.indexBuffer.Buffer->setLabel(NS::String::string("Sphere Index Buffer", NS::UTF8StringEncoding));
        geo.positionBuffer.Buffer->setLabel(NS::String::string("Sphere Position Buffer", NS::UTF8StringEncoding));
        geo.normalBuffer.Buffer->setLabel(NS::String::string("Sphere Normal Buffer", NS::UTF8StringEncoding));
    }

    // Box
    {
        TriMesh   mesh = TriMesh::Cube(glm::vec3(15, 1, 4.5f), false, triMeshOptions);
        Geometry& geo  = outBoxGeometryy;

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetTriangles()),
            DataPtr(mesh.GetTriangles()),
            &geo.indexBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetPositions()),
            DataPtr(mesh.GetPositions()),
            &geo.positionBuffer));

        CHECK_CALL(CreateBuffer(
            pRenderer,
            SizeInBytes(mesh.GetNormals()),
            DataPtr(mesh.GetNormals()),
            &geo.normalBuffer));

        geo.indexCount  = 3 * mesh.GetNumTriangles();
        geo.vertexCount = mesh.GetNumVertices();

        geo.indexBuffer.Buffer->setLabel(NS::String::string("Box Index Buffer", NS::UTF8StringEncoding));
        geo.positionBuffer.Buffer->setLabel(NS::String::string("Box Position Buffer", NS::UTF8StringEncoding));
        geo.normalBuffer.Buffer->setLabel(NS::String::string("Box Normal Buffer", NS::UTF8StringEncoding));
    }
}

void CreateBLASes(
    MetalRenderer*   pRenderer,
    const Geometry&  sphereGeometry,
    const Geometry&  boxGeometry,
    MetalAS*         pSphereBLAS,
    MetalAS*         pBoxBLAS)
{
    std::vector<const Geometry*>  geometries = {&sphereGeometry, &boxGeometry};
    std::vector<MetalAS*>         BLASes     = {pSphereBLAS, pBoxBLAS};
    
    for (uint32_t i = 0; i < 2; ++i) {
        auto pGeometry = geometries[i];
        auto pBLAS     = BLASes[i];
        
        // Fill out geometry descriptor
        auto geometryDesc = NS::TransferPtr(MTL::AccelerationStructureTriangleGeometryDescriptor::alloc()->init());
        geometryDesc->setIndexType(MTL::IndexTypeUInt32);
        geometryDesc->setIndexBuffer(pGeometry->indexBuffer.Buffer.get());
        geometryDesc->setVertexBuffer(pGeometry->positionBuffer.Buffer.get());
        geometryDesc->setVertexFormat(MTL::AttributeFormatFloat3);
        geometryDesc->setVertexStride(12);
        geometryDesc->setTriangleCount(pGeometry->indexCount / 3);
        
        // Add geometry descriptor to a descriptor array
        auto pGeometryDesc = geometryDesc.get();
        auto pDescriptors = NS::Array::array((NS::Object**)&pGeometryDesc, 1);
        
        // Fill out acceleration structure descriptor with geometry descriptor array
        auto accelStructDescriptor = NS::TransferPtr(MTL::PrimitiveAccelerationStructureDescriptor::alloc()->init());
        accelStructDescriptor->setGeometryDescriptors(pDescriptors);
        
        // Calculate sizes for acceleration structure building
        auto accelSizes = pRenderer->Device->accelerationStructureSizes(accelStructDescriptor.get());
        
        // Scratch buffer
        auto scratchBuffer = NS::TransferPtr(pRenderer->Device->newBuffer(accelSizes.buildScratchBufferSize, MTL::ResourceStorageModePrivate));
        
        // Acceleration structure storage
        auto accelStruct = NS::TransferPtr(pRenderer->Device->newAccelerationStructure(accelSizes.accelerationStructureSize));
        
        // Buffer for Metal to write teh compacted accelerature strcuture's size
        auto compactedSizeBuffer = NS::TransferPtr(pRenderer->Device->newBuffer(sizeof(uint32_t), MTL::ResourceStorageModeShared));
                       
        // Build acceleration structure
        auto pCommandBuffer = pRenderer->Queue->commandBuffer();
        auto pEncoder = pCommandBuffer->accelerationStructureCommandEncoder();
        pEncoder->buildAccelerationStructure(accelStruct.get(), accelStructDescriptor.get(), scratchBuffer.get(), 0);
        pEncoder->writeCompactedAccelerationStructureSize(accelStruct.get(), compactedSizeBuffer.get(), 0);
        pEncoder->endEncoding();
        pCommandBuffer->commit();
        pCommandBuffer->waitUntilCompleted();
        
        // Compacted acceleration structure storage
        const uint32_t compactedSize = *((uint32_t*)compactedSizeBuffer->contents());
        auto compactedAccelStruct = NS::TransferPtr(pRenderer->Device->newAccelerationStructure(compactedSize));
        
        // Compact acceleration structure
        pCommandBuffer = pRenderer->Queue->commandBuffer();
        pEncoder = pCommandBuffer->accelerationStructureCommandEncoder();
        pEncoder->copyAndCompactAccelerationStructure(accelStruct.get(), compactedAccelStruct.get());
        pEncoder->endEncoding();
        pCommandBuffer->commit();
        pCommandBuffer->waitUntilCompleted();
        
        // Store compacted acceleration structure
        pBLAS->AS = compactedAccelStruct;
    }
}

void CreateTLAS(
    MetalRenderer*                   pRenderer,
    const MetalAS*                   pSphereBLAS,
    const MetalAS*                   pBoxBLAS,
    MetalAS*                         pTLAS,
    MetalBuffer*                     pInstanceBuffer,
    std::vector<MaterialParameters>& outMaterialParams)
{
    // clang-format off
    std::vector<glm::mat3x4> transforms = {
        // Rough plastic sphere
        {{1.0f, 0.0f, 0.0f, -3.75f},
         {0.0f, 1.0f, 0.0f,  2.0f},
         {0.0f, 0.0f, 1.0f,  0.0f}},
        /*
        {{1.0f, 0.0f, 0.0f,  0.0f},
         {0.0f, 1.0f, 0.0f,  0.0f},
         {0.0f, 0.0f, 1.0f,  0.0f}},
         */
        // Shiny plastic sphere
        {{1.0f, 0.0f, 0.0f, -1.25f},
         {0.0f, 1.0f, 0.0f,  2.0f},
         {0.0f, 0.0f, 1.0f,  0.0f}},
        // Glass sphere
        {{1.0f, 0.0f, 0.0f,  1.25f},
         {0.0f, 1.0f, 0.0f,  2.0f},
         {0.0f, 0.0f, 1.0f,  0.0f}},
        // Gold sphere
        {{1.0f, 0.0f, 0.0f,  3.75f},
         {0.0f, 1.0f, 0.0f,  2.0f},
         {0.0f, 0.0f, 1.0f,  0.0f}},
        // Box
        {{1.0f, 0.0f, 0.0f,  0.0f},
         {0.0f, 1.0f, 0.0f,  0.5f},
         {0.0f, 0.0f, 1.0f,  0.0f}},
    };
    // clang-format on
    
    // Material params
    {
        // Rough plastic
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(1, 1, 1);
            materialParams.roughness           = 1.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // Shiny plastic
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(1, 1, 1);
            materialParams.roughness           = 0;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.5f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // Glass
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(1, 1, 1);
            materialParams.roughness           = 0;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 1.50f;

            outMaterialParams.push_back(materialParams);
        }

        // Gold with a bit of roughness
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = F0_MetalGold;
            materialParams.roughness           = 0.30f;
            materialParams.metallic            = 1;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // Box
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(0.6f, 0.7f, 0.75f);
            materialParams.roughness           = 1.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }
    }    
    
    // Allocate buffer for instance descriptors
    const uint32_t bufferSize = static_cast<uint32_t>(transforms.size() * sizeof(MTL::AccelerationStructureInstanceDescriptor));
    CHECK_CALL(CreateBuffer(pRenderer, bufferSize, nullptr, MTL::ResourceStorageModeShared, pInstanceBuffer));
    
    // Cast buffer pointer to instance descriptors
    auto pDescriptors = (MTL::AccelerationStructureInstanceDescriptor*)pInstanceBuffer->Buffer->contents();
    
    // Instance descriptors
    std::vector<const MTL::AccelerationStructure*> BLASes;
    {
        auto CopyTransposed = [](const glm::mat3x4& src, MTL::PackedFloat4x3& dst) {
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 3; ++row)
                {
                   dst.columns[column][row] = src[row][column];
                }
            }
        };
    
        // Zero out everything before we begin setting values
        memset(pDescriptors, 0, bufferSize);
     
        uint32_t transformIdx = 0;
           
        // Rough plastic sphere
        CopyTransposed(transforms[transformIdx], pDescriptors[transformIdx].transformationMatrix);
        pDescriptors[transformIdx].mask                       = 1;
        pDescriptors[transformIdx].accelerationStructureIndex = transformIdx;
        BLASes.push_back(pSphereBLAS->AS.get());
        ++transformIdx;
        
        // Shiny plastic sphere
        CopyTransposed(transforms[transformIdx], pDescriptors[transformIdx].transformationMatrix);
        pDescriptors[transformIdx].mask                       = 1;
        pDescriptors[transformIdx].accelerationStructureIndex = transformIdx;
        BLASes.push_back(pSphereBLAS->AS.get());
        ++transformIdx;

        // Glass sphere
        CopyTransposed(transforms[transformIdx], pDescriptors[transformIdx].transformationMatrix);
        pDescriptors[transformIdx].options                    = MTL::AccelerationStructureInstanceOptionNonOpaque;
        pDescriptors[transformIdx].mask                       = 1;
        pDescriptors[transformIdx].accelerationStructureIndex = transformIdx;
        BLASes.push_back(pSphereBLAS->AS.get());
        ++transformIdx;

        // Gold sphere
        CopyTransposed(transforms[transformIdx], pDescriptors[transformIdx].transformationMatrix);
        pDescriptors[transformIdx].mask                       = 1;
        pDescriptors[transformIdx].accelerationStructureIndex = transformIdx;
        BLASes.push_back(pSphereBLAS->AS.get());
        ++transformIdx;

        // Box
        CopyTransposed(transforms[transformIdx], pDescriptors[transformIdx].transformationMatrix);
        pDescriptors[transformIdx].mask                       = 1;
        pDescriptors[transformIdx].accelerationStructureIndex = transformIdx;
        BLASes.push_back(pBoxBLAS->AS.get());
        ++transformIdx;
    }
    
    // Add BLASes to instanced acceleration structure array
    NS::Array* pInstancedAccelStructs = NS::Array::array((const NS::Object* const*)DataPtr(BLASes), CountU32(BLASes));
    
    // Fill out acceleration structure descriptor
    auto accelStructDescriptor = NS::TransferPtr(MTL::InstanceAccelerationStructureDescriptor::alloc()->init());
    accelStructDescriptor->setInstancedAccelerationStructures(pInstancedAccelStructs);
    accelStructDescriptor->setInstanceCount(CountU32(BLASes));
    accelStructDescriptor->setInstanceDescriptorBuffer(pInstanceBuffer->Buffer.get());
    
    // Calculate sizes for acceleration structure building
    auto accelSizes = pRenderer->Device->accelerationStructureSizes(accelStructDescriptor.get());
    
    // Scratch buffer
    auto scratchBuffer = NS::TransferPtr(pRenderer->Device->newBuffer(accelSizes.buildScratchBufferSize, MTL::ResourceStorageModePrivate));
    
    // Acceleration structure storage
    auto accelStruct = NS::TransferPtr(pRenderer->Device->newAccelerationStructure(accelSizes.accelerationStructureSize));
    
    // Buffer for Metal to write teh compacted accelerature strcuture's size
    auto compactedSizeBuffer = NS::TransferPtr(pRenderer->Device->newBuffer(sizeof(uint32_t), MTL::ResourceStorageModeShared));
                   
    // Build acceleration structure
    auto pCommandBuffer = pRenderer->Queue->commandBuffer();
    auto pEncoder = pCommandBuffer->accelerationStructureCommandEncoder();
    pEncoder->buildAccelerationStructure(accelStruct.get(), accelStructDescriptor.get(), scratchBuffer.get(), 0);
    pEncoder->writeCompactedAccelerationStructureSize(accelStruct.get(), compactedSizeBuffer.get(), 0);
    pEncoder->endEncoding();
    pCommandBuffer->commit();
    pCommandBuffer->waitUntilCompleted();
    
    // Compacted acceleration structure storage
    const uint32_t compactedSize = *((uint32_t*)compactedSizeBuffer->contents());
    auto compactedAccelStruct = NS::TransferPtr(pRenderer->Device->newAccelerationStructure(compactedSize));
    
    // Compact acceleration structure
    pCommandBuffer = pRenderer->Queue->commandBuffer();
    pEncoder = pCommandBuffer->accelerationStructureCommandEncoder();
    pEncoder->copyAndCompactAccelerationStructure(accelStruct.get(), compactedAccelStruct.get());
    pEncoder->endEncoding();
    pCommandBuffer->commit();
    pCommandBuffer->waitUntilCompleted();
    
    pTLAS->AS = compactedAccelStruct;
}
    
void CreateIBLTextures(
    MetalRenderer* pRenderer,
    IBLTextures&   outIBLTextures)
{
    // IBL file
    auto iblFile = GetAssetPath("IBL/old_depot_4k.ibl");

    IBLMaps ibl = {};
    if (!LoadIBLMaps32f(iblFile, &ibl))
    {
        GREX_LOG_ERROR("failed to load: " << iblFile);
        return;
    }

    outIBLTextures.envNumLevels = ibl.numLevels;

    // Irradiance
    {
        CHECK_CALL(CreateTexture(
            pRenderer,
            ibl.irradianceMap.GetWidth(),
            ibl.irradianceMap.GetHeight(),
            MTL::PixelFormatRGBA32Float,
            ibl.irradianceMap.GetSizeInBytes(),
            ibl.irradianceMap.GetPixels(),
            &outIBLTextures.irrTexture));
    }

    // Environment
    {
        const uint32_t pixelStride = ibl.environmentMap.GetPixelStride();
        const uint32_t rowStride   = ibl.environmentMap.GetRowStride();

        std::vector<MipOffset> mipOffsets;
        uint32_t               levelOffset = 0;
        uint32_t               levelWidth  = ibl.baseWidth;
        uint32_t               levelHeight = ibl.baseHeight;
        for (uint32_t i = 0; i < ibl.numLevels; ++i)
        {
            MipOffset mipOffset = {};
            mipOffset.Offset    = levelOffset;
            mipOffset.RowStride = rowStride;

            mipOffsets.push_back(mipOffset);

            levelOffset += (rowStride * levelHeight);
            levelWidth >>= 1;
            levelHeight >>= 1;
        }

        CHECK_CALL(CreateTexture(
            pRenderer,
            ibl.baseWidth,
            ibl.baseHeight,
            MTL::PixelFormatRGBA32Float,
            mipOffsets,
            ibl.environmentMap.GetSizeInBytes(),
            ibl.environmentMap.GetPixels(),
            &outIBLTextures.envTexture));
    }

    GREX_LOG_INFO("Loaded " << iblFile);
}
