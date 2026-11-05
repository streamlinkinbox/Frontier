//============================================================================================================================================
//                                                          PLUMEPRESSURE.SLANG
//============================================================================================================================================
// 🧩 Pressure projection of the coarse velocity: divergence, warm-started Jacobi sweeps, gradient subtraction. A port of PlumePressure.wgsl.

#pragma once
#include "IntrinsicParity.slang"
#include "PlumeLayouts.slang"

// 📝 Solves ∇²q = ∇·u and replaces u with u − ∇q, the divergence-free part. Boundary conditions per neighbour: a solid cell answers with the
//    centre's q (no flow through the wall — Neumann); a neighbour outside the window sideways or above is open air at q = 0 (Dirichlet); below
//    the floor is ground (Neumann). q persists between steps at toroidal texel addresses, so each step starts from the last solution of the same
//    world cells — a few sweeps then do the work of many. A cell the window has only just covered restarts from zero.
//
//    Bindings per program: ResolveDivergence 1 → 2 (and clears 3, 4 where newly covered); RelaxPressure 1, 2, 3 → 4; ProjectVelocity 1, 3 → 5.

  [[vk::binding(0, 0)]] ConstantBuffer<PlumeConstants> Plume;
  [[vk::binding(1, 0)]] Texture3D<float4>              VelocityState;
  [[vk::binding(2, 0)]] RWStructuredBuffer<float>      Divergence;
  [[vk::binding(3, 0)]] RWStructuredBuffer<float>      PressureSource;
  [[vk::binding(4, 0)]] RWStructuredBuffer<float>      PressureTarget;
  [[vk::binding(5, 0)]] [[vk::image_format("rgba16f")]] RWTexture3D<float4> LatticeTarget;

bool ClassifyInside(int3 Local)
{
    return all(Local >= int3(0)) && all(Local < int3(Plume.VelocityExtent.xyz));
}

float4 LoadVelocity(int3 Local)
{
    let Clamped = clamp(Local, int3(0), int3(Plume.VelocityExtent.xyz) - int3(1));
    return LoadTexel(VelocityState, ResolveVelocityTexel(Plume, Clamped), 0);
}

uint ResolveLocalIndex(int3 Local)
{
    let Extent = int3(Plume.VelocityExtent.xyz);
    return ToUnsigned((Local.z * Extent.y + Local.y) * Extent.x + Local.x);
}

uint ResolvePressureIndex(int3 Local)
{
    let Extent = int3(Plume.VelocityExtent.xyz);
    let Texel = ResolveVelocityTexel(Plume, Local);
    return ToUnsigned((Texel.z * Extent.y + Texel.y) * Extent.x + Texel.x);
}

// 📝 q at a neighbour under the boundary rules above; Centre is the cell's own q.
float ResolveNeighbourPressure(int3 Local, float Centre)
{
    if (Local.z < 0)
    {
        return Centre;
    }
    if (!ClassifyInside(Local))
    {
        return 0.0;
    }
    if (LoadVelocity(Local).w > 0.5)
    {
        return Centre;
    }
    return PressureSource[ResolvePressureIndex(Local)];
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ResolveDivergence(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
    {
        return;
    }
    let Local = int3(Invocation);
    let Own = LoadVelocity(Local);
    let Scale = 0.5 / Plume.Lattice.y;
    let Flux = (LoadVelocity(Local + int3(1, 0, 0)).x - LoadVelocity(Local - int3(1, 0, 0)).x)
             + (LoadVelocity(Local + int3(0, 1, 0)).y - LoadVelocity(Local - int3(0, 1, 0)).y)
             + (LoadVelocity(Local + int3(0, 0, 1)).z - LoadVelocity(Local - int3(0, 0, 1)).z);
    Divergence[ResolveLocalIndex(Local)] = SelectBranch(Flux * Scale, 0.0, Own.w > 0.5);
    let Position = ProjectVelocityWorld(Plume, Invocation);
    if (!ClassifyWindowInterior(Position, Plume.PriorVelocityOrigin.xyz, Plume.VelocityExtent.xyz, Plume.Lattice.y, Plume.Lattice.z))
    {
        let Index = ResolvePressureIndex(Local);
        PressureSource[Index] = 0.0;
        PressureTarget[Index] = 0.0;
    }
}

[shader("compute")]
[numthreads(4, 4, 4)]
void RelaxPressure(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
    {
        return;
    }
    let Local = int3(Invocation);
    let Index = ResolvePressureIndex(Local);
    let Centre = PressureSource[Index];
    if (LoadVelocity(Local).w > 0.5)
    {
        PressureTarget[Index] = Centre;
        return;
    }
    let Sum = ResolveNeighbourPressure(Local + int3(1, 0, 0), Centre) + ResolveNeighbourPressure(Local - int3(1, 0, 0), Centre)
            + ResolveNeighbourPressure(Local + int3(0, 1, 0), Centre) + ResolveNeighbourPressure(Local - int3(0, 1, 0), Centre)
            + ResolveNeighbourPressure(Local + int3(0, 0, 1), Centre) + ResolveNeighbourPressure(Local - int3(0, 0, 1), Centre);
    let Edge = Plume.Lattice.y;
    PressureTarget[Index] = (Sum - Divergence[ResolveLocalIndex(Local)] * Edge * Edge) / 6.0;
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ProjectVelocity(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
    {
        return;
    }
    let Local = int3(Invocation);
    let Own = LoadVelocity(Local);
    if (Own.w > 0.5)
    {
        LatticeTarget[uint3(ResolveVelocityTexel(Plume, Local))] = Own;
        return;
    }
    let Centre = PressureSource[ResolvePressureIndex(Local)];
    let Scale = 0.5 / Plume.Lattice.y;
    let Gradient = float3(ResolveNeighbourPressure(Local + int3(1, 0, 0), Centre) - ResolveNeighbourPressure(Local - int3(1, 0, 0), Centre),
                          ResolveNeighbourPressure(Local + int3(0, 1, 0), Centre) - ResolveNeighbourPressure(Local - int3(0, 1, 0), Centre),
                          ResolveNeighbourPressure(Local + int3(0, 0, 1), Centre) - ResolveNeighbourPressure(Local - int3(0, 0, 1), Centre)) * Scale;
    var Velocity = Own.xyz - Gradient;
    let Speed = length(Velocity);
    Velocity = SelectBranch(Velocity, Velocity * (PlumeSpeedCeiling / Speed), Speed > PlumeSpeedCeiling);
    LatticeTarget[uint3(ResolveVelocityTexel(Plume, Local))] = float4(Velocity, 0.0);
}
