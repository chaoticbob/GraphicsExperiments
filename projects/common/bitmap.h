#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

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
};

template <typename T>
struct Pixel4T
{
    static const uint32_t NumChannels   = 3;
    static const uint32_t ChannelStride = sizeof(T);
    static const uint32_t PixelStride   = NumChannels * ChannelStride;

    using ChannelT = T;

    T r;
    T g;
    T b;
    T a;
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
        return PixelT::PixelSize;
    }

    uint32_t GetRowStride() const
    {
        return mRowStride;
    }

    PixelT* GetPixels()
    {
        return mPixels.empty() ? nullptr : mPixels.data();
    }

    const PixelT* GetPixels() const
    {
        return mPixels.empty() ? nullptr : mPixels.data();
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

    static bool Load(const std::filesystem::path& absPath, BitmapRGBA32f* pBitmap);
    static bool Save(const std::filesystem::path& absPath, const BitmapRGBA32f* pBitmap);
};

// =================================================================================================
// Load functions
// =================================================================================================
BitmapRGBA8u LoadImage8u(const std::filesystem::path& subPath);