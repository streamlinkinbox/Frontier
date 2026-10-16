//============================================================================================================================================
//                                                         SMOKEILLUMINATION.WGSL
//============================================================================================================================================
// 🧩 The light each 0.5 m smoke cell receives, once per tick: sun through the scene and through the smoke, sky, ground bounce, fire.

// 📝 Frontier's viewport marched up to sixteen shadow taps for every march sample of every pixel. Here the light is
//    resolved per coarse cell, only in bricks with smoke in or beside them, and the march reads it with one filtered
//    tap. That moves the cost from pixels × samples × taps to occupied cells × taps, and it lets the light see the
//    scene: the sun term traces the ONE scene distance field, so smoke inside the tunnel or in the lee of a wall is
//    in shadow, and the sky term is occluded by whatever stands above the cell. A burning tyre lights its own plume.
//
//    Texel: rgb = ambient + fire radiance arriving at the cell, a = sun visibility (scene × smoke transmittance).

//! include "SmokeLattice.wgsl"

@group(3) @binding(0) var FineLatest                 : texture_3d<f32>;
@group(3) @binding(1) var LightTarget                : texture_storage_3d<rgba16float, write>;
@group(3) @binding(2) var<storage, read> BrickFlags  : array<u32>;

var<workgroup> DemandVote  : atomic<u32>;
var<workgroup> DemandValue : u32;

fn SampleLatestDensity(Position: vec3f) -> f32
{
    return textureSampleLevel(FineLatest, RepeatSampler, Position / (vec3f(Smoke.FineExtent) * Smoke.FineCell), 0.0).x;
}

fn SampleSky(Direction: vec3f) -> vec3f
{
    return textureSampleLevel(SkyView, SkySampler, ProjectSkyCoordinates(Direction), 0.0).rgb * Smoke.AmbientScale;
}

@compute @workgroup_size(4, 4, 4)
fn IntegrateLight(@builtin(workgroup_id) Group: vec3u, @builtin(local_invocation_id) LaneCell: vec3u,
                  @builtin(local_invocation_index) Lane: u32)
{
    let WorldBrick = ResolveWorldBrick(Smoke.FineOrigin) + vec3i(Group);
    if (Lane == 0u)
    {
        atomicStore(&DemandVote, 0u);
    }
    workgroupBarrier();
    if (Lane < 27u)
    {
        let Neighbour = WorldBrick + vec3i(i32(Lane % 3u) - 1, i32((Lane / 3u) % 3u) - 1, i32(Lane / 9u) - 1);
        let Local = Neighbour - ResolveWorldBrick(Smoke.FineOrigin);
        if (all(Local >= vec3i(0)) && all(Local < vec3i(Smoke.BrickExtent))
            && BrickFlags[Smoke.FineParity * Smoke.BrickCount + ResolveSmokeBrickSlot(Neighbour, Smoke.BrickExtent)] != 0u)
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

    // 📝 A fine brick spans 4³ coarse cells, one per lane.
    let WorldCell = (WorldBrick * SmokeBrickEdge + SmokeAnchor) / 2 + vec3i(LaneCell);
    let DomainCell = WorldCell - ResolveCoarseOrigin();
    let Texel = WrapLattice(WorldCell, ResolveCoarseExtent());
    if (ClassifyCoarseShell(DomainCell))
    {
        textureStore(LightTarget, Texel, vec4f(0.0));
        return;
    }
    let Position = ResolveCoarseCentre(WorldCell);

    // ① sun (or moon): soft scene shadow through the distance field, then the smoke's own transmittance toward it
    var Sun = 0.0;
    if (any(Smoke.SunIlluminance > vec3f(0.0)))
    {
        let Scene = TraverseSceneShadow(Position, Smoke.SunDirection, 0.05, 60.0, 4.0);
        var Depth = 0.0;
        if (Scene > 0.0)
        {
            for (var Step = 1u; Step <= 10u; Step++)
            {
                Depth += SampleLatestDensity(Position + Smoke.SunDirection * (f32(Step) * 0.45)) * 0.45;
            }
        }
        Sun = Scene * exp(-Depth * Smoke.Extinction);
    }

    // ② sky dome: zenith and four directions 35° up, occluded by geometry above the cell and by smoke above it
    let Sky = (SampleSky(vec3f(0.0, 0.0, 1.0)) + SampleSky(vec3f(0.819, 0.0, 0.574)) + SampleSky(vec3f(-0.819, 0.0, 0.574))
             + SampleSky(vec3f(0.0, 0.819, 0.574)) + SampleSky(vec3f(0.0, -0.819, 0.574))) * 0.2;
    let Overhead = TraverseSceneDistance(Position, vec3f(0.0, 0.0, 1.0), 0.05, 8.0, 0.2);
    let Openness = select(1.0, clamp(Overhead.Travel / 8.0, 0.0, 1.0), Overhead.Hit);
    var Above = 0.0;
    for (var Step = 1u; Step <= 4u; Step++)
    {
        Above += SampleLatestDensity(Position + vec3f(0.0, 0.0, f32(Step) * 0.5)) * 0.5;
    }
    // 📝 A thick column has a dark belly: the sky seen through the smoke above falls to under a fifth (the sibling
    //    Plume's 0.18 + 0.82·T^0.6 of the zenith transmittance).
    let SkyShare = mix(0.2, 1.0, Openness) * (0.18 + 0.82 * pow(exp(-Above * Smoke.Extinction), 0.6));

    // ③ the sunlit ground below bounces a warm share back up (course albedo ≈ 0.35, Lambertian)
    let Bounce = Smoke.SunIlluminance * max(Smoke.SunDirection.z, 0.0) * (0.35 / 3.14159265) * Openness * 0.5;

    // ④ burning tyres, an inverse-square point light each
    var Fire = vec3f(0.0);
    for (var Index = 0u; Index < Smoke.FireCount; Index++)
    {
        let Toward = Smoke.FireLights[Index].xyz - Position;
        Fire += Smoke.FireColour.rgb * Smoke.FireLights[Index].w / max(dot(Toward, Toward), 0.35);
    }
    textureStore(LightTarget, Texel, vec4f(Sky * SkyShare + Bounce + Fire, Sun));
}
