//============================================================================================================================================
// 📦 ParametricSketcher/Presentation/Shaders/ShadingRecords.slang — Uniform and per-draw records shared by every SolidArc raster stage
//============================================================================================================================================
// Layout is std140-safe: every member is a float4 / float4x4 so the C++ mirror and the SPIR-V agree byte for byte.
//    The software raster and the Vulkan raster both fill exactly this struct.

#ifndef SOLIDARC_SHADING_RECORDS
#define SOLIDARC_SHADING_RECORDS

struct ViewRecord
{
    float4x4 ViewClip;          // world → clip (Vulkan clip space, depth 0..1, Y down)
    float4x4 ClipView;          // inverse, for the analytic grid ray
    float4   EyePosition;       // xyz = camera position [m], w = 1 for perspective / 0 for orthographic
    float4   Viewport;          // xy = size [px], zw = 1/size
    float4   GridStyle;         // x = minor cell [m], y = major every N cells, z = fade radius [m], w = line half-width [px]
    float4   Illumination;      // xyz = key light direction (unit, world), w = ambient
};

struct DrawRecord
{
    float4x4 ModelWorld;        // object → world
    float4   Tint;              // rgb = base colour, a = opacity
    float4   Selection;         // x = 0 none / 1 hovered / 2 selected, y = pick id (as float bits), z = line width [px], w = point size [px]
};

#endif
