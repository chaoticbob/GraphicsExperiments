#include "window.h"

#include "dx_renderer.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
// Shader code
// =============================================================================
const char* gRayTracingShaders = R"(

struct CameraProperties {
	float4x4 ViewInverse;
	float4x4 ProjInverse;
};

RaytracingAccelerationStructure  Scene        : register(t0); // Acceleration structure
RWTexture2D<float4>              RenderTarget : register(u1); // Output textures
ConstantBuffer<CameraProperties> Cam          : register(b2); // Constant buffer

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
    float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    payload.color = float4(barycentrics, 1);
}
)";

// =============================================================================
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1280;
static uint32_t gWindowHeight = 720;
static bool     gEnableDebug  = true;

static LPCWSTR gHitGroupName         = L"MyHitGroup";
static LPCWSTR gRayGenShaderName     = L"MyRaygenShader";
static LPCWSTR gMissShaderName       = L"MyMissShader";
static LPCWSTR gClosestHitShaderName = L"MyClosestHitShader";

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
void CreateBLAS(DxRenderer* pRenderer, ID3D12Resource** ppBLAS);
void CreateTLAS(DxRenderer* pRenderer, ID3D12Resource* pBLAS, ID3D12Resource** ppTLAS);
void CreateOutputTexture(DxRenderer* pRenderer, ID3D12Resource** ppBuffer);
void CreateConstantBuffer(DxRenderer* pRenderer, ID3D12Resource* pRayGenSRT, ID3D12Resource** ppConstantBuffer);
void CreateDescriptorHeap(DxRenderer* pRenderer, ID3D12DescriptorHeap** ppHeap);

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
    ComPtr<IDxcBlob> shaderBinary;
    {
        ComPtr<IDxcLibrary> dxcLibrary;
        CHECK_CALL(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&dxcLibrary)));

        ComPtr<IDxcCompiler3> dxcCompiler;
        CHECK_CALL(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler)));

        DxcBuffer source = {};
        source.Ptr       = gRayTracingShaders;
        source.Size      = strlen(gRayTracingShaders);

        LPCWSTR args[2] = {L"-T", L"lib_6_3"};

        ComPtr<IDxcResult> result;
        CHECK_CALL(dxcCompiler->Compile(
            &source,
            args,
            ARRAYSIZE(args),
            nullptr,
            IID_PPV_ARGS(&result)));

        ComPtr<IDxcBlob> errors;
        CHECK_CALL(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr));
        if (errors && (errors->GetBufferSize() > 0)) {
            std::string       errorMsg = std::string(reinterpret_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error: " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            return EXIT_FAILURE;
        }

        CHECK_CALL(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBinary), nullptr));
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
        shaderBinary->GetBufferSize(),
        shaderBinary->GetBufferPointer(),
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
    // Bottom level acceleration structure
    // *************************************************************************
    ComPtr<ID3D12Resource> blasBuffer;
    CreateBLAS(renderer.get(), &blasBuffer);

    // *************************************************************************
    // Top level acceleration structure
    // *************************************************************************
    ComPtr<ID3D12Resource> tlasBuffer;
    CreateTLAS(renderer.get(), blasBuffer.Get(), &tlasBuffer);

    // *************************************************************************
    // Output texture
    // *************************************************************************
    ComPtr<ID3D12Resource> outputTexture;
    CreateOutputTexture(renderer.get(), &outputTexture);

    // *************************************************************************
    // Constant buffer
    // *************************************************************************
    ComPtr<ID3D12Resource> constantBuffer;
    CreateConstantBuffer(renderer.get(), rgenSRT.Get(), &constantBuffer);

    // *************************************************************************
    // Descriptor heaps
    // *************************************************************************
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    CreateDescriptorHeap(renderer.get(), &descriptorHeap);

    // Write descriptor to descriptor heap
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension                    = D3D12_UAV_DIMENSION_TEXTURE2D;

        D3D12_CPU_DESCRIPTOR_HANDLE descriptor = descriptorHeap->GetCPUDescriptorHandleForHeapStart();

        // Output texture (u1)
        renderer->Device->CreateUnorderedAccessView(outputTexture.Get(), nullptr, &uavDesc, descriptor);
    }

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = GrexWindow::Create(gWindowWidth, gWindowHeight, "001_raytracing_basic_d3d12");
    if (!window) {
        assert(false && "Window::Create failed");
        return EXIT_FAILURE;
    }

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
    // Main loop
    // *************************************************************************
    while (window->PollEvents()) {
        CHECK_CALL(commandAllocator->Reset());
        CHECK_CALL(commandList->Reset(commandAllocator.Get(), nullptr));

        // Trace rays
        {
            commandList->SetComputeRootSignature(globalRootSig.Get());
            commandList->SetDescriptorHeaps(1, descriptorHeap.GetAddressOf());

            // Acceleration structure (t0)
            commandList->SetComputeRootShaderResourceView(0, tlasBuffer->GetGPUVirtualAddress());
            // Output texture (u1)
            commandList->SetComputeRootDescriptorTable(1, descriptorHeap->GetGPUDescriptorHandleForHeapStart());
            // Constant buffer (b2)
            commandList->SetComputeRootConstantBufferView(2, constantBuffer->GetGPUVirtualAddress());

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
}

void CreateGlobalRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig)
{
    D3D12_DESCRIPTOR_RANGE range            = {};
    range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors                    = 1;
    range.BaseShaderRegister                = 1;
    range.RegisterSpace                     = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[3];
    // Accleration structure (t0)
    rootParameters[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace  = 0;
    rootParameters[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    // Output texture (u1) - descriptor table because texture resources can't be root descriptors
    rootParameters[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges   = &range;
    rootParameters[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
    // Constant buffer (b2)
    rootParameters[2].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[2].Descriptor.ShaderRegister = 2;
    rootParameters[2].Descriptor.RegisterSpace  = 0;
    rootParameters[2].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters             = 3;
    rootSigDesc.pParameters               = rootParameters;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    CHECK_CALL(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error));
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
    shaderConfig.MaxPayloadSizeInBytes          = 4 * sizeof(float); // float4 color
    shaderConfig.MaxAttributeSizeInBytes        = 2 * sizeof(float); // float2 barycentrics

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
    pipelineConfigDesc.MaxTraceRecursionDepth           = 1;

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

void CreateBLAS(DxRenderer* pRenderer, ID3D12Resource** ppBLAS)
{
    // clang-format off
    std::vector<float> vertices =
    {
         0.0f,  1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f
    };  

    std::vector<uint32_t> indices =
    {
        0, 1, 2
    };
    // clang-format on

    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;

    CHECK_CALL(CreateBuffer(pRenderer, SizeInBytes(vertices), vertices.data(), &vertexBuffer));
    CHECK_CALL(CreateBuffer(pRenderer, SizeInBytes(indices), indices.data(), &indexBuffer));

    D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc       = {};
    geometryDesc.Type                                 = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometryDesc.Triangles.IndexFormat                = DXGI_FORMAT_R32_UINT;
    geometryDesc.Triangles.IndexCount                 = 3;
    geometryDesc.Triangles.IndexBuffer                = indexBuffer->GetGPUVirtualAddress();
    geometryDesc.Triangles.VertexFormat               = DXGI_FORMAT_R32G32B32_FLOAT;
    geometryDesc.Triangles.VertexCount                = 3;
    geometryDesc.Triangles.VertexBuffer.StartAddress  = vertexBuffer->GetGPUVirtualAddress();
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

void CreateTLAS(DxRenderer* pRenderer, ID3D12Resource* pBLAS, ID3D12Resource** ppTLAS)
{
    // clang-format off
	float transformMatrix[3][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f} 
    };
    // clang-format on

    D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
    instanceDesc.InstanceMask                   = 1;
    instanceDesc.AccelerationStructure          = pBLAS->GetGPUVirtualAddress();
    memcpy(instanceDesc.Transform, &transformMatrix, 12 * sizeof(float));

    ComPtr<ID3D12Resource> instanceBuffer;
    CHECK_CALL(CreateBuffer(pRenderer, sizeof(instanceDesc), &instanceDesc, &instanceBuffer));

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    //
    inputs.Type          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags         = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.DescsLayout   = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs      = 1;
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

void CreateConstantBuffer(DxRenderer* pRenderer, ID3D12Resource* pRayGenSRT, ID3D12Resource** ppConstantBuffer)
{
    struct Camera
    {
        glm::mat4 viewInverse;
        glm::mat4 projInverse;
    };

    Camera camera      = {};
    camera.projInverse = glm::inverse(glm::perspective(glm::radians(60.0f), gWindowWidth / static_cast<float>(gWindowHeight), 0.1f, 512.0f));
    camera.viewInverse = glm::inverse(glm::translate(glm::mat4(1), glm::vec3(0.0f, 0.0f, -2.5f)));

    CHECK_CALL(CreateBuffer(
        pRenderer,          // pRenderer
        sizeof(Camera),     // srcSize
        &camera,            // pSrcData
        256,                // minAlignment
        ppConstantBuffer)); // ppResource
}

void CreateDescriptorHeap(DxRenderer* pRenderer, ID3D12DescriptorHeap** ppHeap)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors             = 1;
    desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    CHECK_CALL(pRenderer->Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(ppHeap)));
}