//============================================================================================================================================
// 📦 Frontier/GeometricRaster/MaterialCodec.cpp — Surface Attribute Decoding Implementation
//============================================================================================================================================

#include "MaterialCodec.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                DECODING OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

SurfaceAttributeRecord MaterialCodec::DecodePixel(uint32_t x, uint32_t y, const VisibilityProjection& Visibility, const GeometryStructure& Geometry) const noexcept
{
    VisibilityIdentifier Ident = Visibility.QueryPixel(x, y);
    if (!Ident.IsValid())
    {
        return SurfaceAttributeRecord{
            Vector3{ 0.0f, 0.0f, 0.0f },
            Vector3{ 0.0f, 1.0f, 0.0f },
            Vector4{ 0.0f, 0.0f, 0.0f, 0.0f },
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            false
        };
    }

    uint32_t PrimitiveToken = Ident.QueryPrimitiveToken();
    const auto& Vertices = Geometry.QueryVertices();
    const auto& Indices = Geometry.QueryIndices();

    if (PrimitiveToken * 3 + 2 >= Indices.size())
    {
        return SurfaceAttributeRecord{
            Vector3{ 0.0f, 0.0f, 0.0f },
            Vector3{ 0.0f, 1.0f, 0.0f },
            Vector4{ 0.5f, 0.5f, 0.5f, 1.0f },
            0.5f,
            0.0f,
            0.0f,
            0.0f,
            true
        };
    }

    uint32_t i0 = Indices[PrimitiveToken * 3 + 0];
    uint32_t i1 = Indices[PrimitiveToken * 3 + 1];
    uint32_t i2 = Indices[PrimitiveToken * 3 + 2];

    const auto& v0 = Vertices[i0];
    const auto& v1 = Vertices[i1];
    const auto& v2 = Vertices[i2];

    Vector3 MeanPos = (v0.SpatialLocation + v1.SpatialLocation + v2.SpatialLocation) / 3.0f;
    Vector3 MeanNormal = (v0.NormalDirection + v1.NormalDirection + v2.NormalDirection).Normalized();
    float MeanU = (v0.TextureCoordinateU + v1.TextureCoordinateU + v2.TextureCoordinateU) / 3.0f;
    float MeanV = (v0.TextureCoordinateV + v1.TextureCoordinateV + v2.TextureCoordinateV) / 3.0f;

    return SurfaceAttributeRecord{
        MeanPos,
        MeanNormal,
        Vector4{ 0.8f, 0.8f, 0.8f, 1.0f },
        0.3f,
        0.0f,
        MeanU,
        MeanV,
        true
    };
}

} // namespace Frontier
