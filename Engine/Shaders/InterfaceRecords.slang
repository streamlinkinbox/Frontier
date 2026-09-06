//============================================================================================================================================
// 📦 Engine/Shaders/InterfaceRecords.slang — Spatial-interface instance layout shared by the raster stages (include only)
//============================================================================================================================================
// Mirrors Engine/SpatialInterface/InterfaceLayoutCodec.h byte for byte (std430) and the InterfaceConstants block
//    written by DeviceExchange/InterfaceExchange.cpp (std140). Change both sides or neither — the C++ side carries a
//    static_assert on the 96-byte size, this side carries the matching field order.
//
// Coordinates: world is right-handed Z-up (CLAUDE.md §7). A figure lives on its own local plane (+X right, +Y up,
//    origin at the centre, metres) and is placed by three transform rows; the fourth row is always (0,0,0,1) and is
//    reconstructed here for free. ViewClip already contains the single Vulkan Y flip and the reverse-Z infinite
//    projection — see GeometricRaster/ClipProjection.h.

#ifndef FRONTIER_INTERFACE_RECORDS
#define FRONTIER_INTERFACE_RECORDS

//------------------------------------------------------------------------------------------------------------------------
//                                                   INTERFACE CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

layout(std140, set = 0, binding = 0) uniform InterfaceConstants
{
    mat4  ViewClip;             // world → clip, current frame
    vec4  CameraOrigin;         // xyz = eye, w = unused
    vec4  Extent;               // xy = render size [px], zw = unused
    vec4  AmbientIrradiance;    // ⑦ xyz = scene-average irradiance on the panel [nit], w = unused
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     INSTANCE SLOT
//------------------------------------------------------------------------------------------------------------------------
// 112 B — InterfaceLayoutCodec.h. Field order is load-bearing.

struct InterfaceInstanceFigure
{
    vec4  RowX;                 // [-]/[m]  world row 0, translation in w
    vec4  RowY;                 // [-]/[m]  world row 1
    vec4  RowZ;                 // [-]/[m]  world row 2

    vec2  HalfExtent;           // [m]      local half size
    float CornerRadius;         // [m]      resolved (token radius already substituted)
    float Opacity;              // [-]      figure opacity × group opacity  ⑤

    vec4  ClipExtent;           // [m]      xy = minimum, zw = maximum, in the figure's own plane  ⑥

    uint  CategoryPalette;      // [-]      category in bits 31..24, palette ordinal in bits 23..0
    float ScalarAlpha;          // [-]      fill fraction / angle fraction / digit / luminance
    float ScalarBeta;           // [-]      thickness / mark count / warn threshold
    uint  Tint;                 // [-]      RGBA8, resolved

    uint  BaseColour;           // [-]      ⑦ RGBA8 albedo of the unlit surface
    float EmissiveWeight;       // [-]      ⑦ 0 = pure albedo (receives room light), 1 = pure emitter
    float ReserveAlpha;         // [-]      reserved, written 0
    float ReserveBeta;          // [-]      reserved, written 0
};

layout(std430, set = 0, binding = 1) readonly buffer InterfaceExtent { InterfaceInstanceFigure Figures[]; };

//------------------------------------------------------------------------------------------------------------------------
//                                                       HELPERS
//------------------------------------------------------------------------------------------------------------------------

// gl_VertexIndex 0..3 → the corners of a triangle strip on the figure's local plane.
vec2 InterfaceCorner(uint VertexIndex, vec2 HalfExtent)
{
    vec2 Sign = vec2((VertexIndex & 1u) != 0u ? 1.0 : -1.0,
                     (VertexIndex & 2u) != 0u ? 1.0 : -1.0);
    return Sign * HalfExtent;
}

vec3 InterfaceLocalToWorld(InterfaceInstanceFigure Figure, vec3 Local)
{
    return vec3(dot(Figure.RowX.xyz, Local) + Figure.RowX.w,
                dot(Figure.RowY.xyz, Local) + Figure.RowY.w,
                dot(Figure.RowZ.xyz, Local) + Figure.RowZ.w);
}

vec4 InterfaceUnpackTint(uint Packed)
{
    return vec4(float( Packed        & 0xFFu),
                float((Packed >>  8) & 0xFFu),
                float((Packed >> 16) & 0xFFu),
                float((Packed >> 24) & 0xFFu)) * (1.0 / 255.0);
}

#endif // FRONTIER_INTERFACE_RECORDS
