/*============================================================================================================================================
                                                           PARTICLEBUFFERS.SLANG
============================================================================================================================================*/
// 🧩 Shared pure .slang 16B-aligned GPU structs (matching ParticleBuffers.h), PRNG hashes & ColourRampSampling bridge

module ParticleBuffers;

import ColourRampSampling;

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

public static const float ParticlePi  = 3.14159265359;
public static const float ParticleTau = 6.28318530718;

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

// Matches C++ ParticleSlot (exact 80-byte / 5 x float4 layout)
public struct ParticleSlot
{
    public float4 PositionAge;      // xyz world position [m], w accumulated age [s]
    public float4 VelocityLifetime; // xyz velocity [m/s], w assigned lifespan [s]
    public float4 ColourRgba;       // linear RGBA output
    public float4 Attributes;       // x size [m], y angle [rad], z spin [rad/s], w seed/species
    public float4 PhaseTiming;      // x transition hold/phase, y floor-impact+1, z/w carrier extras
};

// Matches C++ StrandVertex (exact 16-byte layout)
public struct StrandVertex
{
    public float4 PositionRadius;   // xyz world position [m], w half-width/param
};

// Matches C++ GlobalParticleUniform (exact 240-byte / 15 x float4 layout)
public struct GlobalParticleUniform
{
    public float4x4 ViewProjection;
    public float4   CameraRight;
    public float4   CameraUp;
    public float4   CameraPosition;       // xyz eye [m], w fov [rad]
    public float4   WindDomainMin;
    public float4   WindDomainSize;
    public float4   WindDimensions;
    public float4   TimingMetrics;        // x dt, y elapsed, z timeScale, w stepIndex
    public float4   OverlaySettings;
    public float4   CurlSwirl;            // x swirlSpeed, y spatialFreq, z evolutionRate, w unused
    public float4   ColliderBallPose;     // xyz centre [m], w radius [m]
    public float4   ColliderBallVelocity; // xyz velocity [m/s], w impulseScale
};

// Matches C++ RigParticleUniform (exact 352-byte / 22 x float4 layout)
public struct RigParticleUniform
{
    public float4 OriginRadius;         // 0: xyz origin [m], w radius [m]
    public float4 DirectionSpread;      // 1: xyz direction, w spread [rad]
    public float4 SpeedDragGravity;     // 2: x speedMin, y speedMax, z drag, w gravity
    public float4 LifetimeSize;         // 3: x lifeMin, y lifeMax, z sizeStart, w sizeEnd
    public float4 PrimaryColour;        // 4: linear RGBA primary
    public float4 SecondaryColour;      // 5: linear RGBA secondary
    public float4 MidpointColour;       // 6: linear RGBA midpoint
    public float4 SimulationStep;       // 7: x dt, y time, z frame, w simFamily
    public float4 FluidResponse;        // 8: x windCoupling, y bounce, z buoyancy, w flutter
    public float4 ReleaseBudget;        // 9: x capacity, y originCategory, z fractionA, w releaseCountThisStep
    public float4 BoxExtentsHead;       // 10: xyz halfExtents, w cutoutShape
    public float4 MolecularTerms;       // 11: x temp, y epsilon/curlFreq, z sigma/curlAmp, w reactRate/curlRate
    public float4 GridLatticeTerms;     // 12: x cellSize, y gridDim, z slotsPerCell, w dissocRate
    public float4 ReactionDamping;      // 13: x reactRadius, y damping, z shellRadius, w streakThinness
    public float4 CarrierDispatch;      // 14: x headStyle, y seedBase, z cutoutShape, w subMode
    public float4 ScaleAndModes;        // 15: x sizeScale, y curlMode, z webMode, w dynMode
    public float4 ColourRangeMeta;      // 16: x stopCount, y scalarChannel, z rangeMin, w rangeMax
    public float4 ColourWrapMeta;       // 17: x repeatCount, y wrapMode, z phaseOffset, w blendCurve
    public float4 StrandAndBlend;       // 18: x strandVertices, y headScale, z headStyle, w blendEquation
    public float4 EffectorPrimary;      // 19: xyz effector A, w size
    public float4 EffectorSecondary;    // 20: xyz effector B, w size
    public float4 EffectorShapes;       // 21: x shapeA, y shapeB, z barrierForm, w barrierMask
};

// Matches C++ ParticleTelemetryCounters (exact 32-byte layout)
public struct ParticleTelemetryCounters
{
    public uint AliveCount;
    public uint ActiveStrandCount;
    public uint BoundsMinFixedX;
    public uint BoundsMinFixedY;
    public uint BoundsMinFixedZ;
    public uint BoundsMaxFixedX;
    public uint BoundsMaxFixedY;
    public uint BoundsMaxFixedZ;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  HASHES & COLOUR BRIDGE
//------------------------------------------------------------------------------------------------------------------------

public uint ParticleHashU(uint X)
{
    uint H = X;
    H ^= H >> 16u;
    H *= 0x7feb352du;
    H ^= H >> 15u;
    H *= 0x846ca68bu;
    H ^= H >> 16u;
    return H;
}

public float ParticleHashFloat(uint Index, uint Salt)
{
    return float(ParticleHashU(Index ^ (Salt * 0x9e3779b9u)) & 0x00FFFFFFu) / 16777216.0;
}

public float4 EvaluateParticleRampColour(
    float                         RawScalar,
    RigParticleUniform            Rig,
    ColourRampLut64Buffer         Lut)
{
    ColourRangeUniform Range;
    Range.RangeMin           = Rig.ColourRangeMeta.z;
    Range.RangeMax           = Rig.ColourRangeMeta.w;
    Range.RepeatCount        = max(1.0, Rig.ColourWrapMeta.x);
    Range.PhaseOffset        = Rig.ColourWrapMeta.z;
    Range.WrapBehaviour      = uint(Rig.ColourWrapMeta.y + 0.5);
    Range.BlendCurve         = uint(Rig.ColourWrapMeta.w + 0.5);
    Range.InterpolationSpace = ColourSpaceLinearSrgb;
    Range.ReservedPad        = 0u;

    float Norm = NormalizeColourScalar(RawScalar, Range);
    float T    = ApplyColourCoordinateWrap(Norm * Range.RepeatCount + Range.PhaseOffset, Range.WrapBehaviour);
    if (Range.BlendCurve == ColourCurveSmooth)
    {
        T = T * T * (3.0 - 2.0 * T);
    }
    else if (Range.BlendCurve == ColourCurveStepped)
    {
        T = floor(T * 7.999) / 7.0;
    }

    float FIdx = clamp(T, 0.0, 1.0) * 63.0;
    uint  Idx0 = min(63u, uint(floor(FIdx)));
    uint  Idx1 = min(63u, Idx0 + 1u);
    float Frac = FIdx - float(Idx0);
    return lerp(Lut.Samples[Idx0], Lut.Samples[Idx1], Frac);
}
