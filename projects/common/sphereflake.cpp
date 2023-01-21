#include "sphereflake.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>
using glm::mat4;
using glm::vec3;
using glm::vec4;

void GenerateSpheres(
    int                       level,
    int                       maxLevels,
    float                     childRadius,
    float                     parentRadius,
    const glm::vec3&          parentCenter,
    const glm::vec3&          parentOrientation,
    std::vector<SphereFlake>& spheres)
{
    if (level >= maxLevels) {
        return;
    }

    // clang-format off
    const std::vector<vec3> kSphereFlakeVectors = {
        { 0.408248290f,  0.408248290f, 0.816496581f},
        { 0.965925826f,  0.258819045f, 0.000000000f},
        { 0.258819045f,  0.965925826f, 0.000000000f},
        {-0.557677536f,  0.149429245f, 0.816496581f},
        {-0.707106781f,  0.707106781f, 0.000000000f},
        {-0.965925826f, -0.258819045f, 0.000000000f},
        { 0.149429245f, -0.557677536f, 0.816496581f},
        {-0.258819045f, -0.965925826f, 0.000000000f},
        { 0.707106781f, -0.707106781f, 0.000000000f},
    };
    // clang-format on

    const vec3 kSphereOrienation = vec3(0, 0, 1);

    glm::quat rotQuat = glm::rotation(kSphereOrienation, parentOrientation);
    mat4      rotMat  = glm::toMat4(rotQuat);

    float dist = parentRadius + childRadius;
    for (uint32_t i = 0; i < 9; ++i) {
        auto dir           = glm::normalize(kSphereFlakeVectors[i]);
        dir                = rotMat * vec4(dir, 0);
        vec3        offset = parentCenter + (dist * dir);
        SphereFlake sphere = {};
        sphere.aabbMin     = offset + vec3(-childRadius);
        sphere.aabbMax     = offset + vec3(childRadius);
        spheres.push_back(sphere);

        vec3 center = (sphere.aabbMax + sphere.aabbMin) / 2.0f;
        GenerateSpheres(level + 1, maxLevels, childRadius / 3.0f, childRadius, center, dir, spheres);
    }
}