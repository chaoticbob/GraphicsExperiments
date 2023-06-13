#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(WIN32)
#    include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

#if defined(__APPLE__)
#include <float.h>
#include <math.h>
#endif

#define GREX_LOG_INFO(MSG)                                 \
    {                                                      \
        std::stringstream ss_grex_log_info;                \
        ss_grex_log_info << "INFO : " << MSG << std::endl; \
        Print(ss_grex_log_info.str().c_str());             \
    }

#define GREX_LOG_ERROR(MSG)                                \
    {                                                      \
        std::stringstream ss_grex_log_info;                \
        ss_grex_log_info << "ERROR: " << MSG << std::endl; \
        Print(ss_grex_log_info.str().c_str());             \
    }

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
