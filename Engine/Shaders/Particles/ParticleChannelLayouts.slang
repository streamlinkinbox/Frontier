/*============================================================================================================================================
                                                      PARTICLECHANNELLAYOUTS.SLANG
============================================================================================================================================*/
// 🧩 16B-aligned GPU mirror of ParticleChannelLayouts.h + direct ForceFieldEvaluationLayouts.slang reuse (+Z Up Right-Hand world space)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Shaders/Compute/ForceFieldEvaluationLayouts.slang"

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
static const uint ParticleSilhouetteShockRing   = 4u;

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

// Matches C++ OrientationChannel (exact 32 B = 2x float4)
struct GpuOrientationSlot
{
    float4 QuaternionXyzw; // xyzw = Unit quaternion orientation
    float4 AngVelInertia;  // xyz  = Angular velocity [rad/s], w = InertiaScalar [x]
};

// Matches C++ FlockSteeringChannel (exact 32 B = 2x float4)
struct GpuFlockSteeringSlot
{
    float4 HeadingWingPhase; // xyz = Unit forward heading, w = WingPhaseRad [rad]
    float4 SteerSpecies;     // xyz = Steering acceleration [m/s2], w = asfloat(SpeciesVariant)
};

// Matches C++ MagneticCuspChannel (exact 32 B = 2x float4)
struct GpuMagneticCuspSlot
{
    float4 DishAnchorHeight; // xyz = Equilibrium dish anchor [m], w = CuspHeightMeters [m]
    float4 NormalSuscept;    // xyz = Outward surface normal,      w = Susceptibility [x]
};

// Matches C++ PyroVolleyChannel (exact 32 B = 2x float4)
struct GpuPyroVolleySlot
{
    uint   TierIndex;         // [idx] - 0=Ascent rocket, 1=Primary star, 2=Consumed
    float  DetonationTimeSec; // [s]   - Scheduled burst timestamp
    uint   SubBurstCount;     // [cnt] - Secondary stars released at detonation
    uint   PatternCode;       // [idx] - Firework pattern code
    float4 TargetTrailGain;   // xyz = Sky-text target offset [m], w = TrailGain [x]
};

// Matches C++ FractureStressChannel (exact 32 B = 2x float4)
struct GpuFractureStressSlot
{
    float4 CellOriginCrackDepth; // xyz = Intact cell origin [m], w = CrackDepthMeters [m]
    float  AccumulatedStress;    // [0-1] - Normalized fracture stress progress
    uint   BranchId;             // [idx] - Fracture propagation branch identifier
    uint   DetachedBit;          // [0|1] - 1 once shard detaches into ballistic fall
    float  ShardScale;           // [x]   - Fragment scale factor
};

// Matches C++ LatticeGridChannel (exact 32 B = 2x float4)
struct GpuLatticeGridSlot
{
    float4 RestGridExtrusion; // xyz = Rest lattice coordinate [m], w = ExtrusionMeters [m]
    uint   GridCoordX;        // [idx] - Integer grid column X
    uint   GridCoordY;        // [idx] - Integer grid row Y
    uint   GridCoordZ;        // [idx] - Integer grid layer Z
    float  WavePhase;         // [rad] - Travelling wave / derez phase
};

// Specialized domain compute uniform block (exact 128 B = 8x float4)
struct GpuDomainUniforms
{
    float4 OriginScalarA;   // xyz = Domain origin/pole/impact [m], w = Primary radius / amplitude [m]
    float4 AuxVectorScalarB;// xyz = Pole / predator / velocity [m], w = Secondary frequency / speed
    float4 ScalarPackC;     // x = Param0, y = Param1, z = Param2, w = Param3
    float4 ScalarPackD;     // x = Param4, y = Param5, z = Param6, w = Param7
    float4 TimingCounts;    // x = DeltaTimeSec, y = TimeSeconds, z = ImpactTimeSec, w = Restitution
    uint4  DomainControl;   // x = ActiveCount, y = Capacity, z = ModeCode, w = VariantFlags
    float4 SdfBoundsMinVox; // xyz = SDF VolumeBoundsMin [m], w = VoxelSpacingMeters [m]
    uint4  SdfBrickDims;    // x = BrickCountX, y = BrickCountY, z = BrickCountZ, w = NarrowBandBits
};

static const uint SolidProfileCube       = 0u;
static const uint SolidProfileShard      = 1u;
static const uint SolidProfileSpike      = 2u;
static const uint SolidProfileWingedBoid = 3u;
static const uint SolidProfileCoinDisc   = 4u;

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
    uint4  ColliderControl;    // x = FloorEnabled (0/1), y = SphereActive (0/1), z = StrandSubMode, w = TickIndex

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
//                                                 MATH & PRNG HELPERS
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

float4 ParticleNormalizeQuat(float4 Quat)
{
    float LenSq = dot(Quat, Quat);
    if (LenSq <= 1.0e-10)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    return Quat * rsqrt(LenSq);
}

float3 ParticleRotateByQuat(float4 Quat, float3 Vec)
{
    float3 Qv = Quat.xyz;
    float3 T  = 2.0 * cross(Qv, Vec);
    return Vec + Quat.w * T + cross(Qv, T);
}

float4 ParticleIntegrateQuat(float4 Quat, float3 AngVel, float DeltaTimeSec)
{
    float4 Omega = float4(AngVel, 0.0);
    float4 Deriv = float4(
        0.5 * ( Omega.x * Quat.w + Omega.y * Quat.z - Omega.z * Quat.y),
        0.5 * (-Omega.x * Quat.z + Omega.y * Quat.w + Omega.z * Quat.x),
        0.5 * ( Omega.x * Quat.y - Omega.y * Quat.x + Omega.z * Quat.w),
        0.5 * (-Omega.x * Quat.x - Omega.y * Quat.y - Omega.z * Quat.z));
    return ParticleNormalizeQuat(Quat + Deriv * DeltaTimeSec);
}

float4 ParticleQuatFromBasis(float3 RightAxis, float3 ForwardAxis, float3 UpAxis)
{
    // Converts orthonormal columns (Right=+X, Forward=+Y, Up=+Z) into a unit quaternion
    float Trace = RightAxis.x + ForwardAxis.y + UpAxis.z;
    float4 Quat = float4(0.0, 0.0, 0.0, 1.0);
    if (Trace > 0.0)
    {
        float S = sqrt(Trace + 1.0) * 2.0;
        Quat = float4(
            (ForwardAxis.z - UpAxis.y) / S,
            (UpAxis.x - RightAxis.z) / S,
            (RightAxis.y - ForwardAxis.x) / S,
            0.25 * S);
    }
    else if (RightAxis.x > ForwardAxis.y && RightAxis.x > UpAxis.z)
    {
        float S = sqrt(max(1.0e-6, 1.0 + RightAxis.x - ForwardAxis.y - UpAxis.z)) * 2.0;
        Quat = float4(
            0.25 * S,
            (RightAxis.y + ForwardAxis.x) / S,
            (UpAxis.x + RightAxis.z) / S,
            (ForwardAxis.z - UpAxis.y) / S);
    }
    else if (ForwardAxis.y > UpAxis.z)
    {
        float S = sqrt(max(1.0e-6, 1.0 + ForwardAxis.y - RightAxis.x - UpAxis.z)) * 2.0;
        Quat = float4(
            (RightAxis.y + ForwardAxis.x) / S,
            0.25 * S,
            (ForwardAxis.z + UpAxis.y) / S,
            (UpAxis.x - RightAxis.z) / S);
    }
    else
    {
        float S = sqrt(max(1.0e-6, 1.0 + UpAxis.z - RightAxis.x - ForwardAxis.y)) * 2.0;
        Quat = float4(
            (UpAxis.x + RightAxis.z) / S,
            (ForwardAxis.z + UpAxis.y) / S,
            0.25 * S,
            (RightAxis.y - ForwardAxis.x) / S);
    }
    return ParticleNormalizeQuat(Quat);
}
