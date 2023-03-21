#include "line_mesh.h"

void LineMesh::AddLine(const LineMesh::Line& line)
{
    mLines.push_back(line);
}

void LineMesh::AddLine(uint32_t vIdx0, uint32_t vIdx1)
{
    LineMesh::Line line = {.vIdx0 = vIdx0, .vIdx1 = vIdx1};
    AddLine(line);
}

void LineMesh::AddVertex(const LineMesh::Vertex& vtx)
{
    mVertices.push_back(vtx);
}

void LineMesh::AddVertex(
    const glm::vec3& position,
    const glm::vec3& vertexColor)
{
    LineMesh::Vertex vtx = {.position = position, .vertexColor = vertexColor};
    AddVertex(vtx);
}

void LineMesh::AddLine(const LineMesh::Vertex& vtx0, const LineMesh::Vertex& vtx1)
{
    AddVertex(vtx0.position, vtx0.vertexColor);
    AddVertex(vtx1.position, vtx1.vertexColor);

    uint32_t n = GetNumVertices();
    AddLine(n - 2, n - 1);
}

LineMesh LineMesh::AxisGridXZ(const glm::vec2& size, uint32_t xSegs, uint32_t zSegs)
{
    uint32_t xLines = xSegs + 1;
    uint32_t zLines = zSegs + 1;

    float x0 = -size.x / 2.0f;
    float z0 = -size.y / 2.0f;
    float x1 = size.x / 2.0f;
    float z1 = size.y / 2.0f;
    float dx = (x1 - x0) / (xLines - 1);
    float dz = (z1 - z0) / (zLines - 1);

    LineMesh mesh;

    // X lines
    for (uint32_t i = 0; i < xLines; ++i) {
        if (i == (zSegs / 2)) {
            continue;
        }

        float     x     = x0 + i * dx;
        glm::vec3 P0    = glm::vec3(x, 0, z0);
        glm::vec3 P1    = glm::vec3(x, 0, z1);
        glm::vec3 color = glm::vec3(0.5f);

        if ((i == 0) || (i == (xLines - 1))) {
            color = glm::vec3(0.6f);
        }

        mesh.AddLine({P0, color}, {P1, color});
    }

    // Z lines
    for (uint32_t i = 0; i < zLines; ++i) {
        if (i == (zSegs / 2)) {
            continue;
        }

        float     z     = z0 + i * dz;
        glm::vec3 P0    = glm::vec3(x0, 0, z);
        glm::vec3 P1    = glm::vec3(x1, 0, z);
        glm::vec3 color = glm::vec3(0.5f);

        if ((i == 0) || (i == (zLines - 1))) {
            color = glm::vec3(0.6f);
        }

        mesh.AddLine({P0, color}, {P1, color});
    }

    // X Axis
    {
        float     z     = z0 + (xSegs / 2) * dz;
        glm::vec3 P0    = glm::vec3(1.25f * x0, 0, z);
        glm::vec3 P1    = glm::vec3(1.25f * x1, 0, z);
        glm::vec3 color = glm::vec3(0.9f, 0, 0);

        mesh.AddLine({P0, color}, {P1, color});

        P0 = glm::vec3(1.15f * x1, 0.0f, z - (0.05f * size.y));
        mesh.AddLine({P0, color}, {P1, color});

        P0 = glm::vec3(1.15f * x1, 0.0f, z + (0.05f * size.y));
        mesh.AddLine({P0, color}, {P1, color});
    }

    // Y Axis
    {
        float     x     = x0 + (zSegs / 2) * dx;
        glm::vec3 P0    = glm::vec3(0, 1.25f * x0, 0);
        glm::vec3 P1    = glm::vec3(0, 1.25f * x1, 0);
        glm::vec3 color = glm::vec3(0, 0.9f, 0);

        mesh.AddLine({P0, color}, {P1, color});

        P0 = glm::vec3(x - (0.05f * size.x), 1.15f * x1, 0);
        mesh.AddLine({P0, color}, {P1, color});
 
        P0 = glm::vec3(x + (0.05f * size.x), 1.15f * x1, 0);
        mesh.AddLine({P0, color}, {P1, color});
    }

    // Z Axis
    {
        float     x     = x0 + (zSegs / 2) * dx;
        glm::vec3 P0    = glm::vec3(0, 0, 1.25f * x0);
        glm::vec3 P1    = glm::vec3(0, 0, 1.25f * x1);
        glm::vec3 color = glm::vec3(0.2f, 0.2f, 0.99f);

        mesh.AddLine({P0, color}, {P1, color});

        P0 = glm::vec3(x - (0.05f * size.x), 0, 1.15f * z1);
        mesh.AddLine({P0, color}, {P1, color});
 
        P0 = glm::vec3(x + (0.05f * size.x), 0, 1.15f * z1);
        mesh.AddLine({P0, color}, {P1, color});
    }

    return mesh;
}