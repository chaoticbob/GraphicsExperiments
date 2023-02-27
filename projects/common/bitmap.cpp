#if defined(WIN32)
#    define NOMINMAX
#endif

#include "bitmap.h"
#include "window.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

#if defined(GREX_ENABLE_EXR)
#    define TINYEXR_IMPLEMENTATION
#    include "tinyexr.h"
#endif

#include <fstream>

std::string ToLowerCaseCopy(std::string s)
{
    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        [](std::string::value_type c) { return std::tolower(c); });
    return s;
}

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
    BitmapFilterMode filterMode,
    BitmapT&         target) const
{
    if (target.Empty()) {
        return;
    }

    float dx             = mWidth / static_cast<float>(target.GetWidth());
    float dy             = mHeight / static_cast<float>(target.GetHeight());
    auto  gaussianKernel = GaussianKernel(3);

    char* pTargetRow = reinterpret_cast<char*>(target.GetPixels());
    for (uint32_t row = 0; row < target.GetHeight(); ++row) {
        PixelT* pPixel = reinterpret_cast<PixelT*>(pTargetRow);
        for (uint32_t col = 0; col < target.GetWidth(); ++col) {
            float x = (col * dx) + 0.5f;
            float y = (row * dy) + 0.5f;

            PixelT sample = PixelT::Black();
            switch (filterMode) {
                default: {
                    sample = GetSample(
                        static_cast<int32_t>(floor(x)),
                        static_cast<int32_t>(floor(y)),
                        modeU,
                        modeV);
                } break;

                case BITMAP_FILTER_MODE_LINEAR: {
                    sample = GetBilinearSample(
                        x,
                        y,
                        modeU,
                        modeV);
                } break;

                case BITMAP_FILTER_MODE_GAUSSIAN: {
                    sample = GetGaussianSample(
                        x,
                        y,
                        gaussianKernel,
                        modeU,
                        modeV);
                } break;
            }

            *pPixel = sample;

            ++pPixel;
        }
        pTargetRow += target.GetRowStride();
    }
}

template <typename PixelT>
void BitmapT<PixelT>::CopyTo(
    uint32_t x0,
    uint32_t y0,
    uint32_t width,
    uint32_t height,
    BitmapT& target) const
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

// Explicit instantiation
template class BitmapT<PixelRGBA8u>;
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

    auto* pDstPixels = pBitmap->GetPixels();
    memcpy(pDstPixels, pData, sizeInBytes);

    stbi_image_free(pData);

    return true;
}

bool BitmapRGBA8u::Save(const std::filesystem::path& absPath, const BitmapRGBA8u* pBitmap)
{
    bool        success = false;
    std::string ext    = ToLowerCaseCopy(absPath.extension().string());
    if (ext == ".jpg") {
        int res = stbi_write_png(
            absPath.string().c_str(),
            pBitmap->GetWidth(),
            pBitmap->GetHeight(),
            pBitmap->GetNumChannels(),
            reinterpret_cast<const float*>(pBitmap->GetPixels()),
            pBitmap->GetRowStride());
        success = (res == 1);
    }
    else if (ext == ".png") {
        int res = stbi_write_png(
            absPath.string().c_str(),
            pBitmap->GetWidth(),
            pBitmap->GetHeight(),
            pBitmap->GetNumChannels(),
            reinterpret_cast<const float*>(pBitmap->GetPixels()),
            pBitmap->GetRowStride());
        success = (res == 1);
    }
    return success;
}

// =================================================================================================
// BitmapRGBA32f
// =================================================================================================
bool BitmapRGBA32f::Load(const std::filesystem::path& absPath, BitmapRGBA32f* pBitmap)
{
    if (!std::filesystem::exists(absPath)) {
        return false;
    }

    std::string ext = ToLowerCaseCopy(absPath.extension().string());
#if defined(GREX_ENABLE_EXR)
    if ((ext != ".exr") && (ext != ".hdr")) {
        assert(false && "input file is not of 32-bit float format");
        return false;
    }
#else
    if (ext != ".hdr") {
        assert(false && "input file is not of 32-bit float format");
        return false;
    }
#endif

    if (pBitmap == nullptr) {
        return false;
    }

    if (ext == ".hdr") {
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
    }
#if defined(GREX_ENABLE_EXR)
    else if (ext == ".exr") {
        float*      pData  = nullptr; // width * height * RGBA
        int         width  = 0;
        int         height = 0;
        const char* err    = nullptr;

        int ret = LoadEXR(&pData, &width, &height, absPath.string().c_str(), &err);
        if (ret != TINYEXR_SUCCESS) {
            if (err) {
                // fprintf(stderr, "ERR : %s\n", err);
                std::string errMsg = err;
                FreeEXRErrorMessage(err); // release memory of error message.
            }
            return false;
        }

        *pBitmap = BitmapRGBA32f(static_cast<uint32_t>(width), static_cast<uint32_t>(height));

        size_t sizeInBytes = pBitmap->GetSizeInBytes();
        memcpy(pBitmap->GetPixels(), pData, sizeInBytes);

        free(pData); // release memory of image data
    }
#endif
    else {
        return false;
    }

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
    assert(res && "Failed to load image");

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
    assert(res && "Failed to load image");

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