//============================================================================================================================================
//                                                           SCENELAYOUTS.SLANG
//============================================================================================================================================
// 🧩 Profile layouts common-domain by every program — refresh, occupant, surface card and material — matching the host encoders.

#pragma once
#include "IntrinsicParity.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                     FRAME RECORD
//------------------------------------------------------------------------------------------------------------------------



struct MoonBodyProfile { float4 Direction; float4 Params; float4 Surface; float4 Atlas; };

struct RefreshConstants
{
    float4x4 ViewProjection;                      // [-]    jittered, this refresh
    float4x4 InverseViewProjection;               // [-]    inverse of the jittered projection
    float4x4 UnjitteredViewProjection;            // [-]    this refresh, no sub-pixel offset
    float4x4 PreviousUnjitteredViewProjection;    // [-]    previous refresh, no sub-pixel offset
    float4x4 View;                                // [-]    world → view
    float3 CameraPosition;                        // [m]
    uint RefreshIndex;                              // [-]    monotonic since bring-up
    float3 SunDirection;                          // [-]    unit, toward the light
    float ElapsedSeconds;                          // [s]
    float3 SunIlluminance;                        // [lx·k] after atmospheric transmittance
    float SkyScale;                                // [-]    multiplies the sky-view radiance
    float2 ScreenExtent;                          // [px]
    float2 InverseScreenExtent;                   // [1/px]
    float2 TraceExtent;                           // [px]   resampling resolution
    float2 InverseTraceExtent;                    // [1/px]
    float2 Jitter;                                // [ndc]  sub-pixel offset folded into ViewProjection
    uint TraceScale;                              // [-]    screen pixels per resampling pixel along one axis
    uint Features;                                // [-]    FeatureBit mask
    uint InspectionMode;                          // [-]    display inspection selector
    float TemporalTallyLimit;                      // [-]    M clamp for temporal reuse
    float SpatialRadius;                           // [px]   resampling-space radius of spatial reuse
    uint SpatialCount;                            // [-]    spatial candidates per pixel
    float3 NightSkyRadiance;                      // [-]    floor radiance of the night sky
    float IncandescenceScale;                           // [-]    multiplies every authored incandescence
    uint Refinements;                              // [-]    RefinementBit mask of the post-parity options (0: the demo)
    float ExposureCompensation;                    // [EV]
    float IndirectScale;                           // [-]    multiplies the resolved indirect term
    uint DistanceInspection;                      // [-]    distance inspection selector
    float3 FocusPosition;                         // [m]    centre of the scene distance field cascades
    uint PresentationControl;                            // [-]    presentation brightness and scale
    float3 FireLightPosition;                     // [m]    power-weighted centre of the burning tyres
    float FireLightIntensity;                      // [-]    point intensity; 0 when nothing burns
    float3 FireLightColour;                       // [-]    chromaticity of the flame light
    float FireLightReach;                          // [m]    influence radius
    float4 SolarDisc; 
    float4 Stars; 
    float4 Night; 
    float4 Moon; 
    float4 MoonFog; 
    float4 Aerial; 
    float4 Flare; 
    float4 FlareHalo; 
    float4 FlareOptics; 
    float4 SolarRadiance; 
    float4 FlareLayers; 
    float4 CelestialControl; 
    MoonBodyProfile MoonBodies[4]; 
    float4 Planets[4]; 
    float4 FogTint; 
    float4 StarControl; 
    float4 Twilight; 
};

// 📝 RefinementBit — options beyond the demo, measured against it (Exhibits/RayTracedLighting); all clear reproduces it.
static const uint RefinementSolarDisc       = 1u;    // [-] the sun as a disc light: representative-point highlight, no disc in reflection rays
static const uint RefinementStratifiedSun   = 2u;    // [-] sun-disc samples stratified over 4×4 pixel blocks and refreshes
static const uint RefinementPenumbraFilter  = 4u;    // [-] ray-traced sun visibility filtered across its penumbra
static const uint RefinementMirrorFilter    = 8u;    // [-] smooth surfaces take the unfiltered reflection, rough ones the variance filter
static const uint RefinementMirrorTrace     = 16u;   // [-] smooth surfaces traced at screen resolution
static const uint RefinementShadowReference = 64u;   // [-] measurement only: 64 stratified sun rays per pixel (converged penumbra)
static const uint RefinementMirrorReference = 128u;  // [-] measurement only: every reflective pixel traced 16 times at screen resolution

bool RefinementEnabled(RefreshConstants Refresh, uint Bit)
{
    return (Refresh.Refinements & Bit) != 0u;
}

// 📝 FeatureBit — the toggles the control panel exposes, read by the programs that honour them.
static const uint FeatureIndirect = 1u;
static const uint FeatureTemporalReuse = 2u;
static const uint FeatureSpatialReuse = 4u;
static const uint FeatureVisibilityReuse = 8u;
static const uint FeatureVarianceFilter = 16u;
static const uint FeatureReflection = 32u;
static const uint FeatureCardRadiosity = 64u;
static const uint FeatureTemporalAntialias = 128u;
static const uint FeatureDirectShadow = 256u;
static const uint FeatureAutoExposure = 512u;
static const uint FeatureRadianceRefresh = 1024u;
static const uint FeatureTemporalRestart = 2048u;   // [-] set for one refresh: every temporal history restarts
static const uint FeatureRayTracing = 8192u;   // [-] RTX TLAS/BLAS multi-bounce ReSTIR path tracer
static const uint FeatureRayTracedShadows = 16384u;  // [-] exact BVH visibility for solar cone & clustered IES lights
static const uint FeatureRussianRoulette = 32768u;  // [-] throughput-weighted Russian roulette termination
static const uint FeatureFireflyFilter = 65536u;  // [-] Jacobian & variance firefly/boiling suppression
static const uint FeatureMultiBounceNEE = 131072u; // [-] Next-Event Estimation + RIS at secondary path vertices
static const uint FeatureRayTracedReflections = 262144u; // [-] exact TLAS/BLAS multi-bounce ray-traced reflections

bool FeatureEnabled(RefreshConstants Refresh, uint Bit)
{
    return (Refresh.Features & Bit) != 0u;
}

uint ResolveMaxBounces(RefreshConstants Refresh)
{
    let Encoded = (Refresh.Features >> 20u) & 7u;
    return SelectBranch(Encoded, 3u, Encoded == 0u);
}

uint ResolveInitialCandidates(RefreshConstants Refresh)
{
    let Encoded = (Refresh.Features >> 24u) & 7u;
    return SelectBranch(Encoded, 2u, Encoded == 0u);
}

uint ResolveReflectionBounces(RefreshConstants Refresh)
{
    let Encoded = (Refresh.Features >> 28u) & 7u;
    return SelectBranch(Encoded, 2u, Encoded == 0u);
}

uint ResolveDistanceInspectionMode(RefreshConstants Refresh)
{
    return Refresh.DistanceInspection & 15u;
}

float ResolveReflectionRoughness(RefreshConstants Refresh)
{
    let Encoded = (Refresh.DistanceInspection >> 8u) & 255u;
    return SelectBranch(0.80, float(Encoded) / 255.0, Encoded > 0u);
}

float ResolveReflectionScale(RefreshConstants Refresh)
{
    let Encoded = (Refresh.DistanceInspection >> 16u) & 0xFFFFu;
    return SelectBranch(1.0, max(float(Encoded) - 1.0, 0.0) / 1000.0, Encoded > 0u);
}

float ResolveHmiBrightness(RefreshConstants Refresh)
{
    let Encoded = (Refresh.PresentationControl >> 8u) & 0xFFFu;
    return SelectBranch(1.0, max(float(Encoded) - 1.0, 0.0) / 1000.0, Encoded > 0u);
}

float ResolveHmiScale(RefreshConstants Refresh)
{
    let Encoded = (Refresh.PresentationControl >> 20u) & 0xFFFu;
    return SelectBranch(1.0, float(Encoded) / 1000.0, Encoded > 0u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   OCCUPANT RECORD
//------------------------------------------------------------------------------------------------------------------------

struct OccupantProfile
{
    float4x4 LocalToWorld;            // [-]
    float4x4 WorldToLocal;            // [-]
    float4x4 PreviousLocalToWorld;    // [-]
    float4x4 PreviousWorldToLocal;    // [-]
    float3 WorldMinimum;              // [m]  current world bounds
    uint Participation;                       // [-]  OccupantParticipation mask
    float3 WorldMaximum;              // [m]
    uint MaterialOverride;            // [-]  0xFFFFFFFF keeps the per-facet family
    float3 VolumeMinimum;             // [m]  local corner of distance texel (0,0,0)
    float VoxelExtent;                 // [m]  distance volume voxel edge
    float3 VolumeTexelOrigin;         // [texel] placement inside the volume space
    uint FirstCard;                   // [-]
    float3 VolumeTexelExtent;         // [texel] resolution
    uint CardCount;                   // [-]
};

static const uint ParticipationRaster = 1u;
static const uint ParticipationDistance = 2u;
static const uint ParticipationCards = 4u;
static const uint ParticipationDynamic = 8u;
static const uint ParticipationFallback = 16u;   // card-less: FirstCard carries the packed albedo
static const uint ParticipationShredded = 32u;   // wheel whose tyre rubber is shredded; ray march dispatches through TyreFamily
// 📝 Beyond the demo, the editor's per-object switches: no shadow (shadow depth and shadow rays) and no indirect light (the
//    distance field and the traced rays).
static const uint ParticipationShadowExempt = 64u;
static const uint ParticipationIndirectExempt = 128u;
static const uint NoMaterialOverride = 0xFFFFFFFFu;
// 📝 Beyond the demo: an override with this bit offsets the per-facet family into the object's own material block.
static const uint MaterialBlockBit = 0x80000000u;

uint ResolveSurfaceFamily(uint RawFamily, uint Override)
{
    if (Override == NoMaterialOverride)
    {
        return RawFamily;
    }
    return SelectBranch(Override, (Override & ~MaterialBlockBit) + RawFamily, (Override & MaterialBlockBit) != 0u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CARD RECORD
//------------------------------------------------------------------------------------------------------------------------

// 📝 One oriented card of an occupant: an axis-aligned face of the occupant's local card bounds, looking inward.
//    Axis 0 +X, 1 −X, 2 +Y, 3 −Y, 4 +Z, 5 −Z names the outward normal of the face the card lies on.
struct CardProfile
{
    float3 LocalMinimum;    // [m]
    uint Axis;              // [-]
    float3 LocalMaximum;    // [m]
    uint Occupant;          // [-]
    uint2 TexelOrigin;     // [texel] tile corner in the card texel space
    uint2 TexelExtent;     // [texel] tile size
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   MATERIAL RECORD
//------------------------------------------------------------------------------------------------------------------------

struct MaterialProfile
{
    float3 Albedo;           // [-]  linear Rec.709 reflectance
    float Roughness;          // [-]  perceptual
    float3 Incandescence;         // [-]  colour × luminance
    float Metalness;          // [-]
    float CoatWeight;         // [-]
    float CoatRoughness;      // [-]
    float SpecularLevel;      // [-]  dielectric F0
    float Transmission;       // [-]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   SHARED CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

static const float Pi = 3.14159265358979;
static const float InversePi = 0.31830988618379;
static const float3 LuminanceRec709 = float3(0.2126, 0.7152, 0.0722);

float ResolveLuminance(float3 Radiance)
{
    return dot(Radiance, LuminanceRec709);
}

// 📝 Builds an orthonormal tangent refresh around a unit normal (Duff et al. 2017, branchless).
float3x3 ConstructTangentBasis(float3 Normal)
{
    let Sign = SelectBranch(-1.0, 1.0, Normal.z >= 0.0);
    let a = -1.0 / (Sign + Normal.z);
    let b = Normal.x * Normal.y * a;
    let Tangent = float3(1.0 + Sign * Normal.x * Normal.x * a, Sign * b, -Sign * Normal.x);
    let Bitangent = float3(b, Sign + Normal.y * Normal.y * a, -Normal.y);
    return float3x3(Tangent, Bitangent, Normal);
}
