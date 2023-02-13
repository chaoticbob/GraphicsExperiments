#include "bitmap.h"
#include "window.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

#include <fstream>

template <typename ChannelT>
struct ImageOps
{
};

stbir_edge ToStb(BitmapSampleMode mode)
{
    switch (mode) {
        default: break;
        case BITMAP_SAMPLE_MODE_CLAMP: return STBIR_EDGE_CLAMP;
        case BITMAP_SAMPLE_MODE_WRAP: return STBIR_EDGE_WRAP;
    }
    return STBIR_EDGE_ZERO;
}

template <typename PixelT>
void BitmapT<PixelT>::ScaleTo(
    BitmapSampleMode modeU,
    BitmapSampleMode modeV,
    BitmapT&         target) const
{
    if (target.Empty()) {
        return;
    }

    /*
    int        num_channels         = static_cast<int>(target.GetNumChannels());
    int        alpha_channel        = 3; // index of alpha channel
    stbir_edge edge_mode_horizontal = ToStb(modeU);
    stbir_edge edge_mode_vertical   = ToStb(modeV);

    int res = stbir_resize(
        reinterpret_cast<const float*>(this->GetPixels()), // input_pixels
        static_cast<int>(this->GetWidth()),                // input_w
        static_cast<int>(this->GetHeight()),               // input_h
        static_cast<int>(this->GetRowStride()),            // input_stride_in_bytes
        reinterpret_cast<float*>(target.GetPixels()),      // output_pixels
        static_cast<int>(target.GetWidth()),               // output_w
        static_cast<int>(target.GetHeight()),              // output_h
        static_cast<int>(target.GetRowStride()),           // output_stride_in_bytes
        STBIR_TYPE_FLOAT,                                  // datatype
        num_channels,                                      // num_channels
        alpha_channel,                                     // alpha_channel
        0,                                                 // flags
        edge_mode_horizontal,                              // edge_mode_horizontal
        edge_mode_vertical,                                // edge_mode_vertical
        STBIR_FILTER_CATMULLROM,                           // filter_horizontal
        STBIR_FILTER_CATMULLROM,                           // filter_vertical
        STBIR_COLORSPACE_SRGB,                             // space
        nullptr);                                          // alloc_context
    */

    float dx = mWidth / static_cast<float>(target.GetWidth());
    float dy = mHeight / static_cast<float>(target.GetHeight());

    PixelT* pPixel = target.GetPixels();
    for (uint32_t y = 0; y < target.GetHeight(); ++y) {
        for (uint32_t x = 0; x < target.GetWidth(); ++x) {
            float  fx     = x * dx;
            float  fy     = y * dy;
            PixelT sample = GetBilinearSample(fx, fy);
            *pPixel       = sample;
            ++pPixel;
        }
    }
}

template <typename PixelT>
void BitmapT<PixelT>::CopyTo(uint32_t x0, uint32_t y0, uint32_t width, uint32_t height, BitmapT& target) const
{
    if ((target.GetWidth() != width) && (target.GetHeight() != GetHeight())) {
        assert(false && "source region dimension doesn't match target dimension");
        return;
    }

    uint32_t x1 = x0 + width;
    uint32_t y1 = y0 + height;
    if ((x1 > mWidth) || (y1 > mHeight)) {
        assert(false && "region is out of bounds");
        return;
    }

    const char* pSrc   = reinterpret_cast<const char*>(this->GetPixels(x0, y0));
    char*       pDst   = reinterpret_cast<char*>(target.GetPixels(0, 0));
    uint32_t    nbytes = target.GetRowStride();
    for (uint32_t y = 0; y < height; ++y) {
        memcpy(pDst, pSrc, nbytes);
        pSrc += this->GetRowStride();
        pDst += target.GetRowStride();
    }
}

template class BitmapT<PixelRGBA32f>;

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

    *pBitmap           = BitmapRGB8u(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
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

    *pBitmap           = BitmapRGB32f(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
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
    if (!std::filesystem::exists(absPath)) {
        return false;
    }

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

    *pBitmap           = BitmapRGBA8u(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
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
    if (!std::filesystem::exists(absPath)) {
        return false;
    }

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

    *pBitmap           = BitmapRGBA32f(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    size_t sizeInBytes = pBitmap->GetSizeInBytes();
    assert((nbytesLoaded == sizeInBytes) && "size mismatch");

    memcpy(pBitmap->GetPixels(), pData, sizeInBytes);

    stbi_image_free(pData);

    return true;
}

bool BitmapRGBA32f::Save(const std::filesystem::path& absPath, const BitmapRGBA32f* pBitmap)
{
    if (pBitmap == nullptr) {
        return false;
    }
    if (pBitmap->Empty()) {
        return false;
    }
    if (pBitmap->GetNumChannels() != 4) {
        return false;
    }

    int res = stbi_write_hdr(
        absPath.string().c_str(),
        pBitmap->GetWidth(),
        pBitmap->GetHeight(),
        pBitmap->GetNumChannels(),
        reinterpret_cast<const float*>(pBitmap->GetPixels()));
    if (res == 0) {
        return false;
    }

    return true;
}

// =================================================================================================
// Load functions
// =================================================================================================
BitmapRGBA8u LoadImage8u(const std::filesystem::path& subPath)
{
    std::filesystem::path absPath = GetAssetPath(subPath);
    if (!std::filesystem::exists(absPath)) {
        return {};
    }

    BitmapRGBA8u bitmap = {};
    bool         res    = BitmapRGBA8u::Load(absPath, &bitmap);
    if (!res) {
        return res;
    }

    return bitmap;
}

BitmapRGBA32f LoadImage32f(const std::filesystem::path& subPath)
{
    std::filesystem::path absPath = GetAssetPath(subPath);
    if (!std::filesystem::exists(absPath)) {
        return {};
    }

    BitmapRGBA32f bitmap = {};
    bool          res    = BitmapRGBA32f::Load(absPath, &bitmap);
    if (!res) {
        return res;
    }

    return bitmap;
}

bool LoadIBLMaps32f(const std::filesystem::path& subPath, IBLMaps* pMaps)
{
    std::filesystem::path absPath = GetAssetPath(subPath);
    if (!std::filesystem::exists(absPath)) {
        return false;
    }

    if (pMaps == nullptr) {
        return false;
    }

    std::ifstream is(absPath.string().c_str());
    if (!is.is_open()) {
        return false;
    }

    std::filesystem::path irrMapFilename;
    is >> irrMapFilename;

    std::filesystem::path envMapFilename;
    is >> envMapFilename;

    is >> pMaps->baseWidth;
    is >> pMaps->baseHeight;
    is >> pMaps->numLevels;

    // Load irradiance map
    {
        std::filesystem::path absIrrMapPath = absPath.parent_path() / irrMapFilename;

        bool res = BitmapRGBA32f::Load(absIrrMapPath, &pMaps->irradianceMap);
        if (!res) {
            assert(false && "irradiance map load failed");
            return false;
        }
    }

    // Environment map
    {
        uint32_t expectedHeight = 0;
        uint32_t levelHeight    = pMaps->baseHeight;
        for (uint32_t i = 0; i < pMaps->numLevels; ++i) {
            expectedHeight += levelHeight;
            levelHeight >>= 1;
        }

        std::filesystem::path absEnvMapPath = absPath.parent_path() / envMapFilename;

        bool res = BitmapRGBA32f::Load(absEnvMapPath, &pMaps->environmentMap);
        if (!res) {
            assert(false && "environment map load failed");
            return false;
        }

        if (pMaps->environmentMap.GetHeight() != expectedHeight) {
            assert(false && "environment map height doesn't match expected height");
            return false;
        }
    }

    return true;
}