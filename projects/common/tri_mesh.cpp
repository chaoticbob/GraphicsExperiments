#if defined(WIN32)
#    define NOMINMAX
#endif

#include "tri_mesh.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#if defined(TRIMESH_USE_MIKKTSPACE)
#    include "mikktspace.h"
#endif

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/string_cast.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <unordered_map>

#include "config.h"

//
// Converts spherical coordinate (theta, phi) to unit cartesian position.
//
// theta is the azimuth angle between [0, 2pi].
// phi is the polar angle between [0, pi].
//
// theta = 0, phi =[0, pi] sweeps the positive X axis from Y = 1 to Y = -1:
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

//
// Returns tangent for spherical coordinate (theta, pi)
//
// theta is the azimuth angle between [0, 2pi].
// phi is the polar angle between [0, pi].
//
// theta = 0, phi =[0, pi] sweeps the positive X axis from Y = 1 to Y = -1:
//    SphericalTangent(0, 0)    = (0, 0, -1)
//    SphericalTangent(0, pi/2) = (0, 0, -1)
//    SphericalTangent(0, pi)   = (0, 0, -1)
//
// theta = [0, 2pi], phi = [pi/2] sweeps a circle:
//    SphericalTangent(0,     pi/2) = ( 0, 0, -1)
//    SphericalTangent(pi/2,  pi/2) = ( 1, 0,  0)
//    SphericalTangent(pi  ,  pi/2) = ( 0, 0,  1)
//    SphericalTangent(3pi/2, pi/2) = (-1, 0,  0)
//    SphericalTangent(2pi,   pi/2) = ( 0, 0, -1)
//
static inline glm::vec3 SphericalTangent(float theta, float phi)
{
    return glm::vec3(
        sin(theta), // x
        0,          // y
        -cos(theta) // z
    );
}

#if defined(TRIMESH_USE_MIKKTSPACE)
struct CalculateTangents
{
    static int  getNumFaces(const SMikkTSpaceContext* pContext);
    static int  getNumVerticesOfFace(const SMikkTSpaceContext* pContext, const int iFace);
    static void getPosition(const SMikkTSpaceContext* pContext, float fvPosOut[], const int iFace, const int iVert);
    static void getNormal(const SMikkTSpaceContext* pContext, float fvNormOut[], const int iFace, const int iVert);
    static void getTexCoord(const SMikkTSpaceContext* pContext, float fvTexcOut[], const int iFace, const int iVert);
    static void setTSpaceBasic(const SMikkTSpaceContext* pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert);

    static void Calculate(TriMesh* pMesh)
    {
        SMikkTSpaceInterface callbacks   = {};
        callbacks.m_getNumFaces          = CalculateTangents::getNumFaces;
        callbacks.m_getNumVerticesOfFace = CalculateTangents::getNumVerticesOfFace;
        callbacks.m_getPosition          = CalculateTangents::getPosition;
        callbacks.m_getNormal            = CalculateTangents::getNormal;
        callbacks.m_getTexCoord          = CalculateTangents::getTexCoord;
        callbacks.m_setTSpaceBasic       = CalculateTangents::setTSpaceBasic;

        SMikkTSpaceContext context = {};
        context.m_pInterface       = &callbacks;
        context.m_pUserData        = pMesh;

        genTangSpace(&context, 180.0f);
    }
};

int CalculateTangents::getNumFaces(const SMikkTSpaceContext* pContext)
{
    TriMesh* pMesh = static_cast<TriMesh*>(pContext->m_pUserData);
    assert((pMesh != nullptr) && "pMesh is NULL!");

    int numFaces = static_cast<int>(pMesh->GetNumTriangles());
    return numFaces;
}

int CalculateTangents::getNumVerticesOfFace(const SMikkTSpaceContext*, const int)
{
    return 3;
}

void CalculateTangents::getPosition(const SMikkTSpaceContext* pContext, float fvPosOut[], const int iFace, const int iVert)
{
    TriMesh* pMesh = static_cast<TriMesh*>(pContext->m_pUserData);
    assert((pMesh != nullptr) && "pMesh is NULL!");

    const TriMesh::Triangle& tri            = pMesh->GetTriangles()[iFace];
    const uint32_t*          pVertexIndices = reinterpret_cast<const uint32_t*>(&tri);
    uint32_t                 vIdx           = pVertexIndices[iVert];
    const glm::vec3&         position       = pMesh->GetPositions()[vIdx];

    fvPosOut[0] = position.x;
    fvPosOut[1] = position.y;
    fvPosOut[2] = position.z;
}

void CalculateTangents::getNormal(const SMikkTSpaceContext* pContext, float fvNormOut[], const int iFace, const int iVert)
{
    TriMesh* pMesh = static_cast<TriMesh*>(pContext->m_pUserData);
    assert((pMesh != nullptr) && "pMesh is NULL!");

    const TriMesh::Triangle& tri            = pMesh->GetTriangles()[iFace];
    const uint32_t*          pVertexIndices = reinterpret_cast<const uint32_t*>(&tri);
    uint32_t                 vIdx           = pVertexIndices[iVert];
    const glm::vec3&         normal         = pMesh->GetNormals()[vIdx];

    fvNormOut[0] = normal.x;
    fvNormOut[1] = normal.y;
    fvNormOut[2] = normal.z;
}

void CalculateTangents::getTexCoord(const SMikkTSpaceContext* pContext, float fvTexcOut[], const int iFace, const int iVert)
{
    TriMesh* pMesh = static_cast<TriMesh*>(pContext->m_pUserData);
    assert((pMesh != nullptr) && "pMesh is NULL!");

    const TriMesh::Triangle& tri            = pMesh->GetTriangles()[iFace];
    const uint32_t*          pVertexIndices = reinterpret_cast<const uint32_t*>(&tri);
    uint32_t                 vIdx           = pVertexIndices[iVert];
    const glm::vec2&         texCoord       = pMesh->GetTexCoords()[vIdx];

    fvTexcOut[0] = texCoord.x;
    fvTexcOut[1] = texCoord.y;
}

void CalculateTangents::setTSpaceBasic(const SMikkTSpaceContext* pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert)
{
    TriMesh* pMesh = static_cast<TriMesh*>(pContext->m_pUserData);
    assert((pMesh != nullptr) && "pMesh is NULL!");

    const TriMesh::Triangle& tri            = pMesh->GetTriangles()[iFace];
    const uint32_t*          pVertexIndices = reinterpret_cast<const uint32_t*>(&tri);
    uint32_t                 vIdx           = pVertexIndices[iVert];
    const glm::vec3&         normal         = pMesh->GetNormals()[vIdx];

    glm::vec3 tangent   = glm::vec3(fvTangent[0], fvTangent[1], fvTangent[2]);
    glm::vec3 bitangent = fSign * glm::cross(normal, glm::vec3(tangent));

    pMesh->SetTangents(vIdx, tangent, bitangent);
}
#else
struct CalculateTangents
{
};
#endif // defined(TRIMESH_USE_MIKKTSPACE)

std::vector<uint32_t> TriMesh::GetIndices() const
{
    std::vector<uint32_t> indices;
    for (auto& tri : mTriangles)
    {
        indices.push_back(tri.vIdx0);
        indices.push_back(tri.vIdx1);
        indices.push_back(tri.vIdx2);
    }
    return indices;
}

uint32_t TriMesh::AddTriangle(const TriMesh::Triangle& tri)
{
    mTriangles.push_back(tri);
    uint32_t newIndex = static_cast<uint32_t>(mTriangles.size() - 1);
    return newIndex;
}

uint32_t TriMesh::AddTriangle(uint32_t vIdx0, uint32_t vIdx1, uint32_t vIdx2)
{
    TriMesh::Triangle tri = {vIdx0, vIdx1, vIdx2};
    return AddTriangle(tri);
}

void TriMesh::AddTriangles(size_t count, const uint32_t* pIndices)
{
    assert((count % 3) == 0);
    size_t n = count / 3;
    for (size_t i = 0; i < n; ++i)
    {
        uint32_t vIdx0 = pIndices[3 * i + 0];
        uint32_t vIdx1 = pIndices[3 * i + 1];
        uint32_t vIdx2 = pIndices[3 * i + 2];
        this->AddTriangle(vIdx0, vIdx1, vIdx2);
    }
}

void TriMesh::SetTriangles(size_t count, const uint32_t* pIndices)
{
    assert((count % 3) == 0);
    mTriangles.clear();

    this->AddTriangles(count, pIndices);
}

void TriMesh::SetTriangles(const std::vector<uint32_t>& indices)
{
    assert((indices.size() % 3) == 0);
    mTriangles.clear();

    size_t n = indices.size() / 3;
    for (size_t i = 0; i < n; ++i)
    {
        uint32_t vIdx0 = indices[3 * i + 0];
        uint32_t vIdx1 = indices[3 * i + 1];
        uint32_t vIdx2 = indices[3 * i + 2];
        this->AddTriangle(vIdx0, vIdx1, vIdx2);
    }
}

uint32_t TriMesh::GetGroupIndex(const std::string& groupName) const
{
    auto it = std::find_if(
        mGroups.begin(),
        mGroups.end(),
        [&groupName](const TriMesh::Group& elem) -> bool {
            bool match = (elem.GetName() == groupName);
            return match;
        });
    if (it == mGroups.end())
    {
        return UINT32_MAX;
    }

    return static_cast<uint32_t>(std::distance(mGroups.begin(), it));
}

uint32_t TriMesh::AddGroup(const TriMesh::Group& newGroup)
{
    if (newGroup.GetName().empty())
    {
        assert(false && "group name cannot be empty");
        return UINT32_MAX;
    }

    auto it = std::find_if(
        mGroups.begin(),
        mGroups.end(),
        [&newGroup](const TriMesh::Group& elem) -> bool {
            bool match = (elem.GetName() == newGroup.GetName());
            return match;
        });
    if (it != mGroups.end())
    {
        assert(false && "group name already exists");
        return UINT32_MAX;
    }

    mGroups.push_back(newGroup);
    uint32_t newIndex = static_cast<uint32_t>(mGroups.size() - 1);

    // Calculate the AABB
    auto& addedGroup = mGroups.back();
    if (addedGroup.GetNumTriangleIndices() > 0)
    {
        TriMesh::Aabb bounds = {};
        // Set the min/max to the first vertex of the first tirangle
        uint32_t    triIdx = addedGroup.GetTriangleIndices()[0];
        const auto& tri    = mTriangles[triIdx];
        bounds.min         = mPositions[tri.vIdx0];
        bounds.max         = mPositions[tri.vIdx0];
        // Iterate through triangles and min/max on each vertex index
        for (const auto& triIdx : addedGroup.GetTriangleIndices())
        {
            const auto& tri = mTriangles[triIdx];
            // vIdx0
            bounds.min = glm::min(bounds.min, mPositions[tri.vIdx0]);
            bounds.max = glm::max(bounds.max, mPositions[tri.vIdx0]);
            // vIdx1
            bounds.min = glm::min(bounds.min, mPositions[tri.vIdx1]);
            bounds.max = glm::max(bounds.max, mPositions[tri.vIdx1]);
            // vIdx2
            bounds.min = glm::min(bounds.min, mPositions[tri.vIdx2]);
            bounds.max = glm::max(bounds.max, mPositions[tri.vIdx2]);
        }

        addedGroup.SetBounds(bounds);
    }

    return newIndex;
}

std::vector<TriMesh::Triangle> TriMesh::GetGroupTriangles(uint32_t groupIndex) const
{
    std::vector<TriMesh::Triangle> triangles;
    if (groupIndex < GetNumGroups())
    {
        for (auto& triangleIndex : mGroups[groupIndex].GetTriangleIndices())
        {
            const TriMesh::Triangle& tri = mTriangles[triangleIndex];
            triangles.push_back(tri);
        }
    }
    return triangles;
}

uint32_t TriMesh::AddMaterial(const TriMesh::Material& material)
{
    mMaterials.push_back(material);
    uint32_t newIndex = static_cast<uint32_t>(mMaterials.size() - 1);
    return newIndex;
}

std::vector<TriMesh::Triangle> TriMesh::GetTrianglesForMaterial(const int32_t materialIndex) const
{
    std::vector<TriMesh::Triangle> triangles;
    // Iterate groups...
    const uint32_t numGroups = GetNumGroups();
    for (uint32_t groupIndex = 0; groupIndex < numGroups; ++groupIndex)
    {
        auto& group = GetGroup(groupIndex);
        // Iterate triangles in group....
        const uint32_t numTriangleIndices = group.GetNumTriangleIndices();
        for (uint32_t i = 0; i < numTriangleIndices; ++i)
        {
            // Look for material indices that match \b materialIndex...
            const int32_t itMaterialIndex = group.GetMaterialIndices()[i];
            if (materialIndex == itMaterialIndex)
            {
                // ...add corresponding triangle if there's a match
                const uint32_t itTriangleIndex = group.GetTriangleIndices()[i];
                const auto&    tri             = GetTriangle(itTriangleIndex);
                triangles.push_back(tri);
            }
        }
    }

    return triangles;
}

void TriMesh::SetPositions(size_t count, const glm::vec3* pPositions)
{
    assert((count > 0) && (pPositions != nullptr));

    mPositions.clear();
    std::copy(pPositions, pPositions + count, std::back_inserter(mPositions));
}

void TriMesh::SetTexCoords(size_t count, const glm::vec2* pTexCoords)
{
    assert((count > 0) && (pTexCoords != nullptr));

    mTexCoords.clear();
    std::copy(pTexCoords, pTexCoords + count, std::back_inserter(mTexCoords));
}

void TriMesh::SetNormals(size_t count, const glm::vec3* pNormals)
{
    assert((count > 0) && (pNormals != nullptr));
    
    mNormals.clear();
    std::copy(pNormals, pNormals + count, std::back_inserter(mNormals));
}

void TriMesh::AddVertex(const TriMesh::Vertex& vtx)
{
    mPositions.push_back(vtx.position);
    if (mPositions.size() > 1)
    {
        mBounds.min = glm::min(mBounds.min, vtx.position);
        mBounds.max = glm::max(mBounds.max, vtx.position);
    }
    else
    {
        mBounds.min = vtx.position;
        mBounds.max = vtx.position;
    }

    if (mOptions.enableVertexColors)
    {
        mVertexColors.push_back(vtx.vertexColor);
    }
    if (mOptions.enableTexCoords)
    {
        mTexCoords.push_back(vtx.texCoord);
    }
    if (mOptions.enableNormals)
    {
        mNormals.push_back(vtx.normal);
    }
    if (mOptions.enableTangents)
    {
        mTangents.push_back(vtx.tangent);
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
    TriMesh::Vertex vtx = {position, vertexColor, texCoord, normal, tangent, bitangent};
    AddVertex(vtx);
}

void TriMesh::Recenter(const glm::vec3& newCenter)
{
    glm::vec3 currentCenter = mBounds.Center();
    glm::vec3 adjustment    = newCenter - currentCenter;

    for (auto& position : mPositions)
    {
        position += adjustment;
    }
}

void TriMesh::ScaleToFit(float targetAxisSpan)
{
    float maxSpan = std::max(mBounds.max.x, std::max(mBounds.max.y, mBounds.max.z));
    float scale   = targetAxisSpan / maxSpan;

    // for (auto& position : mPositions) {
    //     position *= scale;
    // }
    for (size_t i = 0; i < mPositions.size(); ++i)
    {
        mPositions[i] *= scale;
        // mNormals[i] *= scale;
    }
}

void TriMesh::SetVertexColors(const glm::vec3& vertexColor)
{
    for (auto& elem : mVertexColors)
    {
        elem = vertexColor;
    }
}

void TriMesh::SetTangents(uint32_t vIdx, const glm::vec3& tangent, const glm::vec3& bitangent)
{
    if (!mOptions.enableTangents)
    {
        return;
    }

    assert((vIdx < mTangents.size()) && "vIdx exceeds tangent storage");
    assert((vIdx < mBitangents.size()) && "vIdx exceeds bitangent storage");

    mTangents[vIdx]   = tangent;
    mBitangents[vIdx] = bitangent;
}

void TriMesh::CalculateBounds()
{
    mBounds = {};

    if (mPositions.empty())
    {
        return;
    }

    size_t i    = 0;
    mBounds.min = mBounds.max = mPositions[i];
    ++i;

    const size_t n = mPositions.size();
    for (; i < n; ++i)
    {
        mBounds.min = glm::min(mBounds.min, mPositions[i]);
        mBounds.max = glm::max(mBounds.max, mPositions[i]);
    }
}

void TriMesh::AppendMesh(const TriMesh& srcMesh, const std::string& groupPrefix)
{
    // We need to offset newly added triangle vertex indices
    // by number of existing vertices in *this* mesh.
    //
    const uint32_t vertexIndexOffset   = this->GetNumVertices();
    const uint32_t triangleIndexOffset = this->GetNumTriangles();
    const uint32_t materialIndexOffset = this->GetNumMaterials();

    // Copy vertex data
    const uint32_t srcNumVertices = srcMesh.GetNumVertices();
    for (uint32_t i = 0; i < srcNumVertices; ++i)
    {
        TriMesh::Vertex vtx = {};
        vtx.position        = srcMesh.GetPositions()[i];

        if (srcMesh.GetOptions().enableVertexColors)
        {
            vtx.vertexColor = srcMesh.GetVertexColors()[i];
        }
        if (srcMesh.GetOptions().enableTexCoords)
        {
            vtx.texCoord = srcMesh.GetTexCoords()[i];
        }
        if (srcMesh.GetOptions().enableNormals)
        {
            vtx.normal = srcMesh.GetNormals()[i];
        }
        if (srcMesh.GetOptions().enableTangents)
        {
            vtx.tangent   = srcMesh.GetTangents()[i];
            vtx.bitangent = srcMesh.GetBitangents()[i];
        }

        this->AddVertex(vtx);
    }

    // Copy triangles
    const uint32_t srcNumTriangles = srcMesh.GetNumTriangles();
    for (uint32_t i = 0; i < srcNumTriangles; ++i)
    {
        TriMesh::Triangle tri = srcMesh.GetTriangles()[i];

        // Offset new indices
        tri.vIdx0 += vertexIndexOffset;
        tri.vIdx1 += vertexIndexOffset;
        tri.vIdx2 += vertexIndexOffset;

        this->AddTriangle(tri);
    }

    // Copy materials
    const uint32_t srcNumMaterials = srcMesh.GetNumMaterials();
    for (uint32_t i = 0; i < srcNumMaterials; ++i)
    {
        auto& material = srcMesh.GetMaterials()[i];
        this->AddMaterial(material);
    }

    // Copy or create groups
    //
    // If there are groups, then copy them...
    const uint32_t srcNumGroups = srcMesh.GetNumGroups();
    if (srcNumGroups > 0)
    {
        for (uint32_t i = 0; i < srcNumGroups; ++i)
        {
            auto&       srcGroup     = srcMesh.GetGroups()[i];
            std::string newGroupName = srcGroup.GetName();
            // Prefix the name with \b groupPrefix if supplied
            if (!groupPrefix.empty())
            {
                newGroupName = groupPrefix + ":" + newGroupName;
            }

            // Create new group
            auto newGroup = Group(newGroupName);

            // Add triangle and material indices
            const uint32_t numSrcTriangleIndices = srcGroup.GetNumTriangleIndices();
            for (uint32_t j = 0; j < numSrcTriangleIndices; ++j)
            {
                uint32_t triangleIndex = srcGroup.GetTriangleIndices()[j];
                int32_t  materialIndex = srcGroup.GetMaterialIndices()[j];
                triangleIndex += triangleIndexOffset;
                if (materialIndex >= 0)
                {
                    materialIndex += materialIndexOffset;
                }
                newGroup.AddTriangleIndex(triangleIndex, materialIndex);
            }

            // Add group
            uint32_t res = this->AddGroup(newGroup);
            assert((res != UINT32_MAX) && "AddGroup failed");
        }
    }
    // ...otherwise create a group using \b groupPrefix for name
    else
    {
        if (!groupPrefix.empty())
        {
            // Group name
            const std::string& newGroupName = groupPrefix;

            // Create new group starting starting from \b triangleIndexOffset
            // for however many triangles there are in \b srcMesh.
            //
            auto newGroup = Group(newGroupName, triangleIndexOffset, srcMesh.GetNumTriangles());

            // Add group
            uint32_t res = this->AddGroup(newGroup);
            assert((res != UINT32_MAX) && "AddGroup failed");
        }
    }
}

void TriMesh::WeldVertices(
    float positionDistanceThreshold,
    float texCoordDistanceThreshold,
    float normalAngleThreshold)
{
    // if (mOptions.enableVertexColors || mOptions.enableTexCoords || mOptions.enableNormals || mOptions.enableTangents)
    if (mOptions.enableVertexColors || mOptions.enableTangents)
    {
        return;
    }

    const float positionDistanceThresholdSq = positionDistanceThreshold * positionDistanceThreshold;
    const float texCoordDistanceThresholdSq = texCoordDistanceThreshold * texCoordDistanceThreshold;

    std::unordered_map<uint32_t, uint32_t> weldedIndexMap;
    std::vector<glm::vec3>                 weldedPositions;
    std::vector<glm::vec2>                 weldedTexCoords;
    std::vector<glm::vec3>                 weldedNormals;

    const uint32_t   vertexCount       = CountU32(mPositions);
    const glm::vec3* pPosition         = mPositions.data();
    const glm::vec2* pTexCoord         = mTexCoords.data();
    const glm::vec3* pNormal           = mNormals.data();
    uint32_t         weldedVertexCount = 0;
    for (uint32_t oldIdx = 0; oldIdx < vertexCount; ++oldIdx, ++pPosition, ++pTexCoord, ++pNormal)
    {
        const glm::vec3* pPosition2 = weldedPositions.data();
        const glm::vec2* pTexCoord2 = weldedTexCoords.data();
        const glm::vec3* pNormal2   = weldedNormals.data();
        //
        uint32_t newIdx = UINT32_MAX;
        for (uint32_t i = 0; i < weldedVertexCount; ++i, ++pPosition2, ++pTexCoord2, ++pNormal2)
        {
            float posDistSq = glm::distance2(*pPosition, *pPosition2);
            float tcDistSq  = glm::distance2(*pTexCoord, *pTexCoord2);

            auto  N     = glm::normalize(*pNormal);
            auto  N2    = glm::normalize(*pNormal2);
            float theta = acos(glm::dot(N, N2) / (glm::length(N) * glm::length(N2)));

            bool withinPositionThreshold = (posDistSq <= positionDistanceThresholdSq);
            bool withinTexCoordThreshold = (tcDistSq <= texCoordDistanceThresholdSq);
            bool withinNormalThreshold   = (theta <= normalAngleThreshold);
            if (withinPositionThreshold && withinTexCoordThreshold && withinNormalThreshold)
            {
                newIdx = i;
                break;
            }
        }

        if (newIdx == UINT32_MAX)
        {
            weldedPositions.push_back(*pPosition);
            weldedTexCoords.push_back(*pTexCoord);
            weldedNormals.push_back(*pNormal);
            ++weldedVertexCount;
            newIdx = weldedVertexCount - 1;
        }

        weldedIndexMap[oldIdx] = newIdx;
    }

    mPositions = weldedPositions;
    mTexCoords = weldedTexCoords;
    mNormals   = weldedNormals;

    for (auto& tri : mTriangles)
    {
        tri.vIdx0 = weldedIndexMap[tri.vIdx0];
        tri.vIdx1 = weldedIndexMap[tri.vIdx1];
        tri.vIdx2 = weldedIndexMap[tri.vIdx2];
    }
}

std::vector<glm::vec3> TriMesh::GetTBNLineSegments(uint32_t* pNumVertices, float length) const
{
    if (pNumVertices == nullptr)
    {
        return {};
    }

    // We need all these attributes
    uint32_t numPs = CountU32(mPositions);
    uint32_t numTs = CountU32(mTangents);
    uint32_t numBs = CountU32(mBitangents);
    uint32_t numNs = CountU32(mNormals);
    if (!((numPs == numTs) && (numTs == numBs) && (numBs == numNs)))
    {
        return {};
    }

    // Get unique indices
    std::vector<uint32_t> uniqueIndices;
    for (auto& tri : mTriangles)
    {
        auto pTriIndices = reinterpret_cast<const uint32_t*>(&tri);
        for (uint32_t i = 0; i < 3; ++i)
        {
            uint32_t vIdx = pTriIndices[i];
            auto     it   = std::find(uniqueIndices.begin(), uniqueIndices.end(), vIdx);
            if (it != uniqueIndices.end())
            {
                continue;
            }
            uniqueIndices.push_back(vIdx);
        }
    }

    // Construct the TBN line segments with vertex colors
    //   T = red
    //   B = green
    //   N = blue
    //
    const glm::vec3 kRed   = glm::vec3(1, 0, 0);
    const glm::vec3 kGreen = glm::vec3(0, 1, 0);
    const glm::vec3 kBlue  = glm::vec3(0, 0, 1);
    //
    std::vector<glm::vec3> vertexData;
    uint32_t               numVertices = 0;
    for (auto& vIdx : uniqueIndices)
    {
        auto& P = mPositions[vIdx];
        auto& T = mTangents[vIdx];
        auto& B = mBitangents[vIdx];
        auto& N = mNormals[vIdx];

        // T
        vertexData.push_back(P);
        vertexData.push_back(kRed);
        vertexData.push_back(P + (length * glm::normalize(T)));
        vertexData.push_back(kRed);
        numVertices += 2;
        // B
        vertexData.push_back(P);
        vertexData.push_back(kGreen);
        vertexData.push_back(P + (length * glm::normalize(B)));
        vertexData.push_back(kGreen);
        numVertices += 2;
        // N
        vertexData.push_back(P);
        vertexData.push_back(kBlue);
        vertexData.push_back(P + (length * glm::normalize(N)));
        vertexData.push_back(kBlue);
        numVertices += 2;
    }

    *pNumVertices = numVertices;

    return vertexData;
}

TriMesh TriMesh::Box(
    const glm::vec3&        size,
    uint8_t                 actives,
    bool                    perTexCoords,
    const TriMesh::Options& options)
{
    const float hx = size.x / 2.0f;
    const float hy = size.y / 2.0f;
    const float hz = size.z / 2.0f;

    // clang-format off
    std::vector<float> vertexData = {  
        // position      // vertex colors    // texcoords  // normal           // tangents         // bitangents
         hx,  hy, -hz,    1.0f, 0.0f, 0.0f,   0.0f, 0.0f,   0.0f, 0.0f,-1.0f,  -1.0f, 0.0f, 0.0f,   0.0f, -1.0f, 0.0f,  //  0  -Z side
         hx, -hy, -hz,    1.0f, 0.0f, 0.0f,   0.0f, 1.0f,   0.0f, 0.0f,-1.0f,  -1.0f, 0.0f, 0.0f,   0.0f, -1.0f, 0.0f,  //  1
        -hx, -hy, -hz,    1.0f, 0.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f,-1.0f,  -1.0f, 0.0f, 0.0f,   0.0f, -1.0f, 0.0f,  //  2
        -hx,  hy, -hz,    1.0f, 0.0f, 0.0f,   1.0f, 0.0f,   0.0f, 0.0f,-1.0f,  -1.0f, 0.0f, 0.0f,   0.0f, -1.0f, 0.0f,  //  3
                                                                                                    
        -hx,  hy,  hz,    0.0f, 1.0f, 0.0f,   0.0f, 0.0f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, -1.0f, 0.0f,  //  4  +Z side
        -hx, -hy,  hz,    0.0f, 1.0f, 0.0f,   0.0f, 1.0f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, -1.0f, 0.0f,  //  5
         hx, -hy,  hz,    0.0f, 1.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, -1.0f, 0.0f,  //  6
         hx,  hy,  hz,    0.0f, 1.0f, 0.0f,   1.0f, 0.0f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, -1.0f, 0.0f,  //  7
                                                                                                    
        -hx,  hy, -hz,   -0.0f, 0.0f, 1.0f,   0.0f, 0.0f,  -1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, -1.0f, 0.0f,  //  8  -X side
        -hx, -hy, -hz,   -0.0f, 0.0f, 1.0f,   0.0f, 1.0f,  -1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, -1.0f, 0.0f,  //  9
        -hx, -hy,  hz,   -0.0f, 0.0f, 1.0f,   1.0f, 1.0f,  -1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, -1.0f, 0.0f,  // 10
        -hx,  hy,  hz,   -0.0f, 0.0f, 1.0f,   1.0f, 0.0f,  -1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, -1.0f, 0.0f,  // 11
                                                                                                    
         hx,  hy,  hz,    1.0f, 1.0f, 0.0f,   0.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,-1.0f,   0.0f, -1.0f, 0.0f,  // 12  +X side
         hx, -hy,  hz,    1.0f, 1.0f, 0.0f,   0.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,-1.0f,   0.0f, -1.0f, 0.0f,  // 13
         hx, -hy, -hz,    1.0f, 1.0f, 0.0f,   1.0f, 1.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,-1.0f,   0.0f, -1.0f, 0.0f,  // 14
         hx,  hy, -hz,    1.0f, 1.0f, 0.0f,   1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,-1.0f,   0.0f, -1.0f, 0.0f,  // 15
                                                                                                    
        -hx, -hy,  hz,    1.0f, 0.0f, 1.0f,   0.0f, 0.0f,   0.0f,-1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, -1.0f,  // 16  -Y side
        -hx, -hy, -hz,    1.0f, 0.0f, 1.0f,   0.0f, 1.0f,   0.0f,-1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, -1.0f,  // 17
         hx, -hy, -hz,    1.0f, 0.0f, 1.0f,   1.0f, 1.0f,   0.0f,-1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, -1.0f,  // 18
         hx, -hy,  hz,    1.0f, 0.0f, 1.0f,   1.0f, 0.0f,   0.0f,-1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, -1.0f,  // 19
                                                                                                    
        -hx,  hy, -hz,    0.0f, 1.0f, 1.0f,   0.0f, 0.0f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,   // 20  +Y side
        -hx,  hy,  hz,    0.0f, 1.0f, 1.0f,   0.0f, 1.0f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,   // 21
         hx,  hy,  hz,    0.0f, 1.0f, 1.0f,   1.0f, 1.0f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,   // 22
         hx,  hy, -hz,    0.0f, 1.0f, 1.0f,   1.0f, 0.0f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f,   // 23
    };

    float u0 = 0.0f;
    float u1 = 1.0f / 3.0f;
    float u2 = 2.0f / 3.0f;
    float u3 = 1.0f;

    float v0 = 0.0f;
    float v1 = 1.0f / 2.0f;
    float v2 = 1.0f;

    std::vector<float> perTexCoordsData{
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
        0,  1,  2, // -Z side (0)
        0,  2,  3,

        4,  5,  6, // +Z side (2)
        4,  6,  7,

        8,  9, 10, // -X side (4)
        8, 10, 11,

        12, 13, 14, // +X side (6)
        12, 14, 15,

        16, 17, 18, // -Y side (8)
        16, 18, 19,

        20, 21, 22, // +Y side (10)
        20, 22, 23,
    };
    // clang-format on

    TriMesh mesh = TriMesh(options);

    glm::mat4 transformMat = glm::mat4(1);
    glm::mat4 rotationMat  = glm::mat4(1);
    if (options.applyTransform)
    {
        glm::mat4 T  = glm::translate(options.transformTranslate);
        glm::mat4 Rx = glm::rotate(options.transformRotate.x, glm::vec3(1, 0, 0));
        glm::mat4 Ry = glm::rotate(options.transformRotate.y, glm::vec3(0, 1, 0));
        glm::mat4 Rz = glm::rotate(options.transformRotate.z, glm::vec3(0, 0, 1));
        glm::mat4 S  = glm::scale(options.transformScale);
        rotationMat  = Rx * Ry * Rz;
        transformMat = T * rotationMat * S;
    }

    const TriMesh::Vertex* pVertices         = reinterpret_cast<const TriMesh::Vertex*>(vertexData.data());
    const glm::vec2*       pPerfaceTexCoords = reinterpret_cast<const glm::vec2*>(perTexCoordsData.data());
    for (size_t i = 0; i < 24; ++i)
    {
        TriMesh::Vertex vtx = pVertices[i];

        if (options.applyTransform)
        {
            vtx.position = glm::vec3(transformMat * glm::vec4(vtx.position, 1));
            vtx.normal   = glm::vec3(rotationMat * glm::vec4(vtx.normal, 0));
        }

        vtx.position += options.center;

        if (options.faceInside)
        {
            vtx.normal = -vtx.normal;
        }

        if (perTexCoords)
        {
            vtx.texCoord = pPerfaceTexCoords[i];
        }

        mesh.AddVertex(vtx);
    }

    const Triangle* pTriangles = reinterpret_cast<const Triangle*>(indexData.data());
    if (actives & AXIS_POS_X)
    {
        Triangle tri0 = pTriangles[6];
        Triangle tri1 = pTriangles[7];
        if (options.faceInside)
        {
            std::swap(tri0.vIdx1, tri0.vIdx2);
            std::swap(tri1.vIdx1, tri1.vIdx2);
        }
        mesh.AddTriangle(tri0);
        mesh.AddTriangle(tri1);
    }
    if (actives & AXIS_NEG_X)
    {
        Triangle tri0 = pTriangles[4];
        Triangle tri1 = pTriangles[5];
        if (options.faceInside)
        {
            std::swap(tri0.vIdx1, tri0.vIdx2);
            std::swap(tri1.vIdx1, tri1.vIdx2);
        }
        mesh.AddTriangle(tri0);
        mesh.AddTriangle(tri1);
    }
    if (actives & AXIS_POS_Y)
    {
        Triangle tri0 = pTriangles[10];
        Triangle tri1 = pTriangles[11];
        if (options.faceInside)
        {
            std::swap(tri0.vIdx1, tri0.vIdx2);
            std::swap(tri1.vIdx1, tri1.vIdx2);
        }
        mesh.AddTriangle(tri0);
        mesh.AddTriangle(tri1);
    }
    if (actives & AXIS_NEG_Y)
    {
        Triangle tri0 = pTriangles[8];
        Triangle tri1 = pTriangles[9];
        if (options.faceInside)
        {
            std::swap(tri0.vIdx1, tri0.vIdx2);
            std::swap(tri1.vIdx1, tri1.vIdx2);
        }
        mesh.AddTriangle(tri0);
        mesh.AddTriangle(tri1);
    }
    if (actives & AXIS_POS_Z)
    {
        Triangle tri0 = pTriangles[2];
        Triangle tri1 = pTriangles[3];
        if (options.faceInside)
        {
            std::swap(tri0.vIdx1, tri0.vIdx2);
            std::swap(tri1.vIdx1, tri1.vIdx2);
        }
        mesh.AddTriangle(tri0);
        mesh.AddTriangle(tri1);
    }
    if (actives & AXIS_NEG_Z)
    {
        Triangle tri0 = pTriangles[0];
        Triangle tri1 = pTriangles[1];
        if (options.faceInside)
        {
            std::swap(tri0.vIdx1, tri0.vIdx2);
            std::swap(tri1.vIdx1, tri1.vIdx2);
        }
        mesh.AddTriangle(tri0);
        mesh.AddTriangle(tri1);
    }

    return mesh;
}

TriMesh TriMesh::Cube(
    const glm::vec3&        size,
    bool                    perTexCoords,
    const TriMesh::Options& options)
{
    return TriMesh::Box(size, ALL_AXES, perTexCoords, options);
}

TriMesh TriMesh::Plane(
    const glm::vec2&        size,
    uint32_t                usegs,
    uint32_t                vsegs,
    glm::vec3               normalToPlane,
    const TriMesh::Options& options)

{
    glm::vec3 P0 = glm::vec3(-0.5f, 0.0f, -0.5f) * glm::vec3(size.x, 1.0, size.y);
    glm::vec3 P1 = glm::vec3(-0.5f, 0.0f, 0.5f) * glm::vec3(size.x, 1.0, size.y);
    glm::vec3 P2 = glm::vec3(0.5f, 0.0f, 0.5f) * glm::vec3(size.x, 1.0, size.y);
    glm::vec3 P3 = glm::vec3(0.5f, 0.0f, -0.5f) * glm::vec3(size.x, 1.0, size.y);

    glm::vec2 uv0 = glm::vec2(0, 0);
    glm::vec2 uv1 = glm::vec2(0, 1);
    glm::vec2 uv2 = glm::vec2(1, 1);
    glm::vec2 uv3 = glm::vec2(1, 0);

    const uint32_t uverts = usegs + 1;
    const uint32_t vverts = vsegs + 1;

    float du = 1.0f / usegs;
    float dv = 1.0f / vsegs;

    const glm::vec3 T = glm::vec3(1, 0, 0);
    const glm::vec3 B = glm::vec3(0, 0, 1);
    const glm::vec3 N = glm::vec3(0, 1, 0);

    glm::quat rotQuat = glm::rotation(N, glm::normalize(normalToPlane));
    glm::mat4 rotMat  = glm::toMat4(rotQuat);

    TriMesh mesh = TriMesh(options);

    for (uint32_t j = 0; j < vverts; ++j)
    {
        for (uint32_t i = 0; i < uverts; ++i)
        {
            float     u = i * du;
            float     v = j * dv;
            glm::vec3 P = (1.0f - u) * (1.0f - v) * P0 +
                          (1.0f - u) * v * P1 +
                          u * v * P2 +
                          u * (1.0f - v) * P3;
            glm::vec3 position  = rotMat * glm::vec4(P, 1);
            glm::vec3 color     = glm::vec3(u, v, 0);
            glm::vec2 texCoord  = glm::vec2(u, v) * options.texCoordScale;
            glm::vec3 normal    = rotMat * glm::vec4(N, 0);
            glm::vec3 tangent   = rotMat * glm::vec4(T, 0);
            glm::vec3 bitangent = rotMat * glm::vec4(B, 0);

            position += options.center;

            mesh.AddVertex(
                position,
                color,
                texCoord,
                normal,
                tangent,
                bitangent);
        }
    }

    for (uint32_t j = 1; j < vverts; ++j)
    {
        for (uint32_t i = 1; i < uverts; i++)
        {
            uint32_t i0 = i - 1;
            uint32_t j0 = j - 1;
            uint32_t i1 = i;
            uint32_t j1 = j;
            uint32_t v0 = j0 * uverts + i0;
            uint32_t v1 = j1 * uverts + i0;
            uint32_t v2 = j1 * uverts + i1;
            uint32_t v3 = j0 * uverts + i1;

            mesh.AddTriangle(v0, v1, v2);
            mesh.AddTriangle(v0, v2, v3);
        }
    }

    return mesh;
}

TriMesh TriMesh::Sphere(
    float                   radius,
    uint32_t                usegs,
    uint32_t                vsegs,
    const TriMesh::Options& options)
{
    constexpr float kPi      = glm::pi<float>();
    constexpr float kTwoPi   = 2.0f * kPi;
    const float     kEpsilon = 0.0000001f;

    const uint32_t uverts = usegs + 1;
    const uint32_t vverts = vsegs + 1;

    float dt = kTwoPi / static_cast<float>(usegs);
    float dp = kPi / static_cast<float>(vsegs);

    TriMesh mesh = TriMesh(options);

    for (uint32_t i = 0; i < uverts; ++i)
    {
        for (uint32_t j = 0; j < vverts; ++j)
        {
            //
            // NOTE: tangent and bitangent needs to flow the same direction
            //       as u and v. Meaning that tangent must point towards u=1
            //       and bitangent must point towards v=1.
            //
            float     theta     = i * dt;
            float     phi       = j * dp;
            float     u         = options.texCoordScale.x * theta / kTwoPi;
            float     v         = options.texCoordScale.y * phi / kPi;
            glm::vec3 P         = SphericalToCartesian(theta, phi);
            glm::vec3 position  = radius * P;
            glm::vec3 color     = glm::vec3(u, v, 0);
            glm::vec2 texCoord  = glm::vec2(u, v);
            glm::vec3 normal    = normalize(position);
            glm::vec3 tangent   = glm::normalize(-SphericalTangent(theta, phi));
            glm::vec3 bitangent = glm::normalize(glm::cross(normal, tangent));

            if (options.invertTexCoordsV)
            {
                v         = options.texCoordScale.y * (1.0f - phi / kPi);
                tangent   = glm::normalize(SphericalTangent(theta, phi));
                bitangent = glm::normalize(glm::cross(normal, tangent));
            }

            position += options.center;

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
    for (uint32_t i = 1; i < uverts; ++i)
    {
        for (uint32_t j = 1; j < vverts; ++j)
        {
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
            mesh.AddTriangle(v0, v1, v2);

            dist0 = glm::distance2(P0, P2);
            dist1 = glm::distance2(P0, P3);
            dist2 = glm::distance2(P2, P3);
            mesh.AddTriangle(v0, v2, v3);
        }
    }

    return mesh;
}

TriMesh TriMesh::Cone(
    float          height,
    float          radius,
    uint32_t       segs,
    const Options& options)
{
    constexpr float     kPi    = glm::pi<float>();
    constexpr float     kTwoPi = 2.0f * kPi;
    constexpr glm::vec3 kUp    = glm::vec3(0, 1, 0);

    TriMesh   mesh = TriMesh(options);
    glm::vec3 tip  = glm::vec3(0, height, 0);

    segs     = std::max<uint32_t>(3, segs);
    float dt = kTwoPi / segs;

    const std::vector<glm::vec3> kColors = {
        glm::vec3(1, 0, 0),
        glm::vec3(0, 1, 0),
        glm::vec3(0, 0, 1),
        glm::vec3(1, 1, 0),
        glm::vec3(1, 0, 1),
        glm::vec3(0, 1, 1),
        glm::vec3(1, 1, 1),
    };

    float     baseMinU = -radius;
    float     baseMinV = -radius;
    glm::vec3 baseP0   = {};
    for (uint32_t i = 0; i < segs; ++i)
    {
        uint32_t i0 = i;
        uint32_t i1 = i + 1;
        float    t0 = -(i0 * dt);
        float    t1 = -(i1 * dt);

        glm::vec3 P0 = tip;
        glm::vec3 P1 = radius * glm::vec3(cos(t0), 0, sin(t0));
        glm::vec3 P2 = radius * glm::vec3(cos(t1), 0, sin(t1));

        glm::vec3 color = kColors[i % kColors.size()];

        glm::vec2 uv0 = glm::vec2(t0 / kTwoPi, 0);
        glm::vec2 uv1 = glm::vec2(t0 / kTwoPi, 1);
        glm::vec2 uv2 = glm::vec2(t1 / kTwoPi, 1);

        glm::vec3 d0 = P1 - P0;
        glm::vec3 d1 = P2 - P0;
        glm::vec3 N0 = glm::normalize(glm::cross(d0, d1));
        glm::vec3 N1 = glm::normalize(P1);
        glm::vec3 N2 = glm::normalize(P2);

        glm::vec3 T0 = glm::normalize(glm::cross(kUp, N0));
        glm::vec3 T1 = glm::normalize(glm::cross(kUp, N1));
        glm::vec3 T2 = glm::normalize(glm::cross(kUp, N2));

        glm::vec3 B0 = glm::normalize(glm::cross(N0, T0));
        glm::vec3 B1 = glm::normalize(glm::cross(N1, T1));
        glm::vec3 B2 = glm::normalize(glm::cross(N2, T2));

        // Slant triangle
        mesh.AddVertex(P0, color, uv0, N0, T0, B0);
        mesh.AddVertex(P1, color, uv1, N1, T1, B1);
        mesh.AddVertex(P2, color, uv2, N2, T2, B2);

        uint32_t n     = mesh.GetNumVertices();
        uint32_t vIdx0 = n - 3;
        uint32_t vIdx1 = n - 2;
        uint32_t vIdx2 = n - 1;
        mesh.AddTriangle(vIdx0, vIdx1, vIdx2);

        // Base triangle
        //
        // # of base triangles = segs - 2
        //
        if ((i > 0) && (i < (segs - 1)))
        {
            P0 = baseP0;

            // Swap P1 and P2 since we're upside down
            glm::vec3 tP = P1;
            P1           = P2;
            P2           = tP;

            color = kColors[0];
            N0 = N1 = N2 = glm::vec3(0, -1, 0);
            T0 = T1 = T2 = glm::vec3(1, 0, 0);
            B0 = B1 = B2 = glm::vec3(0, 0, 1);

            mesh.AddVertex(P0, color, uv0, N0, T0, B0);
            mesh.AddVertex(P1, color, uv1, N1, T1, B1);
            mesh.AddVertex(P2, color, uv2, N2, T2, B2);

            uint32_t n     = mesh.GetNumVertices();
            uint32_t vIdx0 = n - 3;
            uint32_t vIdx1 = n - 2;
            uint32_t vIdx2 = n - 1;
            mesh.AddTriangle(vIdx0, vIdx1, vIdx2);
        }
        else
        {
            baseP0 = P1;
        }
    }

    // Build base

    return mesh;
}

TriMesh TriMesh::CornellBox(const TriMesh::Options& options)
{
    TriMesh  mesh       = TriMesh(options);
    uint32_t materialId = 0;

    const float mainBoxWidth  = 5.5f;
    const float mainBoxHeight = 5.5f;
    const float mainBoxDepth  = 6.6f;

    // Light
    //  L = 1.3
    //  W = 1.05
    //
    {
        const float length = 1.3f;
        const float width  = 1.05f;

        // Light
        {
            const glm::vec3 baseColor = glm::vec3(1, 1, 1);

            TriMesh::Options thisOptions = options;
            thisOptions.center           = glm::vec3(0, mainBoxHeight - 0.01f, -2.518f);
            thisOptions.faceInside       = false;

            TriMesh plane = TriMesh::Plane(
                glm::vec2(length, width),
                1,
                1,
                glm::vec3(0, -1, 0),
                thisOptions);
            plane.SetVertexColors(baseColor);

            Material material  = {};
            material.name      = "white light";
            material.id        = ++materialId;
            material.baseColor = baseColor;
            plane.AddMaterial(material);
            plane.AddGroup(TriMesh::Group("light", 0, plane.GetNumTriangles(), plane.GetNumMaterials() - 1));

            mesh.AppendMesh(plane);
        }
    }

    // Main box
    //  W = 5.5
    //  H = 5.5
    //  D = 6.6
    //
    {
        const float hw = mainBoxWidth / 2.0f;
        const float hh = mainBoxHeight / 2.0f;
        const float hd = mainBoxDepth / 2.0f;

        // Left wall (red)
        {
            const glm::vec3 baseColor = glm::vec3(1, 0, 0);

            TriMesh::Options thisOptions = options;
            thisOptions.center           = glm::vec3(-hw, hh, -hd);
            thisOptions.faceInside       = false;

            TriMesh plane = TriMesh::Plane(
                glm::vec2(mainBoxHeight, mainBoxDepth),
                1,
                1,
                glm::vec3(1, 0, 0),
                thisOptions);
            plane.SetVertexColors(baseColor);

            Material material  = {};
            material.name      = "red surface";
            material.id        = ++materialId;
            material.baseColor = baseColor;
            plane.AddMaterial(material);
            plane.AddGroup(TriMesh::Group("left wall", 0, plane.GetNumTriangles(), plane.GetNumMaterials() - 1));

            mesh.AppendMesh(plane);
        }

        // Right wall (greeen)
        {
            const glm::vec3 baseColor = glm::vec3(0, 1, 0);

            TriMesh::Options thisOptions = options;
            thisOptions.center           = glm::vec3(hw, hh, -hd);

            TriMesh plane = TriMesh::Plane(
                glm::vec2(mainBoxHeight, mainBoxDepth),
                1,
                1,
                glm::vec3(-1, 0, 0),
                thisOptions);
            plane.SetVertexColors(baseColor);

            Material material  = {};
            material.name      = "green surface";
            material.id        = ++materialId;
            material.baseColor = baseColor;
            plane.AddMaterial(material);
            plane.AddGroup(TriMesh::Group("right wall", 0, plane.GetNumTriangles(), plane.GetNumMaterials() - 1));

            mesh.AppendMesh(plane);
        }

        // Back wall, ceiling, and floor (white)
        {
            const glm::vec3 baseColor = glm::vec3(1, 1, 1);

            TriMesh::Options thisOptions = options;
            thisOptions.center           = glm::vec3(0, hh, -hd);
            thisOptions.faceInside       = true;

            TriMesh box = TriMesh::Box(
                glm::vec3(mainBoxWidth, mainBoxHeight, mainBoxDepth),
                AXIS_POS_Y | AXIS_NEG_Y | AXIS_NEG_Z,
                false,
                thisOptions);
            box.SetVertexColors(baseColor);

            Material material  = {};
            material.name      = "white surface";
            material.id        = ++materialId;
            material.baseColor = baseColor;
            box.AddMaterial(material);
            box.AddGroup(TriMesh::Group("back wall, ceiling, and floor", 0, box.GetNumTriangles(), box.GetNumMaterials() - 1));

            mesh.AppendMesh(box);
        }
    }

    // Small box
    //  W = 1.67
    //  H = 1.67
    //  D = 1.67
    //
    {
        const float width  = 1.67f;
        const float height = 1.67f;
        const float depth  = 1.67f;

        const float hw = width / 2.0f;
        const float hh = height / 2.0f;
        const float hd = depth / 2.0f;

        // Back wall, ceiling, and floor (white)
        {
            const glm::vec3 baseColor = glm::vec3(0.80f, 0.66f, 0.44f);

            TriMesh::Options thisOptions = options;
            thisOptions.center           = glm::vec3(0.9f, hh, -2);
            thisOptions.faceInside       = false;
            thisOptions.applyTransform   = true;
            thisOptions.transformRotate  = glm::vec3(0, -0.4075f, 0);

            TriMesh box = TriMesh::Box(
                glm::vec3(width, height, depth),
                ALL_AXES,
                false,
                thisOptions);
            box.SetVertexColors(baseColor);

            Material material  = {};
            material.name      = "khaki surface";
            material.id        = ++materialId;
            material.baseColor = baseColor;
            box.AddMaterial(material);
            box.AddGroup(TriMesh::Group("small box", 0, box.GetNumTriangles(), box.GetNumMaterials() - 1));

            mesh.AppendMesh(box);
        }
    }

    // Tall box
    //  W = 1.67
    //  H = 3.3
    //  D = 1.67
    //
    {
        const float width  = 1.67f;
        const float height = 3.3f;
        const float depth  = 1.67f;

        const float hw = width / 2.0f;
        const float hh = height / 2.0f;
        const float hd = depth / 2.0f;

        // Back wall, ceiling, and floor (white)
        {
            const glm::vec3 baseColor = glm::vec3(0.80f, 0.66f, 0.44f);

            TriMesh::Options thisOptions = options;
            thisOptions.center           = glm::vec3(-0.92f, hh, -3.755f);
            thisOptions.faceInside       = false;
            thisOptions.applyTransform   = true;
            thisOptions.transformRotate  = glm::vec3(0, 0.29718f, 0);

            TriMesh box = TriMesh::Box(
                glm::vec3(width, height, depth),
                ALL_AXES,
                false,
                thisOptions);
            box.SetVertexColors(baseColor);

            Material material  = {};
            material.name      = "khaki surface";
            material.id        = ++materialId;
            material.baseColor = baseColor;
            box.AddMaterial(material);
            box.AddGroup(TriMesh::Group("tall box", 0, box.GetNumTriangles(), box.GetNumMaterials() - 1));

            mesh.AppendMesh(box);
        }
    }

    return mesh;
}

bool TriMesh::LoadOBJ(const std::string& path, const std::string& mtlBaseDir, const TriMesh::Options& options, TriMesh* pMesh)
{
    if (pMesh == nullptr)
    {
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
    bool        loaded = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(), mtlBaseDir.c_str(), true);

    if (!loaded || !err.empty())
    {
        return false;
    }

    size_t numShapes = shapes.size();
    if (numShapes == 0)
    {
        return false;
    }

    // Create mesh
    *pMesh = TriMesh(options);

    // Track which material ids are used - we do this
    // because the OBJ file can have materials that are
    // never used. We don't want any gaps in our material
    // indices because it'll lead to wasting GPU memory
    // later on.
    //
    std::vector<int> activeMaterialIds;

    // Transform options
    glm::mat4 transformMat = glm::mat4(1);
    glm::mat4 rotationMat  = glm::mat4(1);
    if (options.applyTransform)
    {
        glm::mat4 T  = glm::translate(options.transformTranslate);
        glm::mat4 Rx = glm::rotate(options.transformRotate.x, glm::vec3(1, 0, 0));
        glm::mat4 Ry = glm::rotate(options.transformRotate.y, glm::vec3(0, 1, 0));
        glm::mat4 Rz = glm::rotate(options.transformRotate.z, glm::vec3(0, 0, 1));
        glm::mat4 S  = glm::scale(options.transformScale);
        rotationMat  = Rx * Ry * Rz;
        transformMat = T * rotationMat * S;
    }

    // Build geometry
    for (size_t shapeIdx = 0; shapeIdx < numShapes; ++shapeIdx)
    {
        const tinyobj::shape_t& shape     = shapes[shapeIdx];
        const tinyobj::mesh_t&  shapeMesh = shape.mesh;

        TriMesh::Group newGroup(shape.name);

        size_t numTriangles = shapeMesh.indices.size() / 3;
        for (size_t triIdx = 0; triIdx < numTriangles; ++triIdx)
        {
            size_t triVtxIdx0 = triIdx * 3 + 0;
            size_t triVtxIdx1 = triIdx * 3 + 1;
            size_t triVtxIdx2 = triIdx * 3 + 2;

            // Index data
            const tinyobj::index_t& dataIdx0 = shapeMesh.indices[triVtxIdx0];
            const tinyobj::index_t& dataIdx1 = shapeMesh.indices[triVtxIdx1];
            const tinyobj::index_t& dataIdx2 = shapeMesh.indices[triVtxIdx2];

            // Vertex data
            TriMesh::Vertex vtx0 = {};
            TriMesh::Vertex vtx1 = {};
            TriMesh::Vertex vtx2 = {};

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
            if ((dataIdx0.texcoord_index != -1) && (dataIdx1.texcoord_index != -1) && (dataIdx2.texcoord_index != -1))
            {
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

                if (options.invertTexCoordsV)
                {
                    vtx0.texCoord.y = 1.0f - vtx0.texCoord.y;
                    vtx1.texCoord.y = 1.0f - vtx1.texCoord.y;
                    vtx2.texCoord.y = 1.0f - vtx2.texCoord.y;
                }
            }

            // Normals
            if ((dataIdx0.normal_index != -1) && (dataIdx1.normal_index != -1) && (dataIdx2.normal_index != -1))
            {
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

            if (options.applyTransform)
            {
                vtx0.position = transformMat * glm::vec4(vtx0.position, 1);
                vtx1.position = transformMat * glm::vec4(vtx1.position, 1);
                vtx2.position = transformMat * glm::vec4(vtx2.position, 1);
                vtx0.normal   = rotationMat * glm::vec4(vtx0.normal, 0);
                vtx1.normal   = rotationMat * glm::vec4(vtx1.normal, 0);
                vtx2.normal   = rotationMat * glm::vec4(vtx2.normal, 0);
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

            uint32_t triangleIndex = pMesh->AddTriangle(vIdx0, vIdx1, vIdx2);
            int32_t  materialIndex = -1;

            const int shapeMaterialId = shapeMesh.material_ids[triIdx];
            if (shapeMaterialId != -1)
            {
                auto it = std::find(activeMaterialIds.begin(), activeMaterialIds.end(), shapeMaterialId);
                if (it == activeMaterialIds.end())
                {
                    activeMaterialIds.push_back(shapeMaterialId);
                    materialIndex = static_cast<int32_t>(activeMaterialIds.size() - 1);
                }
                else
                {
                    materialIndex = static_cast<int32_t>(std::distance(activeMaterialIds.begin(), it));
                }
            }

            newGroup.AddTriangleIndex(triangleIndex, materialIndex);
        }

        uint32_t res = pMesh->AddGroup(newGroup);
        assert((res != UINT32_MAX) && "AddGroup (LoadOBJ) failed");
    }

#if defined(TRIMESH_USE_MIKKTSPACE)
    CalculateTangents::Calculate(pMesh);
#endif

    // Materials
    //
    // Only copy the materials in \b activeMaterialIds.
    //
    const uint32_t numActiveMaterials = static_cast<uint32_t>(activeMaterialIds.size());
    for (uint32_t i = 0; i < numActiveMaterials; ++i)
    {
        const size_t materialId = activeMaterialIds[i];
        auto&        material   = materials[materialId];

        TriMesh::Material newMaterial = {};
        newMaterial.name              = material.name;
        newMaterial.id                = static_cast<uint32_t>(materialId);
        newMaterial.F0                = glm::vec3(0.04f);
        newMaterial.baseColor         = glm::vec3(material.diffuse[0], material.diffuse[1], material.diffuse[2]);
        newMaterial.roughness         = material.roughness;
        newMaterial.metalness         = material.metallic;
        newMaterial.albedoTexture     = material.diffuse_texname;
        newMaterial.normalTexture     = material.normal_texname;
        newMaterial.roughnessTexture  = material.roughness_texname;
        newMaterial.metalnessTexture  = material.metallic_texname;
        newMaterial.aoTexture         = material.ambient_texname;

        pMesh->AddMaterial(newMaterial);
    }

    return true;
}

bool TriMesh::LoadOBJ2(const std::string& path, TriMesh* pMesh)
{
    if (pMesh == nullptr)
    {
        return false;
    }

    tinyobj::attrib_t                attrib;
    std::vector<tinyobj::shape_t>    shapes;
    std::vector<tinyobj::material_t> materials;

    std::string warn;
    std::string err;
    bool        loaded = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(), nullptr, false);

    if (!loaded || !err.empty())
    {
        return false;
    }

    size_t numShapes = shapes.size();
    if (numShapes == 0)
    {
        return false;
    }

    // Make sure all vertex positions are 3 components wide
    if ((attrib.vertices.size() % 3) != 0)
    {
        return false;
    }
    
    // If there's normals and tex coords, they need to line up with the vertex positions
    const size_t positionCount = attrib.vertices.size() / 3;
    const size_t normalCount   = attrib.normals.size() / 3;
    const size_t texCoordCount = attrib.texcoords.size() / 2;
    if ((normalCount > 0) && (normalCount != positionCount)) {
        return false;
    }
    if ((texCoordCount > 0) && (texCoordCount != positionCount)) {
        return false;
    }

    // Create mesh
    *pMesh = TriMesh();

    // Set positions
    pMesh->SetPositions(positionCount, reinterpret_cast<const glm::vec3*>(attrib.vertices.data()));
    // Set normals
    if (normalCount > 0) {
        pMesh->SetNormals(normalCount, reinterpret_cast<const glm::vec3*>(attrib.normals.data()));
    }
    // Set tex coords
    if (texCoordCount > 0) {
        pMesh->SetTexCoords(texCoordCount, reinterpret_cast<const glm::vec2*>(attrib.texcoords.data()));
    }

    for (size_t shapeIdx = 0; shapeIdx < numShapes; ++shapeIdx)
    {
        auto& shape = shapes[shapeIdx];
        if ((shape.mesh.indices.size() % 3) != 0)
        {
            return false;
        }

        size_t numTriangles = shape.mesh.indices.size() / 3;
        for (size_t triIdx = 0; triIdx < numTriangles; ++triIdx)
        {
            uint32_t vIdx0 = static_cast<uint32_t>(shape.mesh.indices[3 * triIdx + 0].vertex_index);
            uint32_t vIdx1 = static_cast<uint32_t>(shape.mesh.indices[3 * triIdx + 1].vertex_index);
            uint32_t vIdx2 = static_cast<uint32_t>(shape.mesh.indices[3 * triIdx + 2].vertex_index);
            pMesh->AddTriangle(vIdx0, vIdx1, vIdx2);
        }
    }

    pMesh->CalculateBounds();
    
    GREX_LOG_INFO("Loaded " << path);
    GREX_LOG_INFO("  num vertices: " << pMesh->GetNumVertices());
    GREX_LOG_INFO("  num indices : " << pMesh->GetNumIndices());

    return true;
}

bool TriMesh::WriteOBJ(const std::string path, const TriMesh& mesh)
{
    std::ofstream os = std::ofstream(path.c_str());
    if (!os.is_open())
    {
        return false;
    }

    bool writeTexCoords = mesh.GetOptions().enableTexCoords;
    bool writeNormals   = mesh.GetOptions().enableNormals;

    os << std::setprecision(6);
    os << std::fixed;

    os << "# vertices\n";
    {
        auto& positions = mesh.GetPositions();
        for (auto& v : positions)
        {
            os << "v " << v.x << " " << v.y << " " << v.z << "\n";
        }
    }

    if (writeTexCoords)
    {
        os << "# texture coordinates\n";
        {
            auto& texCoords = mesh.GetTexCoords();
            for (auto& vt : texCoords)
            {
                os << "vt " << vt.x << " " << vt.y << "\n";
            }
        }
    }

    if (writeNormals)
    {
        os << "# normals\n";
        {
            auto& normals = mesh.GetNormals();
            for (auto& vn : normals)
            {
                os << "vn " << vn.x << " " << vn.y << " " << vn.z << "\n";
            }
        }
    }

    os << "# triangle faces\n";
    os << "g"
       << " " << std::filesystem::path(path).filename().replace_extension("") << "\n";
    {
        auto& triangles = mesh.GetTriangles();
        for (auto& tri : triangles)
        {
            // OBJ indices are 1 based
            uint32_t vIdx0 = tri.vIdx0 + 1;
            uint32_t vIdx1 = tri.vIdx1 + 1;
            uint32_t vIdx2 = tri.vIdx2 + 1;

            os << "f";
            os << " ";

            os << vIdx0;
            os << "/";
            if (writeTexCoords)
            {
                os << vIdx0;
            }
            os << "/";
            if (writeNormals)
            {
                os << vIdx0;
            }
            os << " ";

            os << vIdx1;
            os << "/";
            if (writeTexCoords)
            {
                os << vIdx1;
            }
            os << "/";
            if (writeNormals)
            {
                os << vIdx1;
            }
            os << " ";

            os << vIdx2;
            os << "/";
            if (writeTexCoords)
            {
                os << vIdx2;
            }
            os << "/";
            if (writeNormals)
            {
                os << vIdx2;
            }
            os << "\n";
        }
    }

    return true;
}
