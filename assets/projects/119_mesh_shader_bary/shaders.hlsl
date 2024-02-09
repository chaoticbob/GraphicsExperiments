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
    float4               PositionCS  : SV_POSITION;
    nointerpolation uint VertexIndex : VERTEX_INDEX;
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
        vertices[gtid].VertexIndex = vertexIndex;
    }
}

enum VertexID { 
    FIRST = 0,
    SECOND = 1,
    THIRD = 2
};

struct PSInput {
    float4               PositionCS  : SV_POSITION;
    float3               Bary        : SV_BARYCENTRICS;
    nointerpolation uint VertexIndex : VERTEX_INDEX;
};

float4 psmain(PSInput input) : SV_TARGET
{
    //uint vIdx0 = GetAttributeAtVertex(input.VertexIndex, 0);
    //uint vIdx1 = GetAttributeAtVertex(input.VertexIndex, 1);
    //uint vIdx2 = GetAttributeAtVertex(input.VertexIndex, 2);
    
    int quadId = (int)(WaveGetLaneIndex() % 4);
    //VertexID i = (VertexID)quadId;
    
    uint vIdx = GetAttributeAtVertex(input.VertexIndex, quadId);
    
    //if (quadId == 0) vIdx = GetAttributeAtVertex(input.VertexIndex, 0);
    //if (quadId == 1) vIdx = GetAttributeAtVertex(input.VertexIndex, 1);
    //if (quadId == 2) vIdx = GetAttributeAtVertex(input.VertexIndex, 2);
        
    float3 Position = VertexPositions[vIdx];
    float3 Position0 = QuadReadLaneAt(Position, 0);
    float3 Position1 = QuadReadLaneAt(Position, 1);
    float3 Position2 = QuadReadLaneAt(Position, 2);
    Position = Position0 * input.Bary.x + Position1 * input.Bary.y + Position2 * input.Bary.z;
    Position = mul(Scene.M, float4(Position, 1.0)).xyz;
    
    float3 Normal = VertexNormals[vIdx];
    float3 Normal0 = QuadReadLaneAt(Normal, 0);
    float3 Normal1 = QuadReadLaneAt(Normal, 1);
    float3 Normal2 = QuadReadLaneAt(Normal, 2);
    Normal = Normal0 * input.Bary.x + Normal1 * input.Bary.y + Normal2 * input.Bary.z;
    Normal = mul(Scene.M, float4(Normal, 0.0)).xyz;
    
    /*
    float3 Position0 = VertexPositions[vIdx0];
    float3 Position1 = VertexPositions[vIdx1];
    float3 Position2 = VertexPositions[vIdx2];
    float3 Position = Position0 * input.Bary.x + Position1 * input.Bary.y + Position2 * input.Bary.z;
    Position = mul(Scene.M, float4(Position, 1.0)).xyz;
    
    float3 Normal0 = VertexNormals[vIdx0];
    float3 Normal1 = VertexNormals[vIdx1];
    float3 Normal2 = VertexNormals[vIdx2];
    float3 Normal  = Normal0 * input.Bary.x + Normal1 * input.Bary.y + Normal2 * input.Bary.z;
    Normal =  mul(Scene.M, float4(Normal, 0.0)).xyz;	
    */
    
    float3 V = normalize(Scene.eyePosition - Position);
    float3 L = normalize(Scene.lightPosition - Position);
    float3 H = normalize(V + L);
    float3 N = normalize(Normal);
    float  NoL = saturate(dot(N, L));
    float  NoH = saturate(dot(N, H));

    float d = NoL;
    float s = pow(NoH, 50.0);
    float a = 0.65;
    
    float3 color = float3(0.549, 0.556, 0.554) * (s + d + a);
    //color = Position;

    return float4(color, 1.0);
}
