#include <iomanip>
#include <iostream>
#include <sstream>

#include "config.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
using glm::mat4;
using glm::vec2;
using glm::vec3;
using glm::vec4;

std::ostream& operator<<(std::ostream& os, const glm::vec3& v)
{
    os << "<" << v.x << ", " << v.y << ", " << v.z << ">";
    return os;
}

#define SETW(N, VALUE) std::setw(N) << VALUE

std::ostream& operator<<(std::ostream& os, const glm::mat4& m)
{
    os << "[";
    os << std::setprecision(5) << std::right;
    os << SETW(8, m[0][0]) << " " << SETW(8, m[0][1]) << " " << SETW(8, m[0][2]) << " " << SETW(8, m[0][3]) << "\n";
    os << " " << SETW(8, m[1][0]) << " " << SETW(8, m[1][1]) << " " << SETW(8, m[1][2]) << " " << SETW(8, m[1][3]) << "\n";
    os << " " << SETW(8, m[2][0]) << " " << SETW(8, m[2][1]) << " " << SETW(8, m[2][2]) << " " << SETW(8, m[2][3]) << "\n";
    os << " " << SETW(8, m[3][0]) << " " << SETW(8, m[3][1]) << " " << SETW(8, m[3][2]) << " " << SETW(8, m[3][3]);
    os << "]";
    return os;
}

struct Ray
{
    vec3 org;
    vec3 dir;

    friend std::ostream& operator<<(std::ostream& os, const Ray& obj)
    {
        os << "org=" << obj.org << ", dir=" << obj.dir;
        return os;
    }
};

struct SimpleCamera
{
    vec3 eye;
    vec3 center;
    mat4 view;
    mat4 viewInverse;
    mat4 proj;
    mat4 projInverse;

    SimpleCamera()
    {
        proj        = glm::perspective(glm::radians(60.0f), 1.67f, 0.1f, 10000.0f);
        projInverse = glm::inverse(proj);
    }

    void LookAt(const vec3& _eye, const vec3& _center)
    {
        eye         = _eye;
        center      = _center;
        view        = glm::lookAt(eye, center, vec3(0, 1, 0));
        viewInverse = glm::inverse(view);
    }

    Ray GetRay(const vec2& uv)
    {
        vec2 d = uv * 2.0f - 1.0f;
        d.y    = -d.y;

        vec3 origin = vec3(viewInverse * vec4(0, 0, 0, 1));
        vec3 target = vec3(projInverse * vec4(d.x, d.y, 1, 1));
        vec3 dir    = vec3(viewInverse * vec4(glm::normalize(target), 0));

        Ray r = {};
        r.org = origin;
        r.dir = dir;

        return r;
    }
};

struct PerspCamera
{
    vec3  eye    = vec3(0, 0, -1);
    vec3  center = vec3(0, 0, 0);
    float fovy   = 60.0f;
    float aspect = 1.67f;
    float zNear  = 0.1f;
    float zFar   = 10000.0f;
    mat4  view;
    mat4  viewInverse;
    mat4  proj;
    mat4  projInverse;
    vec3  imagePlaneUL;
    vec3  imagePlaneLR;

    PerspCamera()
    {
        proj        = glm::perspective(glm::radians(fovy), aspect, zNear, zFar);
        projInverse = glm::inverse(proj);

        float tanHalfFov = tan(glm::radians(fovy / 2.0f));

        float nearPlaneH = 1.0f / (tanHalfFov * aspect);
        float nearPlaneW = 1.0 / tanHalfFov;
    }

    void LookAt(const vec3& _eye, const vec3& _center)
    {
        eye         = _eye;
        center      = _center;
        view        = glm::lookAt(eye, center, vec3(0, 1, 0));
        viewInverse = glm::inverse(view);
    }

    Ray GetRay(const vec2& uv)
    {
        vec2 d = uv * 2.0f - 1.0f;
        d.y    = -d.y;

        vec3 origin = vec3(viewInverse * vec4(0, 0, 0, 1));
        vec3 target = vec3(projInverse * vec4(d.x, d.y, 1, 1));
        vec3 dir    = vec3(viewInverse * vec4(glm::normalize(target), 0));

        Ray r = {};
        r.org = origin;
        r.dir = dir;

        return r;
    }
};

int main(int argc, char** argv)
{
    PerspCamera cam;
    cam.LookAt(vec3(0, 0, 1), vec3(0, 0, 0));

    std::stringstream ss;
    ss << cam.GetRay(vec2(0, 0)) << std::endl;
    ss << cam.GetRay(vec2(1, 0)) << std::endl;
    ss << cam.GetRay(vec2(0, 1)) << std::endl;
    ss << cam.GetRay(vec2(1, 1)) << std::endl;

    Print(ss.str().c_str());

    return EXIT_SUCCESS;
}