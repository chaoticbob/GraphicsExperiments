#pragma once

#include "dx_renderer.h"

#include <glm/glm.hpp>
using float2   = glm::vec2;
using float3   = glm::vec3;
using float4   = glm::vec4;
using float2x2 = glm::mat2x2;
using float3x3 = glm::mat3x3;
using float4x4 = glm::mat4x4;

#include <map>
#include <vector>

class DxDrawContext
{
public:
    enum BlendMode
    {
        BLEND_MODE_NONE     = 0,
        BLEND_MODE_ALPHA    = 1,
        BLEND_MODE_ADDITIVE = 2,
    };

    enum CullMode
    {
        CULL_MODE_NONE  = 0,
        CULL_MODE_BACK  = 1,
        CULL_MODE_FRONT = 2,
    };

    enum Error
    {
        SUCCESS                      = 0,
        ERROR_NO_SHADER_CODE         = -1,
        ERROR_NO_VS_ENTRY_POINT      = -2,
        ERROR_NO_PS_ENTRY_POINT      = -3,
        ERROR_VS_COMPILE_FAILED      = -4,
        ERROR_PS_COMPILE_FAILED      = -5,
        ERROR_ROOT_SIG_CREATE_FAILED = -6,
        ERROR_PIPELINE_CREATE_FAILED = -7,
    };

    DxDrawContext(DxRenderer* pRenderer, DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat = DXGI_FORMAT_UNKNOWN);
    ~DxDrawContext();

    static int32_t GetStockProgramDrawVertexColor();

    int32_t CreateProgram(
        const std::string& shaderCode,
        const std::string& vsEntryPoint,
        const std::string& psEntryPoint);

    void Reset();

    void SetProgram(int32_t program);

    void SetDepthRead(bool enable = true);
    void SetDepthWrite(bool enable = true);

    void SetBlendNone();
    void SetBlendAlpha();
    void SetBlendAdditive();

    void SetMatrix(const float4x4& matrix);

    void SetBatchMatrix(uint32_t batchId, const float4x4& matrix);

    uint32_t BeginLines();
    void     EndLines();

    uint32_t BeginTriangles();
    void     EndTriangles();

    void Vertex(const float2& pos);
    void Vertex(const float3& pos);

    void Color(const float3& color);
    void Color(const float4& color);

    void TexCoord(const float2& texCoord);

    void FlushToCommandList(ID3D12GraphicsCommandList* pCmdList);

    void DrawGridXZ(const float2& size, uint32_t xSegs, uint32_t zSegs);

private:
    enum PrimitiveMode
    {
        PRIMITIVE_NODE_UNKNOWN = 0,
        PRIMITIVE_MODE_LINES   = 1,
        PRIMITIVE_MODE_TRIS    = 2,
    };

    enum DepthFlag
    {
        DEPTH_FLAG_NONE       = 0x0,
        DEPTH_FLAG_READ_ONLY  = 0x1,
        DEPTH_FLAG_WRITE_ONLY = 0x2,
        DEPTH_FLAG_READ_WRITE = 0x3,
    };

    union GraphicsPipelineConfig
    {
        struct
        {
            uint32_t primitiveMode : 4;
            uint32_t depthEnable   : 1;
            uint32_t blendMode     : 4;
            uint32_t cullMode      : 4;
        } bits;
        uint32_t mask = 0;
    };

    struct GraphicsState
    {
        GraphicsPipelineConfig pipelineConfig = {};
        uint32_t               depthFlags     = 0;
        float4x4               mvpMatrix      = float4x4(1);
    };

    struct Batch
    {
        uint32_t               batchId        = 0;
        int32_t                programId      = -1;
        GraphicsPipelineConfig pipelineConfig = {};
        float4x4               mvpMatrix      = float4x4(1);
        uint32_t               start          = UINT32_MAX;
        uint32_t               end            = UINT32_MAX;
    };

    struct VertexData
    {
        float3 position;
        float4 color;
        float2 texCoord;
    };

    DxRenderer* mRenderer  = nullptr;
    DXGI_FORMAT mRTVFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT mDSVFormat = DXGI_FORMAT_UNKNOWN;

    GraphicsState mGraphicsState    = {};
    int32_t       mCurrentProgramId = -1;

    VertexData              mVertex   = {};
    std::vector<VertexData> mVertices = {};

    Batch              mBatch   = {};
    std::vector<Batch> mBatches = {};

    std::map<ID3D12GraphicsCommandList*, ComPtr<ID3D12Resource>> mVertexBuffers;

    struct Program
    {
        int32_t                                         id        = 0;
        ComPtr<ID3D12RootSignature>                     rootSig   = nullptr;
        std::map<uint32_t, ComPtr<ID3D12PipelineState>> pipelines = {};
    };

    int32_t                    mProgramIdCounter = 0;
    std::map<int32_t, Program> mPrograms;
};