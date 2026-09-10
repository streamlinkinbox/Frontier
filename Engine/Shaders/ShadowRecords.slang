//============================================================================================================================================
// 📦 Engine/Shaders/ShadowRecords.slang — Shadow pass constants shared by the raster and the sampler (include only)
//============================================================================================================================================
// Mirrors ShadowFrameConfiguration in Engine/DeviceExchange/ShadowExchange.h byte for byte (std140). Change both
//    sides or neither.
//
// One block serves every light tap: the raster reads ShadowLightClip[ShadowTapIndex] for the tap it is drawing, the
//    sampler reads all of them plus the matching Geometry entries. ShadowTapIndex is the only field that changes
//    between the taps of a single frame, so it is a push constant rather than part of the uniform block.

#ifndef FRONTIER_SHADOW_RECORDS
#define FRONTIER_SHADOW_RECORDS

// Filters — mirror ShadowTechniqueCategory in DisplayPresentation/FidelityClassifier.h and ShadowFilterKind in
//    GeometricRaster/VisibilityRaster.h. The quality tier picks one; nothing here decides it.
const uint kShadowFilterHard = 0u;   // a single depth comparison: aliased, binary, cheapest
const uint kShadowFilterPcf  = 1u;   // fixed-radius box: constant-width penumbra, stable but not physical
const uint kShadowFilterPcss = 2u;   // blocker search + similar triangles: contact-hardening, physically scaled

const uint kShadowMaximumTaps = 4u;      // matches VisibilityRaster::kLightTaps — the stratified taps over a luminaire
const uint kShadowBlockerTaps = 5u;      // blocker-search kernel side, in texels (PCSS step one)

layout(std140, set = 1, binding = 0) uniform ShadowConstants
{
    mat4 ShadowLightClip[kShadowMaximumTaps];   // world → light clip, one per tap (ordinary depth, near→0 far→1)
    vec4 ShadowTapOrigin[kShadowMaximumTaps];   // xyz = the tap's world position, w = the luminaire's size [m] (PCSS LightSize)
    vec4 ShadowTapRadiance[kShadowMaximumTaps]; // xyz = the tap's emitted radiance [nit], w = the area it integrates [m²]
    vec4 ShadowTapNormal[kShadowMaximumTaps];   // xyz = the emitter's unit normal (the LdotL term), w = tan(half-angle) of this tap's frustum
    vec4 ShadowGeometry;                        // x = map side [px], y = light near [m], z = light far [m], w = depth bias [m]
    uvec4 ShadowControl;                        // x = filter kind, y = filter taps per side, z = live tap count, w = flags
};

// The tap being drawn is the only thing that changes between the draws of one frame, so it is a push constant
//    declared by the raster stages themselves — this header stays includable by the resolve pass, which has a push
//    block of its own and must not inherit a second one.

// Linear eye-space distance from a stored ordinary-depth value, for a perspective light frustum. The filters compare
//    and average in metres so a bias in metres means the same thing everywhere in the map.
float ShadowLinearDepth(float Stored, float Near, float Far)
{
    // Stored = (Far / (Far − Near)) · (1 − Near / z)  ⇒  z = Near·Far / (Far − Stored·(Far − Near)).
    return (Near * Far) / max(Far - Stored * (Far - Near), 1e-6);
}

#endif // FRONTIER_SHADOW_RECORDS
