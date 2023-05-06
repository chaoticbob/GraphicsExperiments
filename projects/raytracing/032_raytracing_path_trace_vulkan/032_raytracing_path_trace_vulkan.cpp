#include "window.h"

#include "dx_renderer.h"
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
// Shader code
// =============================================================================
const char* gClearRayGenSamplesShader = R"(
#version 460

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform RWTexture2D<float4> AccumTarget; // Accumulation texture
layout(binding = 1) uinform RWStructuredBuffer<uint> RayGenSamples; // Ray generation samples

void csmain()
{
  imageStore(AccumTarget, gl_GlobalInvocationID.xy, vec4(0,0,0,0));

  uint idx = gl_GlobalInvocationID.y * 1920 + gl_GlobalInvocationID.x;
  imageStore(RayGenSamples, idx, 0);
}
)";

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1920;
static uint32_t gWindowHeight = 1080;
static bool     gEnableDebug  = true;

static LPCWSTR gHitGroupName         = L"MyHitGroup";
static LPCWSTR gRayGenShaderName     = L"MyRaygenShader";
static LPCWSTR gMissShaderName       = L"MyMissShader";
static LPCWSTR gClosestHitShaderName = L"MyClosestHitShader";

static float gTargetAngle = 0.0f;
static float gAngle       = 0.0f;

static bool     gResetRayGenSamples = true;
static uint32_t gMaxSamples         = 4096;
static uint32_t gCurrentMaxSamples  = 0;

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
    uint  MaxSamples;
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
   VkImage irrTexture;
   VkImage envTexture;
   uint32_t envNumLevels;
};

struct MaterialParameters
{
    vec3  baseColor;
    float roughness;
    float metallic;
    float specularReflectance;
    float ior;
};

void CreateDescriptorSetLayout(VulkanRenderer* pRenderer, VkDescriptorSetLayout* pLayout);
void CreatePipelineLayout(VulkanRenderer* pRenderer, VkDescriptorSetLayout descriptorSetLayout, VkPipelineLayout* pLayout);
void CreateShaderModules(
   VulkanRenderer* pRenderer,
   const std::vector<uint32_t>& spirvRGEN,
   const std::vector<uint32_t>& spirvMISS,
   const std::vector<uint32_t>& spirvCHIT,
   const std::vector<uint32_t>& spirvRINT,
   VkShaderModule* pModuleRGEN,
   VkShaderModule* pModuleMISS,
   VkShaderModule* pModuleCHIT);
void CreateRayTracingPipeline(
   VulkanRenderer* pRenderer,
   VkShaderModule   moduleRGEN,
   VkShaderModule   moduleMISS,
   VkShaderModule   moduleCHIT,
   VkPipelineLayout pipelineLayout,
   VkPipeline* pPipeline);
void CreateShaderBindingTables(
    VulkanRenderer*                                  pRenderer,
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rayTracingProperties,
    VkPipeline                                       pipeline,
    VulkanBuffer*                                    pRayGenSBT,
    VulkanBuffer*                                    pMissSBT,
    VulkanBuffer*                                    pHitGroupSBT);
void CreateGlobalRootSig(VulkanRenderer* pRenderer, ID3D12RootSignature** ppRootSig);
void CreateRayTracingStateObject(
    VulkanRenderer*          pRenderer,
    ID3D12RootSignature* pGlobalRootSig,
    size_t               shadeBinarySize,
    const void*          pShaderBinary,
    ID3D12StateObject**  ppStateObject);
void CreateShaderRecordTables(
    VulkanRenderer*        pRenderer,
    ID3D12StateObject* pStateObject,
    ID3D12Resource**   ppRayGenSRT,
    ID3D12Resource**   ppMissSRT,
    ID3D12Resource**   ppHitGroupSRT);
void CreateGeometries(
    VulkanRenderer* pRenderer,
    Geometry&       outSphereGeometry,
    Geometry&       outBoxGeometry);
void CreateBLASes(
   VulkanRenderer* pRenderer,
   const Geometry& sphereGeometry,
   const Geometry& boxGeometry,
   VkAccelerationStructureKHR* pSphereBLAS,
   VkAccelerationStructureKHR* pBoxBLAS);
void CreateTLAS(
   VulkanRenderer* pRenderer,
   VkAccelerationStructureKHR *pSphereBLAS,
   VkAccelerationStructureKHR *pBoxBLAS,
   VulkanBuffer *pTLASBuffer,
   VkAccelerationStructureKHR *pTLAS,
   std::vector<MaterialParameters>& outMaterialParams);
void CreateOutputTexture(VulkanRenderer* pRenderer, VulkanImage* pBuffer);
void CreateAccumTexture(VulkanRenderer* pRenderer, VulkanImage* pBuffer);
void CreateIBLTextures(
   VulkanRenderer* pRenderer,
   VkImage* pBRDFLUT,
   IBLTextures& outIBLTextures);
void CreateDescriptorHeap(VulkanRenderer* pRenderer, ID3D12DescriptorHeap** ppHeap);
void WriteDescriptors(
    VulkanRenderer*           pRenderer,
    ID3D12DescriptorHeap* pDescriptorHeap,
    ID3D12Resource*       pOutputTexture,
    ID3D12Resource*       pAccumTexture,
    ID3D12Resource*       pRayGenSamplesBuffer,
    const Geometry&       sphereGeometry,
    const Geometry&       boxGeometry,
    const IBLTextures&    iblTextures);

void MouseMove(int x, int y, int buttons)
{
    static int prevX = x;
    static int prevY = y;

    if (buttons & MOUSE_BUTTON_LEFT) {
        int dx = x - prevX;
        int dy = y - prevY;

        gTargetAngle += 0.25f * dx;

        gResetRayGenSamples = true;
    }

    prevX = x;
    prevY = y;
}

void WriteDescriptors(std::unique_ptr<VulkanRenderer>& renderer, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap, Microsoft::WRL::ComPtr<ID3D12Resource>& outputTexture, Microsoft::WRL::ComPtr<ID3D12Resource>& accumTexture, Microsoft::WRL::ComPtr<ID3D12Resource>& rayGenSamplesBuffer, Geometry& sphereGeometry, Geometry& boxGeometry, IBLTextures& iblTextures);

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
   // Compile shaders
   // *************************************************************************
   std::vector<uint32_t> spirvRGEN;
   std::vector<uint32_t> spirvMISS;
   std::vector<uint32_t> spirvCHIT;
   {
      auto source = LoadString("projects/032_raytracing_path_trace_vulkan/shaders.glsl");

      std::string   errorMsg;
      CompileResult res = CompileGLSL(source, gRayGenShaderName, VK_SHADER_STAGE_RAYGEN_BIT_KHR, {}, &spirvRGEN, &errorMsg);
      if (res != COMPILE_SUCCESS) {
         std::stringstream ss;
         ss << "\n"
            << "Shader compiler error (RGEN): " << errorMsg << "\n";
         GREX_LOG_ERROR(ss.str().c_str());
         return EXIT_FAILURE;
      }

      res = CompileGLSL(source, gMissShaderName, VK_SHADER_STAGE_MISS_BIT_KHR, {}, &spirvMISS, &errorMsg);
      if (res != COMPILE_SUCCESS) {
         std::stringstream ss;
         ss << "\n"
            << "Shader compiler error (MISS): " << errorMsg << "\n";
         GREX_LOG_ERROR(ss.str().c_str());
         return EXIT_FAILURE;
      }

      res = CompileGLSL(source, gClosestHitShaderName, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, {}, &spirvCHIT, &errorMsg);
      if (res != COMPILE_SUCCESS) {
         std::stringstream ss;
         ss << "\n"
            << "Shader compiler error (CHIT): " << errorMsg << "\n";
         GREX_LOG_ERROR(ss.str().c_str());
         return EXIT_FAILURE;
      }
   }

   std::vector<uint32_t> spirvClearRayGenSamples
   {
       std::string errorMsg;
       HRESULT hr = CompileGLSL(gClearRayGenSamplesShader, "csmain", "cs_6_5", {}, &spirvClearRayGenSamples, &errorMsg);
       if (FAILED(hr)) {
           std::stringstream ss;
           ss << "\n"
              << "Shader compiler error (clear ray gen): " << errorMsg << "\n";
           GREX_LOG_ERROR(ss.str().c_str());
           assert(false);
           return EXIT_FAILURE;
       }
   }

      // *************************************************************************
      // Descriptor Set Layout
      // *************************************************************************
   VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
   CreateDescriptorSetLayout(renderer.get(), &descriptorSetLayout);

   // *************************************************************************
   // Pipeline layout
   //
   // This is used for pipeline creation and setting the descriptor buffer(s).
   //
   // *************************************************************************
   VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
   CreatePipelineLayout(renderer.get(), descriptorSetLayout, &pipelineLayout);

   // *************************************************************************
   // Shader module
   // *************************************************************************
   VkShaderModule moduleRGEN = VK_NULL_HANDLE;
   VkShaderModule moduleMISS = VK_NULL_HANDLE;
   VkShaderModule moduleCHIT = VK_NULL_HANDLE;
   CreateShaderModules(
      renderer.get(),
      spirvRGEN,
      spirvMISS,
      spirvCHIT,
      &moduleRGEN,
      &moduleMISS,
      &moduleCHIT);

   // *************************************************************************
   // Get ray tracing properties
   // *************************************************************************
   VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingProperties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR };
   {
      VkPhysicalDeviceProperties2 properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
      properties.pNext                       = &rayTracingProperties;
      vkGetPhysicalDeviceProperties2(renderer->PhysicalDevice, &properties);
   }

   // *************************************************************************
   // Ray tracing pipeline
   //
   // The pipeline is created with 3 shader groups:
   //    1) Ray gen
   //    2) Miss
   //    3) Hitgroup
   //
   // *************************************************************************
   VkPipeline pipeline = VK_NULL_HANDLE;
   CreateRayTracingPipeline(
      renderer.get(),
      moduleRGEN,
      moduleMISS,
      moduleCHIT,
      pipelineLayout,
      &pipeline);

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
   // Shader binding tables
   //
   // This assumes that there are 3 shader groups in the pipeline:
   //    1) Ray gen
   //    2) Miss
   //    3) Hitgroup
   //
   // *************************************************************************
   VulkanBuffer rgenSBT = {};
   VulkanBuffer missSBT = {};
   VulkanBuffer hitgSBT = {};
   CreateShaderBindingTables(
      renderer.get(),
      rayTracingProperties,
      pipeline,
      &boxGeometry
      &rgenSBT,
      &missSBT,
      &hitgSBT);

   // *************************************************************************
   // Clear ray gen pipeline
   // *************************************************************************
   VkDescriptorSetLayout clearRayGenDescriptorSetLayout = VK_NULL_HANDLE;
   VkPipeline clearRayGenPipeline = VK_NULL_HANDLE;
   {
      std::vector<VkDescriptorSetLayoutBinding> bindings = {};
      // layout(binding = 0) uniform RWTexture2D<float4> AccumTarget; // Accumulation texture
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 0;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_COMPUTE_BIT;

         bindings.push_back(binding);
      }
      // layout(binding = 1) uinform RWStructuredBuffer<uint> RayGenSamples; // Ray generation samples
      {
         VkDescriptorSetLayoutBinding binding = {};
         binding.binding                      = 1;
         binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
         binding.descriptorCount              = 1;
         binding.stageFlags                   = VK_SHADER_STAGE_COMPUTE_BIT;

         bindings.push_back(binding);
      }

      VkDescriptorSetLayoutCreateInfo createInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
      createInfo.flags                           = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
      createInfo.bindingCount                    = CountU32(bindings);
      createInfo.pBindings                       = DataPtr(bindings);

      CHECK_CALL(vkCreateDescriptorSetLayout(pRenderer->Device, &createInfo, nullptr, &clearRayGenDescriptorSetLayout));

      VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
      pipelineLayoutCreateInfo.pSetLayouts       = clearRayGenDescriptorSetLayout;
      pipelineLayoutCreateInfo.setLayoutCount    = 1;

      VkPipelineLayout clearRayGenPipelineLayout = VK_NULL_HANDLE;
      CHECK_CALL(vkCreatePipelineLayout(pRenderer->Device, &pipelineLayoutCreateInfo, nullptr, &clearRayGenPipelineLayout));

      VkComputePipelineCreateInfo createComputePipelineInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
      createComputePipelineInfo.stage            = VK_SHADER_STAGE_COMPUTE_BIT;
      createComputePipelineInfo.layout           = clearRayGenPipelineLayout;

      CHECK_CALL(vkCreateComputePipelines(pRenderer->Device, nullptr, 1, createComputePipelineInfo, nullptr, &clearRayGenPipeline));
   }

   // *************************************************************************
   // Create geometry
   // *************************************************************************
   Geometry sphereGeometry;
   Geometry knobGeometry;
   Geometry monkeyGeometry;
   Geometry teapotGeometry;
   Geometry boxGeometry;
   CreateGeometries(
      renderer.get(),
      sphereGeometry,
      boxGeometry);

   // *************************************************************************
   // Bottom level acceleration structure
   // *************************************************************************
   VkAccelerationStructureKHR sphereBLAS;
   VkAccelerationStructureKHR boxBLAS;
   CreateBLASes(
      renderer.get(),
      sphereGeometry,
      boxGeometry,
      &sphereBLAS,
      &boxBLAS);

   // *************************************************************************
   // Top level acceleration structure
   // *************************************************************************
   VkAccelerationStructureKHR tlasBuffer;
   std::vector<MaterialParameters> materialParams;
   CreateTLAS(
      renderer.get(),
      &sphereBLAS,
      &boxBLAS,
      &tlasBuffer,
      materialParams);

   // *************************************************************************
   // Output and accumulation texture
   // *************************************************************************
   VulkanBuffer outputTexture;
   VulkanBuffer accumTexture;
   CreateOutputTexture(renderer.get(), &outputTexture);
   CreateAccumTexture(renderer.get(), &accumTexture);

   // *************************************************************************
   // Material params buffer
   // *************************************************************************
   VkBuffer materialParamsBuffer;
   CHECK_CALL(CreateBuffer(
      renderer.get(),
      SizeInBytes(materialParams),
      DataPtr(materialParams),
      &materialParamsBuffer));

   // *************************************************************************
   // Scene params constant buffer
   // *************************************************************************
   VkBuffer sceneParamsBuffer;
   CHECK_CALL(CreateBuffer(
      renderer.get(),
      Align<size_t>(sizeof(SceneParameters), 256),
      nullptr,
      &sceneParamsBuffer));

   // *************************************************************************
   // Ray gen samples buffer
   // *************************************************************************
   VkBuffer rayGenSamplesBuffer;
   CHECK_CALL(CreateUAVBuffer(
      renderer.get(),
      (gWindowWidth* gWindowHeight * sizeof(uint32_t)),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      &rayGenSamplesBuffer));

   // *************************************************************************
   // Descriptor heaps
   // *************************************************************************
   VkImage brdfLUT;
   IBLTextures iblTextures = {};
   CreateIBLTextures(
      renderer.get(),
      &brdfLUT,
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
   auto window = Window::Create(gWindowWidth, gWindowHeight, "031_raytracing_path_trace_d3d12");
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
   // Imgui
   // *************************************************************************
   if (!window->InitImGuiForD3D12(renderer.get())) {
      assert(false && "Window::InitImGuiForD3D12 failed");
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
      window->ImGuiNewFrameD3D12();

      if (ImGui::Begin("Scene")) {
         ImGui::SliderInt("Max Samples Per Pixel", reinterpret_cast<int*>(&gMaxSamples), 1, 16384);
      }
      ImGui::End();

      // ---------------------------------------------------------------------

      CHECK_CALL(commandAllocator->Reset());
      CHECK_CALL(commandList->Reset(commandAllocator.Get(), nullptr));

      if (gCurrentMaxSamples != gMaxSamples) {
         gCurrentMaxSamples  = gMaxSamples;
         gResetRayGenSamples = true;
      }

      // Smooth out the rotation on Y
      gAngle += (gTargetAngle - gAngle) * 0.1f;
      // Keep resetting until the angle is somewhat stable
      if (fabs(gTargetAngle - gAngle) > 0.1f) {
         gResetRayGenSamples = true;
      }

      // Camera matrices
      mat4 transformEyeMat     = glm::rotate(glm::radians(-gAngle), vec3(0, 1, 0));
      vec3 startingEyePosition = vec3(0, 3.5f, 6.0f);
      vec3 eyePosition         = transformEyeMat * vec4(startingEyePosition, 1);
      mat4 viewMat             = glm::lookAt(eyePosition, vec3(0, 3, 0), vec3(0, 1, 0));
      mat4 projMat             = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);

      // Set constant buffer values
      pSceneParams->ViewInverseMatrix       = glm::inverse(viewMat);
      pSceneParams->ProjectionInverseMatrix = glm::inverse(projMat);
      pSceneParams->EyePosition             = eyePosition;
      pSceneParams->MaxSamples              = gCurrentMaxSamples;

      // Reset ray gen samples
      if (gResetRayGenSamples) {
         commandList->SetDescriptorHeaps(1, descriptorHeap.GetAddressOf());

         commandList->SetComputeRootSignature(clearRayGenRootSig.Get());
         commandList->SetPipelineState(clearRayGenPSO.Get());

         D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
         descriptorTable.ptr += (kOutputResourcesOffset + 1) * renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
         commandList->SetComputeRootDescriptorTable(0, descriptorTable);

         commandList->Dispatch(gWindowWidth / 8, gWindowHeight / 8, 1);
         gResetRayGenSamples = false;
      }

      // Trace rays
      {
         commandList->SetComputeRootSignature(globalRootSig.Get());
         commandList->SetDescriptorHeaps(1, descriptorHeap.GetAddressOf());

         D3D12_GPU_DESCRIPTOR_HANDLE descriptorHeapStart = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
         UINT                        descriptorIncSize   = renderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

         // Acceleration structure (t0)
         commandList->SetComputeRootShaderResourceView(0, tlasBuffer->GetGPUVirtualAddress());
         // Output texture (u1)
         // Accumulation texture (u2)
         // Ray generation samples (u3)
         D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = { descriptorHeapStart.ptr + kOutputResourcesOffset * descriptorIncSize };
         commandList->SetComputeRootDescriptorTable(1, descriptorHeap->GetGPUDescriptorHandleForHeapStart());
         // Scene params (b5)
         commandList->SetComputeRootConstantBufferView(2, sceneParamsBuffer->GetGPUVirtualAddress());
         //  Index buffer (t20)
         //  Position buffer (t25)
         //  Normal buffer (t30)
         descriptorTable = { descriptorHeapStart.ptr + kGeoBuffersOffset * descriptorIncSize };
         commandList->SetComputeRootDescriptorTable(3, descriptorTable);
         // Environment map (t12)
         descriptorTable = { descriptorHeapStart.ptr + kIBLTextureOffset * descriptorIncSize };
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

         // ImGui
         {
            UINT bufferIndex = renderer->Swapchain->GetCurrentBackBufferIndex();

            ComPtr<ID3D12Resource> swapchainBuffer;
            CHECK_CALL(renderer->Swapchain->GetBuffer(bufferIndex, IID_PPV_ARGS(&swapchainBuffer)));

            CHECK_CALL(commandAllocator->Reset());
            CHECK_CALL(commandList->Reset(commandAllocator.Get(), nullptr));

            D3D12_RESOURCE_BARRIER preRenderBarrier = CreateTransition(swapchainBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
            commandList->ResourceBarrier(1, &preRenderBarrier);
            {
               commandList->OMSetRenderTargets(
                  1,
                  &renderer->SwapchainRTVDescriptorHandles[bufferIndex],
                  false,
                  &renderer->SwapchainDSVDescriptorHandles[bufferIndex]);

               // Viewport and scissor
               D3D12_VIEWPORT viewport = { 0, 0, static_cast<float>(gWindowWidth), static_cast<float>(gWindowHeight), 0, 1 };
               commandList->RSSetViewports(1, &viewport);
               D3D12_RECT scissor = { 0, 0, static_cast<long>(gWindowWidth), static_cast<long>(gWindowHeight) };
               commandList->RSSetScissorRects(1, &scissor);

               // Draw ImGui
               window->ImGuiRenderDrawData(renderer.get(), commandList.Get());
            }
            D3D12_RESOURCE_BARRIER postRenderBarrier = CreateTransition(swapchainBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
            commandList->ResourceBarrier(1, &postRenderBarrier);

            commandList->Close();

            ID3D12CommandList* pList = commandList.Get();
            renderer->Queue->ExecuteCommandLists(1, &pList);

            if (!WaitForGpu(renderer.get())) {
               assert(false && "WaitForGpu failed");
               break;
            }
         }
      }

      if (!SwapchainPresent(renderer.get())) {
         assert(false && "SwapchainPresent failed");
         break;
      }
   }

   return 0;
}

void CreateDescriptorSetLayout(VulkanRenderer* pRenderer, VkDescriptorSetLayout* pLayout)
{
   std::vector<VkDescriptorSetLayoutBinding> bindings = {};
   // layout(binding = 0) uniform accelerationStructureEXT Scene;
   {
      VkDescriptorSetLayoutBinding binding = {};
      binding.binding                      = 0;
      binding.descriptorType               = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
      binding.descriptorCount              = 1;
      binding.stageFlags                   = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

      bindings.push_back(binding);
   }
   // layout(binding = 1) uniform image2D RenderTarget;
   {
      VkDescriptorSetLayoutBinding binding = {};
      binding.binding                      = 1;
      binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      binding.descriptorCount              = 1;
      binding.stageFlags                   = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

      bindings.push_back(binding);
   }
   // layout(binding = 2) uniform image2d AccumTarget
   {
      VkDescriptorSetLayoutBinding binding = {};
      binding.binding                      = 2;
      binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      binding.descriptorCount              = 1;
      binding.stageFlags                   = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

      bindings.push_back(binding);
   }
   // layout(binding = 3) uniform buffer RayGenSamples;
   {
      VkDescriptorSetLayoutBinding binding = {};
      binding.binding                      = 3;
      binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      binding.descriptorCount              = 1;
      binding.stageFlags                   = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

      bindings.push_back(binding);
   }
   // layout(binding = 5) uniform buffer SceneParams;
   {
      VkDescriptorSetLayoutBinding binding = {};
      binding.binding                      = 5;
      binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      binding.descriptorCount              = 1;
      binding.stageFlags                   = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

      bindings.push_back(binding);
   }
   // layout(binding = 9) uniform buffer MaterialParameters MaterialParams;	// Material params
   {
      VkDescriptorSetLayoutBinding binding = {};
      binding.binding                      = 9;
      binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      binding.descriptorCount              = 1;
      binding.stageFlags                   = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

      bindings.push_back(binding);
   }
   // layout(binding = 10) uniform sampler2D IBLMapSampler;
   {
      VkDescriptorSetLayoutBinding binding = {};
      binding.binding                      = 10;
      binding.descriptorType               = VK_DESCRIPTOR_TYPE_SAMPLER;
      binding.descriptorCount              = 1;
      binding.stageFlags                   = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

      bindings.push_back(binding);
   }
   // layout(binding = 20) uniform buffer Triangle Triangles[5];	// Index buffer (4 spheres, 1 box)
   {
      VkDescriptorSetLayoutBinding binding = {};
      binding.binding                      = 20;
      binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      binding.descriptorCount              = 5;
      binding.stageFlags                   = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

      bindings.push_back(binding);
   }
   // layout(binding = 25) uniform buffer vec3 Positions[5];	// Position buffer (4 spheres, 1 box)
   {
      VkDescriptorSetLayoutBinding binding = {};
      binding.binding                      = 25;
      binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      binding.descriptorCount              = 5;
      binding.stageFlags                   = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

      bindings.push_back(binding);
   }
   // layout(binding = 30) uniform buffer vec3 Normals[5];	// Normal buffer (4 spheres, 1 box)
   {
      VkDescriptorSetLayoutBinding binding = {};
      binding.binding                      = 30;
      binding.descriptorType               = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      binding.descriptorCount              = 5;
      binding.stageFlags                   = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

      bindings.push_back(binding);
   }
   // layout(binding = 100) uniform image2D IBLEnvironmentMap;
   {
      VkDescriptorSetLayoutBinding binding = {};
      binding.binding                      = 100;
      binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
      binding.descriptorCount              = 1;
      binding.stageFlags                   = VK_SHADER_STAGE_MISS_BIT_KHR;

      bindings.push_back(binding);
   }

   VkDescriptorSetLayoutCreateInfo createInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
   createInfo.flags                           = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
   createInfo.bindingCount                    = CountU32(bindings);
   createInfo.pBindings                       = DataPtr(bindings);

   CHECK_CALL(vkCreateDescriptorSetLayout(pRenderer->Device, &createInfo, nullptr, pLayout));
}

void CreatePipelineLayout(VulkanRenderer* pRenderer, VkDescriptorSetLayout descriptorSetLayout, VkPipelineLayout* pLayout)
{
   VkPipelineLayoutCreateInfo createInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
   createInfo.setLayoutCount             = 1;
   createInfo.pSetLayouts                = &descriptorSetLayout;

   CHECK_CALL(vkCreatePipelineLayout(pRenderer->Device, &createInfo, nullptr, pLayout));
}

void CreateShaderModules(
   VulkanRenderer* pRenderer,
   const std::vector<uint32_t>& spirvRGEN,
   const std::vector<uint32_t>& spirvMISS,
   const std::vector<uint32_t>& spirvCHIT,
   const std::vector<uint32_t>& spirvRINT,
   VkShaderModule* pModuleRGEN,
   VkShaderModule* pModuleMISS,
   VkShaderModule* pModuleCHIT)
{
   // Ray gen
   {
      VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
      createInfo.codeSize                 = SizeInBytes(spirvRGEN);
      createInfo.pCode                    = DataPtr(spirvRGEN);

      CHECK_CALL(vkCreateShaderModule(pRenderer->Device, &createInfo, nullptr, pModuleRGEN));
   }

   // Miss
   {
      VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
      createInfo.codeSize                 = SizeInBytes(spirvMISS);
      createInfo.pCode                    = DataPtr(spirvMISS);

      CHECK_CALL(vkCreateShaderModule(pRenderer->Device, &createInfo, nullptr, pModuleMISS));
   }

   // Closest hit
   {
      VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
      createInfo.codeSize                 = SizeInBytes(spirvCHIT);
      createInfo.pCode                    = DataPtr(spirvCHIT);

      CHECK_CALL(vkCreateShaderModule(pRenderer->Device, &createInfo, nullptr, pModuleCHIT));
   }
}

void CreateRayTracingPipeline(
   VulkanRenderer* pRenderer,
   VkShaderModule   moduleRGEN,
   VkShaderModule   moduleMISS,
   VkShaderModule   moduleCHIT,
   VkPipelineLayout pipelineLayout,
   VkPipeline* pPipeline)
{
   // Shader stages
   std::vector<VkPipelineShaderStageCreateInfo> shaderStages = {};
   // Ray gen
   {
      VkPipelineShaderStageCreateInfo createInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
      createInfo.stage                           = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
      createInfo.module                          = moduleRGEN;
      createInfo.pName                           = "main";

      shaderStages.push_back(createInfo);
   }
   // Miss
   {
      VkPipelineShaderStageCreateInfo createInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
      createInfo.stage                           = VK_SHADER_STAGE_MISS_BIT_KHR;
      createInfo.module                          = moduleMISS;
      createInfo.pName                           = "main";

      shaderStages.push_back(createInfo);
   }
   // Closest hit
   {
      VkPipelineShaderStageCreateInfo createInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
      createInfo.stage                           = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
      createInfo.module                          = moduleCHIT;
      createInfo.pName                           = "main";

      shaderStages.push_back(createInfo);
   }
   // Shader groups
   std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups = {};
   // Ray gen
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
   // Closest hit
   {
      VkRayTracingShaderGroupCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
      createInfo.type                                 = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
      createInfo.generalShader                        = VK_SHADER_UNUSED_KHR;
      createInfo.closestHitShader                     = 2; // shaderStages[2]
      createInfo.anyHitShader                         = VK_SHADER_UNUSED_KHR;
      createInfo.intersectionShader                   = VK_SHADER_UNUSED_KHR;

      shaderGroups.push_back(createInfo);
   }

   VkRayTracingPipelineCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
   createInfo.flags                             = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
   createInfo.stageCount                        = CountU32(shaderStages);
   createInfo.pStages                           = DataPtr(shaderStages);
   createInfo.groupCount                        = CountU32(shaderGroups);
   createInfo.pGroups                           = DataPtr(shaderGroups);
   createInfo.maxPipelineRayRecursionDepth      = 5;
   createInfo.layout                            = pipelineLayout;
   createInfo.basePipelineHandle                = VK_NULL_HANDLE;
   createInfo.basePipelineIndex                 = -1;

   CHECK_CALL(fn_vkCreateRayTracingPipelinesKHR(
      pRenderer->Device, // device
      VK_NULL_HANDLE,    // deferredOperation
      VK_NULL_HANDLE,    // pipelineCache
      1,                 // createInfoCount
      &createInfo,       // pCreateInfos
      nullptr,           // pAllocator
      pPipeline));       // pPipelines

}

void CreateShaderBindingTables(
   VulkanRenderer* pRenderer,
   VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rayTracingProperties,
   VkPipeline pipeline,
   VulkanBuffer* pRayGenSBT,
   VulkanBuffer* pMissSBT,
   VulkanBuffer* pHitGroupSBT)
{
   // Hardcoded group count
   const uint32_t groupCount = 3;

   // Handle sizes
   uint32_t groupHandleSize        = rayTracingProperties.shaderGroupHandleSize;
   uint32_t groupHandleAlignment   = rayTracingProperties.shaderGroupHandleAlignment;
   uint32_t alignedGroupHandleSize = Align(groupHandleSize, groupHandleAlignment);
   uint32_t totalGroupDataSize     = groupCount * groupHandleSize;

   //
   // This is what the shader group handles look like
   // in handlesData based on the pipeline. The offsets
   // are in bytes - assuming alignedHandleSize is 32 bytes.
   //
   //  +--------+
   //  |  RGEN  | offset = 0
   //  +--------+
   //  |  MISS  | offset = 32
   //  +--------+
   //  |  HITG  | offset = 64
   //  +--------+
   //
   std::vector<char> groupHandlesData(totalGroupDataSize);
   CHECK_CALL(fn_vkGetRayTracingShaderGroupHandlesKHR(
      pRenderer->Device,         // device
      pipeline,                  // pipeline
      0,                         // firstGroup
      groupCount,                // groupCount
      totalGroupDataSize,        // dataSize
      groupHandlesData.data())); // pData)

  // Usage flags for SBT buffer
   VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;

   char* pShaderGroupHandleRGEN = groupHandlesData.data();
   char* pShaderGroupHandleMISS = groupHandlesData.data() + groupHandleSize;
   char* pShaderGroupHandleHITG = groupHandlesData.data() + 2 * groupHandleSize;

   //
   // Create buffers for each shader group's SBT and copy the
   // the shader group handles into each buffer.
   //
   // The size of the SBT buffers must be aligned to
   // VkPhysicalDeviceRayTracingPipelinePropertiesKHR::shaderGroupBaseAlignment.
   //
   const uint32_t shaderGroupBaseAlignment = rayTracingProperties.shaderGroupBaseAlignment;
   // Ray gen
   {
      CHECK_CALL(CreateBuffer(
         pRenderer,                // pRenderer
         groupHandleSize,          // srcSize
         pShaderGroupHandleRGEN,   // pSrcData
         usageFlags,               // usageFlags
         shaderGroupBaseAlignment, // minAlignment
         pRayGenSBT));             // pBuffer
   }
   // Miss
   {
      CHECK_CALL(CreateBuffer(
         pRenderer,                // pRenderer
         groupHandleSize,          // srcSize
         pShaderGroupHandleMISS,   // pSrcData
         usageFlags,               // usageFlags
         shaderGroupBaseAlignment, // minAlignment
         pMissSBT));               // pBuffer
   }
   // HITG: closest hit
   {
      CHECK_CALL(CreateBuffer(
         pRenderer,                // pRenderer
         groupHandleSize,          // srcSize
         pShaderGroupHandleHITG,   // pSrcData
         usageFlags,               // usageFlags
         shaderGroupBaseAlignment, // minAlignment
         pHitGroupSBT));           // pBuffer
   }
}

void CreateGlobalRootSig(VulkanRenderer* pRenderer, ID3D12RootSignature** ppRootSig)
{
    // Output range
    D3D12_DESCRIPTOR_RANGE rangeOutput            = {};
    rangeOutput.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rangeOutput.NumDescriptors                    = 3;
    rangeOutput.BaseShaderRegister                = 1;
    rangeOutput.RegisterSpace                     = 0;
    rangeOutput.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // Geometry buffers range
    D3D12_DESCRIPTOR_RANGE rangeGeometryBuffers            = {};
    rangeGeometryBuffers.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeGeometryBuffers.NumDescriptors                    = 15;
    rangeGeometryBuffers.BaseShaderRegister                = 20;
    rangeGeometryBuffers.RegisterSpace                     = 0;
    rangeGeometryBuffers.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // IBLrange
    D3D12_DESCRIPTOR_RANGE rangeIBL            = {};
    rangeIBL.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeIBL.NumDescriptors                    = 1;
    rangeIBL.BaseShaderRegister                = 100;
    rangeIBL.RegisterSpace                     = 0;
    rangeIBL.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[6];
    // Accleration structure (t0)
    rootParameters[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace  = 0;
    rootParameters[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    // Output texture (u1)
    // Accumulation texture (u2)
    // Ray generaiton sampling (u3)
    rootParameters[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges   = &rangeOutput;
    rootParameters[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
    // Scene params (b5)
    rootParameters[2].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[2].Descriptor.ShaderRegister = 5;
    rootParameters[2].Descriptor.RegisterSpace  = 0;
    rootParameters[2].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    //  Index buffers (t20)
    //  Position buffers (t25)
    //  Normal buffers (t30)
    rootParameters[3].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[3].DescriptorTable.pDescriptorRanges   = &rangeGeometryBuffers;
    rootParameters[3].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
    // Environment map (t12)
    rootParameters[4].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[4].DescriptorTable.pDescriptorRanges   = &rangeIBL;
    rootParameters[4].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
    // Material params (t9)
    rootParameters[5].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[5].Descriptor.ShaderRegister = 9;
    rootParameters[5].Descriptor.RegisterSpace  = 0;
    rootParameters[5].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    // IBLMapSampler (s10)
    staticSamplers[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].MipLODBias       = D3D12_DEFAULT_MIP_LOD_BIAS;
    staticSamplers[0].MaxAnisotropy    = 0;
    staticSamplers[0].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[0].MinLOD           = 0;
    staticSamplers[0].MaxLOD           = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister   = 10;
    staticSamplers[0].RegisterSpace    = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters             = 6;
    rootSigDesc.pParameters               = rootParameters;
    rootSigDesc.NumStaticSamplers         = 1;
    rootSigDesc.pStaticSamplers           = staticSamplers;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    HRESULT          hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
    if (FAILED(hr)) {
        std::string errorMsg = std::string(reinterpret_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize());

        std::stringstream ss;
        ss << "\n"
           << "D3D12SerializeRootSignature failed: " << errorMsg << "\n";
        GREX_LOG_ERROR(ss.str().c_str());
        assert(false);
    }
    CHECK_CALL(pRenderer->Device->CreateRootSignature(
        0,                         // nodeMask
        blob->GetBufferPointer(),  // pBloblWithRootSignature
        blob->GetBufferSize(),     // blobLengthInBytes
        IID_PPV_ARGS(ppRootSig))); // riid, ppvRootSignature
}

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
    shaderConfig.MaxPayloadSizeInBytes          = 4 * sizeof(float) + 3 * sizeof(uint32_t); // color, ray depth, sample count, ior
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
    pipelineConfigDesc.MaxTraceRecursionDepth           = 8;

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
void CreateGeometries(
    VulkanRenderer* pRenderer,
    Geometry&       outSphereGeometry,
    Geometry&       outBoxGeometry)
{
   // Sphere
   {
      TriMesh mesh = TriMesh::Sphere(1.0f, 256, 256, { .enableNormals = true });

      Geometry& geo = outSphereGeometry;

      CHECK_CALL(CreateBuffer(
         pRenderer,
         SizeInBytes(mesh.GetTriangles()),
         DataPtr(mesh.GetTriangles()),
         VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
         0,
         &geo.indexBuffer));

      CHECK_CALL(CreateBuffer(
         pRenderer,
         SizeInBytes(mesh.GetPositions()),
         DataPtr(mesh.GetPositions()),
         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
         0,
         &geo.positionBuffer));

      CHECK_CALL(CreateBuffer(
         pRenderer,
         SizeInBytes(mesh.GetNormals()),
         DataPtr(mesh.GetNormals()),
         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
         0,
         &geo.normalBuffer));

      geo.indexCount  = 3 * mesh.GetNumTriangles();
      geo.vertexCount = mesh.GetNumVertices();
   }

   // Box
   {
      TriMesh   mesh = TriMesh::Cube(glm::vec3(15, 1, 4.5f), false, { .enableNormals = true });
      Geometry& geo  = outBoxGeometryy;

      CHECK_CALL(CreateBuffer(
         pRenderer,
         SizeInBytes(mesh.GetTriangles()),
         DataPtr(mesh.GetTriangles()),
         VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
         0,
         &geo.indexBuffer));

      CHECK_CALL(CreateBuffer(
         pRenderer,
         SizeInBytes(mesh.GetPositions()),
         DataPtr(mesh.GetPositions()),
         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
         0,
         &geo.positionBuffer));

      CHECK_CALL(CreateBuffer(
         pRenderer,
         SizeInBytes(mesh.GetNormals()),
         DataPtr(mesh.GetNormals()),
         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
         0,
         &geo.normalBuffer));

      geo.indexCount  = 3 * mesh.GetNumTriangles();
      geo.vertexCount = mesh.GetNumVertices();
   }
}

void CreateBLASes(
   VulkanRenderer* pRenderer,
   const Geometry& sphereGeometry,
   const Geometry& boxGeometry,
   VkAccelerationStructureKHR* pSphereBLAS,
   VkAccelerationStructureKHR* pBoxBLAS)
{
   std::vector<const Geometry*>  geometries = { &sphereGeometry, &boxGeometry };
   std::vector<VkAccelerationStructureKHR*> BLASes     = { pSphereBLAS, pBoxBLAS };

   for (uint32_t i = 0; i < 2; ++i) {
      auto pGeometry = geometries[i];
      auto pBLAS    = BLASes[i];

      VkAccelerationStructureTypeKHR asType= VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

      VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo  = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
      buildGeometryInfo.type                                         = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      buildGeometryInfo.flags                                        = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
      buildGeometryInfo.mode                                         = asType;
      buildGeometryInfo.geometryCount                                = 1;
      buildGeometryInfo.pGeometries                                  = pGeometry;

      VkAccelerationStructureBuildSizesInfoKHR blasSizesInfo         = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
      vkGetAccelerationStructureBuildSizesKHR(
         pRenderer->Device,
         VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
         &buildGeometryInfo,
         {1},
         blasSizesInfo);

      // Scratch buffer
      VulkanBuffer scratchBuffer;
      CHECK_CALL(CreateUAVBuffer(
         pRenderer,
         blasSizesInfo.buildScratchSize,
         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
         0,
         &scratchBuffer));

      // Storage buffer
      VulkanBuffer blasBuffer;
      CHECK_CALL(CreateUAVBuffer(
         pRenderer,
         blasSizesInfo.accelerationStructureSize,
         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
         &blasBuffer));

      VkAccelerationStructureGeometryTrianglesDataKHR trianglesData  = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR };
      trianglesData.vertexFormat                                     = VK_FORMAT_R32G32B32_SFLOAT;
      trianglesData.vertexData                                       = pGeometry->positionBuffer.Buffer
      trianglesData.maxVertex                                        = pGeometry->vertexCount;
      trianglesData.vertexStride                                     = 12;
      trianglesData.indexType                                        = VK_INDEX_TYPE_UINT32;
      trianglesData.indexData                                        = pGeometry->indexBuffer.Buffer;

      VkAccelerationStructureGeometryDataKHR geometryDesc            = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
      geometryDesc.geometryType                                      = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
      geometryDesc.geometry                                          = trianglesData;
      geometryDesc.Flags                                             = VK_GEOMETRY_OPAQUE_BIT_KHR;

      VkAccelerationStructureCreateInfoKHR accelerationStructureInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
      accelerationStructureInfo.buffer                               = pBLAS;
      accelerationStructureInfo.size                                 = blasSizesInfo.accelerationStructureSize;
      accelerationStructureInfo.type                                 = asType;
      accelerationStructureInfo.deviceAddress                        = pRenderer->Device;

      vkCreateAccelerationStructureKHR(
         pRenderer->Device,
         &accelerationStructureInfo,
         nullptr,
         pBLAS);
   }
}

void CreateTLAS(
   VulkanRenderer* pRenderer,
   VkAccelerationStructureKHR *pSphereBLAS,
   VkAccelerationStructureKHR *pBoxBLAS,
   VulkanBuffer *pTLASBuffer,
   VkAccelerationStructureKHR *pTLAS,
   std::vector<MaterialParameters>& outMaterialParams)
{
   // clang-format off
   std::vector<glm::mat3x4> transforms = {
      // Rough plastic sphere
      {{1.0f, 0.0f, 0.0f, -3.75f},
       {0.0f, 1.0f, 0.0f,  2.0f},
       {0.0f, 0.0f, 1.0f,  0.0f}},
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

   std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
   {
      D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
      instanceDesc.InstanceMask                   = 1;
      instanceDesc.AccelerationStructure          = pSphereBLAS->GetGPUVirtualAddress();
      instanceDesc.Flags                          = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;

      uint32_t transformIdx = 0;

      // Rough plastic sphere
      memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
      instanceDescs.push_back(instanceDesc);
      ++transformIdx;

      // Shiny plastic sphere
      memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
      instanceDescs.push_back(instanceDesc);
      ++transformIdx;

      // Glass sphere
      D3D12_RAYTRACING_INSTANCE_DESC thisInstanceDesc = instanceDesc;
      thisInstanceDesc.Flags |= D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE | D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;
      memcpy(thisInstanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
      instanceDescs.push_back(thisInstanceDesc);
      ++transformIdx;

      // Gold sphere
      memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
      instanceDescs.push_back(instanceDesc);
      ++transformIdx;

      // Box
      instanceDesc.AccelerationStructure = pBoxBLAS->GetGPUVirtualAddress();
      memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
      instanceDescs.push_back(instanceDesc);
      ++transformIdx;
   }

   VulkanBuffer instanceBuffer;
   CHECK_CALL(CreateBuffer(pRenderer, SizeInBytes(instanceDescs), DataPtr(instanceDescs), &instanceBuffer));

   // Get acceleration structure build size
   VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
   {
      // Geometry
      VkAccelerationStructureGeometryKHR geometry    = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
      geometry.flags                                 = VK_GEOMETRY_OPAQUE_BIT_KHR;
      geometry.geometryType                          = VK_GEOMETRY_TYPE_INSTANCES_KHR;
      geometry.geometry.triangles.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
      geometry.geometry.instances.arrayOfPointers    = VK_FALSE;
      geometry.geometry.instances.data.deviceAddress = GetDeviceAddress(pRenderer, &instanceBuffer);

      // Build geometry info
      VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
      //
      buildGeometryInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
      buildGeometryInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
      buildGeometryInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
      buildGeometryInfo.geometryCount = 1;
      buildGeometryInfo.pGeometries   = &geometry;

      const uint32_t maxPrimitiveCount = 1;
      fn_vkGetAccelerationStructureBuildSizesKHR(
         pRenderer->Device,
         VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
         &buildGeometryInfo,
         &maxPrimitiveCount,
         &buildSizesInfo);
   }

   // Create acceleration structure buffer
   {
      VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;

      CHECK_CALL(CreateBuffer(
         pRenderer,
         buildSizesInfo.accelerationStructureSize,
         usageFlags,
         VMA_MEMORY_USAGE_GPU_ONLY,
         0,
         pTLASBuffer));
   }

   // Create accleration structure object
   {
      VkAccelerationStructureCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
      createInfo.buffer                               = pTLASBuffer->Buffer;
      createInfo.offset                               = 0;
      createInfo.size                                 = buildSizesInfo.accelerationStructureSize;
      createInfo.type                                 = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
      createInfo.deviceAddress                        = 0;

      CHECK_CALL(fn_vkCreateAccelerationStructureKHR(pRenderer->Device, &createInfo, nullptr, pTLAS));
   }

   // Create scratch buffer
   VulkanBuffer scratchBuffer = {};
   {
      // Get acceleration structure properties
      //
      // Obviously this can be cached if it's accessed frequently.
      //
      VkPhysicalDeviceAccelerationStructurePropertiesKHR accelStructProperties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
      VkPhysicalDeviceProperties2                        properties            = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
      properties.pNext                                                         = &accelStructProperties;
      vkGetPhysicalDeviceProperties2(pRenderer->PhysicalDevice, &properties);

      VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

      CHECK_CALL(CreateBuffer(
         pRenderer,                                                            // pRenderer
         buildSizesInfo.buildScratchSize,                                      // srcSize
         usageFlags,                                                           // usageFlags
         VMA_MEMORY_USAGE_GPU_ONLY,                                            // memoryUsage
         accelStructProperties.minAccelerationStructureScratchOffsetAlignment, // minAlignment
         &scratchBuffer));                                                     // pBuffer
   }

   // Build acceleration structure
   {
      // Geometry
      VkAccelerationStructureGeometryKHR geometry    = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
      geometry.flags                                 = VK_GEOMETRY_OPAQUE_BIT_KHR;
      geometry.geometryType                          = VK_GEOMETRY_TYPE_INSTANCES_KHR;
      geometry.geometry.triangles.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
      geometry.geometry.instances.arrayOfPointers    = VK_FALSE;
      geometry.geometry.instances.data.deviceAddress = GetDeviceAddress(pRenderer, &instanceBuffer);

      // Build geometry info
      VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
      //
      buildGeometryInfo.type                      = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
      buildGeometryInfo.flags                     = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
      buildGeometryInfo.mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
      buildGeometryInfo.dstAccelerationStructure  = *pTLAS;
      buildGeometryInfo.geometryCount             = 1;
      buildGeometryInfo.pGeometries               = &geometry;
      buildGeometryInfo.scratchData.deviceAddress = GetDeviceAddress(pRenderer, &scratchBuffer);

      // Build range info
      VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo = {};
      buildRangeInfo.primitiveCount                           = 1;

      CommandObjects cmdBuf = {};
      CHECK_CALL(CreateCommandBuffer(pRenderer, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, &cmdBuf));

      VkCommandBufferBeginInfo vkbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
      vkbi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

      CHECK_CALL(vkBeginCommandBuffer(cmdBuf.CommandBuffer, &vkbi));

      const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &buildRangeInfo;
      fn_vkCmdBuildAccelerationStructuresKHR(cmdBuf.CommandBuffer, 1, &buildGeometryInfo, &pBuildRangeInfo);

      CHECK_CALL(vkEndCommandBuffer(cmdBuf.CommandBuffer));

      CHECK_CALL(ExecuteCommandBuffer(pRenderer, &cmdBuf));

      if (!WaitForGpu(pRenderer)) {
         assert(false && "WaitForGpu failed");
      }
   }
}

void CreateOutputTexture(VulkanRenderer* pRenderer, VulkanBuffer* pBuffer)
{
}

void CreateAccumTexture(VulkanRenderer* pRenderer, VulkanBuffer* pBuffer)
{
}

void CreateIBLTextures(
   VulkanRenderer* pRenderer,
   VkImage* pBRDFLUT,
   IBLTextures& outIBLTextures)
{
   // BRDF LUT
   {
      auto bitmap = LoadImage32f(GetAssetPath("IBL/brdf_lut.hdr"));
      if (bitmap.Empty()) {
         assert(false && "Load image failed");
         return;
      }

      CHECK_CALL(CreateTexture(
         pRenderer,
         bitmap.GetWidth(),
         bitmap.GetHeight(),
         VK_FORMAT_R32G32B32A32_SFLOAT,
         bitmap.GetSizeInBytes(),
         bitmap.GetPixels(),
         pBRDFLUT));
   }

   // IBL file
   auto iblFile = GetAssetPath("IBL/old_depot_4k.ibl");

   IBLMaps ibl = {};
   if (!LoadIBLMaps32f(iblFile, &ibl)) {
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
         DXGI_FORMAT_R32G32B32A32_FLOAT,
         ibl.irradianceMap.GetSizeInBytes(),
         ibl.irradianceMap.GetPixels(),
         &outIBLTextures.irrTexture));
   }

   // Environment
   {
      const uint32_t pixelStride = ibl.environmentMap.GetPixelStride();
      const uint32_t rowStride   = ibl.environmentMap.GetRowStride();

      std::vector<VkMipOffset> mipOffsets;
      uint32_t                 levelOffset = 0;
      uint32_t                 levelWidth  = ibl.baseWidth;
      uint32_t                 levelHeight = ibl.baseHeight;
      for (uint32_t i = 0; i < ibl.numLevels; ++i) {
         VkMipOffset mipOffset = {};
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
         VK_FORMAT_R32G32B32A32_SFLOAT,
         mipOffsets,
         ibl.environmentMap.GetSizeInBytes(),
         ibl.environmentMap.GetPixels(),
         &outIBLTextures.envTexture));
   }

   GREX_LOG_INFO("Loaded " << iblFile);
}

void CreateDescriptorHeap(VulkanRenderer* pRenderer, ID3D12DescriptorHeap** ppHeap)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors             = 256;
    desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    CHECK_CALL(pRenderer->Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(ppHeap)));
}

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
            {
                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

                uavDesc.Format        = DXGI_FORMAT_B8G8R8A8_UNORM;
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                pRenderer->Device->CreateUnorderedAccessView(pOutputTexture, nullptr, &uavDesc, descriptor);
                descriptor.ptr += descriptorIncSize;

                uavDesc.Format        = DXGI_FORMAT_R32G32B32A32_FLOAT;
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                pRenderer->Device->CreateUnorderedAccessView(pAccumTexture, nullptr, &uavDesc, descriptor);
                descriptor.ptr += descriptorIncSize;
            }

            // Ray generation samples (u3)
            {
                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
                uavDesc.ViewDimension                    = D3D12_UAV_DIMENSION_BUFFER;
                uavDesc.Buffer.FirstElement              = 0;
                uavDesc.Buffer.NumElements               = gWindowWidth * gWindowHeight;
                uavDesc.Buffer.StructureByteStride       = sizeof(uint32_t);
                uavDesc.Buffer.CounterOffsetInBytes      = 0;
                uavDesc.Buffer.Flags                     = D3D12_BUFFER_UAV_FLAG_NONE;

                pRenderer->Device->CreateUnorderedAccessView(pRayGenSamplesBuffer, nullptr, &uavDesc, descriptor);
                descriptor.ptr += descriptorIncSize;
            }
        }

        // Geometry
        {
            const uint32_t kGeometryStride      = 5;
            const uint32_t kNumSpheres          = 4;
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

            // Box
            {
                uint32_t                    i          = 4;
                uint32_t                    offset     = 0;
                D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};

                // Index buffer (t20)
                offset     = kGeoBuffersOffset + (kIndexBufferIndex * kGeometryStride) + i;
                descriptor = {kBaseDescriptor.ptr + offset * kIncrementSize};
                CreateDescriptoBufferSRV(pRenderer, 0, boxGeometry.indexCount / 3, 12, boxGeometry.indexBuffer.Get(), descriptor);

                // Position buffer (t25)
                offset     = kGeoBuffersOffset + (kPositionBufferIndex * kGeometryStride) + i;
                descriptor = {kBaseDescriptor.ptr + offset * kIncrementSize};
                CreateDescriptoBufferSRV(pRenderer, 0, boxGeometry.vertexCount, 4, boxGeometry.positionBuffer.Get(), descriptor);

                // Normal buffer (t30)
                offset     = kGeoBuffersOffset + (kNormalBufferIndex * kGeometryStride) + i;
                descriptor = {kBaseDescriptor.ptr + offset * kIncrementSize};
                CreateDescriptoBufferSRV(pRenderer, 0, boxGeometry.vertexCount, 4, boxGeometry.normalBuffer.Get(), descriptor);
            }
        }

        // IBL Textures
        {
            D3D12_CPU_DESCRIPTOR_HANDLE descriptor = pDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
            descriptor.ptr += kIBLTextureOffset * pRenderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            // Environment map
            CreateDescriptorTexture2D(pRenderer, iblTextures.envTexture.Get(), descriptor, 0, iblTextures.envNumLevels);
            descriptor.ptr += pRenderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }
}