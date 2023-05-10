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
const uint32_t kIBLTextureOffset      = 100;
const uint32_t kMaxIBLs               = 100;
const uint32_t kMaxGeometries         = 75;

// =============================================================================
// Shader code
// =============================================================================
const char* gClearRayGenSamplesShader = R"(

RWTexture2D<float4>      AccumTarget   : register(u0); // Accumulation texture
RWStructuredBuffer<uint> RayGenSamples : register(u1); // Ray generation samples

[numthreads(8, 8, 1)]
void csmain(uint3 tid : SV_DispatchThreadId)
{
    AccumTarget[tid.xy] = float4(0, 0, 0, 0);

    uint idx = tid.y * 1920 + tid.x;
    RayGenSamples[idx] = 0;    
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

static std::vector<std::string> gMaterialNames = {};
static std::vector<std::string> gIBLNames      = {};

static uint32_t gIBLIndex           = 0;
static uint32_t gCurrentIBLIndex    = 0xFFFFFFFF;
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
    mat4     ViewInverseMatrix;
    mat4     ProjectionInverseMatrix;
    mat4     ViewProjectionMatrix;
    vec3     EyePosition;
    uint32_t IBLIndex;
    uint     MaxSamples;
    uint     NumLights;
    Light    Lights[8];
};

struct Geometry
{
    uint32_t               indexCount;
    ComPtr<ID3D12Resource> indexBuffer;
    uint32_t               vertexCount;
    ComPtr<ID3D12Resource> positionBuffer;
    ComPtr<ID3D12Resource> normalBuffer;
};

struct IBLTextures
{
    ComPtr<ID3D12Resource> irrTexture;
    ComPtr<ID3D12Resource> envTexture;
    uint32_t               envNumLevels;
};

struct MaterialParameters
{
    vec3  baseColor;
    float roughness;
    float metallic;
    float specularReflectance;
    float ior;
    vec3  emissionColor;
};

void CreateGlobalRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig);
void CreateRayTracingStateObject(
    DxRenderer*          pRenderer,
    ID3D12RootSignature* pGlobalRootSig,
    size_t               shadeBinarySize,
    const void*          pShaderBinary,
    ID3D12StateObject**  ppStateObject);
void CreateShaderRecordTables(
    DxRenderer*        pRenderer,
    ID3D12StateObject* pStateObject,
    ID3D12Resource**   ppRayGenSRT,
    ID3D12Resource**   ppMissSRT,
    ID3D12Resource**   ppHitGroupSRT);
void CreateGeometries(
    DxRenderer* pRenderer,
    Geometry&   outSphereGeometry,
    Geometry&   outKnobGeometry,
    Geometry&   outMonkeyGeometry,
    Geometry&   outTeapotGeometry,
    Geometry&   outBoxGeometry);
void CreateBLASes(
    DxRenderer*      pRenderer,
    const Geometry&  sphereGeometry,
    const Geometry&  knobGeometry,
    const Geometry&  monkeyGeometry,
    const Geometry&  teapotGeometry,
    const Geometry&  boxGeometry,
    ID3D12Resource** ppSphereBLAS,
    ID3D12Resource** ppKnobBLAS,
    ID3D12Resource** ppMonkeyBLAS,
    ID3D12Resource** ppTeapotBLAS,
    ID3D12Resource** ppBoxBLAS);
void CreateTLAS(
    DxRenderer*                      pRenderer,
    ID3D12Resource*                  pSphereBLAS,
    ID3D12Resource*                  pKnobBLAS,
    ID3D12Resource*                  pMonkeyBLAS,
    ID3D12Resource*                  pTeapotBLAS,
    ID3D12Resource*                  pBoxBLAS,
    ID3D12Resource**                 ppTLAS,
    std::vector<MaterialParameters>& outMaterialParams);
void CreateOutputTexture(DxRenderer* pRenderer, ID3D12Resource** ppBuffer);
void CreateAccumTexture(DxRenderer* pRenderer, ID3D12Resource** ppBuffer);
void CreateIBLTextures(
    DxRenderer*               pRenderer,
    std::vector<IBLTextures>& outIBLTextures);
void CreateDescriptorHeap(DxRenderer* pRenderer, ID3D12DescriptorHeap** ppHeap);
void WriteDescriptors(
    DxRenderer*                     pRenderer,
    ID3D12DescriptorHeap*           pDescriptorHeap,
    ID3D12Resource*                 pOutputTexture,
    ID3D12Resource*                 pAccumTexture,
    ID3D12Resource*                 pRayGenSamplesBuffer,
    const Geometry&                 sphereGeometry,
    const Geometry&                 knobGeometry,
    const Geometry&                 monkeyGeometry,
    const Geometry&                 teapotGeometry,
    const Geometry&                 boxGeometry,
    const std::vector<IBLTextures>& iblTextures);

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

// =============================================================================
// main()
// =============================================================================
int main(int argc, char** argv)
{
    std::unique_ptr<DxRenderer> renderer = std::make_unique<DxRenderer>();

    if (!InitDx(renderer.get(), gEnableDebug)) {
        return EXIT_FAILURE;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    //
    CHECK_CALL(renderer->Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)));

    bool isRayTracingSupported = (options5.RaytracingTier == D3D12_RAYTRACING_TIER_1_1);
    if (!isRayTracingSupported) {
        assert(false && "Required ray tracing tier not supported");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Compile shaders
    // *************************************************************************
    std::vector<char> rayTraceDxil;
    {
        auto source = LoadString("projects/033_034_raytracing_path_trace_pbr/shaders.hlsl");
        assert((!source.empty()) && "no shader source!");

        std::string errorMsg;
        HRESULT     hr = CompileHLSL(source, "", "lib_6_5", &rayTraceDxil, &errorMsg);
        if (FAILED(hr)) {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (raytracing): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }
    }

    std::vector<char> clearRayGenDxil;
    {
        std::string errorMsg;
        HRESULT     hr = CompileHLSL(gClearRayGenSamplesShader, "csmain", "cs_6_5", &clearRayGenDxil, &errorMsg);
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
    // Global root signature
    //
    // This is a root signature that is shared across all raytracing shaders
    // invoked during a DispatchRays() call.
    //
    // *************************************************************************
    ComPtr<ID3D12RootSignature> globalRootSig;
    CreateGlobalRootSig(renderer.get(), &globalRootSig);

    // *************************************************************************
    // Ray tracing pipeline state object
    // *************************************************************************
    ComPtr<ID3D12StateObject> stateObject;
    CreateRayTracingStateObject(
        renderer.get(),
        globalRootSig.Get(),
        rayTraceDxil.size(),
        rayTraceDxil.data(),
        &stateObject);

    // *************************************************************************
    // Shader record tables
    // *************************************************************************
    ComPtr<ID3D12Resource> rgenSRT;
    ComPtr<ID3D12Resource> missSRT;
    ComPtr<ID3D12Resource> hitgSRT;
    CreateShaderRecordTables(
        renderer.get(),
        stateObject.Get(),
        &rgenSRT,
        &missSRT,
        &hitgSRT);

    // *************************************************************************
    // Clear ray gen pipeline
    // *************************************************************************
    ComPtr<ID3D12RootSignature> clearRayGenRootSig;
    ComPtr<ID3D12PipelineState> clearRayGenPSO;
    {
        D3D12_DESCRIPTOR_RANGE range            = {};
        range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        range.NumDescriptors                    = 2;
        range.BaseShaderRegister                = 0;
        range.RegisterSpace                     = 0;
        range.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER rootParameters[1];
        rootParameters[0].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[0].DescriptorTable.pDescriptorRanges   = &range;
        rootParameters[0].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
        rootSigDesc.NumParameters             = 1;
        rootSigDesc.pParameters               = rootParameters;

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
        CHECK_CALL(renderer->Device->CreateRootSignature(
            0,                                   // nodeMask
            blob->GetBufferPointer(),            // pBloblWithRootSignature
            blob->GetBufferSize(),               // blobLengthInBytes
            IID_PPV_ARGS(&clearRayGenRootSig))); // riid, ppvRootSignature

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature                    = clearRayGenRootSig.Get();
        psoDesc.CS                                = {clearRayGenDxil.data(), clearRayGenDxil.size()};
        CHECK_CALL(renderer->Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&clearRayGenPSO)));
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
        knobGeometry,
        monkeyGeometry,
        teapotGeometry,
        boxGeometry);

    // *************************************************************************
    // Bottom level acceleration structure
    // *************************************************************************
    ComPtr<ID3D12Resource> sphereBLAS;
    ComPtr<ID3D12Resource> knobBLAS;
    ComPtr<ID3D12Resource> monkeyBLAS;
    ComPtr<ID3D12Resource> teapotBLAS;
    ComPtr<ID3D12Resource> boxBLAS;
    CreateBLASes(
        renderer.get(),
        sphereGeometry,
        knobGeometry,
        monkeyGeometry,
        teapotGeometry,
        boxGeometry,
        &sphereBLAS,
        &knobBLAS,
        &monkeyBLAS,
        &teapotBLAS,
        &boxBLAS);

    // *************************************************************************
    // Top level acceleration structure
    // *************************************************************************
    ComPtr<ID3D12Resource>          tlasBuffer;
    std::vector<MaterialParameters> materialParams;
    CreateTLAS(
        renderer.get(),
        sphereBLAS.Get(),
        knobBLAS.Get(),
        monkeyBLAS.Get(),
        teapotBLAS.Get(),
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
    // IBL textures
    // *************************************************************************
    std::vector<IBLTextures> iblTextures = {};
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
        knobGeometry,
        monkeyGeometry,
        teapotGeometry,
        boxGeometry,
        iblTextures);

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, "033_raytracing_path_trace_pbr_d3d12");
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
    // Misc vars
    // *************************************************************************
    uint32_t sampleCount     = 0;
    float    rayGenStartTime = 0;

    // *************************************************************************
    // Main loop
    // *************************************************************************
    while (window->PollEvents()) {
        window->ImGuiNewFrameD3D12();

        if (ImGui::Begin("Scene")) {
            ImGui::SliderInt("Max Samples Per Pixel", reinterpret_cast<int*>(&gMaxSamples), 1, 16384);

            static const char* currentIBLName = gIBLNames[0].c_str();
            if (ImGui::BeginCombo("IBL", currentIBLName)) {
                for (size_t i = 0; i < gIBLNames.size(); ++i) {
                    bool isSelected = (currentIBLName == gIBLNames[i]);
                    if (ImGui::Selectable(gIBLNames[i].c_str(), isSelected)) {
                        currentIBLName = gIBLNames[i].c_str();
                        gIBLIndex      = static_cast<uint32_t>(i);
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();

            float progress = sampleCount / static_cast<float>(gMaxSamples);
            char  buf[256] = {};
            sprintf(buf, "%d/%d Samples", sampleCount, gMaxSamples);
            ImGui::ProgressBar(progress, ImVec2(-1, 0), buf);

            ImGui::Separator();

            float currentTime = static_cast<float>(glfwGetTime());
            float elapsedTime = currentTime - rayGenStartTime;

            ImGui::Text("Render time: %0.3f seconds", elapsedTime);
        }
        ImGui::End();

        // ---------------------------------------------------------------------

        CHECK_CALL(commandAllocator->Reset());
        CHECK_CALL(commandList->Reset(commandAllocator.Get(), nullptr));

        if (gCurrentMaxSamples != gMaxSamples) {
            gCurrentMaxSamples  = gMaxSamples;
            gResetRayGenSamples = true;
        }

        if (gCurrentIBLIndex != gIBLIndex) {
            gCurrentIBLIndex    = gIBLIndex;
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
        vec3 startingEyePosition = vec3(0, 4.0f, 8.5f);
        vec3 eyePosition         = transformEyeMat * vec4(startingEyePosition, 1);
        mat4 viewMat             = glm::lookAt(eyePosition, vec3(0, 3, 0), vec3(0, 1, 0));
        mat4 projMat             = glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 10000.0f);

        // Set constant buffer values
        pSceneParams->ViewInverseMatrix       = glm::inverse(viewMat);
        pSceneParams->ProjectionInverseMatrix = glm::inverse(projMat);
        pSceneParams->IBLIndex                = gCurrentIBLIndex;
        pSceneParams->EyePosition             = eyePosition;
        pSceneParams->MaxSamples              = gCurrentMaxSamples;

        // Reset ray gen samples
        if (gResetRayGenSamples) {
            sampleCount = 0;
            rayGenStartTime = static_cast<float>(glfwGetTime());

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
            D3D12_GPU_DESCRIPTOR_HANDLE descriptorTable = {descriptorHeapStart.ptr + kOutputResourcesOffset * descriptorIncSize};
            commandList->SetComputeRootDescriptorTable(1, descriptorHeap->GetGPUDescriptorHandleForHeapStart());
            // Scene params (b5)
            commandList->SetComputeRootConstantBufferView(2, sceneParamsBuffer->GetGPUVirtualAddress());
            //  Index buffer (t20)
            //  Position buffer (t45)
            //  Normal buffer (t70)
            descriptorTable = {descriptorHeapStart.ptr + kGeoBuffersOffset * descriptorIncSize};
            commandList->SetComputeRootDescriptorTable(3, descriptorTable);
            // Environment map (t100)
            descriptorTable = {descriptorHeapStart.ptr + kIBLTextureOffset * descriptorIncSize};
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
                    D3D12_VIEWPORT viewport = {0, 0, static_cast<float>(gWindowWidth), static_cast<float>(gWindowHeight), 0, 1};
                    commandList->RSSetViewports(1, &viewport);
                    D3D12_RECT scissor = {0, 0, static_cast<long>(gWindowWidth), static_cast<long>(gWindowHeight)};
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

        // Update sample count
        if (sampleCount < gMaxSamples) {
            ++sampleCount;
        }

        if (!SwapchainPresent(renderer.get())) {
            assert(false && "SwapchainPresent failed");
            break;
        }
    }

    return 0;
}

void CreateGlobalRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig)
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
    rangeGeometryBuffers.NumDescriptors                    = kMaxGeometries;
    rangeGeometryBuffers.BaseShaderRegister                = 20;
    rangeGeometryBuffers.RegisterSpace                     = 0;
    rangeGeometryBuffers.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // IBLrange
    D3D12_DESCRIPTOR_RANGE rangeIBL            = {};
    rangeIBL.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeIBL.NumDescriptors                    = kMaxIBLs;
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
    //  Position buffers (t45)
    //  Normal buffers (t70)
    rootParameters[3].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[3].DescriptorTable.pDescriptorRanges   = &rangeGeometryBuffers;
    rootParameters[3].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
    // Environment map (t100)
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
    DxRenderer*          pRenderer,
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

void CreateShaderRecordTables(
    DxRenderer*        pRenderer,
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
    DxRenderer* pRenderer,
    Geometry&   outSphereGeometry,
    Geometry&   outKnobGeometry,
    Geometry&   outMonkeyGeometry,
    Geometry&   outTeapotGeometry,
    Geometry&   outBoxGeometryy)
{
    // Sphere
    {
        TriMesh mesh = TriMesh::Sphere(1.0f, 256, 256, {.enableNormals = true});

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

    // Knob
    {
        TriMesh::Options options  = {.enableNormals = true};
        options.applyTransform    = true;
        options.transformRotate.y = glm::radians(180.0f);

        TriMesh mesh;
        bool    res = TriMesh::LoadOBJ(GetAssetPath("models/material_knob.obj").string(), "", options, &mesh);
        if (!res) {
            assert(false && "failed to load model");
        }
        mesh.ScaleToFit(1.25f);

        Geometry& geo = outKnobGeometry;

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

    // Monkey
    {
        TriMesh::Options options = {.enableNormals = true};

        TriMesh mesh;
        bool    res = TriMesh::LoadOBJ(GetAssetPath("models/monkey_lowres.obj").string(), "", options, &mesh);
        if (!res) {
            assert(false && "failed to load model");
        }
        mesh.ScaleToFit(1.20f);

        Geometry& geo = outMonkeyGeometry;

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

    // Teapot
    {
        TriMesh::Options options  = {.enableNormals = true};
        options.applyTransform    = true;
        options.transformRotate.y = glm::radians(160.0f);

        TriMesh mesh;
        bool    res = TriMesh::LoadOBJ(GetAssetPath("models/teapot.obj").string(), "", options, &mesh);
        if (!res) {
            assert(false && "failed to load model");
        }
        mesh.ScaleToFit(1.5f);

        Geometry& geo = outTeapotGeometry;

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
        TriMesh::Options options = {.enableNormals = true};

        TriMesh mesh;
        bool    res = TriMesh::LoadOBJ(GetAssetPath("models/shelf.obj").string(), "", options, &mesh);
        if (!res) {
            assert(false && "failed to load model");
        }

        Geometry& geo = outBoxGeometryy;

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

void CreateBLASes(
    DxRenderer*      pRenderer,
    const Geometry&  sphereGeometry,
    const Geometry&  knobGeometry,
    const Geometry&  monkeyGeometry,
    const Geometry&  teapotGeometry,
    const Geometry&  boxGeometry,
    ID3D12Resource** ppSphereBLAS,
    ID3D12Resource** ppKnobBLAS,
    ID3D12Resource** ppMonkeyBLAS,
    ID3D12Resource** ppTeapotBLAS,
    ID3D12Resource** ppBoxBLAS)
{
    std::vector<const Geometry*>  geometries = {&sphereGeometry, &knobGeometry, &monkeyGeometry, &teapotGeometry, &boxGeometry};
    std::vector<ID3D12Resource**> BLASes     = {ppSphereBLAS, ppKnobBLAS, ppMonkeyBLAS, ppTeapotBLAS, ppBoxBLAS};

    uint32_t n = static_cast<uint32_t>(geometries.size());
    for (uint32_t i = 0; i < n; ++i) {
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
        geometryDesc.Flags                                = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

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

void CreateTLAS(
    DxRenderer*                      pRenderer,
    ID3D12Resource*                  pSphereBLAS,
    ID3D12Resource*                  pKnobBLAS,
    ID3D12Resource*                  pMonkeyBLAS,
    ID3D12Resource*                  pTeapotBLAS,
    ID3D12Resource*                  pBoxBLAS,
    ID3D12Resource**                 ppTLAS,
    std::vector<MaterialParameters>& outMaterialParams)
{
    // clang-format off
     std::vector<glm::mat3x4> transforms = {
         // Rough plastic sphere
         {{ 1.0f, 0.0f, 0.0f, 1.25f},
          { 0.0f, 1.0f, 0.0f, 4.0f},
          { 0.0f, 0.0f, 1.0f, 1.5f}},
         // Shiny plastic sphere 
         {{-1.0f, 0.0f,  0.0f, -1.25f},
          { 0.0f, 1.0f,  0.0f,  1.0f},
          { 0.0f, 0.0f, -1.0f, -1.5f}},
         // Crystal sphere
         {{1.0f, 0.0f, 0.0f,  3.75f},
          {0.0f, 1.0f, 0.0f,  1.0f},
          {0.0f, 0.0f, 1.0f,  1.5f}},
         // Metal sphere
         {{-1.0f, 0.0f,  0.0f,  3.75f},
          { 0.0f, 1.0f,  0.0f,  4.0f},
          { 0.0f, 0.0f, -1.0f, -1.5f}},

         // Rough plastic knob
         {{-1.0f, 0.0f,  0.0f,  3.75f},
          { 0.0f, 1.0f,  0.0f,  0.96f},
          { 0.0f, 0.0f, -1.0f, -1.5f}},
         // Shiny plastic knob 
         {{-1.0f, 0.0f,  0.0f, -3.75f},
          { 0.0f, 1.0f,  0.0f,  3.96f},
          { 0.0f, 0.0f, -1.0f, -1.5f}},
         // Glass knob
         {{1.0f, 0.0f, 0.0f, -3.75f},
          {0.0f, 1.0f, 0.0f,  3.96f},
          {0.0f, 0.0f, 1.0f,  1.5f}},
         // Metal knob
         {{1.0f, 0.0f, 0.0f, -1.25f},
          {0.0f, 1.0f, 0.0f,  0.96f},
          {0.0f, 0.0f, 1.0f,  1.5f}},

         // Rough plastic monkey
         {{-1.0f, 0.0f,  0.0f,  1.25f},
          { 0.0f, 1.0f,  0.0f,  3.96f},
          { 0.0f, 0.0f, -1.0f, -1.5f}},
         // Shiny plastic monkey 
         {{1.0f, 0.0f, 0.0f,  1.25f},
          {0.0f, 1.0f, 0.0f,  0.96f},
          {0.0f, 0.0f, 1.0f,  1.5f}},
         // Diamond monkey
         {{-1.0f, 0.0f,  0.0f, -3.75f},
          { 0.0f, 1.0f,  0.0f,  0.96f},
          { 0.0f, 0.0f, -1.0f, -1.5f}},
         // Metal monkey
         {{ 1.0f, 0.0f,  0.0f,  3.75f},
          { 0.0f, 1.0f,  0.0f,  3.96f},
          { 0.0f, 0.0f,  1.0f,  1.5f}},

         // Rough plastic teapot
         {{ 1.0f, 0.0f,  0.0f, -3.75f},
          { 0.0f, 1.0f,  0.0f,  0.001f},
          { 0.0f, 0.0f,  1.0f,  1.35f}},
         // Shiny plastic teapot 
         {{1.0f, 0.0f, 0.0f, -1.25f},
          {0.0f, 1.0f, 0.0f,  3.001f},
          {0.0f, 0.0f, 1.0f,  1.35f}},
         // Glass teapot
         {{-1.0f, 0.0f,  0.0f, -1.25f},
          { 0.0f, 1.0f,  0.0f,  3.001f},
          { 0.0f, 0.0f, -1.0f, -1.35f}},
         // Metal teapot
         {{-1.0f, 0.0f,  0.0f,  1.25f},
          { 0.0f, 1.0f,  0.0f,  0.001f},
          { 0.0f, 0.0f, -1.0f, -1.35f}},

         // Box
         {{1.0f, 0.0f, 0.0f,  0.0f},
          {0.0f, 1.0f, 0.0f,  0.0f},
          {0.0f, 0.0f, 1.0f,  0.0f}},
     };
    // clang-format on

    // Material params
    {
        // ---------------------------------------------------------------------
        // Spheres
        // ---------------------------------------------------------------------

        // Rough plastic
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(0.0f, 1.0f, 1.0f);
            materialParams.roughness           = 1.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // Shiny plastic
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(0.07f, 0.05f, 0.1f);
            materialParams.roughness           = 0.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 1.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // Crystal
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = F0_DiletricCrystal;
            materialParams.roughness           = 0;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.5f;
            materialParams.ior                 = 2.0f;

            outMaterialParams.push_back(materialParams);
        }

        // Metal with a bit of roughness
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = F0_MetalChromium;
            materialParams.roughness           = 0.25f;
            materialParams.metallic            = 1;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // ---------------------------------------------------------------------
        // Knob
        // ---------------------------------------------------------------------

        // Rough plastic
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(1.0f, 0.0f, 1.0f);
            materialParams.roughness           = 1.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // Shiny plastic
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(1.25f, 0.07f, 0.05f);
            materialParams.roughness           = 0.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 1.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // Glass
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(1, 1, 1);
            materialParams.roughness           = 0;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.5f;
            materialParams.ior                 = 1.5f;

            outMaterialParams.push_back(materialParams);
        }

        // Metal with a bit of roughness
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = F0_MetalGold;
            materialParams.roughness           = 0.25f;
            materialParams.metallic            = 1;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // ---------------------------------------------------------------------
        // Monkey
        // ---------------------------------------------------------------------

        // Rough plastic
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(1.0f, 1.0f, 0.2f);
            materialParams.roughness           = 1.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // Shiny plastic
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(0.2f, 1.0f, 0.2f);
            materialParams.roughness           = 0.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 1.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // Diamond
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = F0_DiletricDiamond + vec3(0, 0, 0.25f);
            materialParams.roughness           = 0;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.5f;
            materialParams.ior                 = 2.418f;

            outMaterialParams.push_back(materialParams);
        }

        // Metal
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = F0_MetalSilver;
            materialParams.roughness           = 0.0f;
            materialParams.metallic            = 1;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // ---------------------------------------------------------------------
        // Teapot
        // ---------------------------------------------------------------------

        // Rough plastic
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(1.0f, 1.0f, 1.0f);
            materialParams.roughness           = 1.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;
            materialParams.emissionColor       = vec3(1.0f, 1.0f, 1.0f);

            outMaterialParams.push_back(materialParams);
        }

        // Shiny plastic
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = 2.0f * vec3(1.0f, 0.35f, 0.05f);
            materialParams.roughness           = 0.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 1.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // Glass
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(1, 1, 1);
            materialParams.roughness           = 0.25f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.5f;
            materialParams.ior                 = 1.5f;

            outMaterialParams.push_back(materialParams);
        }

        // Metal with a bit of roughness
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = F0_MetalCopper;
            materialParams.roughness           = 0.45f;
            materialParams.metallic            = 1;
            materialParams.specularReflectance = 0.0f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }

        // ---------------------------------------------------------------------
        // Box
        // ---------------------------------------------------------------------

        // Box
        {
            MaterialParameters materialParams  = {};
            materialParams.baseColor           = vec3(0.35f, 0.36f, 0.36f);
            materialParams.roughness           = 1.0f;
            materialParams.metallic            = 0;
            materialParams.specularReflectance = 0.2f;
            materialParams.ior                 = 0;

            outMaterialParams.push_back(materialParams);
        }
    }

    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
    {
        D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
        instanceDesc.InstanceMask                   = 1;
        instanceDesc.Flags                          = D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;

        uint32_t transformIdx = 0;

        // ---------------------------------------------------------------------
        // Sphere
        // ---------------------------------------------------------------------
        instanceDesc.AccelerationStructure = pSphereBLAS->GetGPUVirtualAddress();

        // Rough plastic sphere
        memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(instanceDesc);
        ++transformIdx;

        // Shiny plastic sphere
        memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(instanceDesc);
        ++transformIdx;

        // Crystal sphere
        D3D12_RAYTRACING_INSTANCE_DESC thisInstanceDesc = instanceDesc;
        thisInstanceDesc.Flags                          = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE | D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;
        memcpy(thisInstanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(thisInstanceDesc);
        ++transformIdx;

        // Metal sphere
        memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(instanceDesc);
        ++transformIdx;

        // ---------------------------------------------------------------------
        // Knob
        // ---------------------------------------------------------------------
        instanceDesc.AccelerationStructure = pKnobBLAS->GetGPUVirtualAddress();

        // Rough plastic knob
        memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(instanceDesc);
        ++transformIdx;

        // Shiny plastic knob
        memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(instanceDesc);
        ++transformIdx;

        // Glass knob
        thisInstanceDesc       = instanceDesc;
        thisInstanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE | D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;
        memcpy(thisInstanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(thisInstanceDesc);
        ++transformIdx;

        // Metal knob
        memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(instanceDesc);
        ++transformIdx;

        // ---------------------------------------------------------------------
        // Monkey
        // ---------------------------------------------------------------------
        instanceDesc.AccelerationStructure = pMonkeyBLAS->GetGPUVirtualAddress();

        // Rough plastic monkey
        memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(instanceDesc);
        ++transformIdx;

        // Shiny plastic monkey
        memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(instanceDesc);
        ++transformIdx;

        // Diamond monkey
        thisInstanceDesc       = instanceDesc;
        thisInstanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE | D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;
        memcpy(thisInstanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(thisInstanceDesc);
        ++transformIdx;

        // Metal monkey
        memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(instanceDesc);
        ++transformIdx;

        // ---------------------------------------------------------------------
        // Teapot
        // ---------------------------------------------------------------------
        instanceDesc.AccelerationStructure = pTeapotBLAS->GetGPUVirtualAddress();

        // Rough plastic teapot
        memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(instanceDesc);
        ++transformIdx;

        // Shiny plastic teapot
        memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(instanceDesc);
        ++transformIdx;

        // Glass teapot
        thisInstanceDesc       = instanceDesc;
        thisInstanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE | D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;
        memcpy(thisInstanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(thisInstanceDesc);
        ++transformIdx;

        // Metal teapot
        memcpy(instanceDesc.Transform, &transforms[transformIdx], sizeof(glm::mat3x4));
        instanceDescs.push_back(instanceDesc);
        ++transformIdx;

        // ---------------------------------------------------------------------
        // Box
        // ---------------------------------------------------------------------
        instanceDesc.AccelerationStructure = pMonkeyBLAS->GetGPUVirtualAddress();

        // Box
        instanceDesc.AccelerationStructure = pBoxBLAS->GetGPUVirtualAddress();
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

void CreateOutputTexture(DxRenderer* pRenderer, ID3D12Resource** ppBuffer)
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

void CreateAccumTexture(DxRenderer* pRenderer, ID3D12Resource** ppBuffer)
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

void CreateIBLTextures(
    DxRenderer*               pRenderer,
    std::vector<IBLTextures>& outIBLTextures)
{
    std::vector<std::filesystem::path> iblFiles;
    {
        auto iblDirs = GetEveryAssetPath("IBL");
        for (auto& dir : iblDirs) {
            for (auto& entry : std::filesystem::directory_iterator(dir)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                auto path = entry.path();
                auto ext  = path.extension();
                if (ext == ".ibl") {
                    path = std::filesystem::relative(path, dir.parent_path());
                    iblFiles.push_back(path);
                }
            }
        }
    }

    size_t maxEntries = std::min<size_t>(kMaxIBLs, iblFiles.size());
    for (size_t i = 0; i < maxEntries; ++i) {
        std::filesystem::path iblFile = iblFiles[i];

        IBLMaps ibl = {};
        if (!LoadIBLMaps32f(iblFile, &ibl)) {
            GREX_LOG_ERROR("failed to load: " << iblFile);
            return;
        }

        IBLTextures iblTexture = {};

        iblTexture.envNumLevels = ibl.numLevels;

        // Environment
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

            ComPtr<ID3D12Resource> texture;
            CHECK_CALL(CreateTexture(
                pRenderer,
                ibl.baseWidth,
                ibl.baseHeight,
                DXGI_FORMAT_R32G32B32A32_FLOAT,
                mipOffsets,
                ibl.environmentMap.GetSizeInBytes(),
                ibl.environmentMap.GetPixels(),
                &texture));
            iblTexture.envTexture = texture;

            outIBLTextures.push_back(iblTexture);
        }

        gIBLNames.push_back(iblFile.filename().replace_extension().string());

        GREX_LOG_INFO("Loaded " << iblFile);
    }
}

void CreateDescriptorHeap(DxRenderer* pRenderer, ID3D12DescriptorHeap** ppHeap)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors             = 256;
    desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    CHECK_CALL(pRenderer->Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(ppHeap)));
}

void WriteDescriptors(
    DxRenderer*                     pRenderer,
    ID3D12DescriptorHeap*           pDescriptorHeap,
    ID3D12Resource*                 pOutputTexture,
    ID3D12Resource*                 pAccumTexture,
    ID3D12Resource*                 pRayGenSamplesBuffer,
    const Geometry&                 sphereGeometry,
    const Geometry&                 knobGeometry,
    const Geometry&                 monkeyGeometry,
    const Geometry&                 teapotGeometry,
    const Geometry&                 boxGeometry,
    const std::vector<IBLTextures>& iblTextures)
{
    const D3D12_CPU_DESCRIPTOR_HANDLE kBaseDescriptor = pDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    const UINT                        kIncrementSize  = pRenderer->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Outputs resources
    {
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor = pDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        descriptor.ptr += kOutputResourcesOffset * kIncrementSize;

        // Output texture (u1)
        // Accumulation texture (u2)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

            uavDesc.Format        = DXGI_FORMAT_B8G8R8A8_UNORM;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            pRenderer->Device->CreateUnorderedAccessView(pOutputTexture, nullptr, &uavDesc, descriptor);
            descriptor.ptr += kIncrementSize;

            uavDesc.Format        = DXGI_FORMAT_R32G32B32A32_FLOAT;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            pRenderer->Device->CreateUnorderedAccessView(pAccumTexture, nullptr, &uavDesc, descriptor);
            descriptor.ptr += kIncrementSize;
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
            descriptor.ptr += kIncrementSize;
        }
    }

    // Geometry
    {
        const uint32_t kBuffersStride       = 25;
        const uint32_t kNumInstances        = 4;
        const uint32_t kIndexBufferIndex    = 0;
        const uint32_t kPositionBufferIndex = 1;
        const uint32_t kNormalBufferIndex   = 2;

        uint32_t indexBufferOffset    = kGeoBuffersOffset + kIndexBufferIndex * kBuffersStride;
        uint32_t positionBufferOffset = kGeoBuffersOffset + kPositionBufferIndex * kBuffersStride;
        uint32_t normalBufferOffset   = kGeoBuffersOffset + kNormalBufferIndex * kBuffersStride;

        // Spheres
        for (uint32_t i = 0; i < kNumInstances; ++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};

            // Index buffer (t20)
            descriptor = {kBaseDescriptor.ptr + indexBufferOffset * kIncrementSize};
            CreateDescriptoBufferSRV(pRenderer, 0, sphereGeometry.indexCount / 3, 12, sphereGeometry.indexBuffer.Get(), descriptor);
            ++indexBufferOffset;

            // Position buffer (t45)
            descriptor = {kBaseDescriptor.ptr + positionBufferOffset * kIncrementSize};
            CreateDescriptoBufferSRV(pRenderer, 0, sphereGeometry.vertexCount, 4, sphereGeometry.positionBuffer.Get(), descriptor);
            ++positionBufferOffset;

            // Normal buffer (t70)
            descriptor = {kBaseDescriptor.ptr + normalBufferOffset * kIncrementSize};
            CreateDescriptoBufferSRV(pRenderer, 0, sphereGeometry.vertexCount, 4, sphereGeometry.normalBuffer.Get(), descriptor);
            ++normalBufferOffset;
        }

        // Knob
        for (uint32_t i = 0; i < kNumInstances; ++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};

            // Index buffer (t20)
            descriptor = {kBaseDescriptor.ptr + indexBufferOffset * kIncrementSize};
            CreateDescriptoBufferSRV(pRenderer, 0, knobGeometry.indexCount / 3, 12, knobGeometry.indexBuffer.Get(), descriptor);
            ++indexBufferOffset;

            // Position buffer (t45)
            descriptor = {kBaseDescriptor.ptr + positionBufferOffset * kIncrementSize};
            CreateDescriptoBufferSRV(pRenderer, 0, knobGeometry.vertexCount, 4, knobGeometry.positionBuffer.Get(), descriptor);
            ++positionBufferOffset;

            // Normal buffer (t70)
            descriptor = {kBaseDescriptor.ptr + normalBufferOffset * kIncrementSize};
            CreateDescriptoBufferSRV(pRenderer, 0, knobGeometry.vertexCount, 4, knobGeometry.normalBuffer.Get(), descriptor);
            ++normalBufferOffset;
        }

        // Monkey
        for (uint32_t i = 0; i < kNumInstances; ++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};

            // Index buffer (t20)
            descriptor = {kBaseDescriptor.ptr + indexBufferOffset * kIncrementSize};
            CreateDescriptoBufferSRV(pRenderer, 0, monkeyGeometry.indexCount / 3, 12, monkeyGeometry.indexBuffer.Get(), descriptor);
            ++indexBufferOffset;

            // Position buffer (t45)
            descriptor = {kBaseDescriptor.ptr + positionBufferOffset * kIncrementSize};
            CreateDescriptoBufferSRV(pRenderer, 0, monkeyGeometry.vertexCount, 4, monkeyGeometry.positionBuffer.Get(), descriptor);
            ++positionBufferOffset;

            // Normal buffer (t70)
            descriptor = {kBaseDescriptor.ptr + normalBufferOffset * kIncrementSize};
            CreateDescriptoBufferSRV(pRenderer, 0, monkeyGeometry.vertexCount, 4, monkeyGeometry.normalBuffer.Get(), descriptor);
            ++normalBufferOffset;
        }

        // Teapot
        for (uint32_t i = 0; i < kNumInstances; ++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};

            // Index buffer (t20)
            descriptor = {kBaseDescriptor.ptr + indexBufferOffset * kIncrementSize};
            CreateDescriptoBufferSRV(pRenderer, 0, teapotGeometry.indexCount / 3, 12, teapotGeometry.indexBuffer.Get(), descriptor);
            ++indexBufferOffset;

            // Position buffer (t45)
            descriptor = {kBaseDescriptor.ptr + positionBufferOffset * kIncrementSize};
            CreateDescriptoBufferSRV(pRenderer, 0, teapotGeometry.vertexCount, 4, teapotGeometry.positionBuffer.Get(), descriptor);
            ++positionBufferOffset;

            // Normal buffer (t70)
            descriptor = {kBaseDescriptor.ptr + normalBufferOffset * kIncrementSize};
            CreateDescriptoBufferSRV(pRenderer, 0, teapotGeometry.vertexCount, 4, teapotGeometry.normalBuffer.Get(), descriptor);
            ++normalBufferOffset;
        }

        // Box
        uint32_t instanceStride = 0 * kNumInstances;
        {
            D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};

            // Index buffer (t20)
            descriptor = {kBaseDescriptor.ptr + indexBufferOffset * kIncrementSize};
            CreateDescriptoBufferSRV(pRenderer, 0, boxGeometry.indexCount / 3, 12, boxGeometry.indexBuffer.Get(), descriptor);
            ++indexBufferOffset;

            // Position buffer (t45)
            descriptor = {kBaseDescriptor.ptr + positionBufferOffset * kIncrementSize};
            CreateDescriptoBufferSRV(pRenderer, 0, boxGeometry.vertexCount, 4, boxGeometry.positionBuffer.Get(), descriptor);
            ++positionBufferOffset;

            // Normal buffer (t70)
            descriptor = {kBaseDescriptor.ptr + normalBufferOffset * kIncrementSize};
            CreateDescriptoBufferSRV(pRenderer, 0, boxGeometry.vertexCount, 4, boxGeometry.normalBuffer.Get(), descriptor);
            ++normalBufferOffset;
        }
    }

    // IBL Textures
    for (uint32_t i = 0; i < iblTextures.size(); ++i) {
        auto& iblTexture = iblTextures[i];

        D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {kBaseDescriptor.ptr + ((kIBLTextureOffset + i) * kIncrementSize)};
        CreateDescriptorTexture2D(pRenderer, iblTexture.envTexture.Get(), descriptor, 0, iblTexture.envNumLevels);
    }
}