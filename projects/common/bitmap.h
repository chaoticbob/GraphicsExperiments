#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <vector>

enum BitmapSampleMode
{
    BITMAP_SAMPLE_MODE_BORDER = 0, // Black pixelS
    BITMAP_SAMPLE_MODE_CLAMP  = 1,
    BITMAP_SAMPLE_MODE_WRAP   = 2,
};

template <typename T>
struct ChannelOp
{
};

template <>
struct ChannelOp<uint8_t>
{
    static uint8_t MaxValue()
    {
        return UINT8_MAX;
    }
};

template <>
struct ChannelOp<float>
{
    static float MaxValue()
    {
        return FLT_MAX;
    }
};

template <typename T>
struct Pixel3T
{
    static const uint32_t NumChannels   = 3;
    static const uint32_t ChannelStride = sizeof(T);
    static const uint32_t PixelStride   = NumChannels * ChannelStride;

    using ChannelT = T;

    T r;
    T g;
    T b;

    Pixel3T operator+=(const Pixel3T& rhs)
    {
        this->r += rhs.r;
        this->g += rhs.g;
        this->b += rhs.b;
        return *this;
    }

    Pixel3T operator*=(float rhs)
    {
        this->r *= rhs;
        this->g *= rhs;
        this->b *= rhs;
        return *this;
    }

    Pixel3T operator*(float rhs) const
    {
        Pixel3T result = {};
        result.r       = this->r * rhs;
        result.g       = this->g * rhs;
        result.b       = this->b * rhs;
        return *this;
    }

    static Pixel3T Black()
    {
        return Pixel3T{0, 0, 0};
    }

    static Pixel3T Bilinear(
        float          u0,
        float          v0,
        float          u1,
        float          v1,
        const Pixel3T& Pu0v0,
        const Pixel3T& Pu1v0,
        const Pixel3T& Pu0v1,
        const Pixel3T& Pu1v1)
    {
        float u0v0    = u0 * v0;
        float u1v0    = u1 * v0;
        float u0v1    = u0 * v1;
        float u1v1    = u1 * v1;
        float r       = (Pu0v0.r * u0v0) + (Pu1v0.r * u1v0) + (Pu0v1.r * u0v1) + (Pu1v1.r * u1v1);
        float g       = (Pu0v0.g * u0v0) + (Pu1v0.g * u1v0) + (Pu0v1.g * u0v1) + (Pu1v1.g * u1v1);
        float b       = (Pu0v0.b * u0v0) + (Pu1v0.b * u1v0) + (Pu0v1.b * u0v1) + (Pu1v1.b * u1v1);
        r             = std::max<float>(r, ChannelOp<ChannelT>::MaxValue());
        g             = std::max<float>(g, ChannelOp<ChannelT>::MaxValue());
        b             = std::max<float>(b, ChannelOp<ChannelT>::MaxValue());
        Pixel3T pixel = {};
        pixel.r       = static_cast<ChannelT>(r);
        pixel.g       = static_cast<ChannelT>(g);
        pixel.b       = static_cast<ChannelT>(b);
        return pixel;
    }
};

template <typename T>
struct Pixel4T
{
    static const uint32_t NumChannels   = 4;
    static const uint32_t ChannelStride = sizeof(T);
    static const uint32_t PixelStride   = NumChannels * ChannelStride;

    using ChannelT = T;

    T r;
    T g;
    T b;
    T a;

    Pixel4T operator+=(const Pixel4T& rhs)
    {
        this->r += rhs.r;
        this->g += rhs.g;
        this->b += rhs.b;
        this->a += rhs.a;
        return *this;
    }

    Pixel4T operator*=(float rhs)
    {
        this->r *= rhs;
        this->g *= rhs;
        this->b *= rhs;
        this->a *= rhs;
        return *this;
    }

    Pixel4T operator*(float rhs) const
    {
        Pixel4T result = {};
        result.r       = this->r * rhs;
        result.g       = this->g * rhs;
        result.b       = this->b * rhs;
        result.a       = this->a * rhs;
        return result;
    }

    static Pixel4T Black()
    {
        return Pixel4T{0, 0, 0, 0};
    }

    static Pixel4T Bilinear(
        float          u0,
        float          v0,
        float          u1,
        float          v1,
        const Pixel4T& Pu0v0,
        const Pixel4T& Pu1v0,
        const Pixel4T& Pu0v1,
        const Pixel4T& Pu1v1)
    {
        float u0v0    = u0 * v0;
        float u1v0    = u1 * v0;
        float u0v1    = u0 * v1;
        float u1v1    = u1 * v1;
        float r       = (Pu0v0.r * u0v0) + (Pu1v0.r * u1v0) + (Pu0v1.r * u0v1) + (Pu1v1.r * u1v1);
        float g       = (Pu0v0.g * u0v0) + (Pu1v0.g * u1v0) + (Pu0v1.g * u0v1) + (Pu1v1.g * u1v1);
        float b       = (Pu0v0.b * u0v0) + (Pu1v0.b * u1v0) + (Pu0v1.b * u0v1) + (Pu1v1.b * u1v1);
        float a       = (Pu0v0.a * u0v0) + (Pu1v0.a * u1v0) + (Pu0v1.a * u0v1) + (Pu1v1.a * u1v1);
        r             = std::min<float>(r, ChannelOp<ChannelT>::MaxValue());
        g             = std::min<float>(g, ChannelOp<ChannelT>::MaxValue());
        b             = std::min<float>(b, ChannelOp<ChannelT>::MaxValue());
        a             = std::min<float>(a, ChannelOp<ChannelT>::MaxValue());
        Pixel4T pixel = {};
        pixel.r       = static_cast<ChannelT>(r);
        pixel.g       = static_cast<ChannelT>(g);
        pixel.b       = static_cast<ChannelT>(b);
        pixel.a       = static_cast<ChannelT>(a);
        return pixel;
    }
};

template <typename T>
struct SelectPixel32f
{
};

template <>
struct SelectPixel32f<Pixel3T<uint8_t>>
{
    using type = Pixel3T<float>;
};

template <>
struct SelectPixel32f<Pixel3T<float>>
{
    using type = Pixel3T<float>;
};

template <>
struct SelectPixel32f<Pixel4T<uint8_t>>
{
    using type = Pixel4T<float>;
};

template <>
struct SelectPixel32f<Pixel4T<float>>
{
    using type = Pixel4T<float>;
};

template <typename PixelT>
class BitmapT
{
public:
    BitmapT(uint32_t width = 0, uint32_t height = 0)
    {
        Resize(width, height);
    }

    ~BitmapT() {}

    bool Empty() const
    {
        return mPixels.empty();
    }

    uint32_t GetWidth() const
    {
        return mWidth;
    }

    uint32_t GetHeight() const
    {
        return mHeight;
    }

    uint32_t GetNumChannels() const
    {
        return PixelT::NumChannels;
    }

    uint32_t GetChannelStride() const
    {
        return PixelT::ChannelStride;
    }

    uint32_t GetPixelStride() const
    {
        return PixelT::PixelStride;
    }

    uint32_t GetRowStride() const
    {
        return mRowStride;
    }

    PixelT* GetPixels(uint32_t x = 0, uint32_t y = 0)
    {
        PixelT* pPixels = nullptr;
        if (!mPixels.empty()) {
            char* pAddress = reinterpret_cast<char*>(mPixels.data());

            size_t pixelStride = GetPixelStride();
            size_t offset      = (y * mRowStride) + (x * pixelStride);
            pAddress += offset;

            pPixels = reinterpret_cast<PixelT*>(pAddress);
        }
        return pPixels;
    }

    const PixelT* GetPixels(uint32_t x = 0, uint32_t y = 0) const
    {
        const PixelT* pPixels = nullptr;
        if (!mPixels.empty()) {
            const char* pAddress = reinterpret_cast<const char*>(mPixels.data());

            size_t pixelStride = GetPixelStride();
            size_t offset      = (y * mRowStride) + (x * pixelStride);
            pAddress += offset;

            pPixels = reinterpret_cast<const PixelT*>(pAddress);
        }
        return pPixels;
    }

    const PixelT& GetPixel(uint32_t x, uint32_t y) const
    {
        return mPixels[(y * mWidth) + x];
    }

    void SetPixel(uint32_t x, uint32_t y, const PixelT& value)
    {
        if (mPixels.empty()) {
            return;
        }

        uint32_t index = (y * mWidth) + x;
        mPixels[index] = value;
    }

    size_t GetSizeInBytes() const
    {
        size_t nbytes = mPixels.size() * sizeof(PixelT);
        return nbytes;
    }

    void Resize(uint32_t width, uint32_t height)
    {
        mWidth     = width;
        mHeight    = height;
        mRowStride = mWidth * PixelT::PixelStride;

        size_t n = mWidth * mHeight;
        mPixels.resize(n);
    }

    static int32_t CalcSampleCoordinate(int32_t x, int32_t res, BitmapSampleMode mode)
    {
        switch (mode) {
            default: break;
            case BITMAP_SAMPLE_MODE_WRAP: {
                if (x < 0) {
                    int32_t r = abs(x % res);
                    x         = (res - r);
                }
                else if (x >= res) {
                    x = x % res;
                }
            } break;
            case BITMAP_SAMPLE_MODE_CLAMP: {
                x = std::max<int32_t>(x, 0);
                x = std::min<int32_t>(x, (res - 1));
            } break;
        }
        return x;
    }

    PixelT GetSample(
        int32_t          x,
        int32_t          y,
        BitmapSampleMode modeU = BITMAP_SAMPLE_MODE_BORDER,
        BitmapSampleMode modeV = BITMAP_SAMPLE_MODE_BORDER) const
    {
        PixelT pixel        = PixelT::Black();
        bool   outOfBoundsX = (x < 0) || (x >= static_cast<int32_t>(mWidth));
        bool   outOfBoundsY = (y < 0) || (y >= static_cast<int32_t>(mHeight));
        bool   outOfBounds  = outOfBoundsX || outOfBoundsY;
        if (outOfBounds && ((modeU == BITMAP_SAMPLE_MODE_BORDER) || (modeV == BITMAP_SAMPLE_MODE_BORDER))) {
            return pixel;
        }
        if (outOfBoundsX) {
            x = CalcSampleCoordinate(x, mWidth, modeU);
        }
        if (outOfBoundsY) {
            y = CalcSampleCoordinate(y, mHeight, modeV);
        }
        // Paranoid check bounds again
        bool inBoundsX = (x >= 0) && (x < static_cast<int32_t>(mWidth));
        assert(inBoundsX && "x is out of bounds");
        bool inBoundsY = (y >= 0) && (y < static_cast<int32_t>(mHeight));
        assert(inBoundsX && "y is out of bounds");
        pixel = GetPixel(x, y);
        return pixel;
    };

    PixelT GetBilinearSample(
        float            x,
        float            y,
        BitmapSampleMode modeU = BITMAP_SAMPLE_MODE_BORDER,
        BitmapSampleMode modeV = BITMAP_SAMPLE_MODE_BORDER) const
    {
        int32_t x0    = static_cast<int32_t>(floor(x));
        int32_t y0    = static_cast<int32_t>(floor(y));
        int32_t x1    = x0 + 1;
        int32_t y1    = y0 + 1;
        float   u1    = x - x0;
        float   u0    = 1.0f - u1;
        float   v1    = y - y0;
        float   v0    = 1.0f - v1;
        PixelT  Pu0v0 = this->GetSample(x0, y0, modeU, modeV);
        PixelT  Pu1v0 = this->GetSample(x1, y0, modeU, modeV);
        PixelT  Pu0v1 = this->GetSample(x0, y1, modeU, modeV);
        PixelT  Pu1v1 = this->GetSample(x1, y1, modeU, modeV);
        PixelT  pixel = PixelT::Bilinear(u0, v0, u1, v1, Pu0v0, Pu1v0, Pu0v1, Pu1v1);
        return pixel;
    }

    PixelT GetBilinearSampleUV(
        float            u,
        float            v,
        BitmapSampleMode modeU = BITMAP_SAMPLE_MODE_BORDER,
        BitmapSampleMode modeV = BITMAP_SAMPLE_MODE_BORDER) const
    {
        float  x     = u * static_cast<float>(mWidth - 1);
        float  y     = v * static_cast<float>(mHeight - 1);
        PixelT pixel = this->GetBilinearSample(x, y, modeU, modeV);
        return pixel;
    }

    PixelT GetGaussianSample(
        float                     x,
        float                     y,
        const std::vector<float>& kernel,
        BitmapSampleMode          modeU = BITMAP_SAMPLE_MODE_BORDER,
        BitmapSampleMode          modeV = BITMAP_SAMPLE_MODE_BORDER) const
    {
        int32_t kernelSize = static_cast<int32_t>(sqrt(static_cast<float>(kernel.size())));
        float   radius     = kernelSize / 2.0f;
        int32_t ix         = static_cast<int32_t>(floor(x));
        int32_t iy         = static_cast<int32_t>(floor(y));

        PixelT   pixel      = PixelT::Black();
        uint32_t numSamples = 0;
        for (int32_t i = 0; i < kernelSize; ++i) {
            for (int32_t j = 0; j < kernelSize; ++j) {
                uint32_t index = (i * kernelSize) + j;
                // float    sampleX = x + (static_cast<float>(j) - radius);
                // float    sampleY = y + (static_cast<float>(i) - radius);
                // PixelT   sample  = GetBilinearSample(sampleX, sampleY, modeU, modeV);

                int32_t sampleX = ix + (j - kernelSize / 2);
                int32_t sampleY = iy + (i - kernelSize / 2);
                if (((sampleX < 0) || (sampleX >= static_cast<int32_t>(mWidth))) && (modeU == BITMAP_SAMPLE_MODE_CLAMP)) {
                    continue;
                }
                if (((sampleY < 0) || (sampleY >= static_cast<int32_t>(mHeight))) && (modeU == BITMAP_SAMPLE_MODE_CLAMP)) {
                    continue;
                }

                PixelT sample = GetSample(sampleX, sampleY, modeU, modeV);
                pixel += (sample * kernel[index]);

                ++numSamples;
            }
        }
        // pixel *= 1.0f / static_cast<float>(numSamples);
        return pixel;
    }

    PixelT GetGaussianSampleUV(
        float                     u,
        float                     v,
        const std::vector<float>& kernel,
        BitmapSampleMode          modeU = BITMAP_SAMPLE_MODE_BORDER,
        BitmapSampleMode          modeV = BITMAP_SAMPLE_MODE_BORDER) const
    {
        float  x     = u * static_cast<float>(mWidth - 1);
        float  y     = v * static_cast<float>(mHeight - 1);
        PixelT pixel = this->GetGaussianSample(x, y, kernel, modeU, modeV);
        return pixel;
    }

protected:
    void ScaleTo(
        BitmapSampleMode modeU,
        BitmapSampleMode modeV,
        BitmapT&         target) const;
    /*
    void ScaleTo(BitmapT& target) const
    {
        if (target.Empty()) {
            return;
        }

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
    */

    void CopyTo(uint32_t x0, uint32_t y0, uint32_t width, uint32_t height, BitmapT& target) const;
    /*
    void CopyTo(uint32_t x0, uint32_t y0, uint32_t width, uint32_t height, BitmapT& target) const
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
    */

protected:
    uint32_t            mWidth     = 0;
    uint32_t            mHeight    = 0;
    uint32_t            mRowStride = 0;
    std::vector<PixelT> mPixels;
};

using PixelRGB8u   = Pixel3T<unsigned char>;
using PixelRGB32f  = Pixel3T<float>;
using PixelRGBA8u  = Pixel4T<unsigned char>;
using PixelRGBA32f = Pixel4T<float>;

//! @class BitmapRGB8u
//!
//!
class BitmapRGB8u : public BitmapT<PixelRGB8u>
{
public:
    BitmapRGB8u(uint32_t width = 0, uint32_t height = 0)
        : BitmapT<PixelRGB8u>(width, height) {}

    ~BitmapRGB8u() {}

    static bool Load(const std::filesystem::path& absPath, BitmapRGB8u* pBitmap);
    static bool Save(const std::filesystem::path& absPath, const BitmapRGB8u* pBitmap);
};

//! @class BitmapRGB32f
//!
//!
class BitmapRGB32f : public BitmapT<PixelRGB32f>
{
public:
    BitmapRGB32f(uint32_t width = 0, uint32_t height = 0)
        : BitmapT<PixelRGB32f>(width, height) {}

    ~BitmapRGB32f() {}

    static bool Load(const std::filesystem::path& absPath, BitmapRGB32f* pBitmap);
    static bool Save(const std::filesystem::path& absPath, const BitmapRGB32f* pBitmap);
};

//! @class BitmapRGBA8u
//!
//!
class BitmapRGBA8u : public BitmapT<PixelRGBA8u>
{
public:
    BitmapRGBA8u(uint32_t width = 0, uint32_t height = 0)
        : BitmapT<PixelRGBA8u>(width, height) {}

    ~BitmapRGBA8u() {}

    static bool Load(const std::filesystem::path& absPath, BitmapRGBA8u* pBitmap);
    static bool Save(const std::filesystem::path& absPath, const BitmapRGBA8u* pBitmap);
};

//! @class BitmapRGBA32f
//!
//!
class BitmapRGBA32f : public BitmapT<PixelRGBA32f>
{
public:
    BitmapRGBA32f(uint32_t width = 0, uint32_t height = 0)
        : BitmapT<PixelRGBA32f>(width, height) {}

    ~BitmapRGBA32f() {}

    BitmapRGBA32f Scale(
        float            xScale,
        float            yScale,
        BitmapSampleMode modeU = BITMAP_SAMPLE_MODE_BORDER,
        BitmapSampleMode modeV = BITMAP_SAMPLE_MODE_BORDER) const
    {
        uint32_t newWidth  = static_cast<uint32_t>((mWidth * std::max<float>(0, xScale)));
        uint32_t newHeight = static_cast<uint32_t>((mHeight * std::max<float>(0, yScale)));
        if ((newWidth == 0) || (newHeight == 0)) {
            return {};
        }

        BitmapRGBA32f newBitmap = BitmapRGBA32f(newWidth, newHeight);
        ScaleTo(modeU, modeV, newBitmap);

        return newBitmap;
    }

    BitmapRGBA32f CopyFrom(uint32_t x, uint32_t y, uint32_t width, uint32_t height) const
    {
        if ((width == 0) || (height == 0)) {
            return {};
        }

        BitmapRGBA32f newBitmap = BitmapRGBA32f(width, height);
        CopyTo(x, y, width, height, newBitmap);

        return newBitmap;
    }

    static bool Load(const std::filesystem::path& absPath, BitmapRGBA32f* pBitmap);
    static bool Save(const std::filesystem::path& absPath, const BitmapRGBA32f* pBitmap);
};

// =================================================================================================
// Load functions
// =================================================================================================
BitmapRGBA8u  LoadImage8u(const std::filesystem::path& subPath);
BitmapRGBA32f LoadImage32f(const std::filesystem::path& subPath);

// =================================================================================================
// IBL
// =================================================================================================
struct IBLMaps
{
    BitmapRGBA32f irradianceMap;
    BitmapRGBA32f environmentMap;
    uint32_t      baseWidth;
    uint32_t      baseHeight;
    uint32_t      numLevels;
};

bool LoadIBLMaps32f(const std::filesystem::path& subPath, IBLMaps* pMaps);

// =================================================================================================
// Image processing
// =================================================================================================
inline std::vector<float> GaussianKernel(uint32_t kernelSize, float sigma = 0)
{
    const float kPi = 3.14159265359f;

    if (kernelSize == 0) {
        return {};
    }

    std::vector<float> kernel(kernelSize * kernelSize);
    std::fill(kernel.begin(), kernel.end(), 0.0f);

    if (sigma <= 0.0f) {
        sigma = 0.3f * (((kernelSize - 1.0f) * 0.5f) - 1) + 0.8f;
    }

    float mean  = kernelSize / 2.0f;
    float delta = kernelSize / static_cast<float>(kernelSize - 1);
    float sum = 0.0f;
    for (uint32_t i = 0; i < kernelSize; i++) {
        for (uint32_t j = 0; j < kernelSize; j++) {
            uint32_t index    = (i * kernelSize) + j;
            float    x        = -mean + j*delta;
            float    y        = -mean + i*delta;
            float    expNumer = (x * x + y * y);
            float    expDenom = (2.0f * sigma * sigma);
            float    denom    = (2.0f * kPi * sigma * sigma);
            float    value    = exp(-expNumer / expDenom) / denom;
            kernel[index]     = value;
            sum += kernel[index];
        }
    }

    for (uint32_t i = 0; i < kernel.size(); ++i) {
        kernel[i] /= sum;
    }

    return kernel;
}