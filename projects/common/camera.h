#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

#define CAMERA_DEFAULT_NEAR_CLIP      0.1f
#define CAMERA_DEFAULT_FAR_CLIP       10000.0f
#define CAMERA_DEFAULT_EYE_POSITION   glm::vec3(0, 0, 1)
#define CAMERA_DEFAULT_LOOK_AT        glm::vec3(0, 0, 0)
#define CAMERA_DEFAULT_WORLD_UP       glm::vec3(0, 1, 0)
#define CAMERA_DEFAULT_VIEW_DIRECTION glm::vec3(0, 0, -1)

// -----------------------------------------------------------------------------
// Camera
// -----------------------------------------------------------------------------
class Camera
{
public:
    Camera(bool pixelAligned = false);

    Camera(float nearClip, float farClip, bool pixelAligned = false);

    virtual ~Camera() {}

    virtual void LookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up = CAMERA_DEFAULT_WORLD_UP);

    const glm::vec3& GetEyePosition() const { return mEyePosition; }
    const glm::vec3& GetTarget() const { return mTarget; }

    const glm::mat4& GetViewMatrix() const { return mViewMatrix; }
    const glm::mat4& GetProjectionMatrix() const { return mProjectionMatrix; }
    const glm::mat4& GetViewProjectionMatrix() const { return mViewProjectionMatrix; }

    glm::vec3 WorldToViewPoint(const glm::vec3& worldPoint) const;
    glm::vec3 WorldToViewVector(const glm::vec3& worldVector) const;

    void MoveAlongViewDirection(float distance);

protected:
    bool              mPixelAligned         = false;
    float             mAspect               = 0;
    float             mNearClip             = CAMERA_DEFAULT_NEAR_CLIP;
    float             mFarClip              = CAMERA_DEFAULT_FAR_CLIP;
    glm::vec3         mEyePosition          = CAMERA_DEFAULT_EYE_POSITION;
    glm::vec3         mTarget               = CAMERA_DEFAULT_LOOK_AT;
    glm::vec3         mViewDirection        = CAMERA_DEFAULT_VIEW_DIRECTION;
    glm::vec3         mWorldUp              = CAMERA_DEFAULT_WORLD_UP;
    mutable glm::mat4 mViewMatrix           = glm::mat4(1);
    mutable glm::mat4 mProjectionMatrix     = glm::mat4(1);
    mutable glm::mat4 mViewProjectionMatrix = glm::mat4(1);
    mutable glm::mat4 mInverseViewMatrix    = glm::mat4(1);
};

// -----------------------------------------------------------------------------
// PerspCamera
// -----------------------------------------------------------------------------
class PerspCamera
    : public Camera
{
public:
    PerspCamera();

    explicit PerspCamera(
        float horizFovDegrees,
        float aspect,
        float nearClip = CAMERA_DEFAULT_NEAR_CLIP,
        float farClip  = CAMERA_DEFAULT_FAR_CLIP);

    explicit PerspCamera(
        const glm::vec3& eye,
        const glm::vec3& target,
        const glm::vec3& up,
        float            horizFovDegrees,
        float            aspect,
        float            nearClip = CAMERA_DEFAULT_NEAR_CLIP,
        float            farClip  = CAMERA_DEFAULT_FAR_CLIP);

    explicit PerspCamera(
        unsigned int pixelWidth,
        unsigned int pixelHeight,
        float        horizFovDegrees = 60.0f);

    // Pixel aligned camera
    explicit PerspCamera(
        unsigned int pixelWidth,
        unsigned int pixelHeight,
        float        horizFovDegrees,
        float        nearClip,
        float        farClip);

    virtual ~PerspCamera();

    void SetPerspective(
        float horizFovDegrees,
        float aspect,
        float nearClip = CAMERA_DEFAULT_NEAR_CLIP,
        float farClip  = CAMERA_DEFAULT_FAR_CLIP);

    void FitToBoundingBox(const glm::vec3& bboxMinWorldSpace, const glm::vec3& bbxoMaxWorldSpace);

private:
    float mHorizFovDegrees = 60.0f;
    float mVertFovDegrees  = 36.98f;
    float mAspect          = 1.0f;
};

// -----------------------------------------------------------------------------
// OrthoCamera
// -----------------------------------------------------------------------------
class OrthoCamera
    : public Camera
{
public:
    OrthoCamera();

    OrthoCamera(
        float left,
        float right,
        float bottom,
        float top,
        float nearClip,
        float farClip);

    virtual ~OrthoCamera();

    void SetOrthographic(
        float left,
        float right,
        float bottom,
        float top,
        float nearClip,
        float farClip);

private:
    float mLeft   = -1.0f;
    float mRight  = 1.0f;
    float mBottom = -1.0f;
    float mTop    = 1.0f;
};

// -----------------------------------------------------------------------------
// ArcballCamera
// -----------------------------------------------------------------------------

//! @class ArcballCamera
//!
//! Adapted from: https://github.com/Twinklebear/arcball-cpp
//!
class ArcballCamera
    : public PerspCamera
{
public:
    ArcballCamera();

    ArcballCamera(
        float horizFovDegrees,
        float aspect,
        float nearClip = CAMERA_DEFAULT_NEAR_CLIP,
        float farClip  = CAMERA_DEFAULT_FAR_CLIP);

    ArcballCamera(
        const glm::vec3& eye,
        const glm::vec3& target,
        const glm::vec3& up,
        float            horizFovDegrees,
        float            aspect,
        float            nearClip = CAMERA_DEFAULT_NEAR_CLIP,
        float            farClip  = CAMERA_DEFAULT_FAR_CLIP);

    virtual ~ArcballCamera() {}

    void LookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up = CAMERA_DEFAULT_WORLD_UP) override;

    //! @fn void Rotate(const glm::vec2& prevPos, const glm::vec2& curPos)
    //!
    //! @param prevPos previous mouse position in normalized device coordinates
    //! @param curPos current mouse position in normalized device coordinates
    //!
    void Rotate(const glm::vec2& prevPos, const glm::vec2& curPos);

    //! @fn void Pan(const glm::vec2& delta)
    //!
    //! @param delta mouse delta in normalized device coordinates
    //!
    void Pan(const glm::vec2& delta);

    //! @fn void Zoom(float amount)
    //!
    void Zoom(float amount);

private:
    void UpdateCamera();

private:
    glm::mat4 mCenterTranslationMatrix;
    glm::mat4 mTranslationMatrix;
    glm::quat mRotationQuat;
};