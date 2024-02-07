struct SceneProperties {
    float4x4      M;
    float4x4      VP;
    float3        eyePosition;
	float 	 	  _pad0;
    float3        lightPosition;
	float 	 	  _pad1;
};

ConstantBuffer<SceneProperties> Scene : register(b0);

struct Meshlet {
	uint VertexOffset;
	uint TriangleOffset;
	uint VertexCount;
	uint TriangleCount;
};

StructuredBuffer<float3>  VertexPositions : register(t1);
StructuredBuffer<float2>  VertexTexCoords : register(t2);
StructuredBuffer<float3>  VertexNormals   : register(t3);
StructuredBuffer<Meshlet> Meshlets        : register(t4);
StructuredBuffer<uint>    VertexIndices   : register(t5);
StructuredBuffer<uint>    TriangleIndices : register(t6);

struct MeshOutput {
    float4 PositionCS : SV_POSITION;
    float3 PositionWS : POSITION_WS;
    float2 TexCoord   : TEXCOORD;
    float3 Normal     : NORMAL;	
    float3 Color      : COLOR;
};

[outputtopology("triangle")]
[numthreads(128, 1, 1)]
void msmain(
                 uint       gtid : SV_GroupThreadID, 
                 uint       gid  : SV_GroupID, 
    out indices  uint3      triangles[128], 
    out vertices MeshOutput vertices[64]) 
{
    Meshlet m = Meshlets[gid];
    SetMeshOutputCounts(m.VertexCount, m.TriangleCount);
       
    if (gtid < m.TriangleCount) {
        //
        // meshopt stores the triangle offset in bytes since it stores the
        // triangle indices as 3 consecutive bytes. 
        //
        // Since we repacked those 3 bytes to a 32-bit uint, our offset is now
        // aligned to 4 and we can easily grab it as a uint without any 
        // additional offset math.
        //
        uint packed = TriangleIndices[m.TriangleOffset + gtid];
        uint vIdx0  = (packed >>  0) & 0xFF;
        uint vIdx1  = (packed >>  8) & 0xFF;
        uint vIdx2  = (packed >> 16) & 0xFF;
        triangles[gtid] = uint3(vIdx0, vIdx1, vIdx2);
    }

    if (gtid < m.VertexCount) {
        uint vertexIndex = m.VertexOffset + gtid;        
        vertexIndex = VertexIndices[vertexIndex];
        
        float4 PositionWS4 = mul(Scene.M, float4(VertexPositions[vertexIndex], 1.0));
		
		vertices[gtid].PositionCS = mul(Scene.VP, PositionWS4);
        vertices[gtid].PositionWS = PositionWS4.xyz;
        vertices[gtid].TexCoord = VertexTexCoords[vertexIndex];
        vertices[gtid].Normal = mul(Scene.M, float4(VertexNormals[vertexIndex], 0.0)).xyz;	
        float3 color = float3(
            float(gid & 1),
            float(gid & 3) / 4,
            float(gid & 7) / 8);
        vertices[gtid].Color = color;
    }
}

float4 psmain(MeshOutput input) : SV_TARGET
{
    float3 V = normalize(Scene.eyePosition - input.PositionWS);
    float3 L = normalize(Scene.lightPosition - input.PositionWS);
    float3 H = normalize(V + L);
    float3 N = normalize(input.Normal);
    float  NoL = saturate(dot(N, L));
    float  NoH = saturate(dot(N, H));

    float d = NoL;
    float s = pow(NoH, 50.0);
    float a = 0.65;
    
    float3 color = float3(0.549, 0.556, 0.554) * (s + d + a);

    return float4(color, 1.0);
}
