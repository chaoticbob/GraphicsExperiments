#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <string>
#include <memory>
#include <vector>

#define DEFAULT_POSITION_DISTANCE_TRESHOLD  1e-6
#define DEFAULT_TEX_COORD_DISTANCE_TRESHOLD 1e-6
#define DEFAULT_NORMAL_ANGLE_THRESHOLD      0.5 * 3.14159265359 / 180

// F0 values
const glm::vec3 F0_Generic         = glm::vec3(0.04f);
const glm::vec3 F0_MetalTitanium   = glm::vec3(0.542f, 0.497f, 0.449f);
const glm::vec3 F0_MetalChromium   = glm::vec3(0.549f, 0.556f, 0.554f);
const glm::vec3 F0_MetalIron       = glm::vec3(0.562f, 0.565f, 0.578f);
const glm::vec3 F0_MetalNickel     = glm::vec3(0.660f, 0.609f, 0.526f);
const glm::vec3 F0_MetalPlatinum   = glm::vec3(0.673f, 0.637f, 0.585f);
const glm::vec3 F0_MetalCopper     = glm::vec3(0.955f, 0.638f, 0.538f);
const glm::vec3 F0_MetalPalladium  = glm::vec3(0.733f, 0.697f, 0.652f);
const glm::vec3 F0_MetalZinc       = glm::vec3(0.664f, 0.824f, 0.850f);
const glm::vec3 F0_MetalGold       = glm::vec3(1.022f, 0.782f, 0.344f);
const glm::vec3 F0_MetalAluminum   = glm::vec3(0.913f, 0.922f, 0.924f);
const glm::vec3 F0_MetalSilver     = glm::vec3(0.972f, 0.960f, 0.915f);
const glm::vec3 F0_DiletricWater   = glm::vec3(0.020f);
const glm::vec3 F0_DiletricPlastic = glm::vec3(0.040f);
const glm::vec3 F0_DiletricGlass   = glm::vec3(0.045f);
const glm::vec3 F0_DiletricCrystal = glm::vec3(0.050f);
const glm::vec3 F0_DiletricGem     = glm::vec3(0.080f);
const glm::vec3 F0_DiletricDiamond = glm::vec3(0.150f);

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

#if defined(__APPLE__) || defined(LINUX)
        // Clang and GCC seems to have a problem with not having a constructor here, resulting
        // in the following or similar error in 101_color_cube_metal:
        //
        //   Default member initializer for 'enableVertexColors' needed within defintion
        //   of enclosing 'TriMesh' outside of member functions.
        //
        // The web seems to indicate that CLANG can have a problem with this kind of
        // thing, so I just added a default constructor here to get past it
        //
        // https://stackoverflow.com/questions/43819314/default-member-initializer-needed-within-definition-of-enclosing-class-outside

        Options() {}
#endif
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
        glm::vec3 tangent     = glm::vec3(0);
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
        std::string name             = "";               //
        uint32_t    id               = 0;                // Material id from source
        glm::vec3   baseColor        = glm::vec3(1);     // Default to white
        glm::vec3   F0               = glm::vec3(0.04f); // Shiny plastic (F0 = 0.04, roughness = 0, metalness = 0)
        float       roughness        = 0;                // Shiny plastic (F0 = 0.04, roughness = 0, metalness = 0)
        float       metalness        = 0;                // Shiny plastic (F0 = 0.04, roughness = 0, metalness = 0)
        std::string albedoTexture    = "";               //
        std::string normalTexture    = "";               //
        std::string roughnessTexture = "";               //
        std::string metalnessTexture = "";               //
        std::string aoTexture        = "";               //
    };

    // -------------------------------------------------------------------------
    // Aabb
    // -------------------------------------------------------------------------
    struct Aabb
    {
        glm::vec3 min;
        glm::vec3 max;

        glm::vec3 Center() const { return (this->min + this->max) / 2.0f; }

        float Width() const { return fabs(this->max.x - this->min.x); }
        float Height() const { return fabs(this->max.y - this->min.y); }
        float Depth() const { return fabs(this->max.z - this->min.z); }
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
            for (uint32_t i = 0; i < indexCount; ++i)
            {
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

    uint32_t                              GetNumIndices() const { return 3 * GetNumTriangles(); }
    std::vector<uint32_t>                 GetIndices() const;
    uint32_t                              GetNumTriangles() const { return static_cast<uint32_t>(mTriangles.size()); }
    const TriMesh::Triangle&              GetTriangle(uint32_t triIdx) const { return mTriangles[triIdx]; }
    const std::vector<TriMesh::Triangle>& GetTriangles() const { return mTriangles; }
    uint32_t                              AddTriangle(const Triangle& tri);
    uint32_t                              AddTriangle(uint32_t vIdx0, uint32_t vIdx1, uint32_t vIdx2);
    void                                  AddTriangles(size_t count, const uint32_t* pIndices);
    void                                  SetTriangles(size_t count, const uint32_t* pIndices);
    void                                  SetTriangles(const std::vector<uint32_t>& indices);

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
    void                          SetPositions(size_t count, const glm::vec3* pPositions);

    const std::vector<glm::vec3>& GetVertexColors() const { return mVertexColors; }
    const std::vector<glm::vec2>& GetTexCoords() const { return mTexCoords; }
    const std::vector<glm::vec3>& GetNormals() const { return mNormals; }
    const std::vector<glm::vec3>& GetTangents() const { return mTangents; }
    const std::vector<glm::vec3>& GetBitangents() const { return mBitangents; }
    const TriMesh::Aabb&          GetBounds() const { return mBounds; }
    void                          SetTexCoords(size_t count, const glm::vec2* pTexCoords);
    void                          SetNormals(size_t count, const glm::vec3* pNormals);

    uint32_t GetNumVertices() const { return static_cast<uint32_t>(mPositions.size()); }

    void AddVertex(const TriMesh::Vertex& vtx);
    void AddVertex(
        const glm::vec3& positions,
        const glm::vec3& vertexColor = glm::vec3(0),
        const glm::vec2& texCoord    = glm::vec2(0),
        const glm::vec3& normal      = glm::vec3(0),
        const glm::vec3& tangent     = glm::vec3(0),
        const glm::vec3& bitangent   = glm::vec3(0));

    void Recenter(const glm::vec3& newCenter = glm::vec3(0));

    void ScaleToFit(float targetAxisSpan = 1.0f);

    // Sets *ALL* vertex colors to \b vertexColor
    void SetVertexColors(const glm::vec3& vertexColor);

    void AppendMesh(const TriMesh& srcMesh, const std::string& groupPrefix = "");

    // Only works if there's only positions, will return if any other attribute is present.
    //
    // Optional - triangles can be spatially sorted with meshopt after welding:
    //
    //     auto indices = mesh.GetIndices();
    //     positions    = mesh.GetPositions();
    //
    //     std::vector<uint32_t> sortedIndices(mesh.GetNumIndices());
    //     meshopt_spatialSortTriangles(
    //         sortedIndices.data(),
    //         indices.data(),
    //         CountU32(indices),
    //         reinterpret_cast<const float*>(positions.data()),
    //         CountU32(positions),
    //         sizeof(glm::vec3));
    //
    void WeldVertices(
        float positionDistanceThreshold = DEFAULT_POSITION_DISTANCE_TRESHOLD,
        float texCoordDistanceThreshold = DEFAULT_TEX_COORD_DISTANCE_TRESHOLD,
        float normalAngleThreshold      = DEFAULT_NORMAL_ANGLE_THRESHOLD);

    std::vector<glm::vec3> GetTBNLineSegments(uint32_t* pNumVertices, float length = 0.1f) const;

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
        const glm::vec2& size          = glm::vec2(1),
        uint32_t         usegs         = 1,
        uint32_t         vsegs         = 1,
        glm::vec3        normalToPlane = glm::vec3(0, 1, 0),
        const Options&   options       = {});

    static TriMesh Sphere(
        float          radius,
        uint32_t       usegs   = 8,
        uint32_t       vsegs   = 8,
        const Options& options = {});

    static TriMesh Cone(
        float          height,
        float          radius,
        uint32_t       segs,
        const Options& options = {});

    static TriMesh CornellBox(const TriMesh::Options& options = {});

    static bool LoadOBJ(const std::string& path, const std::string& mtlBaseDir, const TriMesh::Options& options, TriMesh* pMesh);
    static bool LoadOBJ2(const std::string& path, TriMesh* pMesh);
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
    std::vector<glm::vec3>         mTangents;
    std::vector<glm::vec3>         mBitangents;
    TriMesh::Aabb                  mBounds = {};

private:
    friend struct CalculateTangents;
    void SetTangents(uint32_t vIdx, const glm::vec3& tangent, const glm::vec3& bitangent);
    void CalculateBounds();
};
