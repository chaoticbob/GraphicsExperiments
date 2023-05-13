#include "window.h"

#include "vk_renderer.h"
#include "tri_mesh.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#define CHECK_CALL(FN)                               \
    {                                                \
        HRESULT hr = FN;                             \
        if (FAILED(hr)) {                            \
            std::stringstream ss;                    \
            ss << "\n";                              \
            ss << "*** FUNCTION CALL FAILED *** \n"; \
            ss << "FUNCTION: " << #FN << "\n";       \
            ss << "\n";                              \
            GREX_LOG_ERROR(ss.str().c_str());        \
            assert(false);                           \
        }                                            \
    }

// =============================================================================
// Macros, enums, and constants
// =============================================================================
const uint32_t kOutputResourcesOffset = 0;
const uint32_t kGeoBuffersOffset      = 20;
const uint32_t kIBLTextureOffset      = 3;

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1920;
static uint32_t gWindowHeight = 1080;
static bool     gEnableDebug  = true;
static bool     gEnableRayTracing = true;

static const char * gHitGroupName         = "MyHitGroup";
static const char * gRayGenShaderName     = "MyRaygenShader";
static const char * gMissShaderName       = "MyMissShader";
static const char * gClosestHitShaderName = "MyClosestHitShader";

static float gTargetAngle = 0.0f;
static float gAngle       = 0.0f;

struct Light
{
    vec3  Position;
    vec3  Color;
    float Intensity;
};

struct SceneParameters
{
    mat4  ViewInverseMatrix;
    mat4  ProjectionInverseMatrix;
    mat4  ViewProjectionMatrix;
    vec3  EyePosition;
    uint  NumLights;
    Light Lights[8];
};

struct Geometry
{
    uint32_t     indexCount;
    VulkanBuffer indexBuffer;
    uint32_t     vertexCount;
    VulkanBuffer positionBuffer;
    VulkanBuffer normalBuffer;
};

struct IBLTextures
{
    VulkanImage irrTexture;
    VulkanImage envTexture;
    uint32_t    envNumLevels;
};

struct MaterialParameters
{
    vec3  baseColor;
    float roughness;
    vec3  absorbColor;
};

void CreateRayTracePipelineLayout(
   VulkanRenderer*         pRenderer,
   VulkanPipelineLayout*   rayTracePipelineLayout);
void CreateRayTracingPipeline(
   VulkanRenderer*               pRenderer,
   VkShaderModule                rayTraceModule,
   const VulkanPipelineLayout&   pipelineLayout,
   VkPipeline*                   pPipeline);
void CreateShaderBindingTables(
   VulkanRenderer*                                          pRenderer,
   const VkPhysicalDeviceRayTracingPipelinePropertiesKHR&   rayTracingProperties,
   VkPipeline                                               pipeline,
   VulkanBuffer*                                            pRayGenSBT,
   VulkanBuffer*                                            pMissSBT,
   VulkanBuffer*                                            pHitGroupSBT);

/*
void CreateRayTracingStateObject(
    VulkanRenderer*      pRenderer,
    ID3D12RootSignature* pGlobalRootSig,
    size_t               shadeBinarySize,
    const void*          pShaderBinary,
    ID3D12StateObject**  ppStateObject);
void CreateShaderRecordTables(
    VulkanRenderer*    pRenderer,
    ID3D12StateObject* pStateObject,
    ID3D12Resource**   ppRayGenSRT,
    ID3D12Resource**   ppMissSRT,
    ID3D12Resource**   ppHitGroupSRT);
void CreateGeometries(
    VulkanRenderer* pRenderer,
    Geometry&       outSphereGeometry,
    Geometry&       outBoxGeometry);
void CreateBLASes(
    VulkanRenderer*  pRenderer,
    const Geometry&  sphereGeometry,
    const Geometry&  boxGeometry,
    ID3D12Resource** ppSphereBLAS,
    ID3D12Resource** ppBoxBLAS);
void CreateTLAS(
    VulkanRenderer*                  pRenderer,
    ID3D12Resource*                  pSphereBLAS,
    ID3D12Resource*                  pBoxBLAS,
    ID3D12Resource**                 ppTLAS,
    std::vector<MaterialParameters>& outMaterialParams);
void CreateOutputTexture(VulkanRenderer* pRenderer, ID3D12Resource** ppBuffer);
void CreateAccumTexture(VulkanRenderer* pRenderer, ID3D12Resource** ppBuffer);
void CreateIBLTextures(
    VulkanRenderer*  pRenderer,
    IBLTextures&     outIBLTextures);
void CreateDescriptorHeap(VulkanRenderer* pRenderer, ID3D12DescriptorHeap** ppHeap);
void WriteDescriptors(
    VulkanRenderer*       pRenderer,
    ID3D12DescriptorHeap* pDescriptorHeap,
    ID3D12Resource*       pOutputTexture,
    ID3D12Resource*       pAccumTexture,
    ID3D12Resource*       pRayGenSamplesBuffer,
    const Geometry&       sphereGeometry,
    const Geometry&       boxGeometry,
    const IBLTextures&    iblTextures);
    */

void MouseMove(int x, int y, int buttons)
{
    static int prevX = x;
    static int prevY = y;

    if (buttons & MOUSE_BUTTON_LEFT) {
        int dx = x - prevX;
        int dy = y - prevY;

        gTargetAngle += 0.25f * dx;
    }

    prevX = x;
    prevY = y;
}

// =============================================================================
// main()
// =============================================================================
int main(int argc, char** argv)
{
    std::unique_ptr<VulkanRenderer> renderer = std::make_unique<VulkanRenderer>();

    if (!InitVulkan(renderer.get(), gEnableDebug, gEnableRayTracing)) {
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Get ray tracing properties
    // *************************************************************************
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    {
        VkPhysicalDeviceProperties2 properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext                       = &rayTracingProperties;
        vkGetPhysicalDeviceProperties2(renderer->PhysicalDevice, &properties);
    }

    // *************************************************************************
    // Get descriptor buffer properties
    // *************************************************************************
    VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptorBufferProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT};
    {
        VkPhysicalDeviceProperties2 properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext                       = &descriptorBufferProperties;
        vkGetPhysicalDeviceProperties2(renderer->PhysicalDevice, &properties);
    }

    // *************************************************************************
    // Compile shaders
    // *************************************************************************
    std::vector<uint32_t> rayTraceSpirv;
    {
        auto source = LoadString("projects/029_raytracing_refract_d3d12/shaders.hlsl");
        assert((!source.empty()) && "no shader source!");

        std::string errorMsg;
        HRESULT     hr = CompileHLSL(source, "", "lib_6_5", &rayTraceSpirv, &errorMsg);
        if (FAILED(hr)) {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (raytracing): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }
    }

    // *************************************************************************
    // Ray tracing descriptor set and pipeline layout
    //
    // This is used for pipeline creation and setting the descriptor buffer(s)
    //
    // *************************************************************************
    VulkanPipelineLayout rayTracePipelineLayout = {};
    CreateRayTracePipelineLayout(renderer.get(), &rayTracePipelineLayout);

    // *************************************************************************
    // Ray tracing Shader module
    // *************************************************************************
    VkShaderModule rayTraceShaderModule = VK_NULL_HANDLE;
    {
       VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
       createInfo.codeSize                 = SizeInBytes(rayTraceSpirv);
       createInfo.pCode                    = DataPtr(rayTraceSpirv);

       CHECK_CALL(vkCreateShaderModule(renderer->Device, &createInfo, nullptr, &rayTraceShaderModule));
    }

    // *************************************************************************
    // Ray tracing pipeline
    // 
    // The pipeline is created with 3 shader groups
    //   1) Ray gen
    //   2) Miss
    //   3) Hitgroup
    // 
    // *************************************************************************
    VkPipeline rayTracePipeline = VK_NULL_HANDLE;
    CreateRayTracingPipeline(
       renderer.get(),
       rayTraceShaderModule,
       rayTracePipelineLayout,
       &rayTracePipeline);

    // *************************************************************************
    // Shader binding tables
    //
    // This assumes there are 3 shader groups in the pipeline:
    //   1) Ray gen
    //   2) Miss
    //   3) Hitgroup
    // *************************************************************************
    VulkanBuffer rgenSBT = {};
    VulkanBuffer missSBT = {};
    VulkanBuffer hitgSBT = {};
    CreateShaderBindingTables(
       renderer.get(),
       rayTracingProperties,
       rayTracePipeline,
       &rgenSBT,
       &missSBT,
       &hitgSBT);

    /*
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
    // Bottom level acceleration structure
    // *************************************************************************
    ComPtr<ID3D12Resource> sphereBLAS;
    ComPtr<ID3D12Resource> boxBLAS;
    CreateBLASes(
        renderer.get(),
        sphereGeometry,
        boxGeometry,
        &sphereBLAS,
        &boxBLAS);

    // *************************************************************************
    // Top level acceleration structure
    // *************************************************************************
    ComPtr<ID3D12Resource>          tlasBuffer;
    std::vector<MaterialParameters> materialParams;
    CreateTLAS(
        renderer.get(),
        sphereBLAS.Get(),
        boxBLAS.Get(),
        &tlasBuffer,
        materialParams);

    // *************************************************************************
    // Output and accumulation texture
    // *************************************************************************
    ComPtr<ID3D12Resource> outputTexture;
    ComPtr<ID3D12Resource> accumTexture;
    CreateOutputTexture(renderer.get(), &outputTexture);
    CreateAccumTexture(renderer.get(), &accumTexture);

    // *************************************************************************
    // Material params buffer
    // *************************************************************************
    ComPtr<ID3D12Resource> materialParamsBuffer;
    CreateBuffer(
        renderer.get(),
        SizeInBytes(materialParams),
        DataPtr(materialParams),
        &materialParamsBuffer);

    // *************************************************************************
    // Scene params constant buffer
    // *************************************************************************
    ComPtr<ID3D12Resource> sceneParamsBuffer;
    CHECK_CALL(CreateBuffer(
        renderer.get(),
        Align<size_t>(sizeof(SceneParameters), 256),
        nullptr,
        &sceneParamsBuffer));

    // *************************************************************************
    // Ray gen samples buffer
    // *************************************************************************
    ComPtr<ID3D12Resource> rayGenSamplesBuffer;
    CHECK_CALL(CreateUAVBuffer(
        renderer.get(),
        (gWindowWidth * gWindowHeight * sizeof(uint32_t)),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        &rayGenSamplesBuffer));

    // *************************************************************************
    // Descriptor heaps
    // *************************************************************************
    IBLTextures iblTextures = {};
    CreateIBLTextures(
        renderer.get(),
        iblTextures);

    // *************************************************************************
    // Descriptor heaps
    // *************************************************************************
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    CreateDescriptorHeap(renderer.get(), &descriptorHeap);

    // Write descriptor to descriptor heap
    WriteDescriptors(
        renderer.get(),
        descriptorHeap.Get(),
        outputTexture.Get(),
        accumTexture.Get(),
        rayGenSamplesBuffer.Get(),
        sphereGeometry,
        boxGeometry,
        iblTextures);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "029_raytracing_refract_d3d12");
    if (!window) {
        assert(false && "Window::Create failed");
        return EXIT_FAILURE;
    }
    window->AddMouseMoveCallbacks(MouseMove);

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetHWND(), window->GetWidth(), window->GetHeight())) {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Command allocator
    // *************************************************************************
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    {
        CHECK_CALL(renderer->Device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,    // type
            IID_PPV_ARGS(&commandAllocator))); // ppCommandList
    }

    // *************************************************************************
    // Command list
    // *************************************************************************
    ComPtr<ID3D12GraphicsCommandList5> commandList;
    {
        CHECK_CALL(renderer->Device->CreateCommandList1(
            0,                              // nodeMask
            D3D12_COMMAND_LIST_TYPE_DIRECT, // type
            D3D12_COMMAND_LIST_FLAG_NONE,   // flags
            IID_PPV_ARGS(&commandList)));   // ppCommandList
    }

    // *************************************************************************
    // Persistent map scene parameters
    // *************************************************************************
    SceneParameters* pSceneParams = nullptr;
    CHECK_CALL(sceneParamsBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pSceneParams)));

    // *************************************************************************
    // Main loop
    // *************************************************************************
    while (window->PollEvents()) {
        CHECK_CALL(commandAllocator->Reset());
        CHECK_CALL(commandList->Reset(commandAllocator.Get(), nullptr));

        // Smooth out the rotation on Y
        gAngle += (gTargetAngle - gAngle) * 0.1f;

        // Camera matrices
        mat4 transformEyeMat     = glm::rotate(glm::radians(-gAngle), vec3(0, 1, 0));
        vec3 startingEyePosition = vec3(0, 1.0f, 4.5f);
        vec3 eyePosition         = transformEyeMat * vec4(startingEyePosition, 1);
        mat4 viewMat             = glm::lookAt(eyePosition, vec3(0, 0, 0), vec3(0, 1, 0));
        mat4 projMat             = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);

        // Set constant buffer values
        pSceneParams->ViewInverseMatrix       = glm::inverse(viewMat);
        pSceneParams->ProjectionInverseMatrix = glm::inverse(projMat);
        pSceneParams->EyePosition             = eyePosition;

        // Trace rays
        {
            commandList->SetComputeRootSignature(globalRootSig.Get());
            commandList->SetDescriptorHeaps(1, descriptorHeap.GetAddressOf());

            // Acceleration structure (t0)
            commandList->SetComputeRootShaderResourceView(0, tlasBuffer->GetGPUVirtualAddress());
            // Output texture (u1)
            commandList->SetComputeRootDescriptorTable(1, descriptorHeap->GetGPUDescriptorHandleForHeapStart());
            // Scene params (b5)
            commandList->SetComputeRootConstantBufferView(2, sceneParamsBuffer->GetGPUVirtualAddress());
            //  Index buffer (t20)
            //  Position buffer (t25)
            //  Normal buffer (t30)
            D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
            descriptorTable.ptr += kGeoBuffersOffset * renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            commandList->SetComputeRootDescriptorTable(3, descriptorTable);
            // Environment map (t12)
            descriptorTable = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
            descriptorTable.ptr += kIBLTextureOffset * renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            commandList->SetComputeRootDescriptorTable(4, descriptorTable);
            // Material params (t9)
            commandList->SetComputeRootShaderResourceView(5, materialParamsBuffer->GetGPUVirtualAddress());

            commandList->SetPipelineState1(stateObject.Get());

            D3D12_DISPATCH_RAYS_DESC dispatchDesc               = {};
            dispatchDesc.RayGenerationShaderRecord.StartAddress = rgenSRT->GetGPUVirtualAddress();
            dispatchDesc.RayGenerationShaderRecord.SizeInBytes  = rgenSRT->GetDesc().Width;
            dispatchDesc.MissShaderTable.StartAddress           = missSRT->GetGPUVirtualAddress();
            dispatchDesc.MissShaderTable.SizeInBytes            = missSRT->GetDesc().Width;
            dispatchDesc.MissShaderTable.StrideInBytes          = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;
            dispatchDesc.HitGroupTable.StartAddress             = hitgSRT->GetGPUVirtualAddress();
            dispatchDesc.HitGroupTable.SizeInBytes              = hitgSRT->GetDesc().Width;
            dispatchDesc.HitGroupTable.StrideInBytes            = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;
            dispatchDesc.Width                                  = gWindowWidth;
            dispatchDesc.Height                                 = gWindowHeight;
            dispatchDesc.Depth                                  = 1;

            commandList->DispatchRays(&dispatchDesc);

            commandList->Close();

            ID3D12CommandList* pList = commandList.Get();
            renderer->Queue->ExecuteCommandLists(1, &pList);

            if (!WaitForGpu(renderer.get())) {
                assert(false && "WaitForGpu failed");
                break;
            }
        }

        // Copy output texture to swapchain buffer
        {
            UINT bufferIndex = renderer->Swapchain->GetCurrentBackBufferIndex();

            ComPtr<ID3D12Resource> swapchainBuffer;
            CHECK_CALL(renderer->Swapchain->GetBuffer(bufferIndex, IID_PPV_ARGS(&swapchainBuffer)));

            CHECK_CALL(commandAllocator->Reset());
            CHECK_CALL(commandList->Reset(commandAllocator.Get(), nullptr));

            D3D12_RESOURCE_BARRIER preCopyBarriers[2];
            preCopyBarriers[0] = CreateTransition(swapchainBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
            preCopyBarriers[1] = CreateTransition(outputTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

            commandList->ResourceBarrier(ARRAYSIZE(preCopyBarriers), preCopyBarriers);

            commandList->CopyResource(swapchainBuffer.Get(), outputTexture.Get());

            D3D12_RESOURCE_BARRIER postCopyBarriers[2];
            postCopyBarriers[0] = CreateTransition(swapchainBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
            postCopyBarriers[1] = CreateTransition(outputTexture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            commandList->ResourceBarrier(ARRAYSIZE(postCopyBarriers), postCopyBarriers);

            commandList->Close();

            ID3D12CommandList* pList = commandList.Get();
            renderer->Queue->ExecuteCommandLists(1, &pList);

            if (!WaitForGpu(renderer.get())) {
                assert(false && "WaitForGpu failed");
                break;
            }
        }

        if (!SwapchainPresent(renderer.get())) {
            assert(false && "SwapchainPresent failed");
            break;
        }
    }

    return 0;
    */
}

void CreateRayTracePipelineLayout(
   VulkanRenderer*         pRenderer,
   VulkanPipelineLayout*   pPipelineLayout)
{
   // Descriptor set layout
   {
      std::vector<VkDescriptorSetLayoutBinding> bindings = {};
      // Acceleration structure (t0)
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 0;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
         bindings.push_back(binding);
      }
      // Output texture (u1)
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 1;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
         bindings.push_back(binding);
      }
      // Scene params (b5)
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 5;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
         bindings.push_back(binding);
      }
      // Index buffers (t20)
      // Position buffers (t25)
      // Normal buffers (t30)
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 20;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
         binding.descriptorCount              = 5;
         binding.stageFlags                   = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
         bindings.push_back(binding);

         binding.binding                      = 25;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
         binding.descriptorCount              = 5;
         binding.stageFlags                   = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
         bindings.push_back(binding);

         binding.binding                      = 30;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
         binding.descriptorCount              = 5;
         binding.stageFlags                   = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
         bindings.push_back(binding);
      }

      // IBLEnvironmentMap (t12)
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 12;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_MISS_BIT_KHR;
         bindings.push_back(binding);
      }

      // Material params (t9)
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 9;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
         bindings.push_back(binding);
      }

      // IBLMapSampler (s14)
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 14;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_MISS_BIT_KHR;
         bindings.push_back(binding);
      }

      VkDescriptorSetLayoutCreateInfo createInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
      createInfo.flags                           = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
      createInfo.bindingCount                    = CountU32(bindings);
      createInfo.pBindings                       = DataPtr(bindings);

      CHECK_CALL(vkCreateDescriptorSetLayout(
         pRenderer->Device,
         &createInfo,
         nullptr,
         &pPipelineLayout->DescriptorSetLayout));
   }

   // Pipeline layout
   {
      VkPipelineLayoutCreateInfo createInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
      createInfo.setLayoutCount             = 1;
      createInfo.pSetLayouts                = &pPipelineLayout->DescriptorSetLayout;

      CHECK_CALL(vkCreatePipelineLayout(
         pRenderer->Device,
         &createInfo,
         nullptr,
         &pPipelineLayout->PipelineLayout));
   }
}

void CreateRayTracingPipeline(
   VulkanRenderer*               pRenderer,
   VkShaderModule                rayTraceModule,
   const VulkanPipelineLayout&   pipelineLayout,
   VkPipeline*                   pPipeline)
{
   // Shader stages
   std::vector<VkPipelineShaderStageCreateInfo> shaderStages = {};
   // Ray gen
   {
      VkPipelineShaderStageCreateInfo createInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
      createInfo.stage                           = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
      createInfo.module                          = rayTraceModule;
      createInfo.pName                           = gRayGenShaderName;

      shaderStages.push_back(createInfo);
   }
   // Miss
   {
      VkPipelineShaderStageCreateInfo createInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
      createInfo.stage                           = VK_SHADER_STAGE_MISS_BIT_KHR;
      createInfo.module                          = rayTraceModule;
      createInfo.pName                           = gMissShaderName;

      shaderStages.push_back(createInfo);
   }
   // Closest Hit
   {
      VkPipelineShaderStageCreateInfo createInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
      createInfo.stage                           = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
      createInfo.module                          = rayTraceModule;
      createInfo.pName                           = gClosestHitShaderName;

      shaderStages.push_back(createInfo);
   }

   // Shader groups
   std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups = {};
   // Ray Gen
   {
      VkRayTracingShaderGroupCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
      createInfo.type                                 = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
      createInfo.generalShader                        = 0; // shaderStages[0]
      createInfo.closestHitShader                     = VK_SHADER_UNUSED_KHR;
      createInfo.anyHitShader                         = VK_SHADER_UNUSED_KHR;
      createInfo.intersectionShader                   = VK_SHADER_UNUSED_KHR;

      shaderGroups.push_back(createInfo);
   }
   // Miss
   {
      VkRayTracingShaderGroupCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
      createInfo.type                                 = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
      createInfo.generalShader                        = 1; // shaderStages[1]
      createInfo.closestHitShader                     = VK_SHADER_UNUSED_KHR;
      createInfo.anyHitShader                         = VK_SHADER_UNUSED_KHR;
      createInfo.intersectionShader                   = VK_SHADER_UNUSED_KHR;

      shaderGroups.push_back(createInfo);
   }
   // Closest Hit
   {
      VkRayTracingShaderGroupCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
      createInfo.type                                 = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
      createInfo.generalShader                        = VK_SHADER_UNUSED_KHR;
      createInfo.closestHitShader                     = 2; // shaderStages[2]
      createInfo.anyHitShader                         = VK_SHADER_UNUSED_KHR;
      createInfo.intersectionShader                   = VK_SHADER_UNUSED_KHR;

      shaderGroups.push_back(createInfo);
   }

   VkRayTracingPipelineInterfaceCreateInfoKHR pipelineInterfaceCreateInfo = { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_INTERFACE_CREATE_INFO_KHR };
   //
   pipelineInterfaceCreateInfo.maxPipelineRayPayloadSize    = 4 * sizeof(float) + 3 * sizeof(uint32_t);  // color, ray depth, sample index , ray type
   pipelineInterfaceCreateInfo.maxPipelineRayHitAttributeSize = 2 * sizeof(float);                       // barycentrics

   VkRayTracingPipelineCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
   createInfo.flags                             = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
   createInfo.stageCount                        = CountU32(shaderStages);
   createInfo.pStages                           = DataPtr(shaderStages);
   createInfo.groupCount                        = CountU32(shaderGroups);
   createInfo.pGroups                           = DataPtr(shaderGroups);
   createInfo.maxPipelineRayRecursionDepth      = 16;
   createInfo.pLibraryInterface                 = &pipelineInterfaceCreateInfo;
   createInfo.layout                            = pipelineLayout.PipelineLayout;
   createInfo.basePipelineHandle                = VK_NULL_HANDLE;
   createInfo.basePipelineIndex                 = -1;

   CHECK_CALL(fn_vkCreateRayTracingPipelinesKHR(
      pRenderer->Device,   // device
      VK_NULL_HANDLE,      // deferredOperation
      VK_NULL_HANDLE,      // pipelineCache
      1,                   // createInfoCount
      &createInfo,         // pCreateInfos
      nullptr,             // pAllocator
      pPipeline));         // pPipelines
}

void CreateShaderBindingTables(
   VulkanRenderer*                                          pRenderer,
   const VkPhysicalDeviceRayTracingPipelinePropertiesKHR&   rayTracingProperties,
   VkPipeline                                               pipeline,
   VulkanBuffer*                                            pRayGenSBT,
   VulkanBuffer*                                            pMissSBT,
   VulkanBuffer*                                            pHitGroupSBT)
{
   // hardcoded group count
   const uint32_t groupCount = 3;

   // Handle sizes
   uint32_t groupHandleSize         = rayTracingProperties.shaderGroupHandleSize;
   uint32_t groupHandleAlignment    = rayTracingProperties.shaderGroupHandleAlignment;
   uint32_t alignedGroupHandleSize  = Align(groupHandleSize, groupHandleAlignment);
   uint32_t totalGroupDataSize      = groupCount * groupHandleSize;

   //
   // This is what the shader group handles look like
   // in handlesData based on the pipeline. The offsets
   // are in bytes - assuming handleSize is 32 bytes
   //
   // +---------------+
   // |  RGEN         | offset = 0
   // +---------------+
   // |  MISS         | offset = 32
   // +---------------+
   // |  HITG         | offset = 64
   // +---------------+
   //
   std::vector<char> groupHandlesData(totalGroupDataSize);
   CHECK_CALL(fn_vkGetRayTracingShaderGroupHandlesKHR(
      pRenderer->Device,         // device
      pipeline,                  // pipeline
      0,                         // firstGroup
      groupCount,                // groupCount
      totalGroupDataSize,        // dataSize
      groupHandlesData.data())); // pData

   // Usage flags for SBT buffer
   VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;

   char* pShaderGroupHandleRGEN = groupHandlesData.data();
   char* pShaderGroupHandleMISS = groupHandlesData.data() + groupHandleSize;
   char* pShaderGroupHandleHITG = groupHandlesData.data() + 2 * groupHandleSize;

   // 
   // Create buffers for each shaders group's SBT and copy the 
   // shader group handles into each buffer.
   //
   // The size of the SBT buffers must be aligned to
   // VKPhysicalDeviceRayTracingPipelinePropertiesKHR::shaderGroupBaseAlignment.
   //
   const uint32_t shaderGroupBaseAlignment = rayTracingProperties.shaderGroupBaseAlignment;
   // Ray gen
   {
      CHECK_CALL(CreateBuffer(
         pRenderer,                 // pRenderer
         groupHandleSize,           // srcSize
         pShaderGroupHandleRGEN,    // pSrcData
         usageFlags,                // usageFlags
         shaderGroupBaseAlignment,  // minAlignment
         pRayGenSBT));              // pBuffer
   }
   // Miss
   {
      CHECK_CALL(CreateBuffer(
         pRenderer,                 // pRenderer
         groupHandleSize,           // srcSize
         pShaderGroupHandleMISS,    // pSrcData
         usageFlags,                // usageFlags
         shaderGroupBaseAlignment,  // minAlignment
         pMissSBT));                // pBuffer
   }
   // HITG: closest hit
   {
      CHECK_CALL(CreateBuffer(
         pRenderer,                 // pRenderer
         groupHandleSize,           // srcSize
         pShaderGroupHandleHITG,    // pSrcData
         usageFlags,                // usageFlags
         shaderGroupBaseAlignment,  // minAlignment
         pHitGroupSBT));            // pBuffer
   }
}


/*
void CreateRayTracingStateObject(
    VulkanRenderer*          pRenderer,
    ID3D12RootSignature* pGlobalRootSig,
    size_t               shadeBinarySize,
    const void*          pShaderBinary,
    ID3D12StateObject**  ppStateObject)
{
    enum
    {
        DXIL_LIBRARY_INDEX       = 0,
        TRIANGLE_HIT_GROUP_INDEX = 1,
        SHADER_CONFIG_INDEX      = 2,
        GLOBAL_ROOT_SIG_INDEX    = 3,
        PIPELINE_CONFIG_INDEX    = 4,
        SUBOBJECT_COUNT,
    };

    //
    // std::vector can't be used here because the association needs
    // to refer to a subobject that's found in the subobject list.
    //
    D3D12_STATE_SUBOBJECT subobjects[SUBOBJECT_COUNT];

    // ---------------------------------------------------------------------
    // DXIL Library
    //
    // This contains the shaders and their entrypoints for the state object.
    // Since shaders are not considered a subobject, they need to be passed
    // in via DXIL library subobjects.
    //
    // Define which shader exports to surface from the library.
    // If no shader exports are defined for a DXIL library subobject, all
    // shaders will be surfaced.
    // In this sample, this could be omitted for convenience since the
    // sample uses all shaders in the library.
    //
    // ---------------------------------------------------------------------
    D3D12_EXPORT_DESC rgenExport = {};
    rgenExport.Name              = gRayGenShaderName;
    rgenExport.ExportToRename    = nullptr;
    rgenExport.Flags             = D3D12_EXPORT_FLAG_NONE;

    D3D12_EXPORT_DESC missExport = {};
    missExport.Name              = gMissShaderName;
    missExport.ExportToRename    = nullptr;
    missExport.Flags             = D3D12_EXPORT_FLAG_NONE;

    D3D12_EXPORT_DESC chitExport = {};
    chitExport.Name              = gClosestHitShaderName;
    chitExport.ExportToRename    = nullptr;
    chitExport.Flags             = D3D12_EXPORT_FLAG_NONE;

    std::vector<D3D12_EXPORT_DESC> exports;
    exports.push_back(rgenExport);
    exports.push_back(missExport);
    exports.push_back(chitExport);

    D3D12_DXIL_LIBRARY_DESC dxilLibraryDesc = {};
    dxilLibraryDesc.DXILLibrary             = {pShaderBinary, shadeBinarySize};
    dxilLibraryDesc.NumExports              = static_cast<UINT>(exports.size());
    dxilLibraryDesc.pExports                = exports.data();

    D3D12_STATE_SUBOBJECT* pSubobject = &subobjects[DXIL_LIBRARY_INDEX];
    pSubobject->Type                  = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    pSubobject->pDesc                 = &dxilLibraryDesc;

    // ---------------------------------------------------------------------
    // Triangle hit group
    //
    // A hit group specifies closest hit, any hit and intersection shaders
    // to be executed when a ray intersects the geometry's triangle/AABB.
    // In this sample, we only use triangle geometry with a closest hit
    // shader, so others are not set.
    //
    // ---------------------------------------------------------------------
    D3D12_HIT_GROUP_DESC hitGroupDesc   = {};
    hitGroupDesc.HitGroupExport         = gHitGroupName;
    hitGroupDesc.Type                   = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitGroupDesc.ClosestHitShaderImport = gClosestHitShaderName;

    pSubobject        = &subobjects[TRIANGLE_HIT_GROUP_INDEX];
    pSubobject->Type  = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    pSubobject->pDesc = &hitGroupDesc;

    // ---------------------------------------------------------------------
    // Shader config
    //
    // Defines the maximum sizes in bytes for the ray payload and attribute
    // structure.
    //
    // ---------------------------------------------------------------------
    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    shaderConfig.MaxPayloadSizeInBytes          = 4 * sizeof(float) + 3 * sizeof(uint32_t); // color, ray depth, sample count, rayType
    shaderConfig.MaxAttributeSizeInBytes        = 2 * sizeof(float);                        // barycentrics

    pSubobject        = &subobjects[SHADER_CONFIG_INDEX];
    pSubobject->Type  = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    pSubobject->pDesc = &shaderConfig;

    // ---------------------------------------------------------------------
    // Global root signature
    //
    // This is a root signature that is shared across all raytracing shaders
    // invoked during a DispatchRays() call.
    //
    // ---------------------------------------------------------------------
    pSubobject        = &subobjects[GLOBAL_ROOT_SIG_INDEX];
    pSubobject->Type  = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    pSubobject->pDesc = &pGlobalRootSig;

    // ---------------------------------------------------------------------
    // Pipeline config
    //
    // Defines the maximum TraceRay() recursion depth.
    //
    // PERFOMANCE TIP: Set max recursion depth as low as needed
    // as drivers may apply optimization strategies for low recursion
    // depths.
    //
    // ---------------------------------------------------------------------
    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfigDesc = {};
    pipelineConfigDesc.MaxTraceRecursionDepth           = 16;

    pSubobject        = &subobjects[PIPELINE_CONFIG_INDEX];
    pSubobject->Type  = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    pSubobject->pDesc = &pipelineConfigDesc;

    // ---------------------------------------------------------------------
    // Create the state object
    // ---------------------------------------------------------------------
    D3D12_STATE_OBJECT_DESC stateObjectDesc = {};
    stateObjectDesc.Type                    = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    stateObjectDesc.NumSubobjects           = SUBOBJECT_COUNT;
    stateObjectDesc.pSubobjects             = subobjects;

    CHECK_CALL(pRenderer->Device->CreateStateObject(&stateObjectDesc, IID_PPV_ARGS(ppStateObject)));
}
*/

/*
void CreateShaderRecordTables(
    VulkanRenderer*        pRenderer,
    ID3D12StateObject* pStateObject,
    ID3D12Resource**   ppRayGenSRT,
    ID3D12Resource**   ppMissSRT,
    ID3D12Resource**   ppHitGroupSRT)
{
    ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
    CHECK_CALL(pStateObject->QueryInterface(IID_PPV_ARGS(&stateObjectProperties)));

    void* pRayGenShaderIdentifier   = stateObjectProperties->GetShaderIdentifier(gRayGenShaderName);
    void* pMissShaderIdentifier     = stateObjectProperties->GetShaderIdentifier(gMissShaderName);
    void* pHitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(gHitGroupName);

    UINT shaderRecordSize = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;

    // -------------------------------------------------------------------------
    // Create buffers for SRTs
    // -------------------------------------------------------------------------
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension           = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment           = 0;
    desc.Width               = shaderRecordSize;
    desc.Height              = 1;
    desc.DepthOrArraySize    = 1;
    desc.MipLevels           = 1;
    desc.Format              = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc          = {1, 0};
    desc.Layout              = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags               = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type                  = D3D12_HEAP_TYPE_UPLOAD;

    // Ray gen
    {
        CHECK_CALL(pRenderer->Device->CreateCommittedResource(
            &heapProperties,                   // pHeapProperties
            D3D12_HEAP_FLAG_NONE,              // HeapFlags
            &desc,                             // pDesc
            D3D12_RESOURCE_STATE_GENERIC_READ, // InitialResourceState
            nullptr,                           // pOptimizedClearValue
            IID_PPV_ARGS(ppRayGenSRT)));       // riidResource, ppvResource

        // Copy shader identifier
        {
            char* pData;
            CHECK_CALL((*ppRayGenSRT)->Map(0, nullptr, reinterpret_cast<void**>(&pData)));

            memcpy(pData, pRayGenShaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

            (*ppRayGenSRT)->Unmap(0, nullptr);
        }
    }

    // Miss
    {
        CHECK_CALL(pRenderer->Device->CreateCommittedResource(
            &heapProperties,                   // pHeapProperties
            D3D12_HEAP_FLAG_NONE,              // HeapFlags
            &desc,                             // pDesc
            D3D12_RESOURCE_STATE_GENERIC_READ, // InitialResourceState
            nullptr,                           // pOptimizedClearValue
            IID_PPV_ARGS(ppMissSRT)));         // riidResource, ppvResource

        // Copy shader identifier
        {
            char* pData;
            CHECK_CALL((*ppMissSRT)->Map(0, nullptr, reinterpret_cast<void**>(&pData)));

            memcpy(pData, pMissShaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

            (*ppMissSRT)->Unmap(0, nullptr);
        }
    }

    // Hit group
    {
        CHECK_CALL(pRenderer->Device->CreateCommittedResource(
            &heapProperties,                   // pHeapProperties
            D3D12_HEAP_FLAG_NONE,              // HeapFlags
            &desc,                             // pDesc
            D3D12_RESOURCE_STATE_GENERIC_READ, // InitialResourceState
            nullptr,                           // pOptimizedClearValue
            IID_PPV_ARGS(ppHitGroupSRT)));     // riidResource, ppvResource

        // Copy shader identifier
        {
            char* pData;
            CHECK_CALL((*ppHitGroupSRT)->Map(0, nullptr, reinterpret_cast<void**>(&pData)));

            memcpy(pData, pHitGroupShaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

            (*ppHitGroupSRT)->Unmap(0, nullptr);
        }
    }
}
*/

/*
void CreateGeometries(
    VulkanRenderer* pRenderer,
    Geometry&   outSphereGeometry,
    Geometry&   outBoxGeometryy)
{
    // Sphere
    {
        TriMesh::Options options = {.enableNormals = true};

        TriMesh mesh;
        bool    res = TriMesh::LoadOBJ(GetAssetPath("models/monkey_lowres.obj").string(), "", options, &mesh);
        if (!res) {
            assert(false && "failed to load model");
        }
        mesh.ScaleToFit(1.2f);

        // mesh = TriMesh::Sphere(1.0f, 256, 256, {.enableNormals = true});

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
    }

    // Box
    {
        TriMesh   mesh = TriMesh::Cube(glm::vec3(15, 1, 4.5f), false, {.enableNormals = true});
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
    }
}
*/

/*
void CreateBLASes(
    VulkanRenderer*      pRenderer,
    const Geometry&  sphereGeometry,
    const Geometry&  boxGeometry,
    ID3D12Resource** ppSphereBLAS,
    ID3D12Resource** ppBoxBLAS)
{
    std::vector<const Geometry*>  geometries = {&sphereGeometry, &boxGeometry};
    std::vector<ID3D12Resource**> BLASes     = {ppSphereBLAS, ppBoxBLAS};

    for (uint32_t i = 0; i < 2; ++i) {
        auto pGeometry = geometries[i];
        auto ppBLAS    = BLASes[i];

        D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc       = {};
        geometryDesc.Type                                 = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometryDesc.Triangles.IndexFormat                = DXGI_FORMAT_R32_UINT;
        geometryDesc.Triangles.IndexCount                 = pGeometry->indexCount;
        geometryDesc.Triangles.IndexBuffer                = pGeometry->indexBuffer->GetGPUVirtualAddress();
        geometryDesc.Triangles.VertexFormat               = DXGI_FORMAT_R32G32B32_FLOAT;
        geometryDesc.Triangles.VertexCount                = pGeometry->vertexCount;
        geometryDesc.Triangles.VertexBuffer.StartAddress  = pGeometry->positionBuffer->GetGPUVirtualAddress();
        geometryDesc.Triangles.VertexBuffer.StrideInBytes = 12;
        geometryDesc.Flags                                = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE; // D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
        //
        inputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        inputs.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.NumDescs       = 1;
        inputs.pGeometryDescs = &geometryDesc;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
        pRenderer->Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

        // Scratch buffer
        ComPtr<ID3D12Resource> scratchBuffer;
        CHECK_CALL(CreateUAVBuffer(
            pRenderer,
            prebuildInfo.ScratchDataSizeInBytes,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            &scratchBuffer));

        // Storage buffer
        CHECK_CALL(CreateUAVBuffer(
            pRenderer,
            prebuildInfo.ResultDataMaxSizeInBytes,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            ppBLAS));

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
        //
        buildDesc.Inputs                           = inputs;
        buildDesc.DestAccelerationStructureData    = (*ppBLAS)->GetGPUVirtualAddress();
        buildDesc.ScratchAccelerationStructureData = scratchBuffer->GetGPUVirtualAddress();

        // Command allocator
        ComPtr<ID3D12CommandAllocator> commandAllocator;
        {
            CHECK_CALL(pRenderer->Device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,    // type
                IID_PPV_ARGS(&commandAllocator))); // ppCommandList
        }

        // Command list
        ComPtr<ID3D12GraphicsCommandList5> commandList;
        {
            CHECK_CALL(pRenderer->Device->CreateCommandList1(
                0,                              // nodeMask
                D3D12_COMMAND_LIST_TYPE_DIRECT, // type
                D3D12_COMMAND_LIST_FLAG_NONE,   // flags
                IID_PPV_ARGS(&commandList)));   // ppCommandList
        }

        // Build acceleration structure
        CHECK_CALL(commandAllocator->Reset());
        CHECK_CALL(commandList->Reset(commandAllocator.Get(), nullptr));
        commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
        commandList->Close();

        ID3D12CommandList* pList = commandList.Get();
        pRenderer->Queue->ExecuteCommandLists(1, &pList);

        bool waitres = WaitForGpu(pRenderer);
        assert(waitres && "WaitForGpu failed");
    }
}
*/

/*
void CreateTLAS(
    VulkanRenderer*                      pRenderer,
    ID3D12Resource*                  pSphereBLAS,
    ID3D12Resource*                  pBoxBLAS,
    ID3D12Resource**                 ppTLAS,
    std::vector<MaterialParameters>& outMaterialParams)
{
    // clang-format off
     std::vector<glm::mat3x4> transforms = {
         // Glass sphere (clear)
         {{1.0f, 0.0f, 0.0f,  0.0f},
          {0.0f, 1.0f, 0.0f,  0.0f},
          {0.0f, 0.0f, 1.0f,  0.0f}},
         // Glass sphere (red)
         {{1.0f, 0.0f, 0.0f,  -2.5f},
          {0.0f, 1.0f, 0.0f,  0.0f},
          {0.0f, 0.0f, 1.0f,  0.0f}},
         // Glass sphere (blue)
         {{1.0f, 0.0f, 0.0f,  2.5f},
          {0.0f, 1.0f, 0.0f,  0.0f},
          {0.0f, 0.0f, 1.0f,  0.0f}},
     };
    // clang-format on

    // Material params
    {
        // Glass sphere (clear)
        {
            MaterialParameters materialParams = {};
            materialParams.baseColor          = vec3(1, 1, 1);
            materialParams.roughness          = 0.0f;
            materialParams.absorbColor        = vec3(0, 0, 0);

            outMaterialParams.push_back(materialParams);
        }

        // Glass sphere (red)
        {
            MaterialParameters materialParams = {};
            materialParams.baseColor          = vec3(1, 0, 0);
            materialParams.roughness          = 0.0f;
            materialParams.absorbColor        = vec3(0, 8, 8);

            outMaterialParams.push_back(materialParams);
        }

        // Glass sphere (blue)
        {
            MaterialParameters materialParams = {};
            materialParams.baseColor          = vec3(0, 0, 1);
            materialParams.roughness          = 0.0f;
            materialParams.absorbColor        = vec3(15, 15, 6);

            outMaterialParams.push_back(materialParams);
        }
    }

    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
    {
        D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
        instanceDesc.InstanceMask                   = 1;
        instanceDesc.AccelerationStructure          = pSphereBLAS->GetGPUVirtualAddress();
        instanceDesc.Flags                          = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;

        uint32_t transformIdx = 0;

        // Glass sphere (clear)
        memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(instanceDesc);
        ++transformIdx;

        // Glass sphere (red)
        memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(instanceDesc);
        ++transformIdx;

        // Glass sphere (blue)
        memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(instanceDesc);
        ++transformIdx;
    }

    ComPtr<ID3D12Resource> instanceBuffer;
    CHECK_CALL(CreateBuffer(pRenderer, SizeInBytes(instanceDescs), DataPtr(instanceDescs), &instanceBuffer));

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    //
    inputs.Type          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags         = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.DescsLayout   = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs      = CountU32(instanceDescs);
    inputs.InstanceDescs = instanceBuffer->GetGPUVirtualAddress();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
    pRenderer->Device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

    // Scratch buffer
    ComPtr<ID3D12Resource> scratchBuffer;
    CHECK_CALL(CreateUAVBuffer(
        pRenderer,
        prebuildInfo.ScratchDataSizeInBytes,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        &scratchBuffer));

    // Storage buffer
    CHECK_CALL(CreateUAVBuffer(
        pRenderer,
        prebuildInfo.ResultDataMaxSizeInBytes,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        ppTLAS));

    // Command allocator
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    {
        CHECK_CALL(pRenderer->Device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,    // type
            IID_PPV_ARGS(&commandAllocator))); // ppCommandList
    }

    // Command list
    ComPtr<ID3D12GraphicsCommandList5> commandList;
    {
        CHECK_CALL(pRenderer->Device->CreateCommandList1(
            0,                              // nodeMask
            D3D12_COMMAND_LIST_TYPE_DIRECT, // type
            D3D12_COMMAND_LIST_FLAG_NONE,   // flags
            IID_PPV_ARGS(&commandList)));   // ppCommandList
    }

    // Build acceleration structure
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs                                             = inputs;
    buildDesc.DestAccelerationStructureData                      = (*ppTLAS)->GetGPUVirtualAddress();
    buildDesc.ScratchAccelerationStructureData                   = scratchBuffer->GetGPUVirtualAddress();

    CHECK_CALL(commandAllocator->Reset());
    CHECK_CALL(commandList->Reset(commandAllocator.Get(), nullptr));
    commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
    commandList->Close();

    ID3D12CommandList* pList = commandList.Get();
    pRenderer->Queue->ExecuteCommandLists(1, &pList);

    bool waitres = WaitForGpu(pRenderer);
    assert(waitres && "WaitForGpu failed");
}
*/

/*
void CreateOutputTexture(VulkanRenderer* pRenderer, ID3D12Resource** ppBuffer)
{
    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type                  = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension           = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment           = 0;
    desc.Width               = gWindowWidth;
    desc.Height              = gWindowHeight;
    desc.DepthOrArraySize    = 1;
    desc.MipLevels           = 1;
    desc.Format              = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc          = {1, 0};
    desc.Layout              = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags               = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    CHECK_CALL(pRenderer->Device->CreateCommittedResource(
        &heapProperties,                       // pHeapProperties
        D3D12_HEAP_FLAG_NONE,                  // HeapFlags
        &desc,                                 // pDesc
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, // InitialResourceState
        nullptr,                               // pOptimizedClearValue
        IID_PPV_ARGS(ppBuffer)));              // riidResource, ppvResource
}
*/

/*
void CreateAccumTexture(VulkanRenderer* pRenderer, ID3D12Resource** ppBuffer)
{
    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type                  = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension           = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment           = 0;
    desc.Width               = gWindowWidth;
    desc.Height              = gWindowHeight;
    desc.DepthOrArraySize    = 1;
    desc.MipLevels           = 1;
    desc.Format              = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleDesc          = {1, 0};
    desc.Layout              = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags               = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    CHECK_CALL(pRenderer->Device->CreateCommittedResource(
        &heapProperties,                       // pHeapProperties
        D3D12_HEAP_FLAG_NONE,                  // HeapFlags
        &desc,                                 // pDesc
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, // InitialResourceState
        nullptr,                               // pOptimizedClearValue
        IID_PPV_ARGS(ppBuffer)));              // riidResource, ppvResource
}
*/

/*
void CreateIBLTextures(
    VulkanRenderer*  pRenderer,
    IBLTextures& outIBLTextures)
{
    // IBL file
    auto iblFile = GetAssetPath("IBL/old_depot_4k.ibl");

    IBLMaps ibl = {};
    if (!LoadIBLMaps32f(iblFile, &ibl)) {
        GREX_LOG_ERROR("failed to load: " << iblFile);
        return;
    }

    outIBLTextures.envNumLevels = ibl.numLevels;

    // Environment only, irradiance is not used
    {
        const uint32_t pixelStride = ibl.environmentMap.GetPixelStride();
        const uint32_t rowStride   = ibl.environmentMap.GetRowStride();

        std::vector<DxMipOffset> mipOffsets;
        uint32_t                 levelOffset = 0;
        uint32_t                 levelWidth  = ibl.baseWidth;
        uint32_t                 levelHeight = ibl.baseHeight;
        for (uint32_t i = 0; i < ibl.numLevels; ++i) {
            DxMipOffset mipOffset = {};
            mipOffset.offset      = levelOffset;
            mipOffset.rowStride   = rowStride;

            mipOffsets.push_back(mipOffset);

            levelOffset += (rowStride * levelHeight);
            levelWidth >>= 1;
            levelHeight >>= 1;
        }

        CHECK_CALL(CreateTexture(
            pRenderer,
            ibl.baseWidth,
            ibl.baseHeight,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            mipOffsets,
            ibl.environmentMap.GetSizeInBytes(),
            ibl.environmentMap.GetPixels(),
            &outIBLTextures.envTexture));
    }

    GREX_LOG_INFO("Loaded " << iblFile);
}
*/

/*
void CreateDescriptorHeap(VulkanRenderer* pRenderer, ID3D12DescriptorHeap** ppHeap)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors             = 256;
    desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    CHECK_CALL(pRenderer->Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(ppHeap)));
}
*/

/*
void WriteDescriptors(
    VulkanRenderer*           pRenderer,
    ID3D12DescriptorHeap* pDescriptorHeap,
    ID3D12Resource*       pOutputTexture,
    ID3D12Resource*       pAccumTexture,
    ID3D12Resource*       pRayGenSamplesBuffer,
    const Geometry&       sphereGeometry,
    const Geometry&       boxGeometry,
    const IBLTextures&    iblTextures)
{
    {
        UINT descriptorIncSize = pRenderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // Outputs resources
        {
            D3D12_CPU_DESCRIPTOR_HANDLE descriptor = pDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
            descriptor.ptr += kOutputResourcesOffset * descriptorIncSize;

            // Output texture (u1)
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

            uavDesc.Format        = DXGI_FORMAT_B8G8R8A8_UNORM;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            pRenderer->Device->CreateUnorderedAccessView(pOutputTexture, nullptr, &uavDesc, descriptor);
            descriptor.ptr += descriptorIncSize;
        }

        // Geometry
        {
            const uint32_t kGeometryStride      = 5;
            const uint32_t kNumSpheres          = 3;
            const uint32_t kIndexBufferIndex    = 0;
            const uint32_t kPositionBufferIndex = 1;
            const uint32_t kNormalBufferIndex   = 2;

            const D3D12_CPU_DESCRIPTOR_HANDLE kBaseDescriptor = pDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
            const UINT                        kIncrementSize  = pRenderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            // Spheres
            for (uint32_t i = 0; i < kNumSpheres; ++i) {
                uint32_t                    offset     = 0;
                D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};

                // Index buffer (t20)
                offset     = kGeoBuffersOffset + (kIndexBufferIndex * kGeometryStride) + i;
                descriptor = {kBaseDescriptor.ptr + offset * kIncrementSize};
                CreateDescriptoBufferSRV(pRenderer, 0, sphereGeometry.indexCount / 3, 12, sphereGeometry.indexBuffer.Get(), descriptor);

                // Position buffer (t25)
                offset     = kGeoBuffersOffset + (kPositionBufferIndex * kGeometryStride) + i;
                descriptor = {kBaseDescriptor.ptr + offset * kIncrementSize};
                CreateDescriptoBufferSRV(pRenderer, 0, sphereGeometry.vertexCount, 4, sphereGeometry.positionBuffer.Get(), descriptor);

                // Normal buffer (t30)
                offset     = kGeoBuffersOffset + (kNormalBufferIndex * kGeometryStride) + i;
                descriptor = {kBaseDescriptor.ptr + offset * kIncrementSize};
                CreateDescriptoBufferSRV(pRenderer, 0, sphereGeometry.vertexCount, 4, sphereGeometry.normalBuffer.Get(), descriptor);
            }
        }

        // IBL Textures
        {
            D3D12_CPU_DESCRIPTOR_HANDLE descriptor = pDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
            descriptor.ptr += kIBLTextureOffset * descriptorIncSize;

            // Environment map
            CreateDescriptorTexture2D(pRenderer, iblTextures.envTexture.Get(), descriptor, 0, iblTextures.envNumLevels);
        }
    }
}
*/