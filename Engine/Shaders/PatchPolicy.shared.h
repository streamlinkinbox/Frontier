// Compiled by BOTH C++ CPU reference and GLSL. No platform types or shader intrinsics.
#ifndef FRONTIER_PATCH_POLICY
#define FRONTIER_PATCH_POLICY
#ifdef __cplusplus
namespace Frontier::PatchGeometry {
#define PATCH_INLINE inline
#else
#define PATCH_INLINE
#endif
PATCH_INLINE bool PatchOpaque(int slabs, int flags, float transmission, float subsurface, float opacity,
                 bool uncertainTextures, bool emissive, bool thinSlab)
{
    return slabs == 1 && (flags & 54) == 0 && transmission == 0.0f && subsurface == 0.0f
        && opacity >= 1.0f && !uncertainTextures && !emissive && !thinSlab;
}
PATCH_INLINE bool PatchChooseCoarse(bool preview, bool opaque, bool alternate, bool backFacing,
                       float error, float scale, float depth, float radius,
                       float lateral, float focalPixels, float nearPlane)
{
    if (!preview || !opaque || !alternate) return false;
    // Negated comparisons fail closed for NaN, degenerate and near-plane cases.
    float worldError = error * scale;
    float nearest = depth - radius - worldError;
    if (!(error >= 0.0f) || !(scale > 0.0f) || !(nearest > nearPlane) || !(focalPixels > 0.0f)) return false;
    if (backFacing) return true;
    float projected = focalPixels * worldError * (1.0f + (lateral + radius) / nearest) / nearest;
    return projected <= 1.0f;
}
#undef PATCH_INLINE
#ifdef __cplusplus
}
#endif
#endif
