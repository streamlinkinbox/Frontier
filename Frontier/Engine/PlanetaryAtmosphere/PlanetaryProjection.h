/*==========================================================================================================================================
                                                  PLANETARY PROJECTION
                                     ------------------------------------------------------------------
             6-body 2K sRGB selenographic lunar atlas (Luna, Ember, Glacier, Sulfur, Shroud, Shard),
             32x32 octahedral Hipparcos star index + Kolmogorov scintillation + Milky Way dust lane,
                  ringed & atmospheric 3D exogenous planets, and GPU PlanetaryProjection dispatch.
==========================================================================================================================================*/
// 🧩 Planetary dome & exogenous body projection: loads the 6 real 2048x1024 sRGB moon maps into a
//    Vulkan 2D-Array atlas, bins J2000 Hipparcos stars into a 32x32 octahedral index, evaluates ringed
//    and atmospheric exogenous planets, and executes GPU sky/celestial rasterization across realms.

#pragma once

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/PlanetaryAtmosphere/AstronomicalEphemeris.h"
#include "Engine/PlanetaryAtmosphere/AtmosphereScattering.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

class AtmosphereProjection;

struct Vec3
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                1. 6-BODY LUNAR ATLAS (2048 x 1024 x 6 sRGB LAYERS)
//------------------------------------------------------------------------------------------------------------------------

enum class LunarBodyPreset : uint32_t
{
    Luna    = 0, // Slice 0: MoonAlbedoSource2048.jpg (NASA SVS 4720 CGI Moon)
    Ember   = 1, // Slice 1: Ember2048.jpg            (Basalt & glowing magma rilles)
    Glacier = 2, // Slice 2: Glacier2048.jpg          (Europa-class fractured cryosphere)
    Sulfur  = 3, // Slice 3: Sulfur2048.jpg           (Io-class volcanic sulfur deposits)
    Shroud  = 4, // Slice 4: Shroud2048.jpg           (Hazy amber-shrouded carbonaceous world)
    Shard   = 5, // Slice 5: Shard2048.jpg            (Hyper-cratered dark silicate body)
};

inline constexpr uint32_t LunarAtlasLayerCount = 6u;

struct LunarBodyDescriptor
{
    LunarBodyPreset Preset                = LunarBodyPreset::Luna;
    Vec3            DirectionUnit         = {0.0f, 0.70710678f, 0.70710678f};
    float           AngularRadiusRad      = 0.00454f; // ~0.52 deg angular diameter
    float           SpinLongitudeRad      = 0.0f;
    float           LibrationLatitudeRad  = 0.0f;
    float           RollAngleRad          = 0.0f;
    float           EarthshineStrength    = 0.0022f;
    float           SurfaceBrightness     = 1.0f;
    float           AtmosphereHaze        = -1.0f;              // < 0 selects preset lunar exosphere/haze; [0..1] overrides
    bool            OverridePhase = false;
    Vec3            PhaseLightDirection = {0,0,-1};
    Vec3            PhaseIrradiance = {1,1,1};
    float           GlowStrength = 1;
    Vec3            AtmosphereTintRgb     = {0.0f, 0.0f, 0.0f}; // {0,0,0} selects preset lunar atmosphere tint
};

[[nodiscard]] float ResolveLunarAtmosphereHaze(const LunarBodyDescriptor& Moon) noexcept;
[[nodiscard]] Vec3  ResolveLunarAtmosphereTint(const LunarBodyDescriptor& Moon) noexcept;

class LunarSixBodyAtlas
{
  public:
    bool BuildFromLunaSource(const std::string& LunaJpegOrPpmPath,
                             uint32_t           TargetWidth  = 2048u,
                             uint32_t           TargetHeight = 1024u);

    [[nodiscard]] bool     Valid() const noexcept { return Width_ > 0 && Height_ > 0 && Rgba8Srgb_.size() == TotalByteSize(); }
    [[nodiscard]] uint32_t Width() const noexcept { return Width_; }
    [[nodiscard]] uint32_t Height() const noexcept { return Height_; }
    [[nodiscard]] uint32_t LayerCount() const noexcept { return LunarAtlasLayerCount; }
    [[nodiscard]] size_t   LayerByteSize() const noexcept { return static_cast<size_t>(Width_) * Height_ * 4u; }
    [[nodiscard]] size_t   TotalByteSize() const noexcept { return LayerByteSize() * LunarAtlasLayerCount; }

    [[nodiscard]] const std::vector<uint8_t>& Rgba8SrgbBytes() const noexcept { return Rgba8Srgb_; }
    [[nodiscard]] const uint8_t*              LayerData(LunarBodyPreset Preset) const noexcept;

    [[nodiscard]] Vec3 SampleLinearAlbedo(LunarBodyPreset Preset, float LongitudeRad, float LatitudeRad) const noexcept;

  private:
    uint32_t             Width_  = 0u;
    uint32_t             Height_ = 0u;
    std::vector<uint8_t> Rgba8Srgb_{};
};

// Decodes an sRGB image file (JPEG, PNG, or binary P6 PPM) directly into tightly packed 3-byte RGB sRGB texels.
[[nodiscard]] bool DecodeSrgbImageRgb8(const std::string&    FilePath,
                                       uint32_t&             OutWidth,
                                       uint32_t&             OutHeight,
                                       std::vector<uint8_t>& OutRgbBytes);

//------------------------------------------------------------------------------------------------------------------------
//                           2. EXOGENOUS 3D PLANETS (BANDED, TERRESTRIAL, RINGS, ATMOSPHERE)
//------------------------------------------------------------------------------------------------------------------------

enum class PlanetarySurfaceArchetype : uint32_t
{
    BandedGasGiant      = 0,
    OceanicTerrestrial  = 1,
    RustyDesertWorld    = 2,
    MethaneIceGiant     = 3,
    CloudVeiledWorld    = 4,
};

struct PlanetaryRingSystem
{
    bool  Enabled          = false;
    float InnerRadiusRatio = 1.32f;
    float OuterRadiusRatio = 2.38f;
    float PeakOpticalDepth = 1.45f;
    Vec3  InnerTintRgb     = {0.88f, 0.80f, 0.66f};
    Vec3  OuterTintRgb     = {0.72f, 0.74f, 0.78f};
};

struct PlanetaryAtmosphereShell
{
    bool  Enabled             = true;
    float ShellHeightFraction = 0.065f;
    Vec3  RayleighTintRgb     = {0.28f, 0.56f, 1.00f};
    float LimbGlowStrength    = 0.85f;
};

struct PlanetaryBodyDescriptor
{
    PlanetarySurfaceArchetype Archetype          = PlanetarySurfaceArchetype::BandedGasGiant;
    Vec3                      DirectionUnit      = {-0.35f, 0.55f, 0.757f};
    Vec3                      SpinAxisUnit       = {0.18f, 0.96f, 0.21f};
    float                     AngularRadiusRad   = 0.018f;
    float                     SpinPhaseRad       = 0.0f;
    float                     SurfaceAlbedoScale = 1.0f;
    uint32_t                  ProceduralSeed     = 42u;
    Vec3 SurfaceTint = {1,1,1};
    float BandContrast = 1;
    float SurfaceDetail = 1;
    float CloudVeil = .8f;
    PlanetaryRingSystem       Rings{};
    PlanetaryAtmosphereShell  Atmosphere{};
};

//------------------------------------------------------------------------------------------------------------------------
//                      3. HIPPARCOS OCTAHEDRAL CELL INDEX, CUSTOM STARS & SCINTILLATION
//------------------------------------------------------------------------------------------------------------------------

struct OctahedralCellSpan
{
    uint32_t First = 0u;
    uint32_t Count = 0u;
};

struct BinnedCatalogueStar
{
    Vec3  DirectionUnit   = {0.0f, 0.0f, 1.0f};
    float Magnitude       = 5.0f;
    Vec3  ChromaticityRgb = {1.0f, 1.0f, 1.0f};
    float PhaseSeed       = 0.0f;
};

struct CustomStarDescriptor
{
    Vec3  DirectionUnit      = {0.0f, 0.8f, 0.6f};
    float RadianceGain       = 1.0f;
    float AngularCoreRad     = 0.0012f;
    float TemperatureKelvin  = 6500.0f;
    float CoronaGlowStrength = 0.6f;
    float SpikeStrength      = 0.45f;
};

struct PlanetaryDomeSpecification
{
    Vec3                                 PrimarySunDirection   = {0.0f, 0.0f, -1.0f};
    Vec3                                 SolarIrradianceRgb    = {1.0f, 0.98f, 0.94f};
    float                                SolarAngularRadiusRad = 0.00465f;
    float                                PixelAngularSizeRad   = 0.0014f;
    float                                TimeSeconds           = 0.0f;    // [s] also the twinkle clock
    float                                ScintillationStrength = 0.35f;   // [0-1] the twinkle's peak swing at the zenith
    float                                MilkyWayStrength      = 1.0f;
    float                                LimitingMagnitude     = 7.5f;
    std::vector<LunarBodyDescriptor>     Moons{};
    std::vector<PlanetaryBodyDescriptor> Planets{};
    std::vector<CustomStarDescriptor>    CustomStars{};
};

struct PlanetaryDomeSample
{
    Vec3  ExoAtmosphericRadiance = {0.0f, 0.0f, 0.0f};
    float OpaqueBodyCoverage     = 0.0f;
};

class PlanetaryDomeProjection
{
  public:
    static constexpr uint32_t OctahedralGridDim   = 32u;
    static constexpr uint32_t OctahedralCellCount = OctahedralGridDim * OctahedralGridDim;

    bool LoadHipparcosCatalogueCsv(const std::string& CsvPath,
                                   double             JulianUt1    = 2451545.0,
                                   double             LatitudeDeg  = 0.0,
                                   double             LongitudeDeg = 0.0);

    void BuildProceduralStarCatalogue(uint32_t StarCount = 8000u, uint64_t Seed = 1337u);

    bool LoadLunarAtlas(const std::string& LunaJpegOrPpmPath,
                        uint32_t           AtlasWidth  = 2048u,
                        uint32_t           AtlasHeight = 1024u);

    [[nodiscard]] const LunarSixBodyAtlas&                LunarAtlas() const noexcept { return LunarAtlas_; }
    [[nodiscard]] const std::vector<OctahedralCellSpan>&  StarCells() const noexcept { return StarCells_; }
    [[nodiscard]] const std::vector<BinnedCatalogueStar>& BinnedStars() const noexcept { return BinnedStars_; }
    [[nodiscard]] uint32_t                                SourceStarCount() const noexcept { return SourceStarCount_; }

    [[nodiscard]] static Vec3  BvColourIndexToRgb(float BvIndex) noexcept;
    [[nodiscard]] static Vec3  KelvinToBlackbodyChromaticity(float TemperatureKelvin) noexcept;
    [[nodiscard]] static float EvaluateStarScintillation(float AltitudeSine,
                                                         float PhaseSeed,
                                                         float TwinkleClock,
                                                         float Depth = 0.35f) noexcept;
    [[nodiscard]] static Vec3  EvaluateMilkyWayBandRadiance(Vec3 DirectionUnit, float Strength = 1.0f) noexcept;

    [[nodiscard]] PlanetaryDomeSample EvaluateDirection(const PlanetaryDomeSpecification& Specification,
                                                        Vec3                              ViewDirectionUnit) const noexcept;

    [[nodiscard]] Vec3 EvaluatePlanetaryBody(const PlanetaryBodyDescriptor& Planet,
                                             Vec3                           ViewDirectionUnit,
                                             Vec3                           SunDirectionUnit,
                                             Vec3                           SolarIrradianceRgb,
                                             float                          PixelAngularSizeRad,
                                             float&                         OutCoverage) const noexcept;

    [[nodiscard]] Vec3 EvaluateLunarBody(const LunarBodyDescriptor& Moon,
                                         Vec3                       ViewDirectionUnit,
                                         Vec3                       SunDirectionUnit,
                                         Vec3                       SolarIrradianceRgb,
                                         float                      PixelAngularSizeRad,
                                         float&                     OutCoverage) const noexcept;

    [[nodiscard]] static Vec3 EvaluateCustomStar(const CustomStarDescriptor& Star,
                                                 Vec3                        ViewDirectionUnit,
                                                 float                       PixelAngularSizeRad) noexcept;

    static void EncodeOctahedral(Vec3 DirectionUnit, float& OutU, float& OutV) noexcept;

  private:
    void BinStarRecords(const std::vector<StarRecord>& Records);

    LunarSixBodyAtlas                LunarAtlas_{};
    std::vector<OctahedralCellSpan>  StarCells_{};
    std::vector<BinnedCatalogueStar> BinnedStars_{};
    uint32_t                         SourceStarCount_ = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                           4. GPU PLANETARY SKY RASTERIZATION COMPUTE SUBMISSION
//------------------------------------------------------------------------------------------------------------------------

class PlanetarySkyRasterization
{
  public:
    bool Initialize(VulkanComputeExchange& Exchange,
                    const std::string&     ShaderBinaryDirectory,
                    const std::string&     LunaSourceJpgPath,
                    const std::string&     HipparcosCsvPath,
                    std::string*           OutDiagnostic = nullptr);
    void Shutdown(VulkanComputeExchange& Exchange) noexcept;

    bool Execute(VulkanComputeExchange&      Exchange,
                 const DeviceMemorySpan&     EnsembleSpan,
                 const AtmosphereProjection& AtmosphereLuts,
                 const DeviceImageArraySlot& VisibilityImage,
                 DeviceImageArraySlot&       HdrFrameTarget,
                 uint32_t                    ViewportWidth,
                 uint32_t                    ViewportHeight);

    [[nodiscard]] uint32_t                       IndexedStarCount() const noexcept { return IndexedStarCount_; }
    [[nodiscard]] const PlanetaryDomeProjection& HostDome() const noexcept { return HostDome_; }

  private:
    ComputeProgramSlot      Program_{};
    DeviceImageArraySlot    LunarAtlasImage_{};
    DeviceMemorySpan        StarCellSpan_{};
    DeviceMemorySpan        StarRecordSpan_{};
    PlanetaryDomeProjection HostDome_{};
    uint32_t                IndexedStarCount_ = 0u;
};

} // namespace Frontier
