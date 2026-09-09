//============================================================================================================================================
//                                                      VISIBILITYRASTER.H
//============================================================================================================================================
// 🧩 The no-ray render path: a CPU visibility buffer under a microrasterizer, the GI-off look. Pass one walks
//    every triangle over the pixels it covers and keeps, per pixel, the nearest triangle id with its barycentrics
//    (the visibility buffer). Pass two shades each covered pixel direct-only lookdev PBR — Lambert plus a GGX
//    specular under the luminaires, visibility from rasterized shadow maps, a flat ambient fill, no bounces.
//    Emissive triangles emit, misses take the sky.
//
//    Nothing here names a ray query: not the traversal, not an occlusion segment, nothing the ReSTIR path shares.
//    The preview's quarantine gate greps this file for those names and fails the run if one appears. Shadow maps
//    (one per fixed light tap, rasterized from the tap point) are what stand between a shaded pixel and its light.
//
//    Near plane: a triangle with any vertex closer than 5 cm is skipped, not clipped. The Cornell camera stands
//    outside the room, so no triangle qualifies; general clipping arrives with the first scene that needs it.

#pragma once

#include <cstdint>
#include <vector>

namespace Frontier {

class SceneStructure;

class VisibilityRaster final
{
public:
    VisibilityRaster() noexcept = default;
    ~VisibilityRaster() noexcept = default;

    VisibilityRaster(const VisibilityRaster&)            = delete;
    VisibilityRaster& operator=(const VisibilityRaster&) = delete;

    static constexpr uint32_t kMiss         = 0xFFFFFFFFu;   // [idx] visibility id where no triangle won
    static constexpr uint32_t kLightTaps    = 4u;            // [cnt] fixed stratified taps over the luminaires
    static constexpr uint32_t kShadowSize   = 512u;          // [px] shadow map side; linear depth entries
    static constexpr float    kShadowHalf   = 65.0f;         // [deg] shadow frustum half-angle about light→centre
    static constexpr float    kNear         = 0.05f;         // [m] nearer vertices skip the triangle, unclipped
    static constexpr float    kShadowBias   = 0.015f;        // [m] depth bias plus a slope term from NdotL
    static constexpr float    kAmbient      = 0.045f;        // [-] flat fill under the direct light

    // Renders Level through the Eye/Forward/Right/Up camera into Width×Height RGBA32 top-down rows. Returns
    //    false on empty input; MeanLum carries the sheets' mean luminance for the run log.
    [[nodiscard]] bool Render(const SceneStructure& Level,
                              const float Eye[3], const float Forward[3],
                              const float Right[3], const float Up[3], float FovYRadians,
                              uint32_t Width, uint32_t Height,
                              unsigned char* Rgba, double& MeanLum) noexcept;

private:
    struct LumiTri
    {
        float A[3], B[3], C[3];   // [m] world-space corners
        float N[3];               // [-] unit face normal
        float Area;               // [m2]
        float Le[3];              // [nit]
    };
    struct LightTap
    {
        float P[3];               // [m] fixed stratified point on a luminaire
        float N[3];               // [-] its triangle's unit normal
        float Le[3];              // [nit]
        float Weight;             // [m2] luminaire area this tap integrates
    };

    void CollectLumi(const SceneStructure& Level) noexcept;
    void PlaceTaps() noexcept;
    void RasterizePrimary(const SceneStructure& Level, const float Eye[3], const float Forward[3],
                          const float Right[3], const float Up[3], float FovYRadians,
                          uint32_t Width, uint32_t Height) noexcept;
    void RasterizeShadow(const SceneStructure& Level, const float Tap[3], const float Centre[3]) noexcept;
    [[nodiscard]] float Shadow(const float P[3], const float N[3], float NdotL,
                             const float Tap[3]) const noexcept;
    void Shade(const SceneStructure& Level, const float Eye[3], const float Forward[3],
               const float Right[3], const float Up[3], float FovYRadians,
               uint32_t Width, uint32_t Height, unsigned char* Rgba, double& MeanLum) noexcept;

    static float EdgeWeight(float Ax, float Ay, float Bx, float By, float Px, float Py) noexcept
    {
        return (Bx - Ax) * (Py - Ay) - (By - Ay) * (Px - Ax);
    }

    std::vector<LumiTri>  Lumi_;     // emissive triangles, rebuilt every render
    LightTap              Taps_[kLightTaps] = {};
    uint32_t              TapCount_  = 0u;

    std::vector<float>    Depth_;    // [m] primary linear depth, Width×Height
    std::vector<uint32_t> TriId_;    // [idx] the visibility buffer: winning triangle or kMiss
    std::vector<float>    Bary_;     // [-] winning (w0, w1) per pixel; w2 = 1 − w0 − w1
    std::vector<float>    TriN_;     // [-] unit face normal per flat triangle, pass one's side table
    std::vector<float>    Shadow_;   // [m] one shadow map's linear depth, kShadowSize², per tap in turn
    std::vector<float>    Acc_;      // [nit] linear accumulator the taps add into, Width×Height×3
    float                 ShadowTan_ = 1.0f;
    float                 ShadowR_[3] = { 1.0f, 0.0f, 0.0f };
    float                 ShadowU_[3] = { 0.0f, 1.0f, 0.0f };
    float                 ShadowD_[3] = { 0.0f, 0.0f, -1.0f };
};

} // namespace Frontier
