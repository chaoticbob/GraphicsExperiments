#include "bitmap.h"
#include "window.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// =================================================================================================
// BitmapRGB8u
// =================================================================================================
bool BitmapRGB8u::Load(const std::filesystem::path& absPath, BitmapRGB8u* pBitmap)
{
    if (pBitmap == nullptr) {
        return false;
    }

    int width   = 0;
    int height  = 0;
    int comp    = 0;
    int reqComp = 3;

    stbi_uc* pData = stbi_load(absPath.string().c_str(), &width, &height, &comp, reqComp);
    if (pData == nullptr) {
        return false;
    }
    size_t nbytesLoaded = static_cast<size_t>(width * height * reqComp);

    *pBitmap = BitmapRGB8u(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    size_t sizeInBytes = pBitmap->GetSizeInBytes();
    assert((nbytesLoaded == sizeInBytes) && "size mismatch");

    memcpy(pBitmap->GetPixels(), pData, sizeInBytes);

    stbi_image_free(pData);

    return true;
}

bool BitmapRGB8u::Save(const std::filesystem::path& absPath, const BitmapRGB8u* pBitmap)
{
    return false;
}

// =================================================================================================
// BitmapRGB32f
// =================================================================================================
bool BitmapRGB32f::Load(const std::filesystem::path& absPath, BitmapRGB32f* pBitmap)
{
    if (pBitmap == nullptr) {
        return false;
    }

    int width   = 0;
    int height  = 0;
    int comp    = 0;
    int reqComp = 3;

    float* pData = stbi_loadf(absPath.string().c_str(), &width, &height, &comp, reqComp);
    if (pData == nullptr) {
        return false;
    }
    size_t nbytesLoaded = static_cast<size_t>(width * height * reqComp * sizeof(float));

    *pBitmap = BitmapRGB32f(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    size_t sizeInBytes = pBitmap->GetSizeInBytes();
    assert((nbytesLoaded == sizeInBytes) && "size mismatch");

    memcpy(pBitmap->GetPixels(), pData, sizeInBytes);

    stbi_image_free(pData);

    return true;
}

bool BitmapRGB32f::Save(const std::filesystem::path& absPath, const BitmapRGB32f* pBitmap)
{
    return false;
}

// =================================================================================================
// BitmapRGBA8u
// =================================================================================================
bool BitmapRGBA8u::Load(const std::filesystem::path& absPath, BitmapRGBA8u* pBitmap)
{
    if (pBitmap == nullptr) {
        return false;
    }

    int width   = 0;
    int height  = 0;
    int comp    = 0;
    int reqComp = 4;

    stbi_uc* pData = stbi_load(absPath.string().c_str(), &width, &height, &comp, reqComp);
    if (pData == nullptr) {
        return false;
    }
    size_t nbytesLoaded = static_cast<size_t>(width * height * reqComp);

    *pBitmap = BitmapRGBA8u(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    size_t sizeInBytes = pBitmap->GetSizeInBytes();
    assert((nbytesLoaded == sizeInBytes) && "size mismatch");

    memcpy(pBitmap->GetPixels(), pData, sizeInBytes);

    stbi_image_free(pData);

    return true;
}

bool BitmapRGBA8u::Save(const std::filesystem::path& absPath, const BitmapRGBA8u* pBitmap)
{
    return false;
}

// =================================================================================================
// BitmapRGBA32f
// =================================================================================================
bool BitmapRGBA32f::Load(const std::filesystem::path& absPath, BitmapRGBA32f* pBitmap)
{
    if (pBitmap == nullptr) {
        return false;
    }

    int width   = 0;
    int height  = 0;
    int comp    = 0;
    int reqComp = 4;

    float* pData = stbi_loadf(absPath.string().c_str(), &width, &height, &comp, reqComp);
    if (pData == nullptr) {
        return false;
    }
    size_t nbytesLoaded = static_cast<size_t>(width * height * reqComp * sizeof(float));

    *pBitmap = BitmapRGBA32f(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    size_t sizeInBytes = pBitmap->GetSizeInBytes();
    assert((nbytesLoaded == sizeInBytes) && "size mismatch");

    memcpy(pBitmap->GetPixels(), pData, sizeInBytes);

    stbi_image_free(pData);

    return true;
}

bool BitmapRGBA32f::Save(const std::filesystem::path& absPath, const BitmapRGBA32f* pBitmap)
{
    return false;
}

// =================================================================================================
// BitmapRGBA32f
// =================================================================================================
BitmapRGBA8u LoadImage8u(const std::filesystem::path& subPath)
{
    std::filesystem::path absPath = GetAssetPath(subPath);
    if (!std::filesystem::exists(absPath)) {
        return {};
    }

    BitmapRGBA8u bitmap = {};
    bool res = BitmapRGBA8u::Load(absPath, &bitmap);
    if (!res) {
        return res;
    }

    return bitmap;
}