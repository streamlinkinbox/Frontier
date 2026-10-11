//============================================================================================================================================
// 📦 Project-Zero/Source/CelestialIntegrator.h — Reference Celestial Fragment Program, Transcribed to CPU Radiance
//============================================================================================================================================
//
//    The declaration of the sky. `CelestialIntegrator` answers one question — *what radiance arrives from this
//    direction?* — and answers it with the reference program's own arithmetic: `atmosphere()`, `dawnGlow()`,
//    `starField()`, `moons()`, `cloudMarch()`, `marchLocal()`, `cloudLayer()`, `applyMedia()`, `rainbow()`,
//    `lensFlare()` and the tonemap chain, in that order.
//
//    Split from the renderer on purpose: Project Zero's ReSTIR path needs the sky as a *light* (an environment
//    radiance and a sun irradiance it can sample) long before it needs it as a backdrop, and a proof needs to
//    call one function without standing up a frame.
//
//============================================================================================================================================

#pragma once

#include "CelestialSpecification.h"
#include <cstdint>
#include <vector>

namespace Frontier::ProjectZero {

//------------------------------------------------------------------------------------------------------------------------
//                                                 MOON ALBEDO SURFACE
//------------------------------------------------------------------------------------------------------------------------
//    A decoded equirectangular moon texture. The reference samples `texture2D(uMoonTexN, uv)`; this is that
//    surface, held as linear-ish sRGB bytes exactly as the JPEG delivers them.

struct MoonAlbedoSurface
{
    uint32_t             Width  = 0u;
    uint32_t             Height = 0u;
    std::vector<uint8_t> Texels;                                // [rgb8] row-major, 3 bytes per texel

    [[nodiscard]] bool Populated() const noexcept { return Width > 0u && Height > 0u && !Texels.empty(); }
    [[nodiscard]] Vector3 Sample(float u, float v) const noexcept;

    //    Load a binary PPM. The atlas ships as JPEG; `Tools/` decodes it to P6 at build time, which keeps a
    //    JPEG decoder out of the engine for what is ultimately one texture.
    [[nodiscard]] static MoonAlbedoSurface LoadPortablePixmap(const char* Path) noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 THE SOLVED FRAME
//------------------------------------------------------------------------------------------------------------------------
//    What the reference computes once per frame on the CPU and uploads as uniforms: the solar direction, the
//    sun's colour from its temperature, the sky-ambient probe, and the wind's running integral.

struct CelestialFrame
{
    Vector3 SunDirection      { 0.0f, 1.0f, 0.0f };             // [-] sky frame, unit
    Vector3 SunColour         { 1.0f, 1.0f, 1.0f };             // [-] from `kelvinRGB(sun_temp)`
    float   SunElevationDeg   = 0.0f;                           // [deg]
    float   SunAzimuthRad     = 0.0f;                           // [rad]
    Vector3 SkyAmbient        { 0.0f, 0.0f, 0.0f };             // [-] the 3-sample hemisphere probe
    Vector3 MoonDirections[4] { };                              // [-] sky frame, unit
    float   WindIntegral[2]   { 0.0f, 0.0f };                   // [m] advection integral, X/Z
    float   WindGustPhase     = 0.0f;                           // [rad]
    float   TimeSeconds       = 0.0f;                           // [s] the reference's `uTime`
    float   StarCeiling       = 0.0f;                           // [-] brightest modelled star
};

//------------------------------------------------------------------------------------------------------------------------
//                                                THE OBSERVER'S BASIS
//------------------------------------------------------------------------------------------------------------------------

struct ObserverFrame
{
    Vector3 Position   { 0.0f, 2.0f, 0.0f };                    // [m] sky frame
    Vector3 Forward    { 0.0f, 0.0f, -1.0f };
    Vector3 Right      { 1.0f, 0.0f, 0.0f };
    Vector3 Upward     { 0.0f, 1.0f, 0.0f };
    float   TangentHalf = 1.0f;                                 // [-] tan(fov/2)
    float   Height      = 2.0f;                                 // [m] `uCamHeight`
};

//------------------------------------------------------------------------------------------------------------------------
//                                               CELESTIAL INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

class CelestialIntegrator
{
public:
    explicit CelestialIntegrator(const CelestialCriteria& Parameters) noexcept;

    // Solve the per-frame quantities the reference computes on the CPU: sun direction, colour, ambient probe.
    void                        SolveFrame(float TimeSeconds) noexcept;

    // Advance the wind integral and gust phase. The reference's `windStep`.
    void                        AdvanceWind(float DeltaSeconds) noexcept;

    void                        AssignMoonSurface(uint32_t Slot, MoonAlbedoSurface Surface) noexcept;

    //    The sky seen along `Direction` from `Observer`, in linear HDR before exposure — everything in the
    //    reference's `else` branch (no ground, no plane): atmosphere, twilight, stars, moons, clouds, local
    //    volumetrics, the solar disc and the shared media path.
    [[nodiscard]] Vector3       SampleSkyRadiance(const Vector3& Direction, const ObserverFrame& Observer,
                                                  float PixelAngle, uint32_t PixelX, uint32_t PixelY) const noexcept;

    //    Everything that is composited over an already-shaded surface at `Distance`: the two analytic fogs and
    //    the local volumetric march, exactly as the reference applies them to `planeCol`.
    [[nodiscard]] Vector3       ApplyAerialPerspective(const Vector3& SurfaceRadiance, const Vector3& Direction,
                                                       float Distance, const ObserverFrame& Observer,
                                                       uint32_t PixelX, uint32_t PixelY) const noexcept;

    //    The optics that live in screen space and therefore sit outside the per-direction integral: the rainbow
    //    (needs the sky mask), the lens flare (needs the sun's screen position) and the tonemap chain.
    [[nodiscard]] Vector3       AddRainbow(const Vector3& Radiance, const Vector3& Direction, float SkyMask) const noexcept;
    [[nodiscard]] Vector3       AddLensFlare(const Vector3& Radiance, float ScreenU, float ScreenV,
                                             const ObserverFrame& Observer) const noexcept;
    [[nodiscard]] Vector3       ResolveDisplay(const Vector3& Radiance, float ScreenU, float ScreenV,
                                               uint32_t PixelX, uint32_t PixelY) const noexcept;

    //    What ReSTIR needs to treat the sky as a light rather than a backdrop.
    [[nodiscard]] Vector3       SampleSunIrradiance() const noexcept;
    [[nodiscard]] Vector3       SampleEnvironmentRadiance(const Vector3& WorldDirection, const ObserverFrame& Observer) const noexcept;

    //------------------------------------------------------------------------------------------------------------
    //                                   THE GROUND — reference `main()` plane / terrain branches
    //------------------------------------------------------------------------------------------------------------
    //    The reference resolves the world under the sky before it resolves the sky itself: the sculpted height
    //    field first, then the checker plane, whichever is nearer. Both are shaded against the same sun, the
    //    same sky ambient and the same local lights, then composited through the shared media path.

    struct GroundSample
    {
        bool    Hit         = false;
        float   Distance    = 0.0f;                             // [m]
        Vector3 Radiance    { 0.0f, 0.0f, 0.0f };               // linear HDR, before `applyMedia`
        Vector3 Normal      { 0.0f, 1.0f, 0.0f };               // sky frame
    };

    //    `terrH` — the sculpted height at a horizontal station, and the sphere-traced hit against it.
    [[nodiscard]] float         TerrainHeightAt(float x, float z) const noexcept;
    [[nodiscard]] bool          TerrainIntersect(const Vector3& Origin, const Vector3& Direction,
                                                 float& OutDistance, Vector3& OutNormal) const noexcept;

    //    The whole ground branch: terrain, then checker plane, shaded and returned unfogged so the caller can
    //    composite it exactly where the reference does.
    [[nodiscard]] GroundSample  SampleGround(const Vector3& Direction, const ObserverFrame& Observer,
                                             float PixelFootprint) const noexcept;

    //    `applyMedia` + `marchLocal` over a surface at `Distance`, which is what the reference does to
    //    `planeCol` before anything else is composited over it.
    [[nodiscard]] Vector3       CompositeGround(const Vector3& Radiance, const Vector3& Direction, float Distance,
                                                const ObserverFrame& Observer,
                                                uint32_t PixelX, uint32_t PixelY) const noexcept;

    //    The emissive glyphs the reference draws where the point and spot lights sit, so a light that is on is
    //    visible in the frame and not merely inferable from what it lights.
    [[nodiscard]] Vector3       AddLightGlyphs(const Vector3& Radiance, const Vector3& Direction,
                                               const ObserverFrame& Observer,
                                               bool GroundHit, float GroundDistance) const noexcept;

    //    The two local lights as they illuminate an opaque surface — `uPLOn`/`uSLOn` in the plane branch.
    [[nodiscard]] Vector3       LocalLightsOnSurface(const Vector3& Position, const Vector3& Albedo) const noexcept;

    [[nodiscard]] const CelestialFrame&    QueryFrame() const noexcept      { return Solved; }
    [[nodiscard]] const CelestialCriteria& QueryCriteria() const noexcept   { return Criteria; }
    [[nodiscard]] CelestialCriteria&       MutableCriteria() noexcept       { return Criteria; }

    //    The reference's own helpers, exposed because the proofs assert on them directly.
    [[nodiscard]] static float  AirMassOf(float ElevationDegrees) noexcept;
    [[nodiscard]] static float  BowAngle(float WavelengthNm, float Order) noexcept;
    [[nodiscard]] static Vector3 KelvinColour(float Temperature) noexcept;
    [[nodiscard]] Vector3       SolveSunDirection(float LocalHours) const noexcept;

    //    `atmosphere(ro, rd, out sky, out trans, out ground)`, transcribed.
    void                        IntegrateAtmosphere(const Vector3& Origin, const Vector3& Direction,
                                                    Vector3& OutSky, Vector3& OutTransmittance, float& OutGround) const noexcept;

    //------------------------------------------------------------------------------------------------------
    //    TEST ACCESS — the shared numeric kernels, exposed for the transliteration proof.
    //------------------------------------------------------------------------------------------------------
    //    `Scratchpad/CelestialTransliterationProof.cpp` re-transcribes each of these straight out of the
    //    reference shader and requires bit-level agreement. They live in an anonymous namespace in the .cpp
    //    so the optimiser can inline them freely; these thunks forward to those exact definitions rather
    //    than restating the arithmetic, so the proof tests the shipping code and not a copy of it.
    //
    //    ⚠️ If you change a kernel, the proof fails unless the reference changed too. That is the point.
    [[nodiscard]] static Vector3 KernelKelvinStar(float t) noexcept;
    [[nodiscard]] static float   KernelHash13(const Vector3& p) noexcept;
    [[nodiscard]] static Vector3 KernelHash33(const Vector3& p) noexcept;
    [[nodiscard]] static float   KernelValueNoise(const Vector3& p) noexcept;
    [[nodiscard]] static Vector3 KernelHue(float h) noexcept;
    [[nodiscard]] static Vector3 KernelRotateY(const Vector3& v, float a) noexcept;
    [[nodiscard]] static Vector3 KernelRotateX(const Vector3& v, float a) noexcept;
    [[nodiscard]] static Vector3 KernelOctahedralDecode(float u, float v) noexcept;
    [[nodiscard]] static float   KernelHenyeyGreenstein(float c, float g) noexcept;
    [[nodiscard]] static float   KernelCloudNoise2(float px, float py) noexcept;
    [[nodiscard]] static float   KernelHeightIntegral(float FalloffHeight, float y0, float y1) noexcept;
    [[nodiscard]] static float   KernelCloudHeightProfile(float hn, float Variety, float Anvil) noexcept;
    //    `tfield` reads Frequency/Seed off the criteria, so it stays an instance method.
    [[nodiscard]] float          TerrainFieldValue(float qx, float qz) const noexcept;
    //    `starField` reads the whole star record plus the solved time, so likewise.
    [[nodiscard]] Vector3        StarFieldValue(const Vector3& Direction, float PixelAngle, float AirMass) const noexcept;

private:
    [[nodiscard]] Vector3       TwilightGlow(const Vector3& Direction, float ElevationDeg, float Facing) const noexcept;
    [[nodiscard]] Vector3       StarField(const Vector3& Direction, float PixelAngle, float AirMass) const noexcept;
    [[nodiscard]] Vector3       MoonDiscs(const Vector3& Direction, const Vector3& Transmittance) const noexcept;
    [[nodiscard]] Vector3       Rainbow(const Vector3& Direction, float RainVisibility, float SkyLuminance) const noexcept;
    [[nodiscard]] Vector3       LensFlare(float u, float v, float SunU, float SunV, float Visibility) const noexcept;

    [[nodiscard]] float         CloudDensity(const Vector3& Position, float Lod) const noexcept;
    [[nodiscard]] float         CloudLightDepth(const Vector3& Position) const noexcept;
    void                        CloudMarch(const Vector3& Origin, const Vector3& Direction, const Vector3& Sky,
                                           const Vector3& Transmittance, float MaximumDistance,
                                           uint32_t PixelX, uint32_t PixelY,
                                           Vector3& OutScatter, float& OutCoverage) const noexcept;
    [[nodiscard]] Vector3       CloudSlab(const Vector3& Direction, const Vector3& Sky, const Vector3& Transmittance,
                                          const ObserverFrame& Observer, float& OutCoverage) const noexcept;

    [[nodiscard]] float         LocalFogDensity(const Vector3& Position) const noexcept;
    [[nodiscard]] float         LocalCloudDensity(const Vector3& Position, float Lod) const noexcept;
    [[nodiscard]] float         UnifiedShadow(const Vector3& Position, float StepLength) const noexcept;
    void                        MarchLocalVolumes(const Vector3& Origin, const Vector3& Direction, float MaximumDistance,
                                                  const Vector3& Sky, const Vector3& HorizonGlow,
                                                  const Vector3& Transmittance, uint32_t PixelX, uint32_t PixelY,
                                                  Vector3& OutScatter, float& OutTransmittance) const noexcept;
    [[nodiscard]] Vector3       ApplyMedia(const Vector3& Radiance, const Vector3& Direction, float Distance,
                                           const Vector3& SkyAmbient, const Vector3& HorizonGlow,
                                           const Vector3& Transmittance, float ObserverHeight) const noexcept;

    [[nodiscard]] Vector3       WindDisplacement(const Vector3& Position) const noexcept;
    [[nodiscard]] Vector3       WindTurbulence(const Vector3& Position, float Time) const noexcept;
    [[nodiscard]] Vector3       SwirlAt(const Vector3& Position) const noexcept;

    //    `tfield` — the terrain's analytic three-sinusoid field, in the panel's normalised patch coordinates.
    [[nodiscard]] float         TerrainField(float qx, float qz) const noexcept;

    CelestialCriteria   Criteria;
    CelestialFrame      Solved;
    MoonAlbedoSurface   MoonSurfaces[4];
};

} // namespace Frontier::ProjectZero
