#pragma once

#include <cstddef>

template <typename T>
static T swapEndian(T u)
{
    union
    {
        T u;
        unsigned char u8[sizeof(T)];
    } source, dest;

    source.u = u;

    for (size_t k = 0; k < sizeof(T); k++)
    {
        dest.u8[k] = source.u8[sizeof(T) - k - 1];
    }

    return dest.u;
}


