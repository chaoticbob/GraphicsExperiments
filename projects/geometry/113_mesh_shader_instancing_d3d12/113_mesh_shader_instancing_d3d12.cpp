#include "window.h"
#include "camera.h"
#include "tri_mesh.h"

#include "dx_renderer.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#include "meshoptimizer.h"

#include <cinttypes>

using float4x4 = glm::mat4;

#define CHECK_CALL(FN)                               \
    {                                                \
        HRESULT hr = FN;                             \
        if (FAILED(hr))                              \
        {                                            \
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
// Globals
// =============================================================================
static uint32_t gWindowWidth  = 1920;
static uint32_t gWindowHeight = 1080;
static bool     gEnableDebug  = false;

void CreateGlobalRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig);
void CreateGeometryBuffers(
    DxRenderer*      pRenderer,
    ID3D12Resource** ppIndexBuffer,
    ID3D12Resource** ppVertexBuffer,
    ID3D12Resource** ppVertexColorBuffer);

// =============================================================================
// main()
// =============================================================================
int main(int argc, char** argv)
{
    // *************************************************************************
    // Renderer
    // *************************************************************************
    std::unique_ptr<DxRenderer> renderer = std::make_unique<DxRenderer>();

    if (!InitDx(renderer.get(), gEnableDebug))
    {
        return EXIT_FAILURE;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
    CHECK_CALL(renderer->Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7)));

    bool isMeshShadingSupported = (options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1);
    if (!isMeshShadingSupported)
    {
        assert(false && "Required mesh shading tier not supported");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Compile shaders
    // *************************************************************************
    std::vector<char> dxilAS;
    std::vector<char> dxilMS;
    std::vector<char> dxilPS;
    {
        auto source = LoadString("projects/113_mesh_shader_instancing/shaders.hlsl");
        assert((!source.empty()) && "no shader source!");

        std::string errorMsg;
        HRESULT     hr = CompileHLSL(source, "asmain", "as_6_5", &dxilAS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (AS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        hr = CompileHLSL(source, "msmain", "ms_6_5", &dxilMS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (MS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }

        hr = CompileHLSL(source, "psmain", "ps_6_5", &dxilPS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (PS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);
            return EXIT_FAILURE;
        }
    }

    // *************************************************************************
    // Make them meshlets!
    // *************************************************************************
    TriMesh::Aabb                meshBounds = {};
    std::vector<glm::vec3>       positions;
    std::vector<meshopt_Meshlet> meshlets;
    std::vector<uint32_t>        meshletVertices;
    std::vector<uint8_t>         meshletTriangles;
    {
        TriMesh::Options options = {.enableVertexColors = true, .enableNormals = true};

        //
        // Use a cube to debug when needed
        //
        // TriMesh mesh = TriMesh::Cube(glm::vec3(0.25f), false, options);

        TriMesh mesh = {};
        bool    res  = TriMesh::LoadOBJ(GetAssetPath("models/horse_statue_01_1k.obj").string(), "", options, &mesh);
        if (!res)
        {
            assert(false && "failed to load model");
        }

        meshBounds = mesh.GetBounds();
        positions  = mesh.GetPositions();

        const size_t kMaxVertices  = 64;
        const size_t kMaxTriangles = 124;
        const float  kConeWeight   = 0.0f;

        const size_t maxMeshlets = meshopt_buildMeshletsBound(mesh.GetNumIndices(), kMaxVertices, kMaxTriangles);

        meshlets.resize(maxMeshlets);
        meshletVertices.resize(maxMeshlets * kMaxVertices);
        meshletTriangles.resize(maxMeshlets * kMaxTriangles * 3);

        size_t meshletCount = meshopt_buildMeshlets(
            meshlets.data(),
            meshletVertices.data(),
            meshletTriangles.data(),
            reinterpret_cast<const uint32_t*>(mesh.GetTriangles().data()),
            mesh.GetNumIndices(),
            reinterpret_cast<const float*>(mesh.GetPositions().data()),
            mesh.GetNumVertices(),
            sizeof(glm::vec3),
            kMaxVertices,
            kMaxTriangles,
            kConeWeight);

        auto& last = meshlets[meshletCount - 1];
        meshletVertices.resize(last.vertex_offset + last.vertex_count);
        meshletTriangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
        meshlets.resize(meshletCount);
    }

    // Repack triangles from 3 consecutive byes to 4-byte uint32_t to 
    // make it easier to unpack on the GPU.
    //
    std::vector<uint32_t> meshletTrianglesU32;
    for (auto& m : meshlets)
    {
        // Save triangle offset for current meshlet
        uint32_t triangleOffset = static_cast<uint32_t>(meshletTrianglesU32.size());

        // Repack to uint32_t
        for (uint32_t i = 0; i < m.triangle_count; ++i)
        {
            uint32_t i0 = 3 * i + 0 + m.triangle_offset;
            uint32_t i1 = 3 * i + 1 + m.triangle_offset;
            uint32_t i2 = 3 * i + 2 + m.triangle_offset;

            uint8_t  vIdx0  = meshletTriangles[i0];
            uint8_t  vIdx1  = meshletTriangles[i1];
            uint8_t  vIdx2  = meshletTriangles[i2];
            uint32_t packed = ((static_cast<uint32_t>(vIdx0) & 0xFF) << 0) |
                              ((static_cast<uint32_t>(vIdx1) & 0xFF) << 8) |
                              ((static_cast<uint32_t>(vIdx2) & 0xFF) << 16);
            meshletTrianglesU32.push_back(packed);
        }

        // Update triangle offset for current meshlet
        m.triangle_offset = triangleOffset;
    }

    ComPtr<ID3D12Resource> positionBuffer;
    ComPtr<ID3D12Resource> meshletBuffer;
    ComPtr<ID3D12Resource> meshletVerticesBuffer;
    ComPtr<ID3D12Resource> meshletTrianglesBuffer;
    {
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(positions), DataPtr(positions), D3D12_HEAP_TYPE_UPLOAD, &positionBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(meshlets), DataPtr(meshlets), D3D12_HEAP_TYPE_UPLOAD, &meshletBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(meshletVertices), DataPtr(meshletVertices), D3D12_HEAP_TYPE_UPLOAD, &meshletVerticesBuffer));
        CHECK_CALL(CreateBuffer(renderer.get(), SizeInBytes(meshletTrianglesU32), DataPtr(meshletTrianglesU32), D3D12_HEAP_TYPE_UPLOAD, &meshletTrianglesBuffer));
    }

    // *************************************************************************
    // Root signature
    // *************************************************************************
    ComPtr<ID3D12RootSignature>
        rootSig;
    CreateGlobalRootSig(renderer.get(), &rootSig);

    // *************************************************************************
    // Graphics pipeline state object
    // *************************************************************************
    D3DX12_MESH_SHADER_PIPELINE_STATE_DESC psoDesc           = {};
    psoDesc.pRootSignature                                   = rootSig.Get();
    psoDesc.AS                                               = {dxilAS.data(), dxilAS.size()};
    psoDesc.MS                                               = {dxilMS.data(), dxilMS.size()};
    psoDesc.PS                                               = {dxilPS.data(), dxilPS.size()};
    psoDesc.BlendState.AlphaToCoverageEnable                 = FALSE;
    psoDesc.BlendState.IndependentBlendEnable                = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable           = FALSE;
    psoDesc.BlendState.RenderTarget[0].LogicOpEnable         = FALSE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_SRC_COLOR;
    psoDesc.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask                                       = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode                         = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise            = TRUE;
    psoDesc.RasterizerState.DepthBias                        = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp                   = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias             = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable                  = FALSE;
    psoDesc.RasterizerState.MultisampleEnable                = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable            = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount                = 0;
    psoDesc.DepthStencilState.DepthEnable                    = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask                 = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc                      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DepthStencilState.StencilEnable                  = FALSE;
    psoDesc.DepthStencilState.StencilReadMask                = D3D12_DEFAULT_STENCIL_READ_MASK;
    psoDesc.DepthStencilState.StencilWriteMask               = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    psoDesc.DepthStencilState.FrontFace.StencilFailOp        = D3D12_STENCIL_OP_KEEP;
    psoDesc.DepthStencilState.FrontFace.StencilDepthFailOp   = D3D12_STENCIL_OP_KEEP;
    psoDesc.DepthStencilState.FrontFace.StencilPassOp        = D3D12_STENCIL_OP_KEEP;
    psoDesc.DepthStencilState.FrontFace.StencilFunc          = D3D12_COMPARISON_FUNC_NEVER;
    psoDesc.DepthStencilState.BackFace                       = psoDesc.DepthStencilState.FrontFace;
    psoDesc.NumRenderTargets                                 = 1;
    psoDesc.RTVFormats[0]                                    = GREX_DEFAULT_RTV_FORMAT;
    psoDesc.DSVFormat                                        = GREX_DEFAULT_DSV_FORMAT;
    psoDesc.SampleDesc.Count                                 = 1;

    // This required unless you want to come up with own struct that handles
    // the stream requirements:
    //    https://microsoft.github.io/DirectX-Specs/d3d/MeshShader.html#createpipelinestate
    //
    CD3DX12_PIPELINE_MESH_STATE_STREAM psoStream = CD3DX12_PIPELINE_MESH_STATE_STREAM(psoDesc);

    D3D12_PIPELINE_STATE_STREAM_DESC steamDesc = {};
    steamDesc.SizeInBytes                      = sizeof(psoStream);
    steamDesc.pPipelineStateSubobjectStream    = &psoStream;

    ComPtr<ID3D12PipelineState> pipelineState;
    //
    HRESULT hr = renderer->Device->CreatePipelineState(&steamDesc, IID_PPV_ARGS(&pipelineState));
    if (FAILED(hr))
    {
        assert(false && "Create pipeline state failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Window
    // *************************************************************************
    auto window = Window::Create(gWindowWidth, gWindowHeight, GREX_BASE_FILE_NAME());
    if (!window)
    {
        assert(false && "Window::Create failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Swapchain
    // *************************************************************************
    if (!InitSwapchain(renderer.get(), window->GetHWND(), window->GetWidth(), window->GetHeight(), 2, GREX_DEFAULT_DSV_FORMAT))
    {
        assert(false && "InitSwapchain failed");
        return EXIT_FAILURE;
    }

    // *************************************************************************
    // Imgui
    // *************************************************************************
    if (!window->InitImGuiForD3D12(renderer.get()))
    {
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
    ComPtr<ID3D12GraphicsCommandList6> commandList;
    {
        CHECK_CALL(renderer->Device->CreateCommandList1(
            0,                              // nodeMask
            D3D12_COMMAND_LIST_TYPE_DIRECT, // type
            D3D12_COMMAND_LIST_FLAG_NONE,   // flags
            IID_PPV_ARGS(&commandList)));   // ppCommandList
    }

    // *************************************************************************
    // Pipeline statistics
    // *************************************************************************
    ComPtr<ID3D12QueryHeap> queryHeap;
    {
        D3D12_QUERY_HEAP_DESC desc = {D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1, 1};
        CHECK_CALL(renderer->Device->CreateQueryHeap(&desc, IID_PPV_ARGS(&queryHeap)));
    }

    ComPtr<ID3D12Resource> queryBuffer;
    CreateBuffer(renderer.get(), sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1), D3D12_HEAP_TYPE_READBACK, &queryBuffer);
    //
    bool hasPiplineStats = false;

    // *************************************************************************
    // Instances
    // *************************************************************************
    const uint32_t        kNumInstanceCols = 20;
    const uint32_t        kNumInstanceRows = 10;
    std::vector<float4x4> instances(kNumInstanceCols * kNumInstanceRows);

    ComPtr<ID3D12Resource> instancesBuffer;
    CreateBuffer(renderer.get(), SizeInBytes(instances), D3D12_HEAP_TYPE_UPLOAD, &instancesBuffer);

    // *************************************************************************
    // Main loop
    // *************************************************************************
    while (window->PollEvents())
    {
        // ---------------------------------------------------------------------

        D3D12_QUERY_DATA_PIPELINE_STATISTICS1 pipelineStatistics = {};
        if (hasPiplineStats)
        {
            void* ptr = nullptr;
            CHECK_CALL(queryBuffer->Map(0, nullptr, &ptr));
            memcpy(&pipelineStatistics, ptr, sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1));
            queryBuffer->Unmap(0, nullptr);
        }

        // ---------------------------------------------------------------------
        window->ImGuiNewFrameD3D12();

        if (ImGui::Begin("Params"))
        {
            ImGui::Columns(2);
            // clang-format off
            ImGui::Text("IAVertices");    ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.IAVertices); ImGui::NextColumn();                
            ImGui::Text("IAPrimitives");  ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.IAPrimitives); ImGui::NextColumn();                
            ImGui::Text("VSInvocations"); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.VSInvocations); ImGui::NextColumn();                
            ImGui::Text("GSInvocations"); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.GSInvocations); ImGui::NextColumn();                
            ImGui::Text("GSPrimitives");  ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.GSPrimitives); ImGui::NextColumn();                
            ImGui::Text("CInvocations");  ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.CInvocations); ImGui::NextColumn();                
            ImGui::Text("CPrimitives");   ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.CPrimitives); ImGui::NextColumn();                
            ImGui::Text("PSInvocations"); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.PSInvocations); ImGui::NextColumn();                
            ImGui::Text("HSInvocations"); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.HSInvocations); ImGui::NextColumn();                
            ImGui::Text("DSInvocations"); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.DSInvocations); ImGui::NextColumn();                
            ImGui::Text("CSInvocations"); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.CSInvocations); ImGui::NextColumn();                
            ImGui::Text("ASInvocations"); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.ASInvocations); ImGui::NextColumn();                
            ImGui::Text("MSInvocations"); ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.MSInvocations); ImGui::NextColumn();                
            ImGui::Text("MSPrimitives");  ImGui::NextColumn(); ImGui::Text("%" PRIu64, pipelineStatistics.MSPrimitives); ImGui::NextColumn();
            // clang-format on
        }
        ImGui::End();

        // ---------------------------------------------------------------------

        // Update instance transforms
        {
            float maxSpan       = std::max<float>(meshBounds.Width(), meshBounds.Depth());
            float instanceSpanX = 2.0f * maxSpan;
            float instanceSpanZ = 4.5f * maxSpan;
            float totalSpanX    = kNumInstanceCols * instanceSpanX;
            float totalSpanZ    = kNumInstanceRows * instanceSpanZ;

            for (uint32_t j = 0; j < kNumInstanceRows; ++j)
            {
                for (uint32_t i = 0; i < kNumInstanceCols; ++i)
                {
                    float x = i * instanceSpanX - (totalSpanX / 2.0f) + instanceSpanX / 2.0f;
                    float y = 0;
                    float z = j * instanceSpanZ - (totalSpanZ / 2.0f) - 2.15f * instanceSpanZ;

                    uint32_t index   = j * kNumInstanceCols + i;
                    float    t       = static_cast<float>(glfwGetTime()) + ((i ^ j + i) / 10.0f);
                    instances[index] = glm::translate(glm::vec3(x, y, z)) * glm::rotate(t, glm::vec3(0, 1, 0));
                }
            }
        }

        // ---------------------------------------------------------------------

        // Copy instances transforms to instances buffer
        {
            void* pDst = nullptr;
            CHECK_CALL(instancesBuffer->Map(0, nullptr, &pDst));
            memcpy(pDst, instances.data(), SizeInBytes(instances));
            instancesBuffer->Unmap(0, nullptr);
        }

        // ---------------------------------------------------------------------

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

            float clearColor[4] = {0.23f, 0.23f, 0.31f, 0};
            commandList->ClearRenderTargetView(renderer->SwapchainRTVDescriptorHandles[bufferIndex], clearColor, 0, nullptr);
            commandList->ClearDepthStencilView(renderer->SwapchainDSVDescriptorHandles[bufferIndex], D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0xFF, 0, nullptr);

            D3D12_VIEWPORT viewport = {0, 0, static_cast<float>(gWindowWidth), static_cast<float>(gWindowHeight), 0, 1};
            commandList->RSSetViewports(1, &viewport);

            D3D12_RECT scissor = {0, 0, static_cast<long>(gWindowWidth), static_cast<long>(gWindowHeight)};
            commandList->RSSetScissorRects(1, &scissor);

            commandList->SetGraphicsRootSignature(rootSig.Get());
            commandList->SetPipelineState(pipelineState.Get());

            PerspCamera camera = PerspCamera(45.0f, window->GetAspectRatio(), 0.1f, 1000.0f);
            camera.LookAt(vec3(0, 0.7f, 3.0f), vec3(0, 0.105f, 0));

            mat4 VP = camera.GetViewProjectionMatrix();
            uint32_t instanceCount = static_cast<uint32_t>(instances.size());
            uint32_t meshletCount = static_cast<uint32_t>(meshlets.size());

            commandList->SetGraphicsRoot32BitConstants(0, 16, &VP, 0);
            commandList->SetGraphicsRoot32BitConstants(0, 1, &instanceCount, 16);
            commandList->SetGraphicsRoot32BitConstants(0, 1, &meshletCount, 17);
            commandList->SetGraphicsRootShaderResourceView(1, positionBuffer->GetGPUVirtualAddress());
            commandList->SetGraphicsRootShaderResourceView(2, meshletBuffer->GetGPUVirtualAddress());
            commandList->SetGraphicsRootShaderResourceView(3, meshletVerticesBuffer->GetGPUVirtualAddress());
            commandList->SetGraphicsRootShaderResourceView(4, meshletTrianglesBuffer->GetGPUVirtualAddress());
            commandList->SetGraphicsRootShaderResourceView(5, instancesBuffer->GetGPUVirtualAddress());

            // DispatchMesh with pipeline statistics
            {
                commandList->BeginQuery(queryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, 0);

                // Amplification shader uses 32 for thread group size
                UINT meshletCount      = static_cast<UINT>(meshlets.size());
                UINT instanceCount     = static_cast<UINT>(instances.size());
                UINT threadGroupCountX = ((meshletCount * instanceCount) / 32) + 1;

                commandList->DispatchMesh(threadGroupCountX, 1, 1);

                commandList->EndQuery(queryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, 0);
            }

            // Resolve query
            commandList->ResolveQueryData(queryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, 0, 1, queryBuffer.Get(), 0);

            // ImGui
            window->ImGuiRenderDrawData(renderer.get(), commandList.Get());
        }
        D3D12_RESOURCE_BARRIER postRenderBarrier = CreateTransition(swapchainBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        commandList->ResourceBarrier(1, &postRenderBarrier);

        commandList->Close();

        ID3D12CommandList* pList = commandList.Get();
        renderer->Queue->ExecuteCommandLists(1, &pList);

        if (!WaitForGpu(renderer.get()))
        {
            assert(false && "WaitForGpu failed");
            break;
        }

        // Command list execution is done we can read the pipeline stats
        hasPiplineStats = true;

        // Present
        if (!SwapchainPresent(renderer.get()))
        {
            assert(false && "SwapchainPresent failed");
            break;
        }
    }

    return 0;
}

void CreateGlobalRootSig(DxRenderer* pRenderer, ID3D12RootSignature** ppRootSig)
{
    std::vector<D3D12_ROOT_PARAMETER> rootParameters;

    // ConstantBuffer<CameraProperties> Cam : register(b0);
    {
        D3D12_ROOT_PARAMETER rootParameter     = {};
        rootParameter.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameter.Constants.Num32BitValues = 18;
        rootParameter.Constants.ShaderRegister = 0;
        rootParameter.Constants.RegisterSpace  = 0;
        rootParameter.ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        rootParameters.push_back(rootParameter);
    }

    // StructuredBuffer<Vertex> Vertices : register(t1);
    {
        D3D12_ROOT_PARAMETER rootParameter      = {};
        rootParameter.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        rootParameter.Descriptor.ShaderRegister = 1;
        rootParameter.Descriptor.RegisterSpace  = 0;
        rootParameter.ShaderVisibility          = D3D12_SHADER_VISIBILITY_MESH;
        rootParameters.push_back(rootParameter);
    }

    // StructuredBuffer<Meshlet> Meshlets : register(t2);
    {
        D3D12_ROOT_PARAMETER rootParameter      = {};
        rootParameter.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        rootParameter.Descriptor.ShaderRegister = 2;
        rootParameter.Descriptor.RegisterSpace  = 0;
        rootParameter.ShaderVisibility          = D3D12_SHADER_VISIBILITY_MESH;
        rootParameters.push_back(rootParameter);
    }

    // ByteAddressBuffer VertexIndices : register(t3);
    {
        D3D12_ROOT_PARAMETER rootParameter      = {};
        rootParameter.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        rootParameter.Descriptor.ShaderRegister = 3;
        rootParameter.Descriptor.RegisterSpace  = 0;
        rootParameter.ShaderVisibility          = D3D12_SHADER_VISIBILITY_MESH;
        rootParameters.push_back(rootParameter);
    }

    // StructuredBuffer<uint> TriangleIndices : register(t4);
    {
        D3D12_ROOT_PARAMETER rootParameter      = {};
        rootParameter.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        rootParameter.Descriptor.ShaderRegister = 4;
        rootParameter.Descriptor.RegisterSpace  = 0;
        rootParameter.ShaderVisibility          = D3D12_SHADER_VISIBILITY_MESH;
        rootParameters.push_back(rootParameter);
    }

    // StructuredBuffer<Instance> Instances : register(t5);
    {
        D3D12_ROOT_PARAMETER rootParameter      = {};
        rootParameter.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        rootParameter.Descriptor.ShaderRegister = 5;
        rootParameter.Descriptor.RegisterSpace  = 0;
        rootParameter.ShaderVisibility          = D3D12_SHADER_VISIBILITY_MESH;
        rootParameters.push_back(rootParameter);
    }

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters             = static_cast<UINT>(rootParameters.size());
    rootSigDesc.pParameters               = rootParameters.data();
    rootSigDesc.Flags                     = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    CHECK_CALL(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error));
    CHECK_CALL(pRenderer->Device->CreateRootSignature(
        0,                         // nodeMask
        blob->GetBufferPointer(),  // pBloblWithRootSignature
        blob->GetBufferSize(),     // blobLengthInBytes
        IID_PPV_ARGS(ppRootSig))); // riid, ppvRootSignature
}
