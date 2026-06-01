#pragma once

#include <cstdint>
#include <limits>

template<typename Tag>
struct ResourceHandle
{
    static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();

    uint32_t Index = InvalidIndex;
    uint32_t Generation = 0;

    constexpr bool IsValid() const
    {
        return Index != InvalidIndex && Generation != 0;
    }

    constexpr explicit operator bool() const
    {
        return IsValid();
    }

    friend constexpr bool operator==(const ResourceHandle& lhs, const ResourceHandle& rhs)
    {
        return lhs.Index == rhs.Index && lhs.Generation == rhs.Generation;
    }

    friend constexpr bool operator!=(const ResourceHandle& lhs, const ResourceHandle& rhs)
    {
        return !(lhs == rhs);
    }
};

struct TextureResourceTag;
struct ShaderResourceTag;
struct MaterialResourceTag;
struct GeometryResourceTag;

using TextureHandle = ResourceHandle<TextureResourceTag>;
using ShaderHandle = ResourceHandle<ShaderResourceTag>;
using MaterialHandle = ResourceHandle<MaterialResourceTag>;
using GeometryHandle = ResourceHandle<GeometryResourceTag>;
