#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

struct Triangle
{
    uint32_t vIdx0 = UINT32_MAX;
    uint32_t vIdx1 = UINT32_MAX;
    uint32_t vIdx2 = UINT32_MAX;
};

struct TriangleVertex
{
    glm::vec3 position    = glm::vec3(0);
    glm::vec3 vertexColor = glm::vec3(0);
    glm::vec2 texCoord    = glm::vec2(0);
    glm::vec3 normal      = glm::vec3(0);
    glm::vec3 tangent     = glm::vec3(0);
    glm::vec3 bitangent   = glm::vec3(0);
};

class TriMesh
{
public:
    struct Options
    {
        bool      enableVertexColors = false;
        bool      enableTexCoords    = false;
        bool      enableNormals      = false;
        bool      enableTangents     = false;
        bool      enableBitangents   = false;
        glm::vec2 texCoordScale      = glm::vec2(1);
        bool      invertTexCoordsV   = false;
    };

    struct Aabb
    {
        glm::vec3 min;
        glm::vec3 max;
    };

    struct Group
    {
        std::string           name;
        std::vector<uint32_t> triangleIndices;
    };

    TriMesh(const Options& options = {})
        : mOptions(options) {}

    ~TriMesh() {}

    const Options& GetOptions() const { return mOptions; }

    const std::vector<Triangle>& GetTriangles() const { return mTriangles; }

    void AddTriangle(const Triangle& tri);
    void AddTriangle(uint32_t vIdx0, uint32_t vIdx1, uint32_t vIdx2);

    const std::vector<glm::vec3>& GetPositions() const { return mPositions; }
    const std::vector<glm::vec3>& GetVertexColors() const { return mVertexColors; }
    const std::vector<glm::vec2>& GetTexCoords() const { return mTexCoords; }
    const std::vector<glm::vec3>& GetNormals() const { return mNormals; }
    const std::vector<glm::vec3>& GetTangents() const { return mTangents; }
    const std::vector<glm::vec3>& GetBitangents() const { return mBitangents; }

    uint32_t GetNumVertices() const { return static_cast<uint32_t>(mPositions.size()); }

    void AddVertex(const TriangleVertex& vtx);
    void AddVertex(
        const glm::vec3& positions,
        const glm::vec3& vertexColor = glm::vec3(0),
        const glm::vec2& texCoord    = glm::vec2(0),
        const glm::vec3& normal      = glm::vec3(0),
        const glm::vec3& tangent     = glm::vec3(0),
        const glm::vec3& bitangent   = glm::vec3(0));

    static TriMesh Cube(const glm::vec3& size, bool perFaceTexCoords, const Options& options = {});

    static TriMesh Plane(
        const glm::vec2& size            = glm::vec2(1),
        uint32_t         usegs           = 1,
        uint32_t         vsegs           = 1,
        glm::vec3        normalToPlane   = glm::vec3(0, 1, 0),
        bool             zAxisModeOpenGL = false,
        const Options&   options         = {});

    static TriMesh Sphere(float radius, uint32_t usegs, uint32_t vsegs, const Options& options = {});

    static bool LoadOBJ(const std::string& path, const Options& options, TriMesh* pMesh);
    static bool WriteOBJ(const std::string path, const TriMesh& mesh);

private:
    Options                mOptions = {};
    std::vector<Triangle>  mTriangles;
    std::vector<glm::vec3> mPositions;
    std::vector<glm::vec3> mVertexColors;
    std::vector<glm::vec2> mTexCoords;
    std::vector<glm::vec3> mNormals;
    std::vector<glm::vec3> mTangents;
    std::vector<glm::vec3> mBitangents;
    Aabb                   mBounds = {};
};