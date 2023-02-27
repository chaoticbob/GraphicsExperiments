#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <vector>

enum BitmapSampleMode
{
    BITMAP_SAMPLE_MODE_BORDER = 0, // Black pixels
    BITMAP_SAMPLE_MODE_CLAMP  = 1,
    BITMAP_SAMPLE_MODE_WRAP   = 2,
};

enum BitmapFilterMode
{
    BITMAP_FILTER_MODE_NEAREST  = 0,
    BITMAP_FILTER_MODE_LINEAR   = 1,
    BITMAP_FILTER_MODE_GAUSSIAN = 2,
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

    static uint8_t Multiply(uint8_t value, float s)
    {
        float fvalue = value * s;
        fvalue       = std::max<float>(fvalue, MaxValue());
        return static_cast<uint8_t>(fvalue);
    }
};

template <>
struct ChannelOp<float>
{
    static float MaxValue()
    {
        return FLT_MAX;
    }

    static float Multiply(float value, float s)
    {
        return value * s;
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

    template <typename U>
    Pixel3T operator=(const Pixel3T<U>& rhs)
    {
        this->r = static_cast<T>(rhs.r);
        this->g = static_cast<T>(rhs.g);
        this->b = static_cast<T>(rhs.b);
        return *this;
    }

    Pixel3T operator+=(const Pixel3T& rhs)
    {
        this->r += rhs.r;
        this->g += rhs.g;
        this->b += rhs.b;
        return *this;
    }

    Pixel3T operator*=(float rhs)
    {
        this->r = ChannelOp<ChannelT>::Multiply(this->r, rhs);
        this->g = ChannelOp<ChannelT>::Multiply(this->g, rhs);
        this->b = ChannelOp<ChannelT>::Multiply(this->b, rhs);
        return *this;
    }

    Pixel3T operator*(float rhs) const
    {
        Pixel3T result = {};
        result.r       = ChannelOp<ChannelT>::Multiply(this->r, rhs);
        result.g       = ChannelOp<ChannelT>::Multiply(this->g, rhs);
        result.b       = ChannelOp<ChannelT>::Multiply(this->b, rhs);
        return result;
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

    Pixel4T() {}

    Pixel4T(T r_, T g_, T b_, T a_)
        : r(r_), g(g_), b(b_), a(a_)
    {
    }

    template <typename U>
    Pixel4T(const Pixel4T<U>& obj)
        : r(static_cast<T>(obj.r)),
          g(static_cast<T>(obj.g)),
          b(static_cast<T>(obj.b)),
          a(static_cast<T>(obj.a))
    {
    }

    template <typename U>
    Pixel4T operator=(const Pixel4T<U>& rhs)
    {
        this->r = static_cast<T>(rhs.r);
        this->g = static_cast<T>(rhs.g);
        this->b = static_cast<T>(rhs.b);
        this->a = static_cast<T>(rhs.a);
        return *this;
    }

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
        this->r = ChannelOp<ChannelT>::Multiply(this->r, rhs);
        this->g = ChannelOp<ChannelT>::Multiply(this->g, rhs);
        this->b = ChannelOp<ChannelT>::Multiply(this->b, rhs);
        this->a = ChannelOp<ChannelT>::Multiply(this->a, rhs);
        return *this;
    }

    Pixel4T operator*(float rhs) const
    {
        Pixel4T result = {};
        result.r       = ChannelOp<ChannelT>::Multiply(this->r, rhs);
        result.g       = ChannelOp<ChannelT>::Multiply(this->g, rhs);
        result.b       = ChannelOp<ChannelT>::Multiply(this->b, rhs);
        result.a       = ChannelOp<ChannelT>::Multiply(this->a, rhs);
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

    static Pixel4T ClampToMaxNoConvert(const Pixel4T<float>& src)
    {
        float   r     = std::min<float>(src.r, ChannelOp<ChannelT>::MaxValue());
        float   g     = std::min<float>(src.g, ChannelOp<ChannelT>::MaxValue());
        float   b     = std::min<float>(src.b, ChannelOp<ChannelT>::MaxValue());
        float   a     = std::min<float>(src.a, ChannelOp<ChannelT>::MaxValue());
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
    BitmapT() {}

    BitmapT(uint32_t width, uint32_t height)
    {
        Resize(width, height);
    }

    BitmapT(uint32_t width, uint32_t height, uint32_t rowStride, void* pExternalStorage)
        : mWidth(width),
          mHeight(height),
          mRowStride(rowStride),
          mExternalStorage(static_cast<PixelT*>(pExternalStorage))
    {
    }

    ~BitmapT() {}

    bool Empty() const
    {
        bool res = (GetPixels() == nullptr);
        return res;
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
        PixelT* pPixels = (mExternalStorage != nullptr) ? mExternalStorage : mStorage.data();
        if (pPixels != nullptr) {
            char* pAddress = reinterpret_cast<char*>(pPixels);

            size_t pixelStride = GetPixelStride();
            size_t offset      = (y * mRowStride) + (x * pixelStride);
            pAddress += offset;

            pPixels = reinterpret_cast<PixelT*>(pAddress);
        }
        return pPixels;
    }

    const PixelT* GetPixels(uint32_t x = 0, uint32_t y = 0) const
    {
        const PixelT* pPixels = (mExternalStorage != nullptr) ? mExternalStorage : mStorage.data();
        if (pPixels != nullptr) {
            const char* pAddress = reinterpret_cast<const char*>(pPixels);

            size_t pixelStride = GetPixelStride();
            size_t offset      = (y * mRowStride) + (x * pixelStride);
            pAddress += offset;

            pPixels = reinterpret_cast<const PixelT*>(pAddress);
        }
        return pPixels;
    }

    PixelT GetPixel(uint32_t x, uint32_t y) const
    {
        auto pPixels = GetPixels(x, y);
        assert((pPixels != nullptr) && "image is empty");
        return *pPixels;
    }

    void SetPixel(uint32_t x, uint32_t y, const PixelT& value)
    {
        auto pPixel = GetPixels(x, y);
        if (pPixel == nullptr) {
            return;
        }

        assert((x < mWidth) && (y < mHeight) && "out of bounds");

        *pPixel = value;
    }

    void Fill(const PixelT& value)
    {
        char* pPtr = reinterpret_cast<char*>(GetPixels());
        for (uint32_t row = 0; row < mHeight; ++row) {
            PixelT* pPixel = reinterpret_cast<PixelT*>(pPtr);
            for (uint32_t col = 0; col < mWidth; ++col) {
                *pPixel = value;
                ++pPixel;
            }
            pPtr += mRowStride;
        }
    }

    size_t GetSizeInBytes() const
    {
        size_t nbytes = mWidth * mHeight * sizeof(PixelT);
        return nbytes;
    }

    void Resize(uint32_t width, uint32_t height)
    {
        if (mExternalStorage != nullptr) {
            return;
        }

        mWidth     = width;
        mHeight    = height;
        mRowStride = mWidth * PixelT::PixelStride;

        size_t n = mWidth * mHeight;
        if (n > 0) {
            mStorage.resize(n);
        }
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
        using PixelT32f = SelectPixel32f<PixelT>::type;

        int32_t kernelSize = static_cast<int32_t>(sqrt(static_cast<float>(kernel.size())));
        float   radius     = kernelSize / 2.0f;
        int32_t ix         = static_cast<int32_t>(floor(x));
        int32_t iy         = static_cast<int32_t>(floor(y));

        PixelT32f pixel32f = PixelT32f::Black();
        for (int32_t i = 0; i < kernelSize; ++i) {
            for (int32_t j = 0; j < kernelSize; ++j) {
                uint32_t index = (i * kernelSize) + j;

                int32_t sampleX = ix + (j - kernelSize / 2);
                int32_t sampleY = iy + (i - kernelSize / 2);
                if (((sampleX < 0) || (sampleX >= static_cast<int32_t>(mWidth))) && (modeU == BITMAP_SAMPLE_MODE_CLAMP)) {
                    continue;
                }
                if (((sampleY < 0) || (sampleY >= static_cast<int32_t>(mHeight))) && (modeU == BITMAP_SAMPLE_MODE_CLAMP)) {
                    continue;
                }

                PixelT32f sample = GetSample(sampleX, sampleY, modeU, modeV);
                pixel32f += (sample * kernel[index]);
            }
        }

        PixelT pixel = PixelT::ClampToMaxNoConvert(pixel32f);
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
        BitmapFilterMode filterMode,
        BitmapT&         target) const;

    void CopyTo(
        uint32_t x0,
        uint32_t y0,
        uint32_t width,
        uint32_t height,
        BitmapT& target) const;

protected:
    uint32_t            mWidth           = 0;
    uint32_t            mHeight          = 0;
    uint32_t            mRowStride       = 0;
    PixelT*             mExternalStorage = nullptr;
    std::vector<PixelT> mStorage;
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
    using PixelT = PixelRGB8u;

    BitmapRGB8u()
        : BitmapT<PixelRGB8u>() {}

    BitmapRGB8u(uint32_t width, uint32_t height)
        : BitmapT<PixelRGB8u>(width, height) {}

    BitmapRGB8u(uint32_t width, uint32_t height, uint32_t rowStride, void* pExternalStorage)
        : BitmapT<PixelRGB8u>(width, height, rowStride, pExternalStorage) {}

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
    using PixelT = PixelRGB32f;

    BitmapRGB32f()
        : BitmapT<PixelRGB32f>() {}

    BitmapRGB32f(uint32_t width, uint32_t height)
        : BitmapT<PixelRGB32f>(width, height) {}

    BitmapRGB32f(uint32_t width, uint32_t height, uint32_t rowStride, void* pExternalStorage)
        : BitmapT<PixelRGB32f>(width, height, rowStride, pExternalStorage) {}

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
    using PixelT = PixelRGBA8u;

    BitmapRGBA8u()
        : BitmapT<PixelRGBA8u>() {}

    BitmapRGBA8u(uint32_t width, uint32_t height)
        : BitmapT<PixelRGBA8u>(width, height) {}

    BitmapRGBA8u(uint32_t width, uint32_t height, uint32_t rowStride, void* pExternalStorage)
        : BitmapT<PixelRGBA8u>(width, height, rowStride, pExternalStorage) {}

    ~BitmapRGBA8u() {}

    BitmapRGBA8u Scale(
        float            xScale,
        float            yScale,
        BitmapSampleMode modeU      = BITMAP_SAMPLE_MODE_BORDER,
        BitmapSampleMode modeV      = BITMAP_SAMPLE_MODE_BORDER,
        BitmapFilterMode filterMode = BITMAP_FILTER_MODE_NEAREST) const
    {
        uint32_t newWidth  = static_cast<uint32_t>((mWidth * std::max<float>(0, xScale)));
        uint32_t newHeight = static_cast<uint32_t>((mHeight * std::max<float>(0, yScale)));
        if ((newWidth == 0) || (newHeight == 0)) {
            return {};
        }

        BitmapRGBA8u newBitmap = BitmapRGBA8u(newWidth, newHeight);
        ScaleTo(modeU, modeV, filterMode, newBitmap);

        return newBitmap;
    }

    void ScaleTo(
        BitmapSampleMode modeU,
        BitmapSampleMode modeV,
        BitmapFilterMode filterMode,
        BitmapRGBA8u&    target) const
    {
        BitmapT<PixelRGBA8u>::ScaleTo(modeU, modeV, filterMode, target);
    }

    void CopyTo(
        uint32_t      x0,
        uint32_t      y0,
        uint32_t      width,
        uint32_t      height,
        BitmapRGBA8u& target) const
    {
        BitmapT<PixelRGBA8u>::CopyTo(x0, y0, width, height, target);
    }

    static bool Load(const std::filesystem::path& absPath, BitmapRGBA8u* pBitmap);
    static bool Save(const std::filesystem::path& absPath, const BitmapRGBA8u* pBitmap);
};

//! @class BitmapRGBA32f
//!
//!
class BitmapRGBA32f : public BitmapT<PixelRGBA32f>
{
public:
    using PixelT = PixelRGBA32f;

    BitmapRGBA32f()
        : BitmapT<PixelRGBA32f>() {}

    BitmapRGBA32f(uint32_t width, uint32_t height)
        : BitmapT<PixelRGBA32f>(width, height) {}

    BitmapRGBA32f(uint32_t width, uint32_t height, uint32_t rowStride, void* pExternalStorage)
        : BitmapT<PixelRGBA32f>(width, height, rowStride, pExternalStorage) {}

    ~BitmapRGBA32f() {}

    BitmapRGBA32f Scale(
        float            xScale,
        float            yScale,
        BitmapSampleMode modeU      = BITMAP_SAMPLE_MODE_BORDER,
        BitmapSampleMode modeV      = BITMAP_SAMPLE_MODE_BORDER,
        BitmapFilterMode filterMode = BITMAP_FILTER_MODE_NEAREST) const
    {
        uint32_t newWidth  = static_cast<uint32_t>((mWidth * std::max<float>(0, xScale)));
        uint32_t newHeight = static_cast<uint32_t>((mHeight * std::max<float>(0, yScale)));
        if ((newWidth == 0) || (newHeight == 0)) {
            return {};
        }

        BitmapRGBA32f newBitmap = BitmapRGBA32f(newWidth, newHeight);
        ScaleTo(modeU, modeV, filterMode, newBitmap);

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
    float sum   = 0.0f;
    for (uint32_t i = 0; i < kernelSize; i++) {
        for (uint32_t j = 0; j < kernelSize; j++) {
            uint32_t index    = (i * kernelSize) + j;
            float    x        = -mean + j * delta;
            float    y        = -mean + i * delta;
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

// =================================================================================================
// Mipmap
// =================================================================================================
#define MAX_MIP_LEVELS 16

struct MipmapAreaInfo
{
    uint32_t baseWidth;
    uint32_t baseHeight;
    uint32_t numLevels;
    uint32_t fullHeight;
};

inline MipmapAreaInfo CalculateMipmapInfo(uint32_t width, uint32_t height, uint32_t maxNumLevels = 0)
{
    MipmapAreaInfo info = {width, height, 1, height};

    // No mips with dimension less than 4
    width >>= 1;
    height >>= 1;
    while ((width > 4) && (height > 0)) {
        ++info.numLevels;
        info.fullHeight += height;
        if ((maxNumLevels > 0) && (info.numLevels >= maxNumLevels)) {
            break;
        }
        width >>= 1;
        height >>= 1;
    }

    return info;
}

template <typename MipBitmapT>
class MipmapT
{
public:
    using PixelT = MipBitmapT::PixelT;

    MipmapT() {}

    MipmapT(
        const MipBitmapT& mip0,
        BitmapSampleMode  modeU      = BITMAP_SAMPLE_MODE_CLAMP,
        BitmapSampleMode  modeV      = BITMAP_SAMPLE_MODE_CLAMP,
        BitmapFilterMode  filterMode = BITMAP_FILTER_MODE_NEAREST)
    {
        BuildMipmap(mip0, modeU, modeV, filterMode);
    }

    void BuildMipmap(
        const MipBitmapT& mip0,
        BitmapSampleMode  modeU      = BITMAP_SAMPLE_MODE_CLAMP,
        BitmapSampleMode  modeV      = BITMAP_SAMPLE_MODE_CLAMP,
        BitmapFilterMode  filterMode = BITMAP_FILTER_MODE_NEAREST)
    {
        if (mip0.Empty()) {
            return;
        }

        // Calculate storage size for all mip maps
        MipmapAreaInfo areaInfo = CalculateMipmapInfo(mip0.GetWidth(), mip0.GetHeight());

        // Allocate storage
        mStorage = MipBitmapT(areaInfo.baseWidth, areaInfo.fullHeight);

        // Create entries for mips
        {
            uint32_t width     = areaInfo.baseWidth;
            uint32_t height    = areaInfo.baseHeight;
            uint32_t rowStride = mStorage.GetRowStride();
            char*    pStorage  = reinterpret_cast<char*>(mStorage.GetPixels());
            uint32_t offset    = 0;
            for (uint32_t level = 0; level < areaInfo.numLevels; ++level) {
                // Create current mip
                MipBitmapT mip = MipBitmapT(width, height, rowStride, pStorage + offset);
                mMips.push_back(mip);
                mOffsets.push_back(offset);

                // Advance storage pointer to next mip
                offset += (height * rowStride);

                // Next mip dimensions
                width >>= 1;
                height >>= 1;
            }
        }

        // Copy mip0
        mip0.CopyTo(0, 0, mip0.GetWidth(), mip0.GetHeight(), mMips[0]);

        // Build mips
        for (uint32_t level = 1; level < areaInfo.numLevels; ++level) {
            uint32_t prevLevel = level - 1;
            mMips[prevLevel].ScaleTo(
                modeU,
                modeV,
                filterMode,
                mMips[level]);
        }
    }

    uint32_t GetNumLevels() const
    {
        return static_cast<uint32_t>(mMips.size());
    }

    const MipBitmapT& GetMip(uint32_t level) const
    {
        if (level >= mMips.size()) {
            assert(false && "level exceeds available mips");
        }
        return mMips[level];
    }

    uint32_t GetWidth(uint32_t level) const
    {
        if (level >= mMips.size()) {
            assert(false && "level exceeds available mips");
        }
        return mMips[level].GetWidth();
    }

    uint32_t GetHeight(uint32_t level) const
    {
        if (level >= mMips.size()) {
            assert(false && "level exceeds available mips");
        }
        return mMips[level].GetWidth();
    }

    uint32_t GetRowStride() const
    {
        return mStorage.GetRowStride();
    }

    const PixelT* GetPixels() const
    {
        return mStorage.GetPixels();
    }

    size_t GetSizeInBytes() const
    {
        return mStorage.GetSizeInBytes();
    }

    const std::vector<uint32_t>& GetOffsets() const
    {
        return mOffsets;
    }

    static bool Load(const std::filesystem::path& absPath, MipmapT* pMipmap)
    {
        return false;
    }

    static bool Save(const std::filesystem::path& absPath, const MipmapT* pMipmap)
    {
        return MipBitmapT::Save(absPath, &pMipmap->mStorage);
    }

private:
    MipBitmapT              mStorage;
    std::vector<MipBitmapT> mMips;
    std::vector<uint32_t>   mOffsets;
};

using MipmapRGBA8u  = MipmapT<BitmapRGBA8u>;
using MipmapRGBA32f = MipmapT<BitmapRGBA32f>;