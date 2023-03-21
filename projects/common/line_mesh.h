#pragma once

#include <glm/glm.hpp>
#include <vector>

class LineMesh
{
public:
    struct Vertex
    {
        glm::vec3 position    = glm::vec3(0);
        glm::vec3 vertexColor = glm::vec3(0);
    };

    struct Line
    {
        uint32_t vIdx0 = UINT32_MAX;
        uint32_t vIdx1 = UINT32_MAX;
    };

    LineMesh() {}

    virtual ~LineMesh() {}

    uint32_t                 GetNumIndices() const { return 2 * GetNumLines(); }
    uint32_t                 GetNumLines() const { return static_cast<uint32_t>(mLines.size()); }
    const std::vector<Line>& GetLines() const { return mLines; }
    void                     AddLine(const LineMesh::Line& line);
    void                     AddLine(uint32_t vIdx0, uint32_t vIdx1);

    uint32_t                   GetNumVertices() const { return static_cast<uint32_t>(mVertices.size()); }
    const std::vector<Vertex>& GetVertices() const { return mVertices; }

    void AddVertex(const LineMesh::Vertex& vtx);
    void AddVertex(
        const glm::vec3& position,
        const glm::vec3& vertexColor);

    // Adds vertex and line
    void AddLine(const LineMesh::Vertex& vtx0, const LineMesh::Vertex& vtx1);

    static LineMesh AxisGridXZ(const glm::vec2& size, uint32_t xSegs, uint32_t zSegs);

private:
    std::vector<Line>   mLines;
    std::vector<Vertex> mVertices;
};