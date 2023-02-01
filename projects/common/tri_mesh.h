#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <string>
#include <memory>
#include <vector>

//
// This class is not optimal, there is a lot of excessive copying.
//
class TriMesh
{
public:
    // -------------------------------------------------------------------------
    // Axis
    // -------------------------------------------------------------------------
    enum Axis
    {
        AXIS_POS_X = 0x01,
        AXIS_NEG_X = 0x02,
        AXIS_POS_Y = 0x04,
        AXIS_NEG_Y = 0x08,
        AXIS_POS_Z = 0x10,
        AXIS_NEG_Z = 0x20,
        ALL_AXES   = AXIS_POS_X | AXIS_NEG_X | AXIS_POS_Y | AXIS_NEG_Y | AXIS_POS_Z | AXIS_NEG_Z,
    };

    // -------------------------------------------------------------------------
    // Types
    // -------------------------------------------------------------------------
    using MaterialId = uint32_t;

    // -------------------------------------------------------------------------
    // Options
    // -------------------------------------------------------------------------
    struct Options
    {
        bool      enableVertexColors = false;
        bool      enableTexCoords    = false;
        bool      enableNormals      = false;
        bool      enableTangents     = false;
        glm::vec3 center             = glm::vec3(0);
        glm::vec2 texCoordScale      = glm::vec2(1);
        bool      faceInside         = false;
        bool      invertTexCoordsV   = false;
        bool      applyTransform     = false;
        glm::vec3 transformTranslate = glm::vec3(0);
        glm::vec3 transformRotate    = glm::vec3(0);
        glm::vec3 transformScale     = glm::vec3(1);
    };

    // -------------------------------------------------------------------------
    // Vertex
    // -------------------------------------------------------------------------
    struct Vertex
    {
        glm::vec3 position    = glm::vec3(0);
        glm::vec3 vertexColor = glm::vec3(0);
        glm::vec2 texCoord    = glm::vec2(0);
        glm::vec3 normal      = glm::vec3(0);
        glm::vec4 tangent     = glm::vec4(0);
        glm::vec3 bitangent   = glm::vec3(0);
    };

    // -------------------------------------------------------------------------
    // Triangle
    // -------------------------------------------------------------------------
    struct Triangle
    {
        uint32_t vIdx0 = UINT32_MAX;
        uint32_t vIdx1 = UINT32_MAX;
        uint32_t vIdx2 = UINT32_MAX;
    };

    // -------------------------------------------------------------------------
    // Material
    // -------------------------------------------------------------------------
    struct Material
    {
        std::string name             = "";           //
        uint32_t    id               = 0;            // Material id from source
        glm::vec3   albedo           = glm::vec3(1); // Default to white
        float       F0               = 0.04f;        // Shiny plastic (F0 = 0.04, roughness = 0, metalness = 0)
        float       roughness        = 0;            // Shiny plastic (F0 = 0.04, roughness = 0, metalness = 0)
        float       metalness        = 0;            // Shiny plastic (F0 = 0.04, roughness = 0, metalness = 0)
        std::string albedoTexture    = "";           //
        std::string normalTexture    = "";           //
        std::string roughnessTexture = "";           //
        std::string metalnessTexture = "";           //
    };

    // -------------------------------------------------------------------------
    // Aabb
    // -------------------------------------------------------------------------
    struct Aabb
    {
        glm::vec3 min;
        glm::vec3 max;

        glm::vec3 Center() const { return (this->min + this->max) / 2.0f; }
    };

    // -------------------------------------------------------------------------
    // Group
    // -------------------------------------------------------------------------
    class Group
    {
    public:
        Group(const std::string& name = "")
            : mName(name) {}

        // Construct group from range with possible single material index
        Group(const std::string& name, uint32_t firstIndex, uint32_t indexCount, int32_t materialIndex = -1)
            : mName(name)
        {
            for (uint32_t i = 0; i < indexCount; ++i) {
                this->AddTriangleIndex(firstIndex + i, materialIndex);
            }
        }

        ~Group() {}

        const std::string& GetName() const
        {
            return mName;
        }

        uint32_t GetNumTriangleIndices() const
        {
            return static_cast<uint32_t>(mTriangleIndices.size());
        }

        const std::vector<uint32_t>& GetTriangleIndices() const
        {
            return mTriangleIndices;
        }

        const std::vector<int32_t>& GetMaterialIndices() const
        {
            return mMaterialIndices;
        }

        void AddTriangleIndex(uint32_t triangleIndex, int32_t materialIndex = -1)
        {
            mTriangleIndices.push_back(triangleIndex);
            mMaterialIndices.push_back(materialIndex);
        }

        void SetMaterialIndices(int32_t materialIndex)
        {
            std::fill(mMaterialIndices.begin(), mMaterialIndices.end(), materialIndex);
        }

        const TriMesh::Aabb GetBounds() const
        {
            return mBounds;
        }

    private:
        friend class TriMesh;
        void SetBounds(const TriMesh::Aabb& bounds)
        {
            mBounds = bounds;
        }

    private:
        std::string           mName            = "";
        std::vector<uint32_t> mTriangleIndices = {};
        std::vector<int32_t>  mMaterialIndices = {};
        TriMesh::Aabb         mBounds          = {};
    };

    // -------------------------------------------------------------------------
    // TriMesh
    // -------------------------------------------------------------------------
    TriMesh(const TriMesh::Options& options = {})
        : mOptions(options) {}

    ~TriMesh() {}

    const TriMesh::Options& GetOptions() const { return mOptions; }

    uint32_t                              GetNumTriangles() const { return static_cast<uint32_t>(mTriangles.size()); }
    const TriMesh::Triangle&              GetTriangle(uint32_t triIdx) const { return mTriangles[triIdx]; }
    const std::vector<TriMesh::Triangle>& GetTriangles() const { return mTriangles; }
    uint32_t                              AddTriangle(const Triangle& tri);
    uint32_t                              AddTriangle(uint32_t vIdx0, uint32_t vIdx1, uint32_t vIdx2);

    uint32_t                              GetNumMaterials() const { return static_cast<uint32_t>(mMaterials.size()); }
    const TriMesh::Material&              GetMaterial(uint32_t materialIndex) const { return mMaterials[materialIndex]; }
    const std::vector<TriMesh::Material>& GetMaterials() const { return mMaterials; }
    uint32_t                              AddMaterial(const TriMesh::Material& material);
    std::vector<TriMesh::Triangle>        GetTrianglesForMaterial(const int32_t materialIndex) const;

    uint32_t                           GetNumGroups() const { return static_cast<uint32_t>(mGroups.size()); }
    const TriMesh::Group&              GetGroup(uint32_t groupIndex) const { return mGroups[groupIndex]; }
    const std::vector<TriMesh::Group>& GetGroups() const { return mGroups; }
    uint32_t                           GetGroupIndex(const std::string& groupName) const;
    uint32_t                           AddGroup(const TriMesh::Group& newGroup); // Returns UINT32_MAX on error
    std::vector<TriMesh::Triangle>     GetGroupTriangles(uint32_t groupIndex) const;

    const std::vector<glm::vec3>& GetPositions() const { return mPositions; }
    const std::vector<glm::vec3>& GetVertexColors() const { return mVertexColors; }
    const std::vector<glm::vec2>& GetTexCoords() const { return mTexCoords; }
    const std::vector<glm::vec3>& GetNormals() const { return mNormals; }
    const std::vector<glm::vec4>& GetTangents() const { return mTangents; }
    const std::vector<glm::vec3>& GetBitangents() const { return mBitangents; }
    const TriMesh::Aabb&          GetBounds() const { return mBounds; }

    uint32_t GetNumVertices() const { return static_cast<uint32_t>(mPositions.size()); }

    void AddVertex(const TriMesh::Vertex& vtx);
    void AddVertex(
        const glm::vec3& positions,
        const glm::vec3& vertexColor = glm::vec3(0),
        const glm::vec2& texCoord    = glm::vec2(0),
        const glm::vec3& normal      = glm::vec3(0),
        const glm::vec4& tangent     = glm::vec4(0),
        const glm::vec3& bitangent   = glm::vec3(0));

    void Recenter(const glm::vec3& newCenter = glm::vec3(0));

    // Sets *ALL* vertex colors to \b vertexColor
    void SetVertexColors(const glm::vec3& vertexColor);

    void AppendMesh(const TriMesh& srcMesh, const std::string& groupPrefix = "");

    static TriMesh Box(
        const glm::vec3& size,
        uint8_t          activeFaces      = ALL_AXES,
        bool             perFaceTexCoords = false,
        const Options&   options          = {});

    static TriMesh Cube(
        const glm::vec3& size,
        bool             perFaceTexCoords = false,
        const Options&   options          = {});

    static TriMesh Plane(
        const glm::vec2& size            = glm::vec2(1),
        uint32_t         usegs           = 1,
        uint32_t         vsegs           = 1,
        glm::vec3        normalToPlane   = glm::vec3(0, 1, 0),
        bool             zAxisModeOpenGL = false,
        const Options&   options         = {});

    static TriMesh Sphere(
        float          radius,
        uint32_t       usegs   = 8,
        uint32_t       vsegs   = 8,
        const Options& options = {});

    static TriMesh CornellBox(const TriMesh::Options& options = {});

    static bool LoadOBJ(const std::string& path, const std::string& mtlBaseDir, const TriMesh::Options& options, TriMesh* pMesh);
    static bool WriteOBJ(const std::string path, const TriMesh& mesh);

private:
    TriMesh::Options               mOptions = {};
    std::vector<TriMesh::Triangle> mTriangles;
    std::vector<TriMesh::Material> mMaterials;
    std::vector<TriMesh::Group>    mGroups;
    std::vector<glm::vec3>         mPositions;
    std::vector<glm::vec3>         mVertexColors;
    std::vector<glm::vec2>         mTexCoords;
    std::vector<glm::vec3>         mNormals;
    std::vector<glm::vec4>         mTangents;
    std::vector<glm::vec3>         mBitangents;
    TriMesh::Aabb                  mBounds = {};

private:
    friend struct CalculateTangents;
    void SetTangents(uint32_t vIdx, const glm::vec4& tangent, const glm::vec3& bitangent);
};