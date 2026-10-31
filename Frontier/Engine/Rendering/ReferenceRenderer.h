/*==========================================================================================================================================
                                                      REFERENCE RENDERER
                                     ------------------------------------------------------------------
                 The complete declared frame executed on CPU: cluster cut + cull -> raster ->
                      ReSTIR direct -> distance-field/card indirect -> SVGF -> atmosphere/clouds
                           -> fog -> display. The SDK phase mirrors it stage for stage.
==========================================================================================================================================*/

// 🧩 Reference renderer: "get something rendering" — every proven portable core wired into one frame.
//
// Each stage calls the same engine code the suites proved (ClusterNesting/VisibilityCull,
// ReservoirSampling, DistanceField, SurfaceCards, VarianceFiltering, AtmosphereModel/CloudField/
// FogMedium), in the order CompleteFrameUnits declares. It is deliberately a *reference*: double
// precision, brute-force shadow rays over the visible triangle set, small images. Its jobs are
// (1) a runnable end-to-end frame before any GPU exists, (2) the numerical oracle the Slang/Vulkan
// port is held to at the SDK phase, (3) owner-visible renders of test scenes today.
// World frame: X east, Y north, Z up, metres — shared by every core it calls.

#pragma once

#include "AtmosphereModel.h"
#include "CloudField.h"
#include "ClusterNesting.h"
#include "DistanceField.h"
#include "FogMedium.h"
#include "SurfaceCards.h"
#include "VisibilityCull.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                           SCENE
//------------------------------------------------------------------------------------------------------------------------

struct ReferenceLight
{
    enum class Kind : uint8_t { Directional, Point };
    Kind          Kind = Kind::Point;
    SpatialVector Vector{};              // Directional: unit direction TOWARD the light. Point: position [m].
    SpectrumRgb   Radiance{1.0, 1.0, 1.0};   // Directional: radiance. Point: intensity (radiance * m^2).
};

struct ReferenceMaterial
{
    SpectrumRgb Albedo{0.8, 0.8, 0.8};
    double      Roughness = 0.5;   // [-] - isotropic GGX perceptual roughness, [0,1]
    double      Metallic = 0.0;    // [-] - dielectric-to-metal Fresnel blend, [0,1]
    double      Ior = 1.5;         // [-] - dielectric relative index of refraction
    double      SpecularLevel = 0.5; // [-] - upstream PreviewScattering dielectric level
};

struct ReferenceMesh
{
    TriangleMesh       Geometry;
    ReferenceMaterial  Material{};
    SpectrumRgb        Emissive{0.0, 0.0, 0.0};
    SpatialVector      Translation{};
    double             Scale = 1.0;
};

// Isotropic one-stratum port of upstream EvaluateScatteringReflection / the
// non-environment path in MaterialSurface.slang. Returns the BRDF f_r [1/sr];
// cosine (N·L) is applied by the direct-light integrator.
[[nodiscard]] SpectrumRgb EvaluateReferenceBrdf(const ReferenceMaterial& Material,
                                                SpatialVector Normal, SpatialVector View,
                                                SpatialVector Light) noexcept;

struct ReferenceScene
{
    std::vector<ReferenceMesh>  Meshes;
    std::vector<ReferenceLight> Lights;
    DistanceFieldScene          Distance;          // indirect/AO approximation of the same scene (may be empty)
    const CardRadiosity*        Cards = nullptr;   // optional solved cards (borrowed)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          SETTINGS
//------------------------------------------------------------------------------------------------------------------------

struct ReferenceRenderSettings
{
    uint32_t Width  = 256;
    uint32_t Height = 160;

    // Direct/indirect sampling
    uint32_t RisCandidates      = 4;
    bool     SpatialReuse       = true;
    uint32_t IndirectSamples    = 8;    // distance-field hemisphere samples per pixel (0 = off)
    uint32_t EnvironmentSamples = 8;    // deterministic cosine/GGX MIS samples per proposal component
    uint32_t AccumulationFrames = 4;    // noisy frames fed to the SVGF temporal history
    bool     Denoise            = true;
    double   CutErrorPx         = 1.0;  // [px] cluster cut budget
    uint64_t Seed               = 1;

    // Environment
    bool                 AtmosphereEnabled = true;
    AtmosphereParameters Atmosphere{};
    SpatialVector        SunDirection{0.0, 0.0, 1.0};   // toward the sun (world)
    SpectrumRgb          SunIrradiance{20.0, 20.0, 20.0};
    bool                 CloudsEnabled = false;
    CloudParameters      Clouds{};
    double               CloudSunScale = 0.06;   // couples march radiance into frame units
    bool                 FogEnabled = false;
    FogParameters        Fog{};
};

struct ReferenceRenderStats
{
    uint32_t TrianglesTotal       = 0;
    uint32_t TrianglesAfterCut    = 0;
    uint32_t TrianglesVisible     = 0;   // after frustum cull
    uint32_t CutNodes             = 0;
    uint32_t SurfacePixels        = 0;
    double   MeanLuminance        = 0.0;
    double   MeanEnvironmentIbl   = 0.0;   // surface pixels only; mean linear luminance before SVGF
};

struct ReferenceImage
{
    uint32_t                 Width  = 0;
    uint32_t                 Height = 0;
    std::vector<SpectrumRgb> Pixels;   // linear HDR, row-major
    ReferenceRenderStats     Stats;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          RENDERER
//------------------------------------------------------------------------------------------------------------------------

class ReferenceRenderer
{
  public:
    // Takes the scene by value: builds world geometry, smooth normals and cluster nestings once.
    void SetScene(ReferenceScene Scene);

    [[nodiscard]] ReferenceImage Render(const CullCamera& Camera, const ReferenceRenderSettings& Settings);

  private:
    struct PreparedMesh
    {
        TriangleMesh               WorldGeometry;
        std::vector<SpatialVector> Normals;   // smooth, per vertex
        ClusterNesting             Nesting;
        ReferenceMaterial         Material{};
        SpectrumRgb                Emissive{};
    };
    ReferenceScene              Scene_;
    std::vector<PreparedMesh>   Prepared_;
    TransmittanceLut            AtmosphereLut_;
    bool                        AtmosphereLutBaked_ = false;
};

// FNV-1a over the linear pixel buffer (quantised) — determinism checks compare two renders.
[[nodiscard]] uint64_t ReferenceImageHash(const ReferenceImage& Image) noexcept;

// Exposure + gamma display encode to packed RGB8 (harness/evidence use).
void EncodeDisplayRgb8(const ReferenceImage& Image, double Exposure, std::vector<uint8_t>& OutRgb8);

} // namespace Frontier
