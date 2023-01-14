#pragma once

#include <cassert>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(WIN32)
#    include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

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