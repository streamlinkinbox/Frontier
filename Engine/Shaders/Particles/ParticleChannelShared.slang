/*============================================================================================================================================
                                                      PARTICLECHANNELSHARED.SLANG
============================================================================================================================================*/
// 🧩 16B-aligned GPU mirror of ParticleChannelLayouts.h + direct ForceFieldEvaluationShared.slang reuse (+Z Up Right-Hand world space)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Shaders/Compute/ForceFieldEvaluationShared.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

static const float ParticleGravityMs2 = 9.80665;

static const uint VolumeShapePointOrigin       = 0u;
static const uint VolumeShapeSphereVolume      = 1u;
static const uint VolumeShapeDiscVolume        = 2u;
static const uint VolumeShapeBoxVolume         = 3u;
static const uint VolumeShapeBoxSurfaceLattice = 4u;
static const uint VolumeShapeDualElectrodeArc  = 5u;

static const uint ColorDriverLifetimeRatio  = 0u;
static const uint ColorDriverSpeedMagnitude = 1u;
static const uint ColorDriverHeightAltitude = 2u;
static const uint ColorDriverSlotHash       = 3u;

static const uint RampWrapClamp    = 0u;
static const uint RampWrapRepeat   = 1u;
static const uint RampWrapPingPong = 2u;

static const uint StrandModeNone        = 0u;
static const uint StrandModeVerletSetup = 1u;
static const uint StrandModeRelax       = 2u;
static const uint StrandModeDeriveVel   = 3u;

static const uint ParticleBlendAlpha    = 0u;
static const uint ParticleBlendAdditive = 1u;

static const uint ParticleSilhouetteSoftDisc    = 0u;
static const uint ParticleSilhouetteSparkStreak = 1u;
static const uint ParticleSilhouetteGlowOrb     = 2u;
static const uint ParticleSilhouetteShardQuad   = 3u;

//------------------------------------------------------------------------------------------------------------------------
//                                                NATIVE MODULAR CHANNEL MIRRORS
//------------------------------------------------------------------------------------------------------------------------

// Matches C++ KinematicsChannel (exact 32 B = 2x float4)
struct GpuKinematicsSlot
{
    float4 PositionInvMass; // xyz = WorldPosition [m] (+Z Up), w = InverseMass [1/kg] (0 = pinned)
    float4 VelocityDrag;    // xyz = LinearVelocity [m/s],      w = DragScale [x]
};

// Matches C++ LifespanChannel (exact 16 B = 1x uint4/float4)
struct GpuLifespanSlot
{
    float AgeSeconds;     // [s]    - Elapsed age since release
    float MaxLifeSeconds; // [s]    - Expiry lifespan (<= 0 = immortal)
    uint  SeedUint32;     // [-]    - Deterministic slot PRNG seed
    uint  LivenessMask;   // [bits] - Bit 0 = active, Bit 1 = settled, Bit 2 = detached
};

// Matches C++ AppearanceChannel (exact 32 B = 2x float4)
struct GpuAppearanceSlot
{
    float4 ColorRgba;     // rgba = Linear scene color & opacity [0..1]
    float4 VisualMetrics; // x = RadiusMeters [m], y = RollRadians [rad], z = SpinRadPerSec [rad/s], w = ColorRampT [0..1]
};

// Matches C++ StrandAnchorChannel (exact 32 B = 2x float4)
struct GpuStrandAnchorSlot
{
    float4 PrevPosRestLen;  // xyz = Previous position [m], w = RestLengthMeters [m]
    float4 AnchorPinWeight; // xyz = Rest anchor [m],       w = PinWeight [0..1]
};

// Matches C++ StrandConnectivityChannel (exact 16 B = 1x uint4)
struct GpuStrandLinkSlot
{
    uint StrandIndex;   // [idx] - Strand / strip / limb index
    uint SegmentIndex;  // [idx] - Position along strand (0 = root)
    uint LinkPrevSlot;  // [idx] - Upstream slot index (0xFFFFFFFFu = root)
    uint LinkCrossSlot; // [idx] - Lateral cloth / web neighbor slot (0xFFFFFFFFu = none)
};

// Matches C++ ParticleReadbackSummary (exact 32 B = 8x 32-bit words)
struct GpuReadbackSummary
{
    uint  AliveCount;         // [count] - Active particle count after tick
    float PeakSpeedMs;        // [m/s]   - Maximum linear speed across active slots
    float BoundsRadiusMeters; // [m]     - Radial bounding extent around centroid
    float TickDurationUs;     // [us]    - Measured tick duration in microseconds
    float BoundsCenterX;      // [m]     - Active cloud centroid X
    float BoundsCenterY;      // [m]     - Active cloud centroid Y
    float BoundsCenterZ;      // [m]     - Active cloud centroid Z
    uint  RevisionCounter;    // [tick]  - Monotonic fixed-step tick counter
};

// Atomic scratch accumulator used internally by ParticleReductionCompute (exact 32 B)
struct GpuReductionScratch
{
    uint AliveSum;        // [count] - Atomic live slot sum
    uint PeakSpeedOrdered;// [bits]  - Atomic ordered-uint max of linear speed
    int  SumPosXFixed;    // [mm]    - Fixed-point centroid sum X (scale 1000)
    int  SumPosYFixed;    // [mm]    - Fixed-point centroid sum Y (scale 1000)
    int  SumPosZFixed;    // [mm]    - Fixed-point centroid sum Z (scale 1000)
    uint MaxDistSqOrdered;// [bits]  - Atomic ordered-uint max squared radius around origin/centroid
    uint ReservedA;
    uint ReservedB;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                SIMULATION UNIFORM BLOCK
//------------------------------------------------------------------------------------------------------------------------

// Matches C++ ParticleGpuUniformBlock (exact 304 B = 19x 16-byte vectors)
struct GpuParticleUniforms
{
    // Timing & dispatch counts
    float DeltaTimeSec;    // [s]    - Fixed simulation substep
    float TimeSeconds;     // [s]    - Accumulated simulation time
    uint  ActiveCount;     // [cnt]  - Active particle range [0 .. ActiveCount)
    uint  Capacity;        // [cnt]  - Allocated SSBO slot capacity

    uint  ReleaseStartSlot;// [idx]  - First slot index for GPU release dispatch
    uint  ReleaseCount;    // [cnt]  - Slots to initialize in Release dispatch
    uint  SourceCount;     // [cnt]  - Active ForceSourceShaderEntry count
    uint  MasterSeed;      // [-]    - Deterministic dispatch seed

    // Gravity, buoyancy, drag & curl
    float4 GravityDragCurl;  // x = GravityScale, y = BuoyancyAccelMs2, z = DragCoeff, w = CurlAmplitudeMs
    float4 CurlTimingBounds; // x = CurlFrequency, y = CurlTimeScroll, z = FloorZMeters, w = FloorRestitution

    // ForceFields coupling parameters
    float4 ForceCoupling; // x = CouplingGain [1/s], y = AccelScale, z = DragScale, w = FloorFriction

    // Dynamic sphere collider
    float4 SphereCenterRadius; // xyz = CentreMeters [m], w = RadiusMeters [m]
    float4 SphereVelBounce;    // xyz = VelocityMs [m/s], w = Restitution [0..1]
    uint4  ColliderFlags;      // x = FloorEnabled (0/1), y = SphereActive (0/1), z = StrandSubMode, w = TickIndex

    // Appearance curve & 3-stop linear color ramp
    float4 ColorStopA;      // rgba = Ramp stop 0.0
    float4 ColorStopC;      // rgba = Ramp stop 0.5
    float4 ColorStopB;      // rgba = Ramp stop 1.0
    float4 SizeSpinMetrics; // x = SizeStartM, y = SizeEndM, z = SizeMultiplier, w = SpinRateRadSec
    float4 ColorDriverNorm; // x = SpeedNormMaxMs, y = HeightNormMaxM, z = PhaseOffset, w = StrandStiffness
    uint4  ColorDriverMode; // x = ColorDriver, y = WrapMode, z = VolumeShape, w = StrandCrossStiffnessBits

    // Volume release parameters
    float4 ReleaseOriginRadius; // xyz = OriginMeters [m], w = RadiusMeters [m]
    float4 ReleaseDirSpread;    // xyz = LaunchDirection,  w = ConeSpreadRad [rad]
    float4 ReleaseBoxExtents;   // xyz = BoxHalfExtents [m], w = SpeedMinMs [m/s]
    float4 ReleaseSpeedLife;    // x = SpeedMaxMs [m/s], y = LifeMinSec [s], z = LifeMaxSec [s], w = ImpulseScale
};

// Matches C++ ParticleViewportGpuUniforms (exact 128 B = 8x float4)
struct GpuParticleViewportUniforms
{
    float4 ViewProjRow0;     // [-] - Row 0 of Reverse-Z +Z Up World -> Clip matrix
    float4 ViewProjRow1;     // [-] - Row 1
    float4 ViewProjRow2;     // [-] - Row 2
    float4 ViewProjRow3;     // [-] - Row 3
    float4 CameraPosTime;    // xyz = CameraWorld [m], w = TimeSeconds [s]
    float4 CameraRightScale; // xyz = CameraRight,     w = SizeScaleMultiplier
    float4 CameraUpBoost;    // xyz = CameraUp,        w = EmissiveBoost
    uint4  ViewportControl;  // x = ActiveCount, y = SilhouetteMode, z = BlendMode, w = RibbonWidthBits
};

//------------------------------------------------------------------------------------------------------------------------
//                                               SHARED MATH & PRNG HELPERS
//------------------------------------------------------------------------------------------------------------------------

uint ParticleXorshift32(inout uint SeedBits)
{
    uint Sample = (SeedBits != 0u) ? SeedBits : 0x5EED1234u;
    Sample ^= Sample << 13u;
    Sample ^= Sample >> 17u;
    Sample ^= Sample << 5u;
    SeedBits = Sample;
    return Sample;
}

float ParticleNextUnit(inout uint SeedBits)
{
    return float(ParticleXorshift32(SeedBits) >> 8u) * (1.0 / 16777216.0);
}

float ParticleNextRange(inout uint SeedBits, float MinVal, float MaxVal)
{
    return MinVal + (MaxVal - MinVal) * ParticleNextUnit(SeedBits);
}

float ParticleEvaluateRampWrap(float SampleCoord, uint WrapMode)
{
    if (WrapMode == RampWrapRepeat)
    {
        return SampleCoord - floor(SampleCoord);
    }
    if (WrapMode == RampWrapPingPong)
    {
        float ModVal = SampleCoord - 2.0 * floor(SampleCoord * 0.5);
        return (ModVal <= 1.0) ? ModVal : (2.0 - ModVal);
    }
    return saturate(SampleCoord);
}

float4 ParticleSampleColorRamp(float4 StopA, float4 StopC, float4 StopB, float RampT)
{
    if (RampT <= 0.5)
    {
        return lerp(StopA, StopC, RampT * 2.0);
    }
    return lerp(StopC, StopB, (RampT - 0.5) * 2.0);
}

// Exact divergence-free 3D curl matching ParticleKinematicsSolvers.cpp::EvaluateAnalyticCurl
float3 ParticleEvaluateAnalyticCurl(float3 WorldPos, float Frequency, float TimePhase)
{
    float Px = WorldPos.x * Frequency;
    float Py = WorldPos.y * Frequency;
    float Pz = WorldPos.z * Frequency;

    float DerivAzDy = -sin(Px + TimePhase) * sin(Py);
    float DerivAyDz =  cos(Pz + TimePhase) * cos(Px);
    float DerivAxDz = -sin(Py + TimePhase) * sin(Pz);
    float DerivAzDx =  cos(Px + TimePhase) * cos(Py);
    float DerivAyDx = -sin(Pz + TimePhase) * sin(Px);
    float DerivAxDy =  cos(Py + TimePhase) * cos(Pz);

    return float3(
        DerivAzDy - DerivAyDz,
        DerivAxDz - DerivAzDx,
        DerivAyDx - DerivAxDy);
}

float4 ParticleProjectWorldClip(GpuParticleViewportUniforms U, float3 WorldPos)
{
    float4 Wp = float4(WorldPos, 1.0);
    return float4(
        dot(U.ViewProjRow0, Wp),
        dot(U.ViewProjRow1, Wp),
        dot(U.ViewProjRow2, Wp),
        dot(U.ViewProjRow3, Wp));
}

uint ParticleFloatToOrderedUint(float NonNegativeFloat)
{
    return asuint(max(0.0, NonNegativeFloat));
}
