#include "dx_draw_context.h"

#include <glm/gtx/quaternion.hpp>

static const char* gDrawVertexColorShaders = R"(

struct CameraProperties {
	float4x4 MVP;
};

ConstantBuffer<CameraProperties> Cam : register(b0); // Constant buffer

struct VSOutput {
    float4 PositionCS : SV_POSITION;
    float4 Color      : COLOR;
};

VSOutput vsmain(float3 PositionOS : POSITION, float4 Color : COLOR0)
{
    VSOutput output = (VSOutput)0;
    output.PositionCS = mul(Cam.MVP, float4(PositionOS, 1));
    output.Color = Color;
    return output;
}

float4 psmain(VSOutput input) : SV_TARGET
{
    return float4(input.Color);   
}
)";

static HRESULT CreatePipeline(
    DxRenderer*                   pRenderer,
    ID3D12RootSignature*          pRootSig,
    const std::vector<char>&      vsShaderBytecode,
    const std::vector<char>&      psShaderBytecode,
    DXGI_FORMAT                   rtvFormat,
    DXGI_FORMAT                   dsvFormat,
    D3D12_CULL_MODE               cullMode,
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType,
    bool                          depthEnable,
    bool                          blendEnable,
    D3D12_BLEND                   srcBlend,
    D3D12_BLEND                   destBlend,
    D3D12_BLEND_OP                blendOp,
    D3D12_BLEND                   srcBlendAlpha,
    D3D12_BLEND                   destBlendAlpha,
    D3D12_BLEND_OP                blendOpAlpha,
    ID3D12PipelineState**         ppPipeline)
{
    D3D12_INPUT_ELEMENT_DESC inputElementDesc[3] = {};
    inputElementDesc[0].SemanticName             = "POSITION";
    inputElementDesc[0].SemanticIndex            = 0;
    inputElementDesc[0].Format                   = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDesc[0].InputSlot                = 0;
    inputElementDesc[0].AlignedByteOffset        = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[0].InputSlotClass           = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[0].InstanceDataStepRate     = 0;
    inputElementDesc[1].SemanticName             = "COLOR";
    inputElementDesc[1].SemanticIndex            = 0;
    inputElementDesc[1].Format                   = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDesc[1].InputSlot                = 0;
    inputElementDesc[1].AlignedByteOffset        = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[1].InputSlotClass           = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[1].InstanceDataStepRate     = 0;
    inputElementDesc[2].SemanticName             = "TEXCOORD";
    inputElementDesc[2].SemanticIndex            = 0;
    inputElementDesc[2].Format                   = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDesc[2].InputSlot                = 0;
    inputElementDesc[2].AlignedByteOffset        = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDesc[2].InputSlotClass           = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElementDesc[2].InstanceDataStepRate     = 0;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc               = {};
    desc.pRootSignature                                   = pRootSig;
    desc.VS                                               = {DataPtr(vsShaderBytecode), CountU32(vsShaderBytecode)};
    desc.PS                                               = {DataPtr(psShaderBytecode), CountU32(psShaderBytecode)};
    desc.BlendState.AlphaToCoverageEnable                 = FALSE;
    desc.BlendState.IndependentBlendEnable                = FALSE;
    desc.BlendState.RenderTarget[0].BlendEnable           = blendEnable;
    desc.BlendState.RenderTarget[0].LogicOpEnable         = FALSE;
    desc.BlendState.RenderTarget[0].SrcBlend              = srcBlend;
    desc.BlendState.RenderTarget[0].DestBlend             = destBlend;
    desc.BlendState.RenderTarget[0].BlendOp               = blendOp;
    desc.BlendState.RenderTarget[0].SrcBlendAlpha         = srcBlendAlpha;
    desc.BlendState.RenderTarget[0].DestBlendAlpha        = destBlendAlpha;
    desc.BlendState.RenderTarget[0].BlendOpAlpha          = blendOpAlpha;
    desc.BlendState.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask                                       = D3D12_DEFAULT_SAMPLE_MASK;
    desc.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode                         = cullMode;
    desc.RasterizerState.FrontCounterClockwise            = TRUE;
    desc.RasterizerState.DepthBias                        = D3D12_DEFAULT_DEPTH_BIAS;
    desc.RasterizerState.DepthBiasClamp                   = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.RasterizerState.SlopeScaledDepthBias             = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.RasterizerState.DepthClipEnable                  = FALSE;
    desc.RasterizerState.MultisampleEnable                = FALSE;
    desc.RasterizerState.AntialiasedLineEnable            = FALSE;
    desc.RasterizerState.ForcedSampleCount                = 0;
    desc.DepthStencilState.DepthEnable                    = depthEnable;
    desc.DepthStencilState.DepthWriteMask                 = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc                      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    desc.DepthStencilState.StencilEnable                  = FALSE;
    desc.DepthStencilState.StencilReadMask                = D3D12_DEFAULT_STENCIL_READ_MASK;
    desc.DepthStencilState.StencilWriteMask               = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    desc.DepthStencilState.FrontFace.StencilFailOp        = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilDepthFailOp   = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilPassOp        = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.FrontFace.StencilFunc          = D3D12_COMPARISON_FUNC_NEVER;
    desc.DepthStencilState.BackFace                       = desc.DepthStencilState.FrontFace;
    desc.InputLayout.NumElements                          = 3;
    desc.InputLayout.pInputElementDescs                   = inputElementDesc;
    desc.IBStripCutValue                                  = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
    desc.PrimitiveTopologyType                            = topologyType;
    desc.NumRenderTargets                                 = 1;
    desc.RTVFormats[0]                                    = rtvFormat;
    desc.DSVFormat                                        = dsvFormat;
    desc.SampleDesc.Count                                 = 1;
    desc.SampleDesc.Quality                               = 0;
    desc.NodeMask                                         = 0;
    desc.CachedPSO                                        = {};
    desc.Flags                                            = D3D12_PIPELINE_STATE_FLAG_NONE;

    HRESULT hr = pRenderer->Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(ppPipeline));
    if (FAILED(hr))
    {
        return hr;
    }

    return S_OK;
}

static int32_t sStockProgramDrawVertexColors = -1;

DxDrawContext::DxDrawContext(DxRenderer* pRenderer, DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
    : mRenderer(pRenderer),
      mRTVFormat(rtvFormat),
      mDSVFormat(dsvFormat)
{
    if (sStockProgramDrawVertexColors == -1)
    {
        sStockProgramDrawVertexColors = CreateProgram(gDrawVertexColorShaders, "vsmain", "psmain");
        assert((sStockProgramDrawVertexColors >= 0) && "create program failed: draw vertex color");
    }
}

DxDrawContext::~DxDrawContext()
{
}

int32_t DxDrawContext::GetStockProgramDrawVertexColor()
{
    return sStockProgramDrawVertexColors;
}

int32_t DxDrawContext::CreateProgram(
    const std::string& shaderCode,
    const std::string& vsEntryPoint,
    const std::string& psEntryPoint)
{
    // -------------------------------------------------------------------------
    // Root signature
    // -------------------------------------------------------------------------
    ComPtr<ID3D12RootSignature> rootSig;
    {
        D3D12_ROOT_PARAMETER rootParameter     = {};
        rootParameter.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameter.Constants.Num32BitValues = 16;
        rootParameter.Constants.ShaderRegister = 0;
        rootParameter.Constants.RegisterSpace  = 0;
        rootParameter.ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;

        D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
        rootSigDesc.NumParameters             = 1;
        rootSigDesc.pParameters               = &rootParameter;
        rootSigDesc.Flags                     = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> error;
        HRESULT          hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
        if (FAILED(hr))
        {
            std::string errorMsg;
            if (error && (error->GetBufferSize() > 0))
            {
                const char* pBuffer    = static_cast<const char*>(error->GetBufferPointer());
                size_t      bufferSize = static_cast<size_t>(error->GetBufferSize());
                errorMsg               = std::string(pBuffer, pBuffer + bufferSize);
            }

            std::stringstream ss;
            ss << "\n"
               << "Serialize root sig error: " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);

            return ERROR_ROOT_SIG_CREATE_FAILED;
        }

        hr = mRenderer->Device->CreateRootSignature(
            0,                        // nodeMask
            blob->GetBufferPointer(), // pBloblWithRootSignature
            blob->GetBufferSize(),    // blobLengthInBytes
            IID_PPV_ARGS(&rootSig));  // riid, ppvRootSignature

        if (FAILED(hr))
        {
            return DxDrawContext::ERROR_ROOT_SIG_CREATE_FAILED;
        }
    }

    // -------------------------------------------------------------------------
    // Compile Shader
    // -------------------------------------------------------------------------
    if (shaderCode.empty())
    {
        return DxDrawContext::ERROR_NO_SHADER_CODE;
    }
    if (vsEntryPoint.empty())
    {
        return DxDrawContext::ERROR_NO_VS_ENTRY_POINT;
    }
    if (psEntryPoint.empty())
    {
        return DxDrawContext::ERROR_NO_PS_ENTRY_POINT;
    }

    std::vector<char> dxilVS;
    std::vector<char> dxilPS;
    {
        std::string errorMsg;
        HRESULT     hr = CompileHLSL(shaderCode, vsEntryPoint.c_str(), "vs_6_0", &dxilVS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (VS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);

            return DxDrawContext::ERROR_VS_COMPILE_FAILED;
        }

        hr = CompileHLSL(shaderCode, psEntryPoint.c_str(), "ps_6_0", &dxilPS, &errorMsg);
        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "\n"
               << "Shader compiler error (PS): " << errorMsg << "\n";
            GREX_LOG_ERROR(ss.str().c_str());
            assert(false);

            return DxDrawContext::ERROR_PS_COMPILE_FAILED;
        }
    }

    // -------------------------------------------------------------------------
    // Program
    // -------------------------------------------------------------------------
    const std::vector<PrimitiveMode> primitiveModes = {PRIMITIVE_MODE_LINES, PRIMITIVE_MODE_TRIS};
    const std::vector<bool>          depthStates    = {false, true};
    const std::vector<BlendMode>     blendModes     = {BLEND_MODE_NONE, BLEND_MODE_ALPHA, BLEND_MODE_ADDITIVE};
    const std::vector<CullMode>      cullModes      = {CULL_MODE_NONE, CULL_MODE_BACK, CULL_MODE_FRONT};

    Program program = {};
    program.id      = ++mProgramIdCounter;
    program.rootSig = rootSig;

    for (auto primitiveMode : primitiveModes)
    {
        for (auto depthEnable : depthStates)
        {
            for (auto blendMode : blendModes)
            {
                for (auto cullMode : cullModes)
                {
                    GraphicsPipelineConfig pipelineConfig = {};
                    pipelineConfig.bits.primitiveMode     = primitiveMode;
                    pipelineConfig.bits.depthEnable       = depthEnable;
                    pipelineConfig.bits.blendMode         = blendMode;
                    pipelineConfig.bits.cullMode          = cullMode;

                    // Primitive mode
                    D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
                    switch (primitiveMode)
                    {
                        default: break;
                        case PRIMITIVE_MODE_LINES: topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE; break;
                        case PRIMITIVE_MODE_TRIS: topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; break;
                    }

                    // Blend mode
                    bool           blendEnable    = false;
                    D3D12_BLEND    srcBlend       = D3D12_BLEND_SRC_COLOR;
                    D3D12_BLEND    destBlend      = D3D12_BLEND_ZERO;
                    D3D12_BLEND_OP blendOp        = D3D12_BLEND_OP_ADD;
                    D3D12_BLEND    srcBlendAlpha  = D3D12_BLEND_SRC_ALPHA;
                    D3D12_BLEND    destBlendAlpha = D3D12_BLEND_ZERO;
                    D3D12_BLEND_OP blendOpAlpha   = D3D12_BLEND_OP_ADD;
                    //
                    if (blendMode == DxDrawContext::BLEND_MODE_ALPHA)
                    {
                        blendEnable = true;
                        // Color
                        srcBlend  = D3D12_BLEND_SRC_ALPHA;
                        destBlend = D3D12_BLEND_INV_SRC_ALPHA;
                        blendOp   = D3D12_BLEND_OP_ADD;
                        // Alpha
                        srcBlendAlpha  = D3D12_BLEND_ZERO; // D3D12_BLEND_SRC_ALPHA;
                        destBlendAlpha = D3D12_BLEND_ZERO; // D3D12_BLEND_INV_SRC_ALPHA;
                        blendOpAlpha   = D3D12_BLEND_OP_ADD;
                    }
                    else if (blendMode == DxDrawContext::BLEND_MODE_ADDITIVE)
                    {
                        blendEnable = true;
                        // Color
                        srcBlend  = D3D12_BLEND_SRC_ALPHA;
                        destBlend = D3D12_BLEND_ONE;
                        blendOp   = D3D12_BLEND_OP_ADD;
                        // Alpha
                        srcBlendAlpha  = D3D12_BLEND_SRC_ALPHA;
                        destBlendAlpha = D3D12_BLEND_ONE;
                        blendOpAlpha   = D3D12_BLEND_OP_ADD;
                    }

                    // Cull mode
                    D3D12_CULL_MODE pipelineCullMode = D3D12_CULL_MODE_NONE;
                    switch (cullMode)
                    {
                        default: break;
                        case CULL_MODE_BACK: pipelineCullMode = D3D12_CULL_MODE_BACK; break;
                        case CULL_MODE_FRONT: pipelineCullMode = D3D12_CULL_MODE_FRONT; break;
                    }

                    ComPtr<ID3D12PipelineState> pipeline;
                    HRESULT                     hr = CreatePipeline(
                        /* pRenderer        */ mRenderer,
                        /* pRootSig         */ rootSig.Get(),
                        /* vsShaderBytecode */ dxilVS,
                        /* psShaderBytecode */ dxilPS,
                        /* rtvFormat        */ mRTVFormat,
                        /* dsvFormat        */ mDSVFormat,
                        /* cullMode         */ pipelineCullMode,
                        /* topologyType     */ topologyType,
                        /* depthEnable      */ depthEnable,
                        /* blendEnable      */ blendEnable,
                        /* srcBlend         */ srcBlend,
                        /* destBlend        */ destBlend,
                        /* blendOp          */ blendOp,
                        /* srcBlendAlpha    */ srcBlendAlpha,
                        /* destBlendAlpha   */ destBlendAlpha,
                        /* blendOpAlpha     */ blendOpAlpha,
                        /* ppPipeline       */ &pipeline);
                    if (FAILED(hr))
                    {
                        return DxDrawContext::ERROR_PIPELINE_CREATE_FAILED;
                    }

                    program.pipelines[pipelineConfig.mask] = pipeline;
                }
            }
        }
    }

    mPrograms[program.id] = program;

    return program.id;
}

void DxDrawContext::Reset()
{
    mVertices.clear();
    mBatches.clear();
}

void DxDrawContext::SetProgram(int32_t program)
{
    mCurrentProgramId = program;
}

void DxDrawContext::SetDepthRead(bool enable)
{
    if (enable)
    {
        mGraphicsState.depthFlags |= DEPTH_FLAG_READ_ONLY;
    }
    else
    {
        mGraphicsState.depthFlags &= ~DEPTH_FLAG_READ_ONLY;
    }

    mGraphicsState.pipelineConfig.bits.depthEnable = (mGraphicsState.depthFlags != DEPTH_FLAG_NONE);
}

void DxDrawContext::SetDepthWrite(bool enable)
{
    if (enable)
    {
        mGraphicsState.depthFlags |= DEPTH_FLAG_WRITE_ONLY;
    }
    else
    {
        mGraphicsState.depthFlags &= ~DEPTH_FLAG_WRITE_ONLY;
    }

    mGraphicsState.pipelineConfig.bits.depthEnable = (mGraphicsState.depthFlags != DEPTH_FLAG_NONE);
}

void DxDrawContext::SetBlendNone()
{
    mGraphicsState.pipelineConfig.bits.blendMode = BLEND_MODE_NONE;
}

void DxDrawContext::SetBlendAlpha()
{
    mGraphicsState.pipelineConfig.bits.blendMode = BLEND_MODE_ALPHA;
}

void DxDrawContext::SetBlendAdditive()
{
    mGraphicsState.pipelineConfig.bits.blendMode = BLEND_MODE_ADDITIVE;
}

void DxDrawContext::SetCullModeNone()
{
    mGraphicsState.pipelineConfig.bits.cullMode = CULL_MODE_NONE;
}

void DxDrawContext::SetCullModeBack()
{
    mGraphicsState.pipelineConfig.bits.cullMode = CULL_MODE_BACK;
}

void DxDrawContext::SetCullModeFront()
{
    mGraphicsState.pipelineConfig.bits.cullMode = CULL_MODE_FRONT;
}

void DxDrawContext::SetMatrix(const float4x4& matrix)
{
    mGraphicsState.mvpMatrix = matrix;
}

void DxDrawContext::SetBatchMatrix(uint32_t batchId, const float4x4& matrix)
{
    auto it = std::find_if(
        mBatches.begin(),
        mBatches.end(),
        [batchId](const Batch& elem) -> bool {
            bool match = (elem.batchId == batchId);
            return match; });
    if (it == mBatches.end())
    {
        return;
    }

    it->mvpMatrix = matrix;
}

uint32_t DxDrawContext::BeginLines()
{
    mGraphicsState.pipelineConfig.bits.primitiveMode = PRIMITIVE_MODE_LINES;

    ++mBatch.batchId;

    mBatch.programId      = mCurrentProgramId;
    mBatch.pipelineConfig = mGraphicsState.pipelineConfig;
    mBatch.mvpMatrix      = mGraphicsState.mvpMatrix;
    mBatch.start          = CountU32(mVertices);

    return mBatch.batchId;
}

void DxDrawContext::EndLines()
{
    mBatch.end = CountU32(mVertices);
    mBatches.push_back(mBatch);

    mGraphicsState.pipelineConfig.bits.primitiveMode = PRIMITIVE_NODE_UNKNOWN;
}

uint32_t DxDrawContext::BeginTriangles()
{
    mGraphicsState.pipelineConfig.bits.primitiveMode = PRIMITIVE_MODE_TRIS;

    ++mBatch.batchId;

    mBatch.programId      = mCurrentProgramId;
    mBatch.pipelineConfig = mGraphicsState.pipelineConfig;
    mBatch.mvpMatrix      = mGraphicsState.mvpMatrix;
    mBatch.start          = CountU32(mVertices);

    return mBatch.batchId;
}

void DxDrawContext::EndTriangles()
{
    mBatch.end = CountU32(mVertices);
    mBatches.push_back(mBatch);

    mGraphicsState.pipelineConfig.bits.primitiveMode = PRIMITIVE_NODE_UNKNOWN;
}

void DxDrawContext::Vertex(const float2& pos)
{
    Vertex(float3(pos, 1));
}

void DxDrawContext::Vertex(const float3& pos)
{
    mVertex.position = pos;
    mVertices.push_back(mVertex);
}

void DxDrawContext::Color(const float3& color)
{
    Color(float4(color, 1));
}

void DxDrawContext::Color(const float4& color)
{
    mVertex.color = color;
}

void DxDrawContext::TexCoord(const float2& texCoord)
{
    mVertex.texCoord = texCoord;
}

void DxDrawContext::FlushToCommandList(ID3D12GraphicsCommandList* pCmdList)
{
    if ((pCmdList == nullptr) || mVertices.empty())
    {
        return;
    }

    auto& vertexBuffer = mVertexBuffers[pCmdList];
    {
        size_t dataSize   = SizeInBytes(mVertices);
        size_t bufferSize = vertexBuffer ? vertexBuffer->GetDesc().Width : 0;
        if (dataSize > bufferSize)
        {
            HRESULT hr = CreateBuffer(mRenderer, dataSize, nullptr, &vertexBuffer);
            if (FAILED(hr))
            {
                assert(false && "create vertex buffer failed");
                return;
            }
        }

        HRESULT hr = CopyDataToBuffer(SizeInBytes(mVertices), DataPtr(mVertices), vertexBuffer.Get());
        if (FAILED(hr))
        {
            assert(false && "copy to vertex buffer failed");
            return;
        }
    }

    // Vertex buffer
    D3D12_VERTEX_BUFFER_VIEW vbvs[1] = {};
    vbvs[0].BufferLocation           = vertexBuffer->GetGPUVirtualAddress();
    vbvs[0].SizeInBytes              = static_cast<UINT>(vertexBuffer->GetDesc().Width);
    vbvs[0].StrideInBytes            = sizeof(VertexData);
    pCmdList->IASetVertexBuffers(0, 1, vbvs);

    uint32_t currentPipelineMask = 0;
    for (auto& batch : mBatches)
    {
        const uint32_t vertexCount = (batch.end - batch.start);
        if (vertexCount == 0)
        {
            continue;
        }

        if (currentPipelineMask != batch.pipelineConfig.mask)
        {
            // Update current pipeline mask
            currentPipelineMask = batch.pipelineConfig.mask;

            // Find program
            auto itProgram = mPrograms.find(batch.programId);
            if (itProgram == mPrograms.end())
            {
                assert(false && "program lookup failed");
                return;
            }
            auto& program = itProgram->second;

            // Find pipeline
            auto itPipeline = program.pipelines.find(currentPipelineMask);
            if (itPipeline == program.pipelines.end())
            {
                assert(false && "pipeline lookup failed");
                return;
            }
            auto& pipeline = itPipeline->second;

            // Set root signature
            pCmdList->SetGraphicsRootSignature(program.rootSig.Get());
            // Set pipeline
            pCmdList->SetPipelineState(pipeline.Get());
        }

        switch (batch.pipelineConfig.bits.primitiveMode)
        {
            default: {
                assert(false && "unknown primitive mode");
                return;
            }
            break;

            case PRIMITIVE_MODE_LINES: {
                pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            }
            break;

            case PRIMITIVE_MODE_TRIS: {
                pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            }
            break;
        }

        // Set MVP root constnats
        pCmdList->SetGraphicsRoot32BitConstants(0, 16, &batch.mvpMatrix, 0);

        // Draw
        pCmdList->DrawInstanced(vertexCount, 1, batch.start, 0);
    }
}

void DxDrawContext::DrawGridXZ(const float2& size, uint32_t xSegs, uint32_t zSegs, float alpha)
{
    this->BeginLines();
    {
        uint32_t xLines = xSegs + 1;
        uint32_t zLines = zSegs + 1;

        float x0 = -size.x / 2.0f;
        float z0 = -size.y / 2.0f;
        float x1 = size.x / 2.0f;
        float z1 = size.y / 2.0f;
        float dx = (x1 - x0) / (xLines - 1);
        float dz = (z1 - z0) / (zLines - 1);

        // X lines
        for (uint32_t i = 0; i < xLines; ++i)
        {
            if (i == (zSegs / 2))
            {
                continue;
            }

            float  x     = x0 + i * dx;
            float3 P0    = float3(x, 0, z0);
            float3 P1    = float3(x, 0, z1);
            float4 color = float4(float3(0.5f), alpha);

            if ((i == 0) || (i == (xLines - 1)))
            {
                color = float4(float3(0.6f), color.a);
            }

            this->Color(color);
            this->Vertex(P0);
            this->Vertex(P1);
        }

        // Z lines
        for (uint32_t i = 0; i < zLines; ++i)
        {
            if (i == (zSegs / 2))
            {
                continue;
            }

            float  z     = z0 + i * dz;
            float3 P0    = float3(x0, 0, z);
            float3 P1    = float3(x1, 0, z);
            float4 color = float4(float3(0.5f), alpha);

            if ((i == 0) || (i == (zLines - 1)))
            {
                color = float4(float3(0.6f), color.a);
            }

            this->Color(color);
            this->Vertex(P0);
            this->Vertex(P1);
        }

        // X Axis
        {
            float  z     = z0 + (xSegs / 2) * dz;
            float3 P0    = float3(1.25f * x0, 0, z);
            float3 P1    = float3(1.25f * x1, 0, z);
            float4 color = float4(float3(0.9f, 0, 0), alpha);

            this->Color(color);
            this->Vertex(P0);
            this->Vertex(P1);

            P0 = float3(1.15f * x1, 0.0f, z - (0.05f * size.y));
            this->Vertex(P0);
            this->Vertex(P1);

            P0 = float3(1.15f * x1, 0.0f, z + (0.05f * size.y));
            this->Vertex(P0);
            this->Vertex(P1);
        }

        // Y Axis
        {
            float  x     = x0 + (zSegs / 2) * dx;
            float  z     = z0 + (xSegs / 2) * dz;
            float3 P0    = float3(x, 1.25f * x0, z);
            float3 P1    = float3(x, 1.25f * x1, z);
            float4 color = float4(float3(0, 0.9f, 0), alpha);

            this->Color(color);
            this->Vertex(P0);
            this->Vertex(P1);

            P0 = float3(x - (0.05f * size.x), 1.15f * x1, 0);
            this->Vertex(P0);
            this->Vertex(P1);

            P0 = float3(x + (0.05f * size.x), 1.15f * x1, 0);
            this->Vertex(P0);
            this->Vertex(P1);
        }

        // Z Axis
        {
            float  x     = x0 + (zSegs / 2) * dx;
            float3 P0    = float3(x, 0, 1.25f * x0);
            float3 P1    = float3(x, 0, 1.25f * x1);
            float4 color = float4(float3(0.2f, 0.2f, 0.99f), alpha);

            this->Color(color);
            this->Vertex(P0);
            this->Vertex(P1);

            P0 = float3(x - (0.05f * size.x), 0, 1.15f * z1);
            this->Vertex(P0);
            this->Vertex(P1);

            P0 = float3(x + (0.05f * size.x), 0, 1.15f * z1);
            this->Vertex(P0);
            this->Vertex(P1);
        }
    }
    this->EndLines();
}

void DxDrawContext::DrawMesh(const float3& position, const float3& scale, const TriMesh& mesh, bool enableVertexColor, float alpha, bool enableTexCoord)
{
    this->BeginTriangles();
    {
        auto& triangles    = mesh.GetTriangles();
        auto& positions    = mesh.GetPositions();
        auto& vertexColors = mesh.GetVertexColors();
        auto& texCoords    = mesh.GetTexCoords();

        for (auto& tri : triangles)
        {
            const uint32_t* indices = reinterpret_cast<const uint32_t*>(&tri);

            for (uint32_t i = 0; i < 3; ++i)
            {
                uint32_t index = indices[i];
                if (enableVertexColor)
                {
                    this->Color(float4(vertexColors[index], alpha));
                }

                if (enableTexCoord)
                {
                    this->TexCoord(texCoords[index]);
                }

                this->Vertex((positions[index] * scale) + position);
            }
        }
    }
    this->EndTriangles();
}

void DxDrawContext::DrawWireCone(const float3& tip, const float3& dir, float height, float angle, uint32_t segs)
{
    this->BeginLines();
    {
        glm::quat rotQuat = glm::rotation(float3(0, 0, 1), glm::normalize(dir));
        glm::mat4 rotMat  = glm::toMat4(rotQuat);

        float r  = height * tan(angle / 2.0f);
        float dt = 2.0f * 3.14159265359f / static_cast<float>(segs);
        for (uint32_t i = 0; i < segs; ++i)
        {
            float  t0 = static_cast<float>(i + 0) * dt;
            float  t1 = static_cast<float>(i + 1) * dt;
            float3 P0 = (r * float3(cos(t0), sin(t0), 0));
            float3 P1 = (r * float3(cos(t1), sin(t1), 0));
            P0 = rotMat * float4(P0, 1);
            P1 = rotMat * float4(P1, 1);
            P0 += tip;
            P1 += tip;
            P0 += height * dir;
            P1 += height * dir;
            this->Vertex(P0);
            this->Vertex(P1);
            //
            this->Vertex(tip);
            this->Vertex(P0);
            //
            this->Vertex(tip);
            this->Vertex(P1);
        }
    }
    this->EndLines();
}
