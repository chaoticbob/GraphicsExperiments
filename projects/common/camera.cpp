#include "camera.h"

#include <algorithm>
#include <glm/gtx/transform.hpp>

// =============================================================================
// Camera
// =============================================================================
Camera::Camera(bool pixelAligned)
    : mPixelAligned(pixelAligned)
{
    LookAt(CAMERA_DEFAULT_EYE_POSITION, CAMERA_DEFAULT_LOOK_AT, CAMERA_DEFAULT_WORLD_UP);
}

Camera::Camera(float nearClip, float farClip, bool pixelAligned)
    : mPixelAligned(pixelAligned),
      mNearClip(nearClip),
      mFarClip(farClip)
{
}

void Camera::LookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up)
{
    const glm::vec3 yAxis = mPixelAligned ? glm::vec3(1, -1, 1) : glm::vec3(1, 1, 1);
    mEyePosition          = eye;
    mTarget               = target;
    mWorldUp              = up;
    mViewDirection        = glm::normalize(mTarget - mEyePosition);
    mViewMatrix           = glm::scale(yAxis) * glm::lookAt(mEyePosition, mTarget, mWorldUp);
    mViewProjectionMatrix = mProjectionMatrix * mViewMatrix;
    mInverseViewMatrix    = glm::inverse(mViewMatrix);
}

glm::vec3 Camera::WorldToViewPoint(const glm::vec3& worldPoint) const
{
    glm::vec3 viewPoint = glm::vec3(mViewMatrix * glm::vec4(worldPoint, 1.0f));
    return viewPoint;
}

glm::vec3 Camera::WorldToViewVector(const glm::vec3& worldVector) const
{
    glm::vec3 viewPoint = glm::vec3(mViewMatrix * glm::vec4(worldVector, 0.0f));
    return viewPoint;
}

void Camera::MoveAlongViewDirection(float distance)
{
    glm::vec3 eyePosition = mEyePosition + (distance * mViewDirection);
    LookAt(eyePosition, mTarget, mWorldUp);
}

void Camera::GetFrustumPlanes(
    Camera::FrustumPlane* pLeft,
    Camera::FrustumPlane* pRight,
    Camera::FrustumPlane* pTop,
    Camera::FrustumPlane* pBottom,
    Camera::FrustumPlane* pNear,
    Camera::FrustumPlane* pFar) const
{
    auto& VP    = this->GetViewProjectionMatrix();
    auto  invVP = glm::inverse(VP);

    auto csNearTL = glm::vec3(-1, 1, -1);
    auto csNearBL = glm::vec3(-1, -1, -1);
    auto csNearBR = glm::vec3(1, -1, -1);
    auto csNearTR = glm::vec3(1, 1, -1);

    auto csFarTL = glm::vec3(-1, 1, 1);
    auto csFarBL = glm::vec3(-1, -1, 1);
    auto csFarBR = glm::vec3(1, -1, 1);
    auto csFarTR = glm::vec3(1, 1, 1);

    auto nearTL = invVP * glm::vec4(csNearTL, 1.0f);
    auto nearBL = invVP * glm::vec4(csNearBL, 1.0f);
    auto nearBR = invVP * glm::vec4(csNearBR, 1.0f);
    auto nearTR = invVP * glm::vec4(csNearTR, 1.0f);

    auto farTL = invVP * glm::vec4(csFarTL, 1.0f);
    auto farBL = invVP * glm::vec4(csFarBL, 1.0f);
    auto farBR = invVP * glm::vec4(csFarBR, 1.0f);
    auto farTR = invVP * glm::vec4(csFarTR, 1.0f);

    nearTL /= nearTL.w;
    nearBL /= nearBL.w;
    nearBR /= nearBR.w;
    nearTR /= nearTR.w;

    farTL /= farTL.w;
    farBL /= farBL.w;
    farBR /= farBR.w;
    farTR /= farTR.w;

    if (pLeft != nullptr)
    {
        auto nearH = glm::vec3(nearTL + nearBL) / 2.0f;
        auto farH  = glm::vec3(farTL + farBL) / 2.0f;
        auto u     = glm::normalize(farH - nearH);
        auto v     = glm::normalize(glm::vec3(nearTL - nearBL));
        auto w     = glm::cross(u, v);
        w          = glm::normalize(w);

        pLeft->Normal   = w;
        pLeft->Position = (nearH + farH) / 2.0f;

        pLeft->C0 = farTL;
        pLeft->C1 = farBL;
        pLeft->C2 = nearBL;
        pLeft->C3 = nearTL;
    }

    if (pRight != nullptr)
    {
        auto nearH = glm::vec3(nearTR + nearBR) / 2.0f;
        auto farH  = glm::vec3(farTR + farBR) / 2.0f;
        auto u     = glm::normalize(farH - nearH);
        auto v     = glm::normalize(glm::vec3(nearBL - nearTL));
        auto w     = glm::cross(u, v);
        w          = glm::normalize(w);

        pRight->Normal   = w;
        pRight->Position = (nearH + farH) / 2.0f;

        pRight->C0 = nearTR;
        pRight->C1 = nearBR;
        pRight->C2 = farBR;
        pRight->C3 = farTR;
    }

    if (pTop != nullptr)
    {
        auto nearH = glm::vec3(nearTL + nearTR) / 2.0f;
        auto farH  = glm::vec3(farTL + farTR) / 2.0f;
        auto u     = glm::normalize(farH - nearH);
        auto v     = glm::normalize(glm::vec3(nearTR - nearTL));
        auto w     = glm::cross(u, v);
        w          = glm::normalize(w);

        pTop->Normal   = w;
        pTop->Position = (nearH + farH) / 2.0f;

        pTop->C0 = farTL;
        pTop->C1 = nearTL;
        pTop->C2 = nearTR;
        pTop->C3 = farTR;
    }

    if (pBottom != nullptr)
    {
        auto nearH = glm::vec3(nearBL + nearBR) / 2.0f;
        auto farH  = glm::vec3(farBL + farBR) / 2.0f;
        auto u     = glm::normalize(farH - nearH);
        auto v     = glm::normalize(glm::vec3(nearBL - nearBR));
        auto w     = glm::cross(u, v);
        w          = glm::normalize(w);

        pBottom->Normal   = w;
        pBottom->Position = (nearH + farH) / 2.0f;

        pBottom->C0 = nearBL;
        pBottom->C1 = farBL;
        pBottom->C2 = farBR;
        pBottom->C3 = nearBR;
    }

    if (pNear != nullptr)
    {
        pNear->Normal   = mViewDirection;
        pNear->Position = (nearTL + nearBR) / 2.0f;

        pNear->C0 = nearTL;
        pNear->C1 = nearBL;
        pNear->C2 = nearBR;
        pNear->C3 = nearTR;
    }

    if (pFar != nullptr)
    {
        pFar->Normal   = -mViewDirection;
        pFar->Position = (farTL + farBR) / 2.0f;

        pFar->C0 = farTL;
        pFar->C1 = farBL;
        pFar->C2 = farBR;
        pFar->C3 = farTR;
    }
}

glm::vec4 Camera::GetFrustumSphere() const
{
    auto& VP    = this->GetViewProjectionMatrix();
    auto  invVP = glm::inverse(VP);

    auto csNearTL = glm::vec3(-1, 1, -1);
    auto csNearBL = glm::vec3(-1, -1, -1);
    auto csNearBR = glm::vec3(1, -1, -1);
    auto csNearTR = glm::vec3(1, 1, -1);

    auto csFarTL = glm::vec3(-1, 1, 1);
    auto csFarBL = glm::vec3(-1, -1, 1);
    auto csFarBR = glm::vec3(1, -1, 1);
    auto csFarTR = glm::vec3(1, 1, 1);

    auto nearTL = invVP * glm::vec4(csNearTL, 1.0f);
    auto nearBL = invVP * glm::vec4(csNearBL, 1.0f);
    auto nearBR = invVP * glm::vec4(csNearBR, 1.0f);
    auto nearTR = invVP * glm::vec4(csNearTR, 1.0f);

    auto farTL = invVP * glm::vec4(csFarTL, 1.0f);
    auto farBL = invVP * glm::vec4(csFarBL, 1.0f);
    auto farBR = invVP * glm::vec4(csFarBR, 1.0f);
    auto farTR = invVP * glm::vec4(csFarTR, 1.0f);

    nearTL /= nearTL.w;
    nearBL /= nearBL.w;
    nearBR /= nearBR.w;
    nearTR /= nearTR.w;

    farTL /= farTL.w;
    farBL /= farBL.w;
    farBR /= farBR.w;
    farTR /= farTR.w;

    auto nearCenter = (nearTL + nearBL + nearBR + nearTR) / 4.0f;
    auto farCenter  = (farTL + farBL + farBR + farTR) / 4.0f;
    auto center     = glm::vec3(nearCenter + farCenter) / 2.0f;

    float r = glm::distance(center, glm::vec3(nearTL));
    r       = std::max(r, glm::distance(center, glm::vec3(nearBL)));
    r       = std::max(r, glm::distance(center, glm::vec3(nearBR)));
    r       = std::max(r, glm::distance(center, glm::vec3(nearTR)));
    r       = std::max(r, glm::distance(center, glm::vec3(farTL)));
    r       = std::max(r, glm::distance(center, glm::vec3(farBL)));
    r       = std::max(r, glm::distance(center, glm::vec3(farBR)));
    r       = std::max(r, glm::distance(center, glm::vec3(farTR)));

    return glm::vec4(center, r);
}

// =============================================================================
// PerspCamera
// =============================================================================
PerspCamera::PerspCamera()
{
}

PerspCamera::PerspCamera(
    float horizFovDegrees,
    float aspect,
    float nearClip,
    float farClip)
    : Camera(nearClip, farClip)
{
    SetPerspective(
        horizFovDegrees,
        aspect,
        nearClip,
        farClip);
}

PerspCamera::PerspCamera(
    const glm::vec3& eye,
    const glm::vec3& target,
    const glm::vec3& up,
    float            horizFovDegrees,
    float            aspect,
    float            nearClip,
    float            farClip)
    : Camera(nearClip, farClip)
{
    LookAt(eye, target, up);

    SetPerspective(
        horizFovDegrees,
        aspect,
        nearClip,
        farClip);
}

PerspCamera::PerspCamera(
    uint32_t pixelWidth,
    uint32_t pixelHeight,
    float    horizFovDegrees)
    : Camera(true)
{
    float aspect   = static_cast<float>(pixelWidth) / static_cast<float>(pixelHeight);
    float eyeX     = pixelWidth / 2.0f;
    float eyeY     = pixelHeight / 2.0f;
    float halfFov  = atan(tan(glm::radians(horizFovDegrees) / 2.0f) / aspect); // horiz fov -> vert fov
    float theTan   = tanf(halfFov);
    float dist     = eyeY / theTan;
    float nearClip = dist / 10.0f;
    float farClip  = dist * 10.0f;

    SetPerspective(horizFovDegrees, aspect, nearClip, farClip);
    LookAt(glm::vec3(eyeX, eyeY, dist), glm::vec3(eyeX, eyeY, 0.0f));
}

PerspCamera::PerspCamera(
    uint32_t pixelWidth,
    uint32_t pixelHeight,
    float    horizFovDegrees,
    float    nearClip,
    float    farClip)
    : Camera(nearClip, farClip, true)
{
    float aspect  = static_cast<float>(pixelWidth) / static_cast<float>(pixelHeight);
    float eyeX    = pixelWidth / 2.0f;
    float eyeY    = pixelHeight / 2.0f;
    float halfFov = atan(tan(glm::radians(horizFovDegrees) / 2.0f) / aspect); // horiz fov -> vert fov
    float theTan  = tanf(halfFov);
    float dist    = eyeY / theTan;

    SetPerspective(horizFovDegrees, aspect, nearClip, farClip);
    LookAt(glm::vec3(eyeX, eyeY, dist), glm::vec3(eyeX, eyeY, 0.0f));
}

PerspCamera::~PerspCamera()
{
}

void PerspCamera::SetPerspective(
    float horizFovDegrees,
    float aspect,
    float nearClip,
    float farClip)
{
    mHorizFovDegrees = horizFovDegrees;
    mAspect          = aspect;
    mNearClip        = nearClip;
    mFarClip         = farClip;

    float horizFovRadians = glm::radians(mHorizFovDegrees);
    float vertFovRadians  = 2.0f * atan(tan(horizFovRadians / 2.0f) / mAspect);
    mVertFovDegrees       = glm::degrees(vertFovRadians);

    mProjectionMatrix = glm::perspective(
        vertFovRadians,
        mAspect,
        mNearClip,
        mFarClip);

    mViewProjectionMatrix = mProjectionMatrix * mViewMatrix;
}

void PerspCamera::FitToBoundingBox(const glm::vec3& bboxMinWorldSpace, const glm::vec3& bbxoMaxWorldSpace)
{
    glm::vec3 min             = bboxMinWorldSpace;
    glm::vec3 max             = bbxoMaxWorldSpace;
    glm::vec3 target          = (min + max) / 2.0f;
    glm::vec3 up              = glm::normalize(mInverseViewMatrix * glm::vec4(0, 1, 0, 0));
    glm::mat4 viewSpaceMatrix = glm::lookAt(mEyePosition, target, up);

    // World space oriented bounding box
    glm::vec3 obb[8] = {
        {min.x, max.y, min.z},
        {min.x, min.y, min.z},
        {max.x, min.y, min.z},
        {max.x, max.y, min.z},
        {min.x, max.y, max.z},
        {min.x, min.y, max.z},
        {max.x, min.y, max.z},
        {max.x, max.y, max.z},
    };

    // Tranform obb from world space to view space
    for (uint32_t i = 0; i < 8; ++i)
    {
        obb[i] = viewSpaceMatrix * glm::vec4(obb[i], 1.0f);
    }

    // Get aabb from obb in view space
    min = max = obb[0];
    for (uint32_t i = 1; i < 8; ++i)
    {
        min = glm::min(min, obb[i]);
        max = glm::max(max, obb[i]);
    }

    // Get x,y extent max
    float xmax = glm::max(glm::abs(min.x), glm::abs(max.x));
    float ymax = glm::max(glm::abs(min.y), glm::abs(max.y));
    float rad  = glm::max(xmax, ymax);
    float fov  = mAspect < 1.0f ? mHorizFovDegrees : mVertFovDegrees;

    // Calculate distance
    float dist = rad / tan(glm::radians(fov / 2.0f));

    // Calculate eye position
    glm::vec3 dir = glm::normalize(mEyePosition - target);
    glm::vec3 eye = target + (dist + mNearClip) * dir;

    // Adjust camera look at
    LookAt(eye, target, up);
}

PerspCamera::FrustumCone PerspCamera::GetFrustumCone(bool fitFarClip) const
{
    PerspCamera::FrustumCone cone = {};
    cone.Tip                      = mEyePosition;
    cone.Dir                      = mViewDirection;
    cone.Height                   = mFarClip;
    cone.Angle                    = glm::radians((mAspect > 1.0) ? mHorizFovDegrees : mVertFovDegrees);

    if (fitFarClip)
    {
        auto& VP    = this->GetViewProjectionMatrix();
        auto  invVP = glm::inverse(VP);

        auto csFarTL = glm::vec3(-1, 1, 1);
        auto csFarBL = glm::vec3(-1, -1, 1);
        auto csFarBR = glm::vec3(1, -1, 1);
        auto csFarTR = glm::vec3(1, 1, 1);

        auto farTL = invVP * glm::vec4(csFarTL, 1.0f);
        auto farBL = invVP * glm::vec4(csFarBL, 1.0f);
        auto farBR = invVP * glm::vec4(csFarBR, 1.0f);
        auto farTR = invVP * glm::vec4(csFarTR, 1.0f);

        farTL /= farTL.w;
        farBL /= farBL.w;
        farBR /= farBR.w;
        farTR /= farTR.w;

        auto farCenter = (farTL + farBL + farBR + farTR) / 4.0f;

        float r    = glm::distance(farCenter, farTL);
        cone.Angle = 2.0f * atan(r / mFarClip);
    }

    return cone;
}

// =============================================================================
// OrthoCamera
// =============================================================================
OrthoCamera::OrthoCamera()
{
}

OrthoCamera::OrthoCamera(
    float left,
    float right,
    float bottom,
    float top,
    float near_clip,
    float far_clip)
{
    SetOrthographic(
        left,
        right,
        bottom,
        top,
        near_clip,
        far_clip);
}

OrthoCamera::~OrthoCamera()
{
}

void OrthoCamera::SetOrthographic(
    float left,
    float right,
    float bottom,
    float top,
    float nearClip,
    float farClip)
{
    mLeft     = left;
    mRight    = right;
    mBottom   = bottom;
    mTop      = top;
    mNearClip = nearClip;
    mFarClip  = farClip;

    mProjectionMatrix = glm::ortho(
        mLeft,
        mRight,
        mBottom,
        mTop,
        mNearClip,
        mFarClip);
}

// =============================================================================
// ArcballCamera
// =============================================================================
ArcballCamera::ArcballCamera()
{
}

ArcballCamera::ArcballCamera(
    float horizFovDegrees,
    float aspect,
    float nearClip,
    float farClip)
    : PerspCamera(horizFovDegrees, aspect, nearClip, farClip)
{
}

ArcballCamera::ArcballCamera(
    const glm::vec3& eye,
    const glm::vec3& target,
    const glm::vec3& up,
    float            horizFovDegrees,
    float            aspect,
    float            nearClip,
    float            farClip)
    : PerspCamera(eye, target, up, horizFovDegrees, aspect, nearClip, farClip)
{
}

void ArcballCamera::UpdateCamera()
{
    mViewMatrix        = mTranslationMatrix * glm::mat4_cast(mRotationQuat) * mCenterTranslationMatrix;
    mInverseViewMatrix = glm::inverse(mViewMatrix);

    // Transform the view space origin into world space for eye position
    mEyePosition = mInverseViewMatrix * glm::vec4(0, 0, 0, 1);
}

void ArcballCamera::LookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up)
{
    Camera::LookAt(eye, target, up);

    glm::vec3 viewDir = target - eye;
    glm::vec3 zAxis   = glm::normalize(viewDir);
    glm::vec3 xAxis   = glm::normalize(glm::cross(zAxis, glm::normalize(up)));
    glm::vec3 yAxis   = glm::normalize(glm::cross(xAxis, zAxis));
    xAxis             = glm::normalize(glm::cross(zAxis, yAxis));

    mCenterTranslationMatrix = glm::inverse(glm::translate(target));
    mTranslationMatrix       = glm::translate(glm::vec3(0.0f, 0.0f, -glm::length(viewDir)));
    mRotationQuat            = glm::normalize(glm::quat_cast(glm::transpose(glm::mat3(xAxis, yAxis, -zAxis))));

    UpdateCamera();
}

static glm::quat ScreenToArcball(const glm::vec2& p)
{
    float dist = glm::dot(p, p);

    // If we're on/in the sphere return the point on it
    if (dist <= 1.0f)
    {
        return glm::quat(0.0f, p.x, p.y, glm::sqrt(1.0f - dist));
    }

    // Otherwise we project the point onto the sphere
    const glm::vec2 proj = glm::normalize(p);
    return glm::quat(0.0f, proj.x, proj.y, 0.0f);
}

void ArcballCamera::Rotate(const glm::vec2& prevPos, const glm::vec2& curPos)
{
    const glm::vec2 kNormalizeDeviceCoordinatesMin = glm::vec2(-1, -1);
    const glm::vec2 kNormalizeDeviceCoordinatesMax = glm::vec2(1, 1);

    // Clamp mouse positions to stay in NDC
    glm::vec2 clampedCurPos  = glm::clamp(curPos, kNormalizeDeviceCoordinatesMin, kNormalizeDeviceCoordinatesMax);
    glm::vec2 clampedPrevPos = glm::clamp(prevPos, kNormalizeDeviceCoordinatesMin, kNormalizeDeviceCoordinatesMax);

    glm::quat mouseCurBall  = ScreenToArcball(clampedCurPos);
    glm::quat mousePrevBall = ScreenToArcball(clampedPrevPos);

    mRotationQuat = mouseCurBall * mousePrevBall * mRotationQuat;

    UpdateCamera();
}

void ArcballCamera::Pan(const glm::vec2& delta)
{
    float     zoomAmount = glm::abs(mTranslationMatrix[3][2]);
    glm::vec4 motion     = glm::vec4(delta.x * zoomAmount, delta.y * zoomAmount, 0.0f, 0.0f);

    // Find the panning amount in the world space
    motion = mInverseViewMatrix * motion;

    mCenterTranslationMatrix = glm::translate(glm::vec3(motion)) * mCenterTranslationMatrix;

    UpdateCamera();
}

void ArcballCamera::Zoom(float amount)
{
    glm::vec3 motion = glm::vec3(0.0f, 0.0f, amount);

    mTranslationMatrix = glm::translate(motion) * mTranslationMatrix;

    UpdateCamera();
}
