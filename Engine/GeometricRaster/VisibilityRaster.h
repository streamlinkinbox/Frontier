//============================================================================================================================================
//                                                      VISIBILITYRASTER.H
//============================================================================================================================================
// 🧩 The no-ray render path: a CPU visibility buffer under a microrasterizer, the GI-off look. Pass one walks
//    every triangle over the pixels it covers and keeps, per pixel, the nearest triangle id with its barycentrics
//    (the visibility buffer). Pass two shades each covered pixel direct-only lookdev PBR — Lambert plus a GGX
//    specular under the luminaires, visibility from rasterized shadow maps, a flat ambient fill, no bounces.
//    Emissive triangles emit, misses take the sky.
//
//    Shadow filtering is a quality-tier decision, carried in by ShadowCriteria (mirroring FidelityCriteria's
//    shadow fields so this header need not depend on the presentation layer):
//       HardShadowMap  — one comparison per tap. Aliased and binary; what Minimal can afford.
//       WidePercentageCloserFilter — a fixed-radius PCF box. Constant-width penumbra: cheap and stable, but the
//          softness does not track occluder distance, so it is a look, not a physical result.
//       PercentageCloserSoftShadow — a blocker search over the map estimates the mean occluder depth, then the
//          similar-triangles relation w = (Receiver − Blocker) / Blocker × LightSize sizes the PCF kernel. Contact
//          stays sharp and the shadow widens with distance, which is what real area lights do. LightSize comes
//          from the luminaire's own area (PlaceTaps already knows it), so the penumbra is physically scaled.
//    This mirrors the GPU pass in Shaders/ShadowRaster.*.slang and Shaders/ShadowResolve.slang, which is what the
//    application actually runs; the CPU path here is the headless proof of the same three techniques.
//
//    Nothing here names a ray query: not the traversal, not an occlusion segment, nothing the ReSTIR path shares.
//    The preview's quarantine gate greps this file for those names and fails the run if one appears. Shadow maps
//    (one per fixed light tap, rasterized from the tap point) are what stand between a shaded pixel and its light.
//
//    Near plane: a triangle with any vertex closer than 5 cm is skipped, not clipped. The Cornell camera stands
//    outside the room, so no triangle qualifies; general clipping arrives with the first scene that needs it.

#pragma once

#include "DisplayPresentation/AtmosphereModel.h"
#include "DisplayPresentation/ColourTransfer.h"
#include "DisplayPresentation/MoonConstantRecord.h"
#include "DisplayPresentation/VolumetricMedia.h"
#include "StarCatalogueIndex.h"
#include <cstdint>
#include <vector>

namespace Frontier {

class SceneStructure;

// Which filter runs over the shadow map, and how big that map is. Mirrors ShadowTechniqueCategory /
//    FidelityCriteria in DisplayPresentation/FidelityClassifier.h — duplicated as a plain record rather than
//    included so GeometricRaster keeps no dependency on the presentation layer. ControlCentreHost resolves the
//    tier and its resolution override into these three numbers; the raster just obeys them.
enum class ShadowFilterKind : uint32_t
{
    HardShadowMap              = 0,
    WidePercentageCloserFilter = 1,
    PercentageCloserSoftShadow = 2
};

struct ShadowCriteria
{
    ShadowFilterKind Filter   = ShadowFilterKind::WidePercentageCloserFilter;
    uint32_t         MapSide  = 512u;   // [px] shadow map side in texels
    uint32_t         TapCount = 5u;     // [cnt] filter kernel side in taps (1 = a single comparison)
};

class VisibilityRaster final
{
public:
    VisibilityRaster() noexcept = default;
    ~VisibilityRaster() noexcept = default;

    VisibilityRaster(const VisibilityRaster&)            = delete;
    VisibilityRaster& operator=(const VisibilityRaster&) = delete;

    static constexpr uint32_t kMiss         = 0xFFFFFFFFu;   // [idx] visibility id where no triangle won
    static constexpr uint32_t kLightTaps    = 4u;            // [cnt] fixed stratified taps over the luminaires
    static constexpr uint32_t kShadowSize   = 512u;          // [px] default shadow map side; Standard's own figure
    static constexpr float    kShadowHalf   = 65.0f;         // [deg] shadow frustum half-angle about light→centre
    static constexpr float    kNear         = 0.05f;         // [m] nearer vertices skip the triangle, unclipped
    static constexpr float    kShadowBias   = 0.015f;        // [m] depth bias plus a slope term from NdotL
    static constexpr float    kAmbient      = 0.045f;        // [-] flat fill under the direct light
    static constexpr uint32_t kBlockerTaps  = 5u;            // [cnt] PCSS blocker-search kernel side, in texels

    // How linear radiance becomes a display pixel. Defaults to ACES with low-light desaturation, matching the
    //    ReSTIR kernel — see ColourTransfer.h for why the two must agree.
    void AssignColourTransfer(const ColourTransfer& Transfer) noexcept { Colour_ = Transfer; }
    [[nodiscard]] const ColourTransfer& QueryColourTransfer() const noexcept { return Colour_; }

    // The celestial background for subsequent renders. Off by default, so a caller that knows nothing about the
    //    sky keeps the flat fallback colour the raster has always used and no existing proof shifts.
    struct CelestialSettings
    {
        bool             Enabled          = false;
        AtmosphereMedium Medium{};
        AtmosphereLight  Light{};
        TwilightSettings Twilight{};                // the pre-dawn glow and the white line (see AtmosphereModel.h)
        // The star field. Null means no stars, which is the default so nothing existing changes. The index is
        //    borrowed, not owned: it is loaded once and shared by every raster that draws the same sky.
        const StarCatalogueIndex* Stars = nullptr;
        // The moons. Null means no moons, which is the default so nothing existing changes. The list is
        //    borrowed, not owned: the project resolves its roster plus the solved frame into one MoonDrawList
        //    and lends it here, the same arrangement as the star catalogue above.
        const MoonDrawList* Moons = nullptr;
        // The clouds, by value: the settings are small, and a disabled struct is the march's own early-out, so
        //    lending values keeps the lifetimes trivial. All three default to disabled, so nothing existing
        //    changes until the project lends live ones. The wind advects them, the budget paces the march, and
        //    the clock is time-of-day seconds — the rain's precedent (Tick passes LocalHours * 3600 to the
        //    precipitation pool), so scrubbing the day cycle drifts the sky and a still frame stays put.
        CloudLayerSettings  CloudLayer{};
        LocalVolumeSettings LocalCloud{};
        LocalVolumeSettings LocalFog{};
        WindSettings        Wind{};
        VolumetricBudget    CloudBudget{};
        float               CloudTime = 0.0f;
        // The planet's own surface, seen when a ray passes below the horizon. Panel: Sky > Ground > Albedo.
        float            GroundAlbedo[3]   = { 0.19f, 0.17f, 0.14f };
        float            StarBrightness    = 1.0f;   // [x] panel: Stars > Field > Brightness
        float            StarSize          = 1.0f;   // [x] panel: Stars > Field > Point Size
        float            LocalSiderealTime = 0.0f;   // [deg] from CelestialFrame
        float            Latitude          = 0.0f;   // [deg] north positive
        float            CameraHeight     = 2.0f;   // [m] above the surface
        uint32_t         SampleCount      = 16u;    // FidelityCriteria::AtmosphereSampleCount
        uint32_t         LightSampleCount = 6u;     // FidelityCriteria::AtmosphereLightSampleCount
    };
    void AssignCelestial(const CelestialSettings& Settings) noexcept { Celestial_ = Settings; }
    [[nodiscard]] const CelestialSettings& QueryCelestial() const noexcept { return Celestial_; }

    // Selects the shadow technique for subsequent renders. Defaults to the Standard tier's PCSS if never called.
    void AssignShadowCriteria(const ShadowCriteria& Criteria) noexcept;
    [[nodiscard]] const ShadowCriteria& QueryShadowCriteria() const noexcept { return Shadows_; }

    // Renders Level through the Eye/Forward/Right/Up camera into Width×Height RGBA32 top-down rows. Returns
    //    false on empty input; MeanLum carries the sheets' mean luminance for the run log.
    [[nodiscard]] bool Render(const SceneStructure& Level,
                              const float Eye[3], const float Forward[3],
                              const float Right[3], const float Up[3], float FovYRadians,
                              uint32_t Width, uint32_t Height,
                              unsigned char* Rgba, double& MeanLum) noexcept;

    // The same render under an orthographic primary: parallel rays through the Eye/Right/Up plane, framing
    //    HalfHeightWorld metres above and below the eye. Shading, taps and shadow maps are shared untouched.
    [[nodiscard]] bool RenderOrthographic(const SceneStructure& Level,
                              const float Eye[3], const float Forward[3],
                              const float Right[3], const float Up[3], float HalfHeightWorld,
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
        float Size;               // [m] the luminaire's own extent, √Area — PCSS's LightSize term
    };

    void CollectLumi(const SceneStructure& Level) noexcept;
    void PlaceTaps() noexcept;
    void RasterizePrimary(const SceneStructure& Level, const float Eye[3], const float Forward[3],
                          const float Right[3], const float Up[3], float FovYRadians,
                          uint32_t Width, uint32_t Height) noexcept;
    void RasterizePrimaryOrthographic(const SceneStructure& Level, const float Eye[3], const float Forward[3],
                          const float Right[3], const float Up[3], float HalfHeightWorld,
                          uint32_t Width, uint32_t Height) noexcept;
    void RasterizeShadow(const SceneStructure& Level, const float Tap[3], const float Centre[3]) noexcept;
    [[nodiscard]] float Shadow(const float P[3], const float N[3], float NdotL,
                             const LightTap& Tap) const noexcept;
    // Fetches one map texel's linear depth; returns 1e30f (nothing occluding) outside the map.
    [[nodiscard]] float ShadowTexel(int32_t X, int32_t Y) const noexcept;
    // Averages the depths of texels nearer than the receiver over a kBlockerTaps² window: PCSS step one. Returns
    //    false when the window found no blocker at all, which means the receiver is fully lit.
    [[nodiscard]] bool BlockerDepth(float TexU, float TexV, float Zc, float Bias, float Radius,
                                    float& OutDepth) const noexcept;
    // The PCF box itself, RadiusTexels wide, centred on (TexU, TexV). Shared by all three techniques: hard is
    //    this with a zero radius and a single tap, wide PCF a fixed radius, PCSS a penumbra-derived one.
    [[nodiscard]] float FilterLit(float TexU, float TexV, float Zc, float Bias,
                                  float RadiusTexels, uint32_t Taps) const noexcept;
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

    CelestialSettings     Celestial_{};   // the sky behind the geometry (disabled = flat fallback colour)
    ColourTransfer        Colour_{};      // linear radiance -> display pixel, shared with the GPU paths
    std::vector<float>    Depth_;    // [m] primary linear depth, Width×Height
    std::vector<uint32_t> TriId_;    // [idx] the visibility buffer: winning triangle or kMiss
    std::vector<float>    Bary_;     // [-] winning (w0, w1) per pixel; w2 = 1 − w0 − w1
    std::vector<float>    TriN_;     // [-] unit face normal per flat triangle, pass one's side table
    std::vector<float>    Shadow_;   // [m] one shadow map's linear depth, Shadows_.MapSide², per tap in turn
    ShadowCriteria        Shadows_{ ShadowFilterKind::PercentageCloserSoftShadow, kShadowSize, 5u };
    std::vector<float>    Acc_;      // [nit] linear accumulator the taps add into, Width×Height×3
    float                 ShadowTan_ = 1.0f;
    float                 ShadowR_[3] = { 1.0f, 0.0f, 0.0f };
    float                 ShadowU_[3] = { 0.0f, 1.0f, 0.0f };
    float                 ShadowD_[3] = { 0.0f, 0.0f, -1.0f };
};

} // namespace Frontier
