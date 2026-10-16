//============================================================================================================================================
//                                                            SMOKEPRESSURE.WGSL
//============================================================================================================================================
// 🧩 Incompressibility of the coarse flow: divergence, a fixed count of Jacobi sweeps, projection, and the per-second losses.

// 📝 The discretisation is CoarseGasField.h's (collocated central differences, Neumann at solids, zero pressure at
//    the window's shell), with two differences. The sweeps start from last tick's pressure instead of zero, which buys
//    convergence for free. And the sources are volume sources: hot friction and combustion gas expands, so each
//    source asks the projection for a positive divergence S where it stands (∇²p = ∇·v − S), and the smoke billows
//    out of the contact patch instead of lying in a line along the ground. The pressure lattice is toroidal like the rest, so cells the window newly covers have
//    their stale value cleared before the first sweep. The sweep count is even, so the result always lands in A.

//! include "SmokeLattice.wgsl"

@group(3) @binding(0) var<storage, read_write> PressureIn  : array<f32>;
@group(3) @binding(1) var<storage, read_write> PressureOut : array<f32>;
@group(3) @binding(2) var<storage, read_write> Divergence  : array<f32>;

// 📝 Velocity a face neighbour presents to this cell: a solid's own velocity, still air past the window, else the flow.
fn LoadNeighbourVelocity(WorldCell: vec3i) -> vec3f
{
    if (!ClassifyCoarseInside(WorldCell))
    {
        return vec3f(0.0);
    }
    let Slot = ResolveCoarseSlot(WorldCell);
    let Obstacle = Obstacles[Slot];
    return select(VelocityIn[Slot].xyz, Obstacle.xyz, Obstacle.w > 0.5);
}

@compute @workgroup_size(4, 4, 4)
fn ResolveDivergence(@builtin(global_invocation_id) Invocation: vec3u)
{
    let DomainCell = vec3i(Invocation);
    if (any(DomainCell >= ResolveCoarseExtent()))
    {
        return;
    }
    let WorldCell = ResolveCoarseOrigin() + DomainCell;
    let Slot = ResolveCoarseSlot(WorldCell);
    if (!ClassifyPriorCoarse(WorldCell) || ClassifyCoarseShell(DomainCell))
    {
        PressureIn[Slot] = 0.0;
    }
    if (ClassifyCoarseShell(DomainCell) || Obstacles[Slot].w > 0.5)
    {
        Divergence[Slot] = 0.0;
        return;
    }
    let X = LoadNeighbourVelocity(WorldCell + vec3i(1, 0, 0)).x - LoadNeighbourVelocity(WorldCell - vec3i(1, 0, 0)).x;
    let Y = LoadNeighbourVelocity(WorldCell + vec3i(0, 1, 0)).y - LoadNeighbourVelocity(WorldCell - vec3i(0, 1, 0)).y;
    let Z = LoadNeighbourVelocity(WorldCell + vec3i(0, 0, 1)).z - LoadNeighbourVelocity(WorldCell - vec3i(0, 0, 1)).z;
    var Expansion = 0.0;
    let Position = ResolveCoarseCentre(WorldCell);
    if (ClassifySourceReach(Position - vec3f(Smoke.CoarseCell), Position + vec3f(Smoke.CoarseCell)))
    {
        for (var Index = 0u; Index < Smoke.SourceCount; Index++)
        {
            Expansion += Smoke.Sources[Index].Rates.w * ResolveSourceWeight(Smoke.Sources[Index], Position, 0.75 * Smoke.CoarseCell);
        }
    }
    Divergence[Slot] = -0.5 * Smoke.CoarseCell * (X + Y + Z) + Smoke.CoarseCell * Smoke.CoarseCell * Expansion;
}

// 📝 Pressure a face neighbour contributes: its own beside a solid (Neumann), zero past the window (Dirichlet).
fn LoadNeighbourPressure(WorldCell: vec3i, Own: f32) -> f32
{
    if (!ClassifyCoarseInside(WorldCell))
    {
        return 0.0;
    }
    let Slot = ResolveCoarseSlot(WorldCell);
    return select(PressureIn[Slot], Own, Obstacles[Slot].w > 0.5);
}

@compute @workgroup_size(4, 4, 4)
fn RelaxPressure(@builtin(global_invocation_id) Invocation: vec3u)
{
    let DomainCell = vec3i(Invocation);
    if (any(DomainCell >= ResolveCoarseExtent()))
    {
        return;
    }
    let WorldCell = ResolveCoarseOrigin() + DomainCell;
    let Slot = ResolveCoarseSlot(WorldCell);
    if (ClassifyCoarseShell(DomainCell) || Obstacles[Slot].w > 0.5)
    {
        PressureOut[Slot] = 0.0;
        return;
    }
    let Own = PressureIn[Slot];
    let Sum = LoadNeighbourPressure(WorldCell + vec3i(1, 0, 0), Own) + LoadNeighbourPressure(WorldCell - vec3i(1, 0, 0), Own)
            + LoadNeighbourPressure(WorldCell + vec3i(0, 1, 0), Own) + LoadNeighbourPressure(WorldCell - vec3i(0, 1, 0), Own)
            + LoadNeighbourPressure(WorldCell + vec3i(0, 0, 1), Own) + LoadNeighbourPressure(WorldCell - vec3i(0, 0, 1), Own);
    PressureOut[Slot] = (Divergence[Slot] + Sum) / 6.0;
}

// 📝 In place on the advected flow (bound as VelocityOut), reading the final pressure from PressureIn.
@compute @workgroup_size(4, 4, 4)
fn ProjectCoarse(@builtin(global_invocation_id) Invocation: vec3u)
{
    let DomainCell = vec3i(Invocation);
    if (any(DomainCell >= ResolveCoarseExtent()))
    {
        return;
    }
    let WorldCell = ResolveCoarseOrigin() + DomainCell;
    let Slot = ResolveCoarseSlot(WorldCell);
    if (ClassifyCoarseShell(DomainCell))
    {
        VelocityOut[Slot] = vec4f(0.0);
        return;
    }
    let Obstacle = Obstacles[Slot];
    if (Obstacle.w > 0.5)
    {
        VelocityOut[Slot] = vec4f(Obstacle.xyz, 0.0);
        return;
    }
    let Own = PressureIn[Slot];
    let Gradient = vec3f(LoadNeighbourPressure(WorldCell + vec3i(1, 0, 0), Own) - LoadNeighbourPressure(WorldCell - vec3i(1, 0, 0), Own),
                         LoadNeighbourPressure(WorldCell + vec3i(0, 1, 0), Own) - LoadNeighbourPressure(WorldCell - vec3i(0, 1, 0), Own),
                         LoadNeighbourPressure(WorldCell + vec3i(0, 0, 1), Own) - LoadNeighbourPressure(WorldCell - vec3i(0, 0, 1), Own));
    let State = VelocityOut[Slot];
    var Velocity = (State.xyz - Gradient * (0.5 / Smoke.CoarseCell)) * Smoke.VelocityRemaining;
    let Speed = length(Velocity);
    if (Speed > Smoke.SpeedLimit)
    {
        Velocity *= Smoke.SpeedLimit / Speed;
    }
    VelocityOut[Slot] = vec4f(Velocity, State.w * Smoke.HeatRemaining);
}
