//============================================================================================================================================
// 📦 Frontier/GeometricRaster/MaterialCodec.h — Surface Attribute Interpolation and Decoupled Material Decoding
//============================================================================================================================================

#pragma once

#include "GeometryStructure.h"
#include "VisibilityProjection.h"
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                              SURFACE ATTRIBUTE RECORD
//------------------------------------------------------------------------------------------------------------------------

struct SurfaceAttributeRecord
{
    Vector3                 WorldPosition;                      // [m] reconstructed world position
    Vector3                 SurfaceNormal;                      // [-] normalized surface normal
    Vector4                 AlbedoColor;                        // [0..1] surface albedo reflectance
    float                   Roughness;                          // [0..1] microfacet roughness
    float                   Metallic;                           // [0..1] metallic conductive factor
    float                   TextureCoordinateU;                 // [0..1] interpolated horizontal UV
    float                   TextureCoordinateV;                 // [0..1] interpolated vertical UV
    bool                    ValidCondition;                     // [bool] true when surface is visible
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    MATERIAL CODEC
//------------------------------------------------------------------------------------------------------------------------

class MaterialCodec
{
public:
    MaterialCodec() noexcept = default;
    ~MaterialCodec() noexcept = default;

    [[nodiscard]] SurfaceAttributeRecord DecodePixel(uint32_t x, uint32_t y, const VisibilityProjection& Visibility, const GeometryStructure& Geometry) const noexcept;

    // Single unified conversion operator
    template<typename TargetType>
    [[nodiscard]] TargetType Convert(const SurfaceAttributeRecord& Record) const noexcept;
};

template<>
inline Vector4 MaterialCodec::Convert<Vector4>(const SurfaceAttributeRecord& Record) const noexcept
{
    return Record.AlbedoColor;
}

template<>
inline bool MaterialCodec::Convert<bool>(const SurfaceAttributeRecord& Record) const noexcept
{
    return Record.ValidCondition;
}

} // namespace Frontier
