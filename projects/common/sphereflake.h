#pragma once

#include "config.h"

#include <glm/glm.hpp>

struct SphereFlake
{
    glm::vec3 aabbMin;
    glm::vec3 aabbMax;
};

void GenerateSpheres(
    int                       level,
    int                       maxLevels,
    float                     childRadius,
    float                     parentRadius,
    const glm::vec3&          parentCenter,
    const glm::vec3&          parentOrientation,
    std::vector<SphereFlake>& spheres);
