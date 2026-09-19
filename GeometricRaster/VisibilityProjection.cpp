//============================================================================================================================================
// 📦 Frontier/GeometricRaster/VisibilityProjection.cpp — Visibility Projection Field Implementation
//============================================================================================================================================

#include "VisibilityProjection.h"
#include <algorithm>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

VisibilityProjection::VisibilityProjection(uint32_t ExtentWidth, uint32_t ExtentHeight) noexcept
    : Width(ExtentWidth)
    , Height(ExtentHeight)
    , VisibilityField(ExtentWidth * ExtentHeight, 0xFFFFFFFFu)
    , DepthField(ExtentWidth * ExtentHeight, 1.0f)
{
}

void VisibilityProjection::ClearExtent() noexcept
{
    std::fill(VisibilityField.begin(), VisibilityField.end(), 0xFFFFFFFFu);
    std::fill(DepthField.begin(), DepthField.end(), 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                FIELD ACCESS OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void VisibilityProjection::WritePixel(uint32_t x, uint32_t y, float Depth, VisibilityIdentifier Identifier) noexcept
{
    if (x < Width && y < Height)
    {
        size_t idx = static_cast<size_t>(y * Width + x);
        if (Depth < DepthField[idx])
        {
            DepthField[idx] = Depth;
            VisibilityField[idx] = Identifier.PackedIdentifier;
        }
    }
}

VisibilityIdentifier VisibilityProjection::QueryPixel(uint32_t x, uint32_t y) const noexcept
{
    if (x < Width && y < Height)
    {
        return VisibilityIdentifier{ VisibilityField[y * Width + x] };
    }
    return VisibilityIdentifier{ 0xFFFFFFFFu };
}

float VisibilityProjection::QueryDepth(uint32_t x, uint32_t y) const noexcept
{
    if (x < Width && y < Height)
    {
        return DepthField[y * Width + x];
    }
    return 1.0f;
}

} // namespace Frontier
