//============================================================================================================================================
// 📦 Frontier/GeometricRaster/VisibilityProjection.h — Visibility Identifier Bit-Packing and Projection Coordination
//============================================================================================================================================

#pragma once

#include "../DeviceExchange/OrientationClassifier.h"
#include <cstdint>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                               VISIBILITY IDENTIFIER
//------------------------------------------------------------------------------------------------------------------------

struct VisibilityIdentifier
{
    uint32_t                PackedIdentifier;                   // [token] 18-bit instance + 14-bit primitive

    [[nodiscard]] static constexpr VisibilityIdentifier Pack(uint32_t InstanceToken, uint32_t PrimitiveToken) noexcept
    {
        return VisibilityIdentifier{ ((InstanceToken & 0x3FFFFu) << 14) | (PrimitiveToken & 0x3FFFu) };
    }

    [[nodiscard]] constexpr uint32_t QueryInstanceToken() const noexcept
    {
        return (PackedIdentifier >> 14) & 0x3FFFFu;
    }

    [[nodiscard]] constexpr uint32_t QueryPrimitiveToken() const noexcept
    {
        return PackedIdentifier & 0x3FFFu;
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return PackedIdentifier != 0xFFFFFFFFu;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                               VISIBILITY PROJECTION
//------------------------------------------------------------------------------------------------------------------------

class VisibilityProjection
{
public:
    VisibilityProjection(uint32_t ExtentWidth, uint32_t ExtentHeight) noexcept;
    ~VisibilityProjection() noexcept = default;

    VisibilityProjection(const VisibilityProjection&) = delete;
    VisibilityProjection& operator=(const VisibilityProjection&) = delete;

    void                    ClearExtent() noexcept;
    void                    WritePixel(uint32_t x, uint32_t y, float Depth, VisibilityIdentifier Identifier) noexcept;

    [[nodiscard]] VisibilityIdentifier QueryPixel(uint32_t x, uint32_t y) const noexcept;
    [[nodiscard]] float     QueryDepth(uint32_t x, uint32_t y) const noexcept;

    [[nodiscard]] uint32_t  QueryWidth() const noexcept { return Width; }
    [[nodiscard]] uint32_t  QueryHeight() const noexcept { return Height; }

    // Single unified conversion operator for resolution
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    uint32_t                Width;                              // [px] viewport width
    uint32_t                Height;                             // [px] viewport height
    std::vector<uint32_t>   VisibilityField;                    // [tokens] 32-bit visibility identifiers
    std::vector<float>      DepthField;                         // [0..1] non-linear device depth field
};

template<>
inline size_t VisibilityProjection::Convert<size_t>() const noexcept
{
    return static_cast<size_t>(Width * Height);
}

} // namespace Frontier
