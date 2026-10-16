//============================================================================================================================================
//                                                           SMOKEADVECTION.WGSL
//============================================================================================================================================
// 🧩 Fine smoke, heat, flame and soot carried by the solved coarse flow plus a divergence-free curl, brick by brick, empty bricks skipped.

// 📝 One workgroup per 8³ brick, each lane owning a 2×2×2 block. A brick runs only when there is something to do:
//    a non-empty brick in its 3×3×3 neighbourhood in the prior state (inflow reaches at most a cell per tick), a
//    non-empty target texel set it must clear, or a source footprint touching it. Otherwise its target texels are
//    already zero and the workgroup returns at once — in a drift, most of the window is air and costs nothing.
//
//    Per cell: RK2 departure point through the coarse flow plus an analytic curl flow; a hardware-trilinear read of
//    the prior texture, refused outside the prior window; per-second losses; the swept sources. Cells inside scene
//    geometry (the distance field, so the car body too) and the window's shell are zero.

//! include "SmokeLattice.wgsl"

@group(3) @binding(0) var FinePrior                    : texture_3d<f32>;
@group(3) @binding(1) var FineTarget                   : texture_storage_3d<rgba16float, write>;
@group(3) @binding(2) var<storage, read_write> BrickFlags : array<u32>;            // [parity · BrickCount + slot] 1 = non-empty
@group(3) @binding(3) var<storage, read_write> Summary  : array<atomic<u32>>;    // occupied brick box and count

var<workgroup> DemandVote  : atomic<u32>;
var<workgroup> DemandValue : u32;
var<workgroup> WrittenVote : atomic<u32>;

// 📝 Cleared at the start of every tick; the fine advection folds every non-empty brick into it.
@compute @workgroup_size(1)
fn ResetSummary()
{
    for (var Axis = 0u; Axis < 3u; Axis++)
    {
        atomicStore(&Summary[Axis], 0xFFFFFFFFu);
        atomicStore(&Summary[3u + Axis], 0u);
    }
    atomicStore(&Summary[6], 0u);
    atomicStore(&Summary[7], 0u);
}


// 📐 A sum of plane waves A·sin(K·x + ωt + φ) with every A ⟂ K is divergence-free term by term: it stirs without
//    adding or removing smoke, and it is anchored to the world, so it does not move with the car.
fn ResolveCurlFlow(Position: vec3f, Time: f32) -> vec3f
{
    var Waves = array<vec4f, 6>(vec4f(0.8205, 0.3102, 0.4803, 2.0346), vec4f(-0.5707, 1.4349, 0.5218, 4.0898),
                                vec4f(1.4074, -1.6510, 1.6240, 3.3669), vec4f(-2.8070, -1.0320, 2.8483, 0.3644),
                                vec4f(0.6286, 2.8285, -5.5941, 0.2356), vec4f(9.0441, -1.9450, -2.9174, 0.4389));
    var Amplitudes = array<vec4f, 6>(vec4f(0.3536, -0.9354, 0.0, 0.4207), vec4f(0.0, 0.2119, -0.5827, 0.3579),
                                     vec4f(-0.2872, 0.0, 0.2489, 0.5926), vec4f(-0.0828, 0.2253, 0.0, 0.7059),
                                     vec4f(0.0, -0.1339, -0.0677, 0.6469), vec4f(0.0276, 0.0, 0.0857, 0.3726));
    var Flow = vec3f(0.0);
    for (var Wave = 0u; Wave < 6u; Wave++)
    {
        let Phase = dot(Waves[Wave].xyz, Position) * Smoke.CurlScale + Amplitudes[Wave].w * Time * 3.0 + Waves[Wave].w;
        Flow += Amplitudes[Wave].xyz * sin(Phase);
    }
    return Flow * Smoke.CurlStrength;
}

fn ResolveFlow(Position: vec3f) -> vec3f
{
    return SampleCoarseFlow(Position).xyz + ResolveCurlFlow(Position, Smoke.Time);
}

fn ClassifyPriorBrick(WorldBrick: vec3i) -> bool
{
    let Local = WorldBrick - ResolveWorldBrick(Smoke.PriorFineOrigin);
    return all(Local >= vec3i(0)) && all(Local < vec3i(Smoke.BrickExtent));
}

// 📝 The prior state at a departure point: the hardware trilinear tap through the repeating sampler lands on the right
//    toroidal texels, and is refused unless its footprint lies inside the prior window.
fn SamplePriorThermo(Position: vec3f) -> vec4f
{
    let Extent = vec3f(Smoke.FineExtent);
    let Local = Position / Smoke.FineCell - vec3f(Smoke.PriorFineOrigin);
    if (any(Local < vec3f(0.5)) || any(Local > Extent - vec3f(0.5)))
    {
        return vec4f(0.0);
    }
    return textureSampleLevel(FinePrior, RepeatSampler, Position / (Extent * Smoke.FineCell), 0.0);
}

@compute @workgroup_size(4, 4, 4)
fn AdvectFine(@builtin(workgroup_id) Group: vec3u, @builtin(local_invocation_id) LaneCell: vec3u,
              @builtin(local_invocation_index) Lane: u32)
{
    let OriginBrick = ResolveWorldBrick(Smoke.FineOrigin);
    let WorldBrick = OriginBrick + vec3i(Group);
    let PriorParity = 1u - Smoke.FineParity;
    if (Lane == 0u)
    {
        atomicStore(&DemandVote, 0u);
        atomicStore(&WrittenVote, 0u);
    }
    workgroupBarrier();

    // ① demand: inflow from a non-empty neighbour, a non-empty target to clear, or a source footprint
    if (Lane < 27u)
    {
        let Neighbour = WorldBrick + vec3i(i32(Lane % 3u) - 1, i32((Lane / 3u) % 3u) - 1, i32(Lane / 9u) - 1);
        if (ClassifyPriorBrick(Neighbour) && BrickFlags[PriorParity * Smoke.BrickCount + ResolveSmokeBrickSlot(Neighbour, Smoke.BrickExtent)] != 0u)
        {
            atomicOr(&DemandVote, 1u);
        }
    }
    else if (Lane == 27u)
    {
        if (BrickFlags[Smoke.FineParity * Smoke.BrickCount + ResolveSmokeBrickSlot(WorldBrick, Smoke.BrickExtent)] != 0u)
        {
            atomicOr(&DemandVote, 1u);
        }
    }
    else if (Lane == 28u)
    {
        let BrickMinimum = vec3f((WorldBrick * SmokeBrickEdge) + SmokeAnchor) * Smoke.FineCell;
        if (ClassifySourceReach(BrickMinimum, BrickMinimum + vec3f(f32(SmokeBrickEdge) * Smoke.FineCell)))
        {
            atomicOr(&DemandVote, 1u);
        }
    }
    workgroupBarrier();
    if (Lane == 0u)
    {
        DemandValue = atomicLoad(&DemandVote);
    }
    if (workgroupUniformLoad(&DemandValue) == 0u)
    {
        return;
    }

    // ② transport, losses, sources — eight cells per lane
    let Extent = vec3i(Smoke.FineExtent);
    let TopLayer = Extent.z - Extent.z / 4;
    let Losses = vec4f(Smoke.SmokeRemaining, Smoke.HeatRemaining, Smoke.FlameRemaining, Smoke.SootRemaining) * Smoke.Fade;
    let SourceNear = ClassifySourceReach(vec3f((WorldBrick * SmokeBrickEdge) + SmokeAnchor) * Smoke.FineCell - vec3f(0.5),
                                         vec3f((WorldBrick * SmokeBrickEdge) + SmokeAnchor + vec3i(SmokeBrickEdge)) * Smoke.FineCell + vec3f(0.5));
    var Written = false;
    for (var Sub = 0u; Sub < 8u; Sub++)
    {
        let DomainCell = vec3i(Group) * SmokeBrickEdge + vec3i(LaneCell * 2u + vec3u(Sub & 1u, (Sub >> 1u) & 1u, Sub >> 2u));
        let WorldCell = Smoke.FineOrigin + DomainCell;
        var Thermo = vec4f(0.0);
        let Shell = any(DomainCell <= vec3i(0)) || any(DomainCell >= Extent - vec3i(1));
        if (!Shell)
        {
            let Position = (vec3f(WorldCell) + 0.5) * Smoke.FineCell;
            if (SampleSceneDistance(Position).x > -0.5 * Smoke.FineCell)
            {
                let First = ResolveFlow(Position);
                let Middle = ResolveFlow(Position - First * (0.5 * Smoke.Step));
                Thermo = SamplePriorThermo(Position - Middle * Smoke.Step) * Losses;
                if (DomainCell.z >= TopLayer)
                {
                    Thermo *= Smoke.TopFade;
                }
                if (SourceNear)
                {
                    for (var Index = 0u; Index < Smoke.SourceCount; Index++)
                    {
                        let Source = Smoke.Sources[Index];
                        let Weight = ResolveSourceWeight(Source, Position, 0.75 * Smoke.FineCell) * Smoke.Step;
                        Thermo += vec4f(Source.End.w, Source.Velocity.w, Source.Rates.x, Source.Rates.y) * Weight;
                    }
                }
                Thermo = clamp(Thermo, vec4f(0.0), vec4f(16.0, 4000.0, 8.0, 16.0));
                if (Thermo.x < 2.0e-4 && Thermo.z < 1.0e-3 && Thermo.w < 2.0e-4)
                {
                    Thermo = vec4f(0.0);
                }
            }
        }
        textureStore(FineTarget, WrapLattice(WorldCell, Extent), Thermo);
        Written = Written || any(Thermo != vec4f(0.0));
    }
    if (Written)
    {
        atomicOr(&WrittenVote, 1u);
    }
    workgroupBarrier();

    // ③ the brick's flag, and the occupied box the march clips to
    if (Lane == 0u)
    {
        let Occupied = atomicLoad(&WrittenVote);
        BrickFlags[Smoke.FineParity * Smoke.BrickCount + ResolveSmokeBrickSlot(WorldBrick, Smoke.BrickExtent)] = Occupied;
        if (Occupied != 0u)
        {
            let Biased = vec3u(WorldBrick + vec3i(SmokeSummaryBias));
            atomicMin(&Summary[0], Biased.x);
            atomicMin(&Summary[1], Biased.y);
            atomicMin(&Summary[2], Biased.z);
            atomicMax(&Summary[3], Biased.x);
            atomicMax(&Summary[4], Biased.y);
            atomicMax(&Summary[5], Biased.z);
            atomicAdd(&Summary[6], 1u);
        }
    }
}
