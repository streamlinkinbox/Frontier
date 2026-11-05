//============================================================================================================================================
//                                                          PLUMEADVECTION.SLANG
//============================================================================================================================================
// 🧩 Semi-Lagrangian transport of both plume lattices, obstruction by the scene distance field, combustion, sources, decay and coverage. A port
//    of PlumeAdvection.wgsl.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"
#include "SceneLayouts.slang"
#include "DistanceMarch.slang"
#include "PlumeLayouts.slang"

// 📝 AdvectVelocity (coarse), AdvectThermo (fine) and ResolveSunTransmittance (coarse) share one layout; the host binds, per step parity,
//      AdvectVelocity:           1 = velocity state,  2 = thermo state,  4 = next velocity
//      AdvectThermo:             1 = final velocity,  2 = thermo state,  4 = next thermo
//      ResolveSunTransmittance:  1 = final velocity,  2 = next thermo,   4 = light
//    A cell the ONE scene distance field calls solid — the ground plane, a wall, a container, the moving ControlVehicle — holds no gas, and a
//    velocity cell inside a moving occupant takes that occupant's point velocity, so the car's body drags and parts the plume. Group 1 is the
//    scene distance field (DistanceMarch.slang).

  [[vk::binding(0, 0)]] ConstantBuffer<PlumeConstants>         Plume;
  [[vk::binding(1, 0)]] Texture3D<float4>                      VelocityState;
  [[vk::binding(2, 0)]] Texture3D<float4>                      ThermoState;
  [[vk::binding(3, 0)]] SamplerState                           LatticeSampler;
  [[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture3D<float4> LatticeTarget;
  [[vk::binding(5, 0)]] StructuredBuffer<PlumeSource>          Sources;
  [[vk::binding(6, 0)]] RWStructuredBuffer<int>                Coverage;
  [[vk::binding(7, 0)]] StructuredBuffer<OccupantProfile>      Occupants;

groupshared int CoverageLow[3];
groupshared int CoverageHigh[3];

//------------------------------------------------------------------------------------------------------------------------
//                                                     OBSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Returns (velocity, 1) inside geometry and (0, 0) in open air. A velocity cell is solid once its centre lies inside a surface, a thermo cell
//    once its centre is a quarter cell inside, so the first layer of gas above the road and around a tyre survives and a wall still holds no
//    smoke. The velocity reach was a quarter cell outside until the flame seats moved beside the tyres: at 0.5 m cells the cell beside a sidewall
//    counted as solid, its velocity was zero, and a fire pooled under the arch instead of rising. The point velocity of a moving occupant is the
//    displacement of the material point now at Position since the previous refresh's transform, over that refresh.
static const float VelocitySolidReach = 0.0;     // [cells]
static const float ThermoSolidReach   = -0.25;   // [cells]

// 📝 Sub-grid buoyant rise: a hot thread of gas or a flame tongue rises faster than the coarse lattice resolves, so the thermo backtrace adds a
//    slip toward +Z that saturates with the cell's heat and flame.
static const float SubgridHeatRise  = 1.4;    // [m/s] at 40 K and above
static const float SubgridFlameRise = 1.1;    // [m/s] at unit flame and above

float4 ResolveSolid(float3 Position, float Edge)
{
    let Sampled = SampleSceneDistance(Position);
    if (Sampled.x >= VelocitySolidReach * Edge)
    {
        return float4(0.0);
    }
    var Velocity = float3(0.0);
    let Level = ToUnsigned(Sampled.y);
    if (Level < Distance.ClipCount && Plume.SourceMinimum.w > 0.0)
    {
        let Identity = ResolveClipIdentity(Level, Position);
        if (Identity < ArrayCount(Occupants))
        {
            let Occupant = Occupants[Identity];
            if ((Occupant.Participation & ParticipationDynamic) != 0u)
            {
                let Prior = (mul(mul(float4(Position, 1.0), Occupant.WorldToLocal), Occupant.PreviousLocalToWorld)).xyz;
                Velocity = (Position - Prior) / Plume.SourceMinimum.w;
                let Speed = length(Velocity);
                Velocity = SelectBranch(Velocity, Velocity * (PlumeSpeedCeiling / Speed), Speed > PlumeSpeedCeiling);
            }
        }
    }
    return float4(Velocity, 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  VELOCITY ADVECTION
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(4, 4, 4)]
void AdvectVelocity(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
    {
        return;
    }
    let Edge     = Plume.Lattice.y;
    let Seconds  = Plume.Lattice.w;
    let Position = ProjectVelocityWorld(Plume, Invocation);
    let Texel    = ResolveVelocityTexel(Plume, int3(Invocation));
    let Solid    = ResolveSolid(Position, Edge);
    if (Solid.w > 0.5)
    {
        LatticeTarget[uint3(Texel)] = Solid;
        return;
    }

    // ① self-advection; a departure point outside the previous window brings the ambient wind
    let Wind = Plume.Wind.xyz;
    var Velocity = Wind;
    if (ClassifyPriorVelocity(Plume, Position))
    {
        let Own = VelocityState.SampleLevel(LatticeSampler, ProjectVelocityCoordinates(Plume, Position), 0.0).xyz;
        let Departure = Position - Own * Seconds;
        if (ClassifyPriorVelocity(Plume, Departure))
        {
            Velocity = VelocityState.SampleLevel(LatticeSampler, ProjectVelocityCoordinates(Plume, Departure), 0.0).xyz;
        }
    }

    // ② viscous relaxation toward the ambient flow, then buoyancy: hot gas rises, particulate sinks. The lift saturates with temperature —
    //    a burning tyre near 75 K, at 2.4 m/s²/K, would fire the flame out of its cells at 180 m/s²; flames rise at metres per second, so the
    //    lift tends to 8× the coefficient.
    Velocity = Wind + (Velocity - Wind) * Plume.Retention.w;
    if (ClassifyPriorThermo(Plume, Position))
    {
        let Thermo = ThermoState.SampleLevel(LatticeSampler, ProjectThermoCoordinates(Plume, Position), 0.0);
        let Lift = Plume.Forces.x * Thermo.g / (1.0 + 0.125 * Thermo.g);
        Velocity.z += (Lift - Plume.Forces.y * (Thermo.r + Thermo.a)) * Seconds;
    }

    // ③ sources impart their momentum as a relaxation toward the source velocity, so a strong source cannot overshoot
    if (ClassifySourceBounds(Plume, Position))
    {
        for (var Index = 0u; Index < uint(Plume.PriorVelocityOrigin.w); Index++)
        {
            let Source = Sources[Index];
            let Weight = ResolveSourceWeight(Source, Position, Edge);
            let Push   = 1.0 - exp(-Weight * Seconds * (4.0 + 0.15 * (Source.Rates.x + Source.Rates.w) + 0.05 * Source.Rates.z));
            Velocity = lerp(Velocity, Source.Velocity.xyz, Push);
        }
    }
    let Speed = length(Velocity);
    Velocity = SelectBranch(Velocity, Velocity * (PlumeSpeedCeiling / Speed), Speed > PlumeSpeedCeiling);
    LatticeTarget[uint3(Texel)] = float4(Velocity, 0.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THERMO ADVECTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 The coarse velocity plus divergence-free detail below the lattice: the curl of a world-anchored trigonometric potential, each component
//    omitting its own coordinate, in two octaves and scaled by how much is happening at the point — clean air stays still, a boiling plume churns.
float3 SampleLatticeVelocity(float3 Position, float Activity)
{
    var Velocity = VelocityState.SampleLevel(LatticeSampler, ProjectVelocityCoordinates(Plume, Position), 0.0).xyz;
    let Time = Plume.Wind.w;
    let Frequency = Plume.Detail.z;
    let First = Position * Frequency + float3(0.43, -0.31, 0.27) * Time;
    let Second = Position * (Frequency * 2.37) + float3(-0.61, 0.53, -0.89) * Time + float3(1.7, 4.1, 2.9);
    let Swirl = float3(sin(First.y) - sin(First.z), sin(First.z) - sin(First.x), sin(First.x) - sin(First.y))
              + 0.45 * float3(sin(Second.y) - sin(Second.z), sin(Second.z) - sin(Second.x), sin(Second.x) - sin(Second.y));
    return Velocity + Plume.Forces.w * (0.3 + 0.7 * Activity) * Swirl;
}

[shader("compute")]
[numthreads(4, 4, 4)]
void AdvectThermo(uint3 Invocation : SV_DispatchThreadID, uint Lane : SV_GroupIndex)
{
    if (Lane < 3u)
    {
        InterlockedExchange(CoverageLow[Lane], 2147483647, PlumeAtomicSink);
        InterlockedExchange(CoverageHigh[Lane], -2147483647, PlumeAtomicSink);
    }
    GroupMemoryBarrierWithGroupSync();

    if (all(Invocation < Plume.ThermoExtent.xyz))
    {
        let Edge     = Plume.Lattice.x;
        let Seconds  = Plume.Lattice.w;
        let Position = ProjectThermoWorld(Plume, Invocation);
        var Thermo   = float4(0.0);
        if (SampleSceneDistance(Position).x >= ThermoSolidReach * Edge)
        {
            // ① second-order (midpoint) backtrace through the final velocity of this step
            var Activity = 0.0;
            var Rise = float2(0.0);
            if (ClassifyPriorThermo(Plume, Position))
            {
                let Here = ThermoState.SampleLevel(LatticeSampler, ProjectThermoCoordinates(Plume, Position), 0.0);
                Activity = clamp(0.6 * (Here.r + Here.a) + 0.02 * Here.g + Here.b, 0.0, 1.0);
                Rise = clamp(float2(Here.g / 40.0, Here.b), float2(0.0), float2(1.0));
            }
            let Slip      = float3(0.0, 0.0, SubgridHeatRise * Rise.x + SubgridFlameRise * Rise.y);
            let Midpoint  = Position - (SampleLatticeVelocity(Position, Activity) + Slip) * (0.5 * Seconds);
            let Departure = Position - (SampleLatticeVelocity(Midpoint, Activity) + Slip) * Seconds;
            if (ClassifyPriorThermo(Plume, Departure))
            {
                Thermo = ThermoState.SampleLevel(LatticeSampler, ProjectThermoCoordinates(Plume, Departure), 0.0);
            }

            // ② combustion: a flame cell keeps heating its gas and sheds soot as it burns — a tyre fire's black column
            let Burning = max(Thermo.b, 0.0) * Seconds;
            Thermo.g += Burning * Plume.Combustion.y;
            Thermo.a += Burning * Plume.Combustion.x;

            // ③ per-second losses, converted for this step by the host
            Thermo = float4(Thermo.r * Plume.Retention.x, Thermo.g * Plume.Retention.y, Thermo.b * Plume.Retention.z, Thermo.a * Plume.Combustion.z);

            // ④ sources: smoke, heat, flame and soot per second at each Gaussian centre
            if (ClassifySourceBounds(Plume, Position))
            {
                for (var Index = 0u; Index < uint(Plume.PriorVelocityOrigin.w); Index++)
                {
                    let Source = Sources[Index];
                    Thermo += Source.Rates * (ResolveSourceWeight(Source, Position, Edge) * Seconds);
                }
            }
            Thermo = clamp(Thermo, float4(0.0), float4(64.0, 4096.0, 16.0, 64.0));
        }
        LatticeTarget[uint3(ResolveThermoTexel(Plume, int3(Invocation)))] = Thermo;

        // ⑤ coverage: the march only enters the box of cells that hold anything visible
        if (Thermo.r > 0.002 || Thermo.a > 0.002 || Thermo.b > 0.01)
        {
            let Local = int3(Invocation);
            for (var Axis = 0u; Axis < 3u; Axis++)
            {
                InterlockedMin(CoverageLow[Axis], Local[Axis]);
                InterlockedMax(CoverageHigh[Axis], Local[Axis]);
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();
    if (Lane < 3u)
    {
        let Low  = CoverageLow[Lane];
        let High = CoverageHigh[Lane];
        if (Low <= High)
        {
            InterlockedMin(Coverage[Lane], Low);
            InterlockedMax(Coverage[4u + Lane], High);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SUN TRANSMITTANCE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Transmittance through the plume once per coarse cell per step, toward the sun (r) and toward the zenith (g): eight steps each that lengthen
//    with distance through the new thermo lattice. The march then reads both with one filtered sample per point — sunlight self-shadowing, and
//    the sky occluded by the smoke above, which is what darkens the belly of a thick column.
float ResolveOpticalDepth(float3 Position, float3 Toward)
{
    var Depth = 0.0;
    var Travel = 0.25 * Plume.Lattice.y;
    for (var Step = 0u; Step < 8u; Step++)
    {
        let Length = Plume.Lattice.x * (1.0 + 0.6 * float(Step));
        let Sample = Position + Toward * (Travel + 0.5 * Length);
        if (!ClassifyWindowInterior(Sample, int3(2 * Plume.VelocityOrigin.xy, 0), Plume.ThermoExtent.xyz, Plume.Lattice.x, Plume.Lattice.z))
        {
            break;
        }
        let Thermo = max(ThermoState.SampleLevel(LatticeSampler, ProjectThermoCoordinates(Plume, Sample), 0.0), float4(0.0));
        Depth += (Thermo.r * Plume.Optical.x + Thermo.a * Plume.Optical.z) * Length;
        Travel += Length;
    }
    return Depth;
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ResolveSunTransmittance(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
    {
        return;
    }
    let Position = ProjectVelocityWorld(Plume, Invocation);
    let Sun = exp(-ResolveOpticalDepth(Position, Plume.Sun.xyz) * 0.65);
    let Zenith = exp(-ResolveOpticalDepth(Position, float3(0.0, 0.0, 1.0)) * 0.5);
    LatticeTarget[uint3(ResolveVelocityTexel(Plume, int3(Invocation)))] = float4(Sun, Zenith, 0.0, 1.0);
}
