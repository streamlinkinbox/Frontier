//============================================================================================================================================
//                                                           SMOKETRANSPORT.WGSL
//============================================================================================================================================
// 🧩 The coarse flow of one tick: obstacles from the scene distance field, buoyancy and source momentum, semi-Lagrangian transport.

// 📝 Frontier's CoarseGasField.h on the device instead of the CPU, one thread per 0.5 m cell. Obstacles come from the
//    ONE scene distance field rather than a ground query and a chassis box: walls, the tunnel, stress objects and the
//    vehicle are all solid, and a cell inside the vehicle moves with it (v + ω × r), so the projection that follows
//    pushes air around the car and draws a wake behind it.

//! include "SmokeLattice.wgsl"

@compute @workgroup_size(4, 4, 4)
fn ResolveObstacle(@builtin(global_invocation_id) Invocation: vec3u)
{
    let DomainCell = vec3i(Invocation);
    if (any(DomainCell >= ResolveCoarseExtent()))
    {
        return;
    }
    let WorldCell = ResolveCoarseOrigin() + DomainCell;
    let Position = ResolveCoarseCentre(WorldCell);
    let Sampled = SampleSceneDistance(Position);
    var Obstacle = vec4f(0.0);
    if (Sampled.x < 0.0)
    {
        var Velocity = vec3f(0.0);
        let Level = u32(Sampled.y);
        if (Level < Distance.ClipCount && ResolveClipIdentity(Level, Position) < Smoke.VehicleOccupants)
        {
            Velocity = Smoke.ChassisLinear + cross(Smoke.ChassisAngular, Position - Smoke.ChassisPosition);
        }
        Obstacle = vec4f(Velocity, 1.0);
    }
    Obstacles[ResolveCoarseSlot(WorldCell)] = Obstacle;
}

// 📝 In place on the flow state (bound as VelocityOut): cells the window newly covers start still, solids take their
//    obstacle velocity, heat lifts, and every source blends its carrier velocity in and adds its heat.
@compute @workgroup_size(4, 4, 4)
fn IntegrateCoarseForces(@builtin(global_invocation_id) Invocation: vec3u)
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
    var State = select(vec4f(0.0), VelocityOut[Slot], ClassifyPriorCoarse(WorldCell));
    State.z += Smoke.BuoyancyLift * State.w * Smoke.Step;
    // 📝 Ambient wind: open air relaxes toward it horizontally; the projection then routes it round every solid.
    let Breeze = clamp(Smoke.WindCoupling * Smoke.Step, 0.0, 1.0);
    State.x += (Smoke.WindX - State.x) * Breeze;
    State.y += (Smoke.WindY - State.y) * Breeze;
    let Position = ResolveCoarseCentre(WorldCell);
    if (ClassifySourceReach(Position - vec3f(Smoke.CoarseCell), Position + vec3f(Smoke.CoarseCell)))
    {
        for (var Index = 0u; Index < Smoke.SourceCount; Index++)
        {
            let Source = Smoke.Sources[Index];
            let Weight = ResolveSourceWeight(Source, Position, 0.75 * Smoke.CoarseCell);
            if (Weight <= 0.0)
            {
                continue;
            }
            State = vec4f(mix(State.xyz, Source.Velocity.xyz, clamp(Weight * Source.Rates.z * Smoke.Step, 0.0, 1.0)),
                          State.w + Source.Velocity.w * Weight * Smoke.Step);
        }
    }
    VelocityOut[Slot] = State;
}

// 📝 The flow state one cell away, read-only here (VelocityIn); outside the window the air is taken as still.
fn LoadCoarseVelocity(WorldCell: vec3i) -> vec3f
{
    let DomainCell = WorldCell - ResolveCoarseOrigin();
    if (any(DomainCell < vec3i(0)) || any(DomainCell >= ResolveCoarseExtent()))
    {
        return vec3f(0.0);
    }
    return VelocityIn[ResolveCoarseSlot(WorldCell)].xyz;
}

// 📐 ω = ∇ × u by central differences over the six face neighbours, per metre.
fn ResolveCoarseCurl(WorldCell: vec3i) -> vec3f
{
    let XP = LoadCoarseVelocity(WorldCell + vec3i(1, 0, 0));
    let XN = LoadCoarseVelocity(WorldCell - vec3i(1, 0, 0));
    let YP = LoadCoarseVelocity(WorldCell + vec3i(0, 1, 0));
    let YN = LoadCoarseVelocity(WorldCell - vec3i(0, 1, 0));
    let ZP = LoadCoarseVelocity(WorldCell + vec3i(0, 0, 1));
    let ZN = LoadCoarseVelocity(WorldCell - vec3i(0, 0, 1));
    return vec3f((YP.z - YN.z) - (ZP.y - ZN.y), (ZP.x - ZN.x) - (XP.z - XN.z), (XP.y - XN.y) - (YP.x - YN.x)) / (2.0 * Smoke.CoarseCell);
}

// 📐 Vorticity confinement (Fedkiw, Stam & Jensen 2001): f = ε·h·(N × ω), N = ∇|ω| / |∇|ω||. It hands back the small
//    swirls a 0.5 m semi-Lagrangian solve smears away, which is most of what makes a plume curl and roll.
fn ResolveConfinement(WorldCell: vec3i) -> vec3f
{
    let Omega = ResolveCoarseCurl(WorldCell);
    let Gradient = vec3f(length(ResolveCoarseCurl(WorldCell + vec3i(1, 0, 0))) - length(ResolveCoarseCurl(WorldCell - vec3i(1, 0, 0))),
                         length(ResolveCoarseCurl(WorldCell + vec3i(0, 1, 0))) - length(ResolveCoarseCurl(WorldCell - vec3i(0, 1, 0))),
                         length(ResolveCoarseCurl(WorldCell + vec3i(0, 0, 1))) - length(ResolveCoarseCurl(WorldCell - vec3i(0, 0, 1))));
    let Magnitude = length(Gradient);
    if (Magnitude < 1.0e-5)
    {
        return vec3f(0.0);
    }
    return Smoke.Vorticity * Smoke.CoarseCell * cross(Gradient / Magnitude, Omega);
}

// 📐 Semi-Lagrangian self-transport of velocity and heat: each cell reads the state one step upstream, then takes the
//    confinement force of the pre-advection flow.
@compute @workgroup_size(4, 4, 4)
fn AdvectCoarse(@builtin(global_invocation_id) Invocation: vec3u)
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
    let Position = ResolveCoarseCentre(WorldCell);
    let Departure = Position - VelocityIn[Slot].xyz * Smoke.Step;
    var State = SampleCoarseFlow(Departure);
    if (Smoke.Vorticity > 0.0)
    {
        State = vec4f(State.xyz + ResolveConfinement(WorldCell) * Smoke.Step, State.w);
    }
    VelocityOut[Slot] = State;
}
