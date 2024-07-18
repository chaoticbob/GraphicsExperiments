#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#if defined(WIN32)
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

#if defined(__APPLE__)
#include <cfloat>
#include <cmath>
#endif

#define GREX_LOG_INFO(MSG)                                 \
    {                                                      \
        std::stringstream ss_grex_log_info;                \
        ss_grex_log_info << "INFO : " << MSG << std::endl; \
        Print(ss_grex_log_info.str().c_str());             \
    }

#define GREX_LOG_WARN(MSG)                                 \
    {                                                      \
        std::stringstream ss_grex_log_info;                \
        ss_grex_log_info << "WARN : " << MSG << std::endl; \
        Print(ss_grex_log_info.str().c_str());             \
    }

#define GREX_LOG_ERROR(MSG)                                \
    {                                                      \
        std::stringstream ss_grex_log_info;                \
        ss_grex_log_info << "ERROR: " << MSG << std::endl; \
        Print(ss_grex_log_info.str().c_str());             \
    }

#define GREX_MAX_VERTEX_ATTRIBUTES 6

#define GREX_BASE_FILE_NAME() \
    std::filesystem::path(__FILE__).filename().replace_extension("").string().c_str()

enum GREXFormat
{
    GREX_FORMAT_UNKNOWN            = 0,
    GREX_FORMAT_R8_UNORM           = 1,
    GREX_FORMAT_R8G8_UNORM         = 2,
    GREX_FORMAT_R8G8B8A8_UNORM     = 3,
    GREX_FORMAT_R8_UINT            = 4,
    GREX_FORMAT_R16_UINT           = 5,
    GREX_FORMAT_R16G16_UINT        = 6,
    GREX_FORMAT_R16G16B16A16_UINT  = 7,
    GREX_FORMAT_R32_UINT           = 8,
    GREX_FORMAT_R32_FLOAT          = 9,
    GREX_FORMAT_R32G32_FLOAT       = 10,
    GREX_FORMAT_R32G32B32_FLOAT    = 11,
    GREX_FORMAT_R32G32B32A32_FLOAT = 12,
    GREX_FORMAT_BC1_RGB            = 13,
    GREX_FORMAT_BC3_RGBA           = 14,
    GREX_FORMAT_BC4_R              = 15,
    GREX_FORMAT_BC5_RG             = 16,
    GREX_FORMAT_BC6H_SFLOAT        = 17,
    GREX_FORMAT_BC6H_UFLOAT        = 18,
    GREX_FORMAT_BC7_RGBA           = 19,
};

struct MipOffset
{
    uint32_t Offset    = 0;
    uint32_t RowStride = 0;
};

inline void Print(const char* c_str)
{
#if defined(WIN32)
    OutputDebugStringA(c_str);
#else
    std::cout << c_str;
#endif
}

template <typename T>
bool IsNull(const T* ptr)
{
    bool res = (ptr == nullptr);
    return res;
}

template <typename T>
T Align(T size, T alignment)
{
    static_assert(std::is_integral<T>::value, "T must be an integral type");
    return (size + (alignment - 1)) & ~(alignment - 1);
}

template <typename T>
size_t SizeInBytes(const std::vector<T>& container)
{
    size_t n = container.size() * sizeof(T);
    return n;
}

template <typename T>
uint32_t CountU32(const std::vector<T>& container)
{
    return static_cast<uint32_t>(container.size());
}

template <typename T>
T* DataPtr(std::vector<T>& container)
{
    return container.empty() ? nullptr : container.data();
}

template <typename T>
const T* DataPtr(const std::vector<T>& container)
{
    return container.empty() ? nullptr : container.data();
}

template <typename T>
bool Contains(const T& elem, const std::vector<T>& container)
{
    auto it    = std::find(container.begin(), container.end(), elem);
    bool found = (it != container.end());
    return found;
}
