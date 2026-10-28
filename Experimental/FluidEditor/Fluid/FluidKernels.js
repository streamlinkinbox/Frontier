/*====================================================================================================================================
                                                       FLUIDKERNELS.JS
====================================================================================================================================*/
// 🧩 Every WGSL kernel behind the FLIP/PIC free-surface solver, as source strings

// 🔴 WGSL has NO f32 atomics — `atomic<T>` is i32/u32 only. Particle-to-grid transfer is genuine
//    scatter (each particle splats onto 8 cells; neighbouring particles collide on the same cell), so
//    the gather form that suits a voxel diffusion pass does not apply here. The route taken is the one
//    the constraint leaves open: accumulate into `atomic<i32>` at fixed point, then normalise back to
//    f32 in a second pass. Retrofitting this later is expensive, so it is the shape from the outset.
//
//    FixedPointScale sets the quantum. Velocities live in cell-widths/second and stay well under ~1e3;
//    at 2^16 the resolution is ~1.5e-5 and the i32 range tops out near 32768, which no realistic sum of
//    weighted velocity contributions reaches. Raising this risks overflow, lowering it shows as banding.
export const FixedPointScale = 65536.0;

// 📝 Shared preamble. Concatenated ahead of every kernel below rather than repeated, because a struct
//    that drifts between two kernels binds without complaint and misreads the buffer silently.
const SharedPreamble = /* wgsl */`

struct SolverProfile
{
    GridExtent        : vec3<u32>,
    ParticleTally     : u32,

    DomainExtent      : f32,
    CellWidth         : f32,
    TimeStep          : f32,
    PicFlipBlend      : f32,

    Gravity           : vec3<f32>,
    Viscosity         : f32,

    WindDirection     : vec3<f32>,
    WindStrength      : f32,

    VortexCentre      : vec3<f32>,
    VortexStrength    : f32,

    VortexAxis        : vec3<f32>,
    TurbulenceStrength: f32,

    TurbulenceScale   : f32,
    ElapsedSeconds    : f32,
    ColliderTally     : u32,
    FieldSeed         : f32,
};

// A collider is kept as one generic record so the count can vary without recompiling a pipeline.
// Shape: 0 = box, 1 = sphere, 2 = terrain bed, 3 = oscillating paddle.
struct ColliderRecord
{
    Shape    : u32,
    Friction : f32,
    Rate     : f32,
    Radius   : f32,
    Centre   : vec3<f32>,
    Pad0     : f32,
    Extent   : vec3<f32>,
    Pad1     : f32,
};

// Cell classification, matching the standard MAC marker convention.
const CellEmpty : u32 = 0u;
const CellFluid : u32 = 1u;
const CellSolid : u32 = 2u;

fn CellIndex(Coordinate : vec3<i32>, GridExtent : vec3<u32>) -> u32
{
    return u32(Coordinate.x)
         + u32(Coordinate.y) * GridExtent.x
         + u32(Coordinate.z) * GridExtent.x * GridExtent.y;
}

fn WithinGrid(Coordinate : vec3<i32>, GridExtent : vec3<u32>) -> bool
{
    return all(Coordinate >= vec3<i32>(0)) && all(Coordinate < vec3<i32>(GridExtent));
}

// 📝 Signed distance to the collider set. Negative inside solid. Returned as a distance rather than a
//    boolean so the surface pass can round obstacles off instead of stair-stepping them.
fn ColliderDistance(Position : vec3<f32>, Record : ColliderRecord, ElapsedSeconds : f32) -> f32
{
    if (Record.Shape == 0u)
    {
        let Offset = abs(Position - Record.Centre) - Record.Extent;
        return length(max(Offset, vec3<f32>(0.0))) + min(max(Offset.x, max(Offset.y, Offset.z)), 0.0);
    }
    if (Record.Shape == 1u)
    {
        return length(Position - Record.Centre) - Record.Radius;
    }
    if (Record.Shape == 2u)
    {
        // A height-field bed: two sine ridges scaled by roughness, so the floor is uneven but analytic.
        let Ridge = sin(Position.x * 6.0 + Record.Rate) * cos(Position.z * 5.0 - Record.Rate);
        let Surface = Record.Centre.y + Record.Radius * Ridge;
        return Position.y - Surface;
    }

    // Paddle: a box swept along X by its rate, so the sim has a moving boundary to respond to.
    let Sweep = sin(ElapsedSeconds * Record.Rate) * 0.25;
    let Shifted = Position - (Record.Centre + vec3<f32>(Sweep, 0.0, 0.0));
    let Offset = abs(Shifted) - Record.Extent;
    return length(max(Offset, vec3<f32>(0.0))) + min(max(Offset.x, max(Offset.y, Offset.z)), 0.0);
}

fn NearestColliderDistance(Position       : vec3<f32>,
                           Colliders      : ptr<storage, array<ColliderRecord>, read>,
                           ColliderTally  : u32,
                           ElapsedSeconds : f32) -> f32
{
    var Nearest = 1e9;
    for (var Index = 0u; Index < ColliderTally; Index = Index + 1u)
    {
        Nearest = min(Nearest, ColliderDistance(Position, (*Colliders)[Index], ElapsedSeconds));
    }
    return Nearest;
}

// The friction of whichever collider is nearest, so a contact damps against the surface it actually
// touched rather than against an arbitrary member of the set.
fn NearestColliderFriction(Position       : vec3<f32>,
                           Colliders      : ptr<storage, array<ColliderRecord>, read>,
                           ColliderTally  : u32,
                           ElapsedSeconds : f32) -> f32
{
    var Nearest  = 1e9;
    var Friction = 0.0;
    for (var Index = 0u; Index < ColliderTally; Index = Index + 1u)
    {
        let Distance = ColliderDistance(Position, (*Colliders)[Index], ElapsedSeconds);
        if (Distance < Nearest)
        {
            Nearest  = Distance;
            Friction = (*Colliders)[Index].Friction;
        }
    }
    return Friction;
}

// Cheap hash-based value noise, used for turbulence. Deterministic in the seed so a paused sim
// stepped twice lands in the same place both times.
fn HashToUnit(Input : vec3<f32>) -> f32
{
    let Folded = fract(Input * 0.3183099 + vec3<f32>(0.71, 0.113, 0.419));
    let Mixed  = dot(Folded, Folded.yzx * 63.7 + vec3<f32>(19.19));
    return fract(sin(Mixed) * 43758.5453);
}

fn CurlNoise(Position : vec3<f32>, FieldSeed : f32) -> vec3<f32>
{
    let Step = 0.35;
    let Seeded = Position + vec3<f32>(FieldSeed);

    let AxisX = HashToUnit(Seeded + vec3<f32>(Step, 0.0, 0.0)) - HashToUnit(Seeded - vec3<f32>(Step, 0.0, 0.0));
    let AxisY = HashToUnit(Seeded + vec3<f32>(0.0, Step, 0.0)) - HashToUnit(Seeded - vec3<f32>(0.0, Step, 0.0));
    let AxisZ = HashToUnit(Seeded + vec3<f32>(0.0, 0.0, Step)) - HashToUnit(Seeded - vec3<f32>(0.0, 0.0, Step));

    return vec3<f32>(AxisY - AxisZ, AxisZ - AxisX, AxisX - AxisY);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                              PARTICLE TO GRID  (SCATTER)
//------------------------------------------------------------------------------------------------------------------------

// 📝 Trilinear splat of particle momentum onto a collocated velocity grid, accumulated at fixed point.
//    Weight is accumulated alongside so the normalise pass can divide momentum by mass; a cell with no
//    contributing particle keeps weight 0 and is classified empty rather than reading as still water.
export const ClearAccumulationKernel = SharedPreamble + /* wgsl */`

@group(0) @binding(0) var<storage, read_write> MomentumAccumulator : array<atomic<i32>>;
@group(0) @binding(1) var<storage, read_write> WeightAccumulator   : array<atomic<i32>>;
@group(0) @binding(2) var<storage, read_write> CellKind            : array<u32>;
@group(0) @binding(3) var<uniform>             Profile             : SolverProfile;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) Invocation : vec3<u32>)
{
    let CellTally = Profile.GridExtent.x * Profile.GridExtent.y * Profile.GridExtent.z;
    if (Invocation.x >= CellTally) { return; }

    atomicStore(&MomentumAccumulator[Invocation.x * 3u + 0u], 0);
    atomicStore(&MomentumAccumulator[Invocation.x * 3u + 1u], 0);
    atomicStore(&MomentumAccumulator[Invocation.x * 3u + 2u], 0);
    atomicStore(&WeightAccumulator[Invocation.x], 0);
    CellKind[Invocation.x] = CellEmpty;
}
`;

export const ScatterKernel = SharedPreamble + /* wgsl */`

struct Particle
{
    Position : vec3<f32>,
    Alive     : f32,
    Velocity : vec3<f32>,
    Foam      : f32,
};

@group(0) @binding(0) var<storage, read>       Particles           : array<Particle>;
@group(0) @binding(1) var<storage, read_write> MomentumAccumulator : array<atomic<i32>>;
@group(0) @binding(2) var<storage, read_write> WeightAccumulator   : array<atomic<i32>>;
@group(0) @binding(3) var<storage, read_write> CellKind            : array<atomic<u32>>;
@group(0) @binding(4) var<uniform>             Profile             : SolverProfile;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) Invocation : vec3<u32>)
{
    if (Invocation.x >= Profile.ParticleTally) { return; }

    let Subject = Particles[Invocation.x];
    if (Subject.Alive < 0.5) { return; }

    // Grid space: position in cell widths, with the grid origin at the domain corner.
    let GridPosition = Subject.Position / Profile.CellWidth;
    let BaseCell     = vec3<i32>(floor(GridPosition));
    let Fraction     = GridPosition - vec3<f32>(BaseCell);

    for (var OffsetZ = 0; OffsetZ < 2; OffsetZ = OffsetZ + 1)
    {
        for (var OffsetY = 0; OffsetY < 2; OffsetY = OffsetY + 1)
        {
            for (var OffsetX = 0; OffsetX < 2; OffsetX = OffsetX + 1)
            {
                let Target = BaseCell + vec3<i32>(OffsetX, OffsetY, OffsetZ);
                if (!WithinGrid(Target, Profile.GridExtent)) { continue; }

                let AxisWeight = vec3<f32>(
                    select(1.0 - Fraction.x, Fraction.x, OffsetX == 1),
                    select(1.0 - Fraction.y, Fraction.y, OffsetY == 1),
                    select(1.0 - Fraction.z, Fraction.z, OffsetZ == 1));

                let Weight = AxisWeight.x * AxisWeight.y * AxisWeight.z;
                if (Weight <= 0.0) { continue; }

                let Index = CellIndex(Target, Profile.GridExtent);

                atomicAdd(&MomentumAccumulator[Index * 3u + 0u],
                          i32(Subject.Velocity.x * Weight * ${FixedPointScale}));
                atomicAdd(&MomentumAccumulator[Index * 3u + 1u],
                          i32(Subject.Velocity.y * Weight * ${FixedPointScale}));
                atomicAdd(&MomentumAccumulator[Index * 3u + 2u],
                          i32(Subject.Velocity.z * Weight * ${FixedPointScale}));
                atomicAdd(&WeightAccumulator[Index], i32(Weight * ${FixedPointScale}));
            }
        }
    }

    // The cell the particle sits in is fluid by definition. Marked from the containing cell rather than
    // from the splat stencil, or the fluid region inflates by a cell in every direction each step.
    if (WithinGrid(BaseCell, Profile.GridExtent))
    {
        atomicStore(&CellKind[CellIndex(BaseCell, Profile.GridExtent)], CellFluid);
    }
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                            NORMALISE, CLASSIFY, APPLY FORCES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Divides accumulated momentum by accumulated mass, marks solid cells, and applies every body force.
//    Also snapshots the pre-pressure velocity, which is what makes the FLIP half of the blend possible —
//    FLIP transfers the *change* across the step, so the original must survive the pressure solve.
export const NormaliseKernel = SharedPreamble + /* wgsl */`

@group(0) @binding(0) var<storage, read>       MomentumAccumulator : array<i32>;
@group(0) @binding(1) var<storage, read>       WeightAccumulator   : array<i32>;
@group(0) @binding(2) var<storage, read_write> Velocity            : array<vec4<f32>>;
@group(0) @binding(3) var<storage, read_write> VelocityOriginal    : array<vec4<f32>>;
@group(0) @binding(4) var<storage, read_write> CellKind            : array<u32>;
@group(0) @binding(5) var<storage, read>       Colliders           : array<ColliderRecord>;
@group(0) @binding(6) var<uniform>             Profile             : SolverProfile;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) Invocation : vec3<u32>)
{
    let CellTally = Profile.GridExtent.x * Profile.GridExtent.y * Profile.GridExtent.z;
    if (Invocation.x >= CellTally) { return; }

    let Index = Invocation.x;
    let Extent = Profile.GridExtent;

    let PlaneStride = Extent.x * Extent.y;
    let Coordinate = vec3<i32>(
        i32(Index % Extent.x),
        i32((Index / Extent.x) % Extent.y),
        i32(Index / PlaneStride));

    let Centre = (vec3<f32>(Coordinate) + vec3<f32>(0.5)) * Profile.CellWidth;

    // Domain walls and every collider are solid. Checked before the mass divide so a solid cell cannot
    // inherit a stale velocity from a particle that drifted into it.
    var Solid = any(Coordinate <= vec3<i32>(0)) || any(Coordinate >= vec3<i32>(Extent) - vec3<i32>(1));
    if (!Solid && Profile.ColliderTally > 0u)
    {
        Solid = NearestColliderDistance(Centre, &Colliders, Profile.ColliderTally, Profile.ElapsedSeconds) < 0.0;
    }

    if (Solid)
    {
        CellKind[Index] = CellSolid;
        Velocity[Index] = vec4<f32>(0.0);
        VelocityOriginal[Index] = vec4<f32>(0.0);
        return;
    }

    let Mass = f32(WeightAccumulator[Index]) / ${FixedPointScale};

    var Resolved = vec3<f32>(0.0);
    if (Mass > 1e-6)
    {
        Resolved = vec3<f32>(
            f32(MomentumAccumulator[Index * 3u + 0u]) / ${FixedPointScale},
            f32(MomentumAccumulator[Index * 3u + 1u]) / ${FixedPointScale},
            f32(MomentumAccumulator[Index * 3u + 2u]) / ${FixedPointScale}) / Mass;
    }
    else
    {
        // No particle reached this cell — it is air, whatever the scatter pass marked.
        CellKind[Index] = CellEmpty;
    }

    // ---- Body forces, all integrated explicitly over the step ----
    if (CellKind[Index] == CellFluid)
    {
        Resolved = Resolved + Profile.Gravity * Profile.TimeStep;

        if (Profile.WindStrength != 0.0)
        {
            Resolved = Resolved + normalize(Profile.WindDirection + vec3<f32>(1e-6))
                     * Profile.WindStrength * Profile.TimeStep;
        }

        if (Profile.VortexStrength != 0.0)
        {
            let Axis   = normalize(Profile.VortexAxis + vec3<f32>(1e-6));
            let Radial = Centre - Profile.VortexCentre;
            let Tangent = cross(Axis, Radial);
            let Falloff = 1.0 / (1.0 + dot(Radial, Radial));
            Resolved = Resolved + Tangent * Profile.VortexStrength * Falloff * Profile.TimeStep;
        }

        if (Profile.TurbulenceStrength != 0.0)
        {
            Resolved = Resolved
                     + CurlNoise(Centre * Profile.TurbulenceScale, Profile.FieldSeed)
                     * Profile.TurbulenceStrength * Profile.TimeStep;
        }

        // Viscosity as a pull toward zero relative motion. Cheap stand-in for a diffusion solve; enough
        // for the dial to read as thicker liquid without a second linear system.
        if (Profile.Viscosity > 0.0)
        {
            Resolved = Resolved * (1.0 - clamp(Profile.Viscosity * Profile.TimeStep, 0.0, 1.0));
        }
    }

    Velocity[Index] = vec4<f32>(Resolved, 0.0);
    VelocityOriginal[Index] = vec4<f32>(Resolved, 0.0);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                   PRESSURE PROJECTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Divergence of the force-applied field. Solid neighbours contribute nothing, which is the free-slip
//    wall condition; empty (air) neighbours are treated as pressure zero by the Jacobi pass below, which
//    is what gives the liquid a free surface rather than a lid.
export const DivergenceKernel = SharedPreamble + /* wgsl */`

@group(0) @binding(0) var<storage, read>       Velocity   : array<vec4<f32>>;
@group(0) @binding(1) var<storage, read>       CellKind   : array<u32>;
@group(0) @binding(2) var<storage, read_write> Divergence : array<f32>;
@group(0) @binding(3) var<uniform>             Profile    : SolverProfile;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) Invocation : vec3<u32>)
{
    let Extent = Profile.GridExtent;
    let CellTally = Extent.x * Extent.y * Extent.z;
    if (Invocation.x >= CellTally) { return; }

    let Index = Invocation.x;
    if (CellKind[Index] != CellFluid) { Divergence[Index] = 0.0; return; }

    let Coordinate = vec3<i32>(
        i32(Index % Extent.x),
        i32((Index / Extent.x) % Extent.y),
        i32(Index / (Extent.x * Extent.y)));

    // Central differences on the collocated grid.
    var Total = 0.0;
    let Axes = array<vec3<i32>, 3>(vec3<i32>(1,0,0), vec3<i32>(0,1,0), vec3<i32>(0,0,1));

    for (var AxisIndex = 0; AxisIndex < 3; AxisIndex = AxisIndex + 1)
    {
        let Axis = Axes[AxisIndex];
        let Ahead  = Coordinate + Axis;
        let Behind = Coordinate - Axis;

        var Forward = 0.0;
        var Reverse = 0.0;

        if (WithinGrid(Ahead, Extent) && CellKind[CellIndex(Ahead, Extent)] != CellSolid)
        {
            Forward = Velocity[CellIndex(Ahead, Extent)][AxisIndex];
        }
        if (WithinGrid(Behind, Extent) && CellKind[CellIndex(Behind, Extent)] != CellSolid)
        {
            Reverse = Velocity[CellIndex(Behind, Extent)][AxisIndex];
        }

        Total = Total + (Forward - Reverse) * 0.5;
    }

    Divergence[Index] = Total / Profile.CellWidth;
}
`;

// 📝 Damped Jacobi over the pressure Poisson system, ping-ponged by the host. Air cells hold pressure 0
//    (the free-surface Dirichlet condition) and solid cells reflect their neighbour (Neumann), so the
//    same kernel serves the interior, the surface, and the walls.
export const PressureJacobiKernel = SharedPreamble + /* wgsl */`

@group(0) @binding(0) var<storage, read>       PressureRead  : array<f32>;
@group(0) @binding(1) var<storage, read_write> PressureWrite : array<f32>;
@group(0) @binding(2) var<storage, read>       Divergence    : array<f32>;
@group(0) @binding(3) var<storage, read>       CellKind      : array<u32>;
@group(0) @binding(4) var<uniform>             Profile       : SolverProfile;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) Invocation : vec3<u32>)
{
    let Extent = Profile.GridExtent;
    let CellTally = Extent.x * Extent.y * Extent.z;
    if (Invocation.x >= CellTally) { return; }

    let Index = Invocation.x;

    // Air is the Dirichlet boundary — pressure pinned at zero is exactly what makes this a free surface.
    if (CellKind[Index] != CellFluid) { PressureWrite[Index] = 0.0; return; }

    let Coordinate = vec3<i32>(
        i32(Index % Extent.x),
        i32((Index / Extent.x) % Extent.y),
        i32(Index / (Extent.x * Extent.y)));

    var NeighbourSum = 0.0;
    var FluidFaces   = 0.0;

    let Offsets = array<vec3<i32>, 6>(
        vec3<i32>( 1, 0, 0), vec3<i32>(-1, 0, 0),
        vec3<i32>( 0, 1, 0), vec3<i32>( 0,-1, 0),
        vec3<i32>( 0, 0, 1), vec3<i32>( 0, 0,-1));

    for (var Face = 0; Face < 6; Face = Face + 1)
    {
        let Neighbour = Coordinate + Offsets[Face];
        if (!WithinGrid(Neighbour, Extent)) { continue; }

        let NeighbourIndex = CellIndex(Neighbour, Extent);
        let Kind = CellKind[NeighbourIndex];

        if (Kind == CellSolid)
        {
            // Neumann: the wall mirrors this cell, so it drops out of the operator entirely.
            continue;
        }

        FluidFaces = FluidFaces + 1.0;
        if (Kind == CellFluid) { NeighbourSum = NeighbourSum + PressureRead[NeighbourIndex]; }
    }

    if (FluidFaces < 0.5) { PressureWrite[Index] = 0.0; return; }

    let Scaled = Divergence[Index] * Profile.CellWidth * Profile.CellWidth / Profile.TimeStep;
    let Relaxed = (NeighbourSum - Scaled) / FluidFaces;

    // 🔴 Damped Jacobi. An undamped Jacobi sweep (weight 1.0) does not converge on this operator — the
    //    high-frequency error mode is untouched and the residual plateaus. 2/3 is the standard damping.
    let DampingWeight = 2.0 / 3.0;
    PressureWrite[Index] = mix(PressureRead[Index], Relaxed, DampingWeight);
}
`;

// Subtract the pressure gradient, leaving a divergence-free field.
export const ProjectKernel = SharedPreamble + /* wgsl */`

@group(0) @binding(0) var<storage, read_write> Velocity : array<vec4<f32>>;
@group(0) @binding(1) var<storage, read>       Pressure : array<f32>;
@group(0) @binding(2) var<storage, read>       CellKind : array<u32>;
@group(0) @binding(3) var<uniform>             Profile  : SolverProfile;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) Invocation : vec3<u32>)
{
    let Extent = Profile.GridExtent;
    let CellTally = Extent.x * Extent.y * Extent.z;
    if (Invocation.x >= CellTally) { return; }

    let Index = Invocation.x;
    if (CellKind[Index] != CellFluid) { return; }

    let Coordinate = vec3<i32>(
        i32(Index % Extent.x),
        i32((Index / Extent.x) % Extent.y),
        i32(Index / (Extent.x * Extent.y)));

    var Gradient = vec3<f32>(0.0);
    let Axes = array<vec3<i32>, 3>(vec3<i32>(1,0,0), vec3<i32>(0,1,0), vec3<i32>(0,0,1));

    for (var AxisIndex = 0; AxisIndex < 3; AxisIndex = AxisIndex + 1)
    {
        let Axis = Axes[AxisIndex];
        let Ahead  = Coordinate + Axis;
        let Behind = Coordinate - Axis;

        var Forward = Pressure[Index];
        var Reverse = Pressure[Index];

        if (WithinGrid(Ahead, Extent) && CellKind[CellIndex(Ahead, Extent)] != CellSolid)
        {
            Forward = Pressure[CellIndex(Ahead, Extent)];
        }
        if (WithinGrid(Behind, Extent) && CellKind[CellIndex(Behind, Extent)] != CellSolid)
        {
            Reverse = Pressure[CellIndex(Behind, Extent)];
        }

        Gradient[AxisIndex] = (Forward - Reverse) * 0.5 / Profile.CellWidth;
    }

    let Current = Velocity[Index].xyz;
    Velocity[Index] = vec4<f32>(Current - Gradient * Profile.TimeStep, 0.0);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                          GRID TO PARTICLE  +  ADVECTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 The PIC/FLIP blend and the position integration, in one pass.
//
//    PIC reads the projected grid velocity outright — stable, but it re-samples the grid every step and
//    the repeated interpolation bleeds energy until the liquid reads as syrup. FLIP instead adds the
//    grid's *change* to the particle's own velocity, preserving detail but accumulating noise with
//    nothing to damp it. Neither is usable alone; the blend dial is the standard remedy and production
//    values sit near 0.95, i.e. mostly FLIP with just enough PIC to bleed off the noise.
export const GatherKernel = SharedPreamble + /* wgsl */`

struct Particle
{
    Position : vec3<f32>,
    Alive     : f32,
    Velocity : vec3<f32>,
    Foam      : f32,
};

@group(0) @binding(0) var<storage, read_write> Particles        : array<Particle>;
@group(0) @binding(1) var<storage, read>       Velocity         : array<vec4<f32>>;
@group(0) @binding(2) var<storage, read>       VelocityOriginal : array<vec4<f32>>;
@group(0) @binding(3) var<storage, read>       Colliders        : array<ColliderRecord>;
@group(0) @binding(4) var<uniform>             Profile          : SolverProfile;

fn SampleField(Field : ptr<storage, array<vec4<f32>>, read>,
               GridPosition : vec3<f32>,
               Extent : vec3<u32>) -> vec3<f32>
{
    let BaseCell = vec3<i32>(floor(GridPosition));
    let Fraction = GridPosition - vec3<f32>(BaseCell);

    var Accumulated = vec3<f32>(0.0);
    var WeightTotal = 0.0;

    for (var OffsetZ = 0; OffsetZ < 2; OffsetZ = OffsetZ + 1)
    {
        for (var OffsetY = 0; OffsetY < 2; OffsetY = OffsetY + 1)
        {
            for (var OffsetX = 0; OffsetX < 2; OffsetX = OffsetX + 1)
            {
                let Target = BaseCell + vec3<i32>(OffsetX, OffsetY, OffsetZ);
                if (!WithinGrid(Target, Extent)) { continue; }

                let AxisWeight = vec3<f32>(
                    select(1.0 - Fraction.x, Fraction.x, OffsetX == 1),
                    select(1.0 - Fraction.y, Fraction.y, OffsetY == 1),
                    select(1.0 - Fraction.z, Fraction.z, OffsetZ == 1));

                let Weight = AxisWeight.x * AxisWeight.y * AxisWeight.z;
                Accumulated = Accumulated + (*Field)[CellIndex(Target, Extent)].xyz * Weight;
                WeightTotal = WeightTotal + Weight;
            }
        }
    }

    if (WeightTotal < 1e-6) { return vec3<f32>(0.0); }
    return Accumulated / WeightTotal;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) Invocation : vec3<u32>)
{
    if (Invocation.x >= Profile.ParticleTally) { return; }

    var Subject = Particles[Invocation.x];
    if (Subject.Alive < 0.5) { return; }

    let GridPosition = Subject.Position / Profile.CellWidth;

    let Projected = SampleField(&Velocity, GridPosition, Profile.GridExtent);
    let Prior     = SampleField(&VelocityOriginal, GridPosition, Profile.GridExtent);

    let PicVelocity  = Projected;
    let FlipVelocity = Subject.Velocity + (Projected - Prior);

    var Resolved = mix(PicVelocity, FlipVelocity, clamp(Profile.PicFlipBlend, 0.0, 1.0));

    // ---- Advection, forward Euler over the substep ----
    var Advanced = Subject.Position + Resolved * Profile.TimeStep;

    // ---- Collider resolution: push out along the gradient, then kill the inbound normal component ----
    if (Profile.ColliderTally > 0u)
    {
        let Distance = NearestColliderDistance(Advanced, &Colliders, Profile.ColliderTally, Profile.ElapsedSeconds);
        if (Distance < 0.0)
        {
            let Probe = Profile.CellWidth * 0.5;
            let Normal = normalize(vec3<f32>(
                NearestColliderDistance(Advanced + vec3<f32>(Probe,0.0,0.0), &Colliders, Profile.ColliderTally, Profile.ElapsedSeconds)
              - NearestColliderDistance(Advanced - vec3<f32>(Probe,0.0,0.0), &Colliders, Profile.ColliderTally, Profile.ElapsedSeconds),
                NearestColliderDistance(Advanced + vec3<f32>(0.0,Probe,0.0), &Colliders, Profile.ColliderTally, Profile.ElapsedSeconds)
              - NearestColliderDistance(Advanced - vec3<f32>(0.0,Probe,0.0), &Colliders, Profile.ColliderTally, Profile.ElapsedSeconds),
                NearestColliderDistance(Advanced + vec3<f32>(0.0,0.0,Probe), &Colliders, Profile.ColliderTally, Profile.ElapsedSeconds)
              - NearestColliderDistance(Advanced - vec3<f32>(0.0,0.0,Probe), &Colliders, Profile.ColliderTally, Profile.ElapsedSeconds))
              + vec3<f32>(1e-9));

            Advanced = Advanced - Normal * Distance;

            // Remove only the component driving into the surface; sliding motion survives.
            let Inbound = min(dot(Resolved, Normal), 0.0);
            let Tangential = Resolved - Normal * Inbound;

            // 📝 Friction is read from the nearest collider, not from index 0 — with several colliders
            //    present, a fixed index applies the wrong surface's friction to every contact.
            let Friction = NearestColliderFriction(Advanced, &Colliders, Profile.ColliderTally,
                                                   Profile.ElapsedSeconds);
            Resolved = Tangential * (1.0 - clamp(Friction, 0.0, 1.0));
        }
    }

    // ---- Domain walls ----
    let Margin = Profile.CellWidth * 1.01;
    let Ceiling = Profile.DomainExtent - Margin;

    for (var AxisIndex = 0; AxisIndex < 3; AxisIndex = AxisIndex + 1)
    {
        if (Advanced[AxisIndex] < Margin)
        {
            Advanced[AxisIndex] = Margin;
            Resolved[AxisIndex] = max(Resolved[AxisIndex], 0.0);
        }
        if (Advanced[AxisIndex] > Ceiling)
        {
            Advanced[AxisIndex] = Ceiling;
            Resolved[AxisIndex] = min(Resolved[AxisIndex], 0.0);
        }
    }

    // Foam tracks sustained agitation, decaying steadily so a settled pool clears rather than staying white.
    let Agitation = length(Resolved);
    Subject.Foam = clamp(Subject.Foam * 0.96 + max(Agitation - 2.5, 0.0) * 0.05, 0.0, 1.0);

    Subject.Velocity = Resolved;
    Subject.Position = Advanced;
    Particles[Invocation.x] = Subject;
}
`;
