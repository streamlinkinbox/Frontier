//============================================================================================================================================
//                                                          PLUMEVORTICITY.SLANG
//============================================================================================================================================
// 🧩 Vorticity confinement on the coarse velocity lattice: the curl, then a force along N × ω that restores lost swirl. A port of PlumeVorticity.wgsl.

#pragma once
#include "IntrinsicParity.slang"
#include "PlumeLayouts.slang"

// 📝 Semi-Lagrangian transport smears small eddies away within a few steps. Confinement (Fedkiw, Stam & Jensen 2001) measures ω = ∇ × u, finds
//    N = ∇|ω| / ‖∇|ω|‖ pointing toward stronger rotation, and adds ε·h·(N × ω), which spins the eddies that remain back up — the rolling billows
//    of tyre smoke, at no cost in lattice resolution. ResolveCurl reads the advected velocity (binding 1) and writes the curl; ConfineVorticity
//    reads both and writes the confined velocity (binding 3). Cells outside the window read as their own neighbour (zero gradient).

  [[vk::binding(0, 0)]] ConstantBuffer<PlumeConstants> Plume;
  [[vk::binding(1, 0)]] Texture3D<float4>              VelocityState;
  [[vk::binding(2, 0)]] RWStructuredBuffer<float4>     Curl;
  [[vk::binding(3, 0)]] [[vk::image_format("rgba16f")]] RWTexture3D<float4> LatticeTarget;

float4 LoadVelocity(int3 Local)
{
    let Clamped = clamp(Local, int3(0), int3(Plume.VelocityExtent.xyz) - int3(1));
    return LoadTexel(VelocityState, ResolveVelocityTexel(Plume, Clamped), 0);
}

uint ResolveLocalIndex(int3 Local)
{
    let Extent = int3(Plume.VelocityExtent.xyz);
    let Clamped = clamp(Local, int3(0), Extent - int3(1));
    return ToUnsigned((Clamped.z * Extent.y + Clamped.y) * Extent.x + Clamped.x);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ResolveCurl(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
    {
        return;
    }
    let Local = int3(Invocation);
    let Scale = 0.5 / Plume.Lattice.y;
    let Xp = LoadVelocity(Local + int3(1, 0, 0)).xyz;
    let Xm = LoadVelocity(Local - int3(1, 0, 0)).xyz;
    let Yp = LoadVelocity(Local + int3(0, 1, 0)).xyz;
    let Ym = LoadVelocity(Local - int3(0, 1, 0)).xyz;
    let Zp = LoadVelocity(Local + int3(0, 0, 1)).xyz;
    let Zm = LoadVelocity(Local - int3(0, 0, 1)).xyz;
    let ω = float3((Yp.z - Ym.z) - (Zp.y - Zm.y), (Zp.x - Zm.x) - (Xp.z - Xm.z), (Xp.y - Xm.y) - (Yp.x - Ym.x)) * Scale;
    Curl[ResolveLocalIndex(Local)] = float4(ω, length(ω));
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ConfineVorticity(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz))
    {
        return;
    }
    let Local = int3(Invocation);
    let Own = LoadVelocity(Local);
    var Velocity = Own.xyz;
    if (Own.w < 0.5 && Plume.Forces.z > 0.0)
    {
        let Scale = 0.5 / Plume.Lattice.y;
        let η = float3(Curl[ResolveLocalIndex(Local + int3(1, 0, 0))].w - Curl[ResolveLocalIndex(Local - int3(1, 0, 0))].w,
                       Curl[ResolveLocalIndex(Local + int3(0, 1, 0))].w - Curl[ResolveLocalIndex(Local - int3(0, 1, 0))].w,
                       Curl[ResolveLocalIndex(Local + int3(0, 0, 1))].w - Curl[ResolveLocalIndex(Local - int3(0, 0, 1))].w) * Scale;
        let Length = length(η);
        if (Length > 1.0e-5)
        {
            let ω = Curl[ResolveLocalIndex(Local)].xyz;
            Velocity += Plume.Forces.z * Plume.Lattice.y * cross(η / Length, ω) * Plume.Lattice.w;
        }
    }
    LatticeTarget[uint3(ResolveVelocityTexel(Plume, Local))] = float4(Velocity, Own.w);
}
