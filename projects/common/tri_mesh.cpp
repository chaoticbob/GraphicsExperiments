#include "tri_mesh.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

#include <fstream>
#include <iomanip>
#include <sstream>

//
// Converts spherical coordinate 'sc' to unit cartesian position.
//
// theta is the azimuth angle between [0, 2pi].
// phi is the polar angle between [0, pi].
//
// theta = 0, phi =[0, pi] sweeps the positive X axis:
//    SphericalToCartesian(0, 0)    = (0,  1, 0)
//    SphericalToCartesian(0, pi/2) = (1,  0, 0)
//    SphericalToCartesian(0, pi)   = (0, -1, 0)
//
// theta = [0, 2pi], phi = [pi/2] sweeps a circle:
//    SphericalToCartesian(0,     pi/2) = ( 1, 0, 0)
//    SphericalToCartesian(pi/2,  pi/2) = ( 0, 0, 1)
//    SphericalToCartesian(pi  ,  pi/2) = (-1, 0, 0)
//    SphericalToCartesian(3pi/2, pi/2) = ( 0, 0,-1)
//    SphericalToCartesian(2pi,   pi/2) = ( 1, 0, 0)
//
static inline glm::vec3 SphericalToCartesian(float theta, float phi)
{
    return glm::vec3(
        cos(theta) * sin(phi), // x
        cos(phi),              // y
        sin(theta) * sin(phi)  // z
    );
}

static inline glm::vec3 SphericalTangent(float theta, float phi)
{
    return glm::vec3(
        sin(theta), // x
        0,          // y
        -cos(theta) // z
    );
}

void TriMesh::AddTriangle(const Triangle& tri)
{
    mTriangles.push_back(tri);
}

void TriMesh::AddTriangle(uint32_t vIdx0, uint32_t vIdx1, uint32_t vIdx2)
{
    Triangle tri = {vIdx0, vIdx1, vIdx2};
    AddTriangle(tri);
}

void TriMesh::AddVertex(const TriangleVertex& vtx)
{
    mPositions.push_back(vtx.position);
    if (mPositions.size() > 1) {
        mBounds.min = glm::min(mBounds.min, vtx.position);
        mBounds.max = glm::max(mBounds.max, vtx.position);
    }
    else {
        mBounds.min = vtx.position;
        mBounds.max = vtx.position;
    }

    if (mOptions.enableVertexColors) {
        mVertexColors.push_back(vtx.vertexColor);
    }
    if (mOptions.enableTexCoords) {
        mTexCoords.push_back(vtx.texCoord);
    }
    if (mOptions.enableNormals) {
        mNormals.push_back(vtx.normal);
    }
    if (mOptions.enableTangents) {
        mTangents.push_back(vtx.tangent);
    }
    if (mOptions.enableBitangents) {
        mBitangents.push_back(vtx.bitangent);
    }
}

void TriMesh::AddVertex(
    const glm::vec3& position,
    const glm::vec3& vertexColor,
    const glm::vec2& texCoord,
    const glm::vec3& normal,
    const glm::vec3& tangent,
    const glm::vec3& bitangent)
{
    TriangleVertex vtx = {position, vertexColor, texCoord, normal, tangent, bitangent};
    AddVertex(vtx);
}

TriMesh TriMesh::Cube(const glm::vec3& size, bool perFaceTexCoords, const Options& options)
{
    const float hx = size.x / 2.0f;
    const float hy = size.y / 2.0f;
    const float hz = size.z / 2.0f;

    // clang-format off
    std::vector<float> vertexData = {  
        // position      // vertex colors    // texcoords  // normal           // tangents          // bitangents
         hx,  hy, -hz,    1.0f, 0.0f, 0.0f,   0.0f, 0.0f,   0.0f, 0.0f,-1.0f,  -1.0f, 0.0f, 0.0f,   0.0f,-1.0f, 0.0f,  //  0  -Z side
         hx, -hy, -hz,    1.0f, 0.0f, 0.0f,   0.0f, 1.0f,   0.0f, 0.0f,-1.0f,  -1.0f, 0.0f, 0.0f,   0.0f,-1.0f, 0.0f,  //  1
        -hx, -hy, -hz,    1.0f, 0.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f,-1.0f,  -1.0f, 0.0f, 0.0f,   0.0f,-1.0f, 0.0f,  //  2
        -hx,  hy, -hz,    1.0f, 0.0f, 0.0f,   1.0f, 0.0f,   0.0f, 0.0f,-1.0f,  -1.0f, 0.0f, 0.0f,   0.0f,-1.0f, 0.0f,  //  3
                                                                                                  
        -hx,  hy,  hz,    0.0f, 1.0f, 0.0f,   0.0f, 0.0f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f,-1.0f, 0.0f,  //  4  +Z side
        -hx, -hy,  hz,    0.0f, 1.0f, 0.0f,   0.0f, 1.0f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f,-1.0f, 0.0f,  //  5
         hx, -hy,  hz,    0.0f, 1.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f,-1.0f, 0.0f,  //  6
         hx,  hy,  hz,    0.0f, 1.0f, 0.0f,   1.0f, 0.0f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f,-1.0f, 0.0f,  //  7
                                                                                                  
        -hx,  hy, -hz,   -0.0f, 0.0f, 1.0f,   0.0f, 0.0f,  -1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f,-1.0f, 0.0f,  //  8  -X side
        -hx, -hy, -hz,   -0.0f, 0.0f, 1.0f,   0.0f, 1.0f,  -1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f,-1.0f, 0.0f,  //  9
        -hx, -hy,  hz,   -0.0f, 0.0f, 1.0f,   1.0f, 1.0f,  -1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f,-1.0f, 0.0f,  // 10
        -hx,  hy,  hz,   -0.0f, 0.0f, 1.0f,   1.0f, 0.0f,  -1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f,-1.0f, 0.0f,  // 11
                                                                                                  
         hx,  hy,  hz,    1.0f, 1.0f, 0.0f,   0.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,-1.0f,   0.0f,-1.0f, 0.0f,  // 12  +X side
         hx, -hy,  hz,    1.0f, 1.0f, 0.0f,   0.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,-1.0f,   0.0f,-1.0f, 0.0f,  // 13
         hx, -hy, -hz,    1.0f, 1.0f, 0.0f,   1.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,-1.0f,   0.0f,-1.0f, 0.0f,  // 14
         hx,  hy, -hz,    1.0f, 1.0f, 0.0f,   1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,-1.0f,   0.0f,-1.0f, 0.0f,  // 15
                                                                                                  
        -hx, -hy,  hz,    1.0f, 0.0f, 1.0f,   0.0f, 0.0f,   0.0f,-1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,-1.0f,  // 16  -Y side
        -hx, -hy, -hz,    1.0f, 0.0f, 1.0f,   0.0f, 1.0f,   0.0f,-1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,-1.0f,  // 17
         hx, -hy, -hz,    1.0f, 0.0f, 1.0f,   1.0f, 1.0f,   0.0f,-1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,-1.0f,  // 18
         hx, -hy,  hz,    1.0f, 0.0f, 1.0f,   1.0f, 0.0f,   0.0f,-1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,-1.0f,  // 19
                                                                                                  
        -hx,  hy, -hz,    0.0f, 1.0f, 1.0f,   0.0f, 0.0f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,  // 20  +Y side
        -hx,  hy,  hz,    0.0f, 1.0f, 1.0f,   0.0f, 1.0f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,  // 21
         hx,  hy,  hz,    0.0f, 1.0f, 1.0f,   1.0f, 1.0f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,  // 22
         hx,  hy, -hz,    0.0f, 1.0f, 1.0f,   1.0f, 0.0f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,  // 23
    };

    float u0 = 0.0f;
    float u1 = 1.0f / 3.0f;
    float u2 = 2.0f / 3.0f;
    float u3 = 1.0f;

    float v0 = 0.0f;
    float v1 = 1.0f / 2.0f;
    float v2 = 1.0f;

    std::vector<float> perFaceTexCoordsData{
        // texcoords 
         u2, v1,   //  0  -Z side
         u2, v2,   //  1
         u3, v2,   //  2
         u3, v1,   //  3
                      
         u2, v0,   //  4  +Z side
         u2, v1,   //  5
         u3, v1,   //  6
         u3, v0,   //  7
                      
         u0, v1,   //  8  -X side
         u0, v2,   //  9
         u1, v2,   // 10
         u1, v1,   // 11
                      
         u0, v0,   // 12  +X side
         u0, v1,   // 13
         u1, v1,   // 14
         u1, v0,   // 15
                      
         u1, v1,   // 16  -Y side
         u1, v2,   // 17
         u2, v2,   // 18
         u2, v1,   // 19
                      
         u1, v0,   // 20  +Y side
         u1, v1,   // 21
         u2, v1,   // 22
         u2, v0,   // 23
    };

    std::vector<uint32_t> indexData = {
        0,  1,  2, // -Z side
        0,  2,  3,

        4,  5,  6, // +Z side
        4,  6,  7,

        8,  9, 10, // -X side
        8, 10, 11,

        12, 13, 14, // +X side
        12, 14, 15,

        16, 17, 18, // -X side
        16, 18, 19,

        20, 21, 22, // +X side
        20, 22, 23,
    };
    // clang-format on

    TriMesh mesh = TriMesh(options);

    const TriangleVertex* pVertices         = reinterpret_cast<const TriangleVertex*>(vertexData.data());
    const glm::vec2*      pPerfaceTexCoords = reinterpret_cast<const glm::vec2*>(perFaceTexCoordsData.data());
    for (size_t i = 0; i < 24; ++i) {
        TriangleVertex vtx = pVertices[i];
        if (perFaceTexCoords) {
            vtx.texCoord = pPerfaceTexCoords[i];
        }
        mesh.AddVertex(vtx);
    }

    const Triangle* pTriangles = reinterpret_cast<const Triangle*>(indexData.data());
    for (size_t i = 0; i < 12; ++i) {
        const Triangle& tri = pTriangles[i];
        mesh.AddTriangle(tri);
    }

    return mesh;
}

TriMesh TriMesh::Plane(
    const glm::vec2& size,
    uint32_t         usegs,
    uint32_t         vsegs,
    glm::vec3        normalToPlane,
    bool             zAxisModeOpenGL,
    const Options&   options)

{
    float     zScale = zAxisModeOpenGL ? -1.0f : 1.0f;
    glm::vec3 P0     = glm::vec3(-0.5f, 0.0f, +0.5f * zScale) * glm::vec3(size.x, 1.0f, size.y);
    glm::vec3 P1     = glm::vec3(-0.5f, 0.0f, -0.5f * zScale) * glm::vec3(size.x, 1.0f, size.y);
    glm::vec3 P2     = glm::vec3(+0.5f, 0.0f, -0.5f * zScale) * glm::vec3(size.x, 1.0f, size.y);
    glm::vec3 P3     = glm::vec3(+0.5f, 0.0f, +0.5f * zScale) * glm::vec3(size.x, 1.0f, size.y);

    const uint32_t uverts = usegs + 1;
    const uint32_t vverts = vsegs + 1;

    float du = 1.0f / usegs;
    float dv = 1.0f / vsegs;

    const glm::vec3 T = glm::vec3(1, 0, 0);
    const glm::vec3 B = glm::vec3(0, 0, zScale);
    const glm::vec3 N = glm::vec3(0, 1, 0);

    glm::quat rotQuat = glm::rotation(N, glm::normalize(normalToPlane));
    glm::mat4 rotMat  = glm::toMat4(rotQuat);

    TriMesh mesh = TriMesh(options);

    for (uint32_t j = 0; j < vverts; ++j) {
        for (uint32_t i = 0; i < uverts; ++i) {
            float     u = i * du;
            float     v = j * dv;
            glm::vec3 P = (1.0f - u) * (1.0f - v) * P0 +
                          (1.0f - u) * v * P1 +
                          u * v * P2 +
                          u * (1.0f - v) * P3;
            glm::vec3 position  = rotMat * glm::vec4(P, 1);
            glm::vec3 color     = glm::vec3(u, v, 0);
            glm::vec2 texCoord  = glm::vec2(u, v);
            glm::vec3 normal    = rotMat * glm::vec4(N, 0);
            glm::vec3 tangent   = rotMat * glm::vec4(T, 0);
            glm::vec3 bitangent = rotMat * glm::vec4(B, 0);

            mesh.AddVertex(
                position,
                color,
                texCoord,
                normal,
                tangent,
                bitangent);
        }
    }

    for (uint32_t j = 1; j < vverts; ++j) {
        for (uint32_t i = 1; i < uverts; i++) {
            uint32_t i0 = i - 1;
            uint32_t j0 = j - 1;
            uint32_t i1 = i;
            uint32_t j1 = j;
            uint32_t v0 = j0 * uverts + i0;
            uint32_t v1 = j1 * uverts + i0;
            uint32_t v2 = j1 * uverts + i1;
            uint32_t v3 = j0 * uverts + i1;

            if (zAxisModeOpenGL) {
                // Counter-clockwise
                mesh.AddTriangle(v0, v1, v2);
                mesh.AddTriangle(v0, v2, v3);
            }
            else {
                // Clockwise
                mesh.AddTriangle(v0, v2, v1);
                mesh.AddTriangle(v0, v3, v2);
            }
        }
    }

    return mesh;
}

TriMesh TriMesh::Sphere(float radius, uint32_t usegs, uint32_t vsegs, const Options& options)
{
    constexpr float kPi      = glm::pi<float>();
    constexpr float kTwoPi   = 2.0f * kPi;
    const float     kEpsilon = 0.00001f;

    const uint32_t uverts = usegs + 1;
    const uint32_t vverts = vsegs + 1;

    float dt = kTwoPi / static_cast<float>(usegs);
    float dp = kPi / static_cast<float>(vsegs);

    TriMesh mesh = TriMesh(options);

    for (uint32_t i = 0; i < uverts; ++i) {
        for (uint32_t j = 0; j < vverts; ++j) {
            float     theta     = i * dt;
            float     phi       = j * dp;
            float     u         = options.texCoordScale.x * theta / kTwoPi;
            float     v         = options.texCoordScale.y * phi / kPi;
            glm::vec3 P         = SphericalToCartesian(theta, phi);
            glm::vec3 position  = radius * P;
            glm::vec3 color     = glm::vec3(u, v, 0);
            glm::vec2 texCoord  = glm::vec2(u, v);
            glm::vec3 normal    = normalize(position);
            glm::vec3 tangent   = -SphericalTangent(theta, phi);
            glm::vec3 bitangent = glm::cross(normal, tangent);

            mesh.AddVertex(
                position,
                color,
                texCoord,
                normal,
                tangent,
                bitangent);
        }
    }

    auto& positions = mesh.GetPositions();

    std::vector<uint32_t> indexData;
    for (uint32_t i = 1; i < uverts; ++i) {
        for (uint32_t j = 1; j < vverts; ++j) {
            uint32_t i0 = i - 1;
            uint32_t i1 = i;
            uint32_t j0 = j - 1;
            uint32_t j1 = j;
            uint32_t v0 = i1 * vverts + j0;
            uint32_t v1 = i1 * vverts + j1;
            uint32_t v2 = i0 * vverts + j1;
            uint32_t v3 = i0 * vverts + j0;

            auto& P0 = positions[v0];
            auto& P1 = positions[v1];
            auto& P2 = positions[v2];
            auto& P3 = positions[v3];

            float dist0 = glm::distance2(P0, P1);
            float dist1 = glm::distance2(P0, P2);
            float dist2 = glm::distance2(P1, P2);
            // Skip degenerate triangles
            if ((dist0 > kEpsilon) && (dist1 > kEpsilon) && (dist2 > kEpsilon)) {
                mesh.AddTriangle(v0, v1, v2);
            }

            dist0 = glm::distance2(P0, P2);
            dist1 = glm::distance2(P0, P3);
            dist2 = glm::distance2(P2, P3);
            // Skip degenerate triangles
            if ((dist0 > kEpsilon) && (dist1 > kEpsilon) && (dist2 > kEpsilon)) {
                mesh.AddTriangle(v0, v2, v3);
            }
        }
    }

    return mesh;
}

bool TriMesh::LoadOBJ(const std::string& path, const Options& options, TriMesh* pMesh)
{
    if (pMesh == nullptr) {
        return false;
    }

    const std::vector<glm::vec3> colors = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 0.0f},
        {1.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},
    };

    tinyobj::attrib_t                attrib;
    std::vector<tinyobj::shape_t>    shapes;
    std::vector<tinyobj::material_t> materials;

    std::string warn;
    std::string err;
    bool        loaded = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(), nullptr, true);

    if (!loaded || !err.empty()) {
        return false;
    }

    size_t numShapes = shapes.size();
    if (numShapes == 0) {
        return false;
    }

    // Build geometry
    for (size_t shapeIdx = 0; shapeIdx < numShapes; ++shapeIdx) {
        const tinyobj::shape_t& shape     = shapes[shapeIdx];
        const tinyobj::mesh_t&  shapeMesh = shape.mesh;

        size_t numTriangles = shapeMesh.indices.size() / 3;
        for (size_t triIdx = 0; triIdx < numTriangles; ++triIdx) {
            size_t triVtxIdx0 = triIdx * 3 + 0;
            size_t triVtxIdx1 = triIdx * 3 + 1;
            size_t triVtxIdx2 = triIdx * 3 + 2;

            // Index data
            const tinyobj::index_t& dataIdx0 = shapeMesh.indices[triVtxIdx0];
            const tinyobj::index_t& dataIdx1 = shapeMesh.indices[triVtxIdx1];
            const tinyobj::index_t& dataIdx2 = shapeMesh.indices[triVtxIdx2];

            // Vertex data
            TriangleVertex vtx0 = {};
            TriangleVertex vtx1 = {};
            TriangleVertex vtx2 = {};

            // Pick a face color
            glm::vec3 faceColor = colors[triIdx % colors.size()];
            vtx0.vertexColor    = faceColor;
            vtx1.vertexColor    = faceColor;
            vtx2.vertexColor    = faceColor;

            // Positions
            {
                int i0        = 3 * dataIdx0.vertex_index + 0;
                int i1        = 3 * dataIdx0.vertex_index + 1;
                int i2        = 3 * dataIdx0.vertex_index + 2;
                vtx0.position = glm::vec3(attrib.vertices[i0], attrib.vertices[i1], attrib.vertices[i2]);

                i0            = 3 * dataIdx1.vertex_index + 0;
                i1            = 3 * dataIdx1.vertex_index + 1;
                i2            = 3 * dataIdx1.vertex_index + 2;
                vtx1.position = glm::vec3(attrib.vertices[i0], attrib.vertices[i1], attrib.vertices[i2]);

                i0            = 3 * dataIdx2.vertex_index + 0;
                i1            = 3 * dataIdx2.vertex_index + 1;
                i2            = 3 * dataIdx2.vertex_index + 2;
                vtx2.position = glm::vec3(attrib.vertices[i0], attrib.vertices[i1], attrib.vertices[i2]);
            }

            // TexCoords
            if ((dataIdx0.texcoord_index != -1) && (dataIdx1.texcoord_index != -1) && (dataIdx2.texcoord_index != -1)) {
                int i0        = 2 * dataIdx0.texcoord_index + 0;
                int i1        = 2 * dataIdx0.texcoord_index + 1;
                vtx0.texCoord = glm::vec2(attrib.texcoords[i0], attrib.texcoords[i1]);

                i0            = 2 * dataIdx1.texcoord_index + 0;
                i1            = 2 * dataIdx1.texcoord_index + 1;
                vtx1.texCoord = glm::vec2(attrib.texcoords[i0], attrib.texcoords[i1]);

                i0            = 2 * dataIdx2.texcoord_index + 0;
                i1            = 2 * dataIdx2.texcoord_index + 1;
                vtx2.texCoord = glm::vec2(attrib.texcoords[i0], attrib.texcoords[i1]);

                // Scale tex coords
                vtx0.texCoord *= options.texCoordScale;
                vtx1.texCoord *= options.texCoordScale;
                vtx2.texCoord *= options.texCoordScale;

                if (options.invertTexCoordsV) {
                    vtx0.texCoord.y = 1.0f - vtx0.texCoord.y;
                    vtx1.texCoord.y = 1.0f - vtx1.texCoord.y;
                    vtx2.texCoord.y = 1.0f - vtx2.texCoord.y;
                }
            }

            // Normals
            if ((dataIdx0.normal_index != -1) && (dataIdx1.normal_index != -1) && (dataIdx2.normal_index != -1)) {
                int i0      = 3 * dataIdx0.normal_index + 0;
                int i1      = 3 * dataIdx0.normal_index + 1;
                int i2      = 3 * dataIdx0.normal_index + 2;
                vtx0.normal = glm::vec3(attrib.normals[i0], attrib.normals[i1], attrib.normals[i2]);

                i0          = 3 * dataIdx1.normal_index + 0;
                i1          = 3 * dataIdx1.normal_index + 1;
                i2          = 3 * dataIdx1.normal_index + 2;
                vtx1.normal = glm::vec3(attrib.normals[i0], attrib.normals[i1], attrib.normals[i2]);

                i0          = 3 * dataIdx2.normal_index + 0;
                i1          = 3 * dataIdx2.normal_index + 1;
                i2          = 3 * dataIdx2.normal_index + 2;
                vtx2.normal = glm::vec3(attrib.normals[i0], attrib.normals[i1], attrib.normals[i2]);
            }

            // Add vertices
            pMesh->AddVertex(vtx0);
            pMesh->AddVertex(vtx1);
            pMesh->AddVertex(vtx2);

            // Triangles
            uint32_t numVertices = pMesh->GetNumVertices();
            uint32_t vIdx0       = numVertices - 3;
            uint32_t vIdx1       = numVertices - 2;
            uint32_t vIdx2       = numVertices - 1;

            pMesh->AddTriangle(vIdx0, vIdx1, vIdx2);
        }
    }

    return true;
}

bool TriMesh::WriteOBJ(const std::string path, const TriMesh& mesh)
{
    std::ofstream os = std::ofstream(path.c_str());
    if (!os.is_open()) {
        return false;
    }

    bool writeTexCoords = mesh.GetOptions().enableTexCoords;
    bool writeNormals   = mesh.GetOptions().enableNormals;

    os << std::setprecision(6);
    os << std::fixed;

    os << "# vertices\n";
    {
        auto& positions = mesh.GetPositions();
        for (auto& v : positions) {
            os << "v " << v.x << " " << v.y << " " << v.z << "\n";
        }
    }

    if (writeTexCoords) {
        os << "# texture coordinates\n";
        {
            auto& texCoords = mesh.GetTexCoords();
            for (auto& vt : texCoords) {
                os << "vt " << vt.x << " " << vt.y << "\n";
            }
        }
    }

    if (writeNormals) {
        os << "# normals\n";
        {
            auto& normals = mesh.GetNormals();
            for (auto& vn : normals) {
                os << "vn " << vn.x << " " << vn.y << " " << vn.z << "\n";
            }
        }
    }

    os << "# triangle faces\n";
    {
        auto& triangles = mesh.GetTriangles();
        for (auto& tri : triangles) {
            // OBJ indices are 1 based
            uint32_t vIdx0 = tri.vIdx0 + 1;
            uint32_t vIdx1 = tri.vIdx1 + 1;
            uint32_t vIdx2 = tri.vIdx2 + 1;

            os << "f";
            os << " ";

            os << vIdx0;
            if (writeTexCoords) {
                os << "/" << vIdx0;
            }
            if (writeNormals) {
                os << "/" << vIdx0;
            }
            os << " ";

            os << vIdx1;
            if (writeTexCoords) {
                os << "/" << vIdx1;
            }
            if (writeNormals) {
                os << "/" << vIdx1;
            }
            os << " ";

            os << vIdx2;
            if (writeTexCoords) {
                os << "/" << vIdx2;
            }
            if (writeNormals) {
                os << "/" << vIdx2;
            }
            os << "\n";
        }
    }

    return true;
}
