//============================================================================================================================================
//                                                           INSPECTIONMARCH.WGSL
//============================================================================================================================================
// 🧩 Primary rays through the scene distance field alone — the field, its stride cost, and the cards as the tracers see them.

// 📝 DistanceInspection selects what each camera ray reports:
//    1  surface normal of the field, tinted by the cascade that answered, with bricks rewritten in the last
//       half-second glowing — the field updating around the moving vehicle is visible here directly;
//    2  traversal steps as a heat ramp;
//    3  card radiance at the hit (Lumen's "surface cache" view): the scene exactly as every indirect ray sees it.

//! include "CardSampling.wgsl"
//! include "RayTraversal.wgsl"

@group(0) @binding(1) var InspectionTarget : texture_storage_2d<rgba16float, write>;

const CascadeTints : array<vec3f, 5> = array<vec3f, 5>(vec3f(1.0, 0.92, 0.80), vec3f(0.70, 0.90, 1.0),
                                                      vec3f(0.78, 1.0, 0.72), vec3f(1.0, 0.75, 0.95), vec3f(0.6, 0.6, 0.6));

fn ProjectHeat(Fraction: f32) -> vec3f
{
    let t = clamp(Fraction, 0.0, 1.0);
    return clamp(vec3f(1.5 * t, 1.5 * t - 0.4, 0.6 - 1.2 * t) + vec3f(0.0, 0.0, 0.3 * (1.0 - t)), vec3f(0.0), vec3f(1.0));
}

fn ResolveHitBrickAge(Hit: TraversalHit) -> vec2f
{
    if (Hit.Level >= Distance.ClipCount)
    {
        return vec2f(1.0e9, 0.0);
    }
    let Voxel = vec3i(floor(Hit.Position / Distance.Clips[Hit.Level].VoxelExtent));
    let Brick = vec3i(floor(vec3f(Voxel) / f32(BrickEdge)));
    return ResolveBrickAge(BrickEpochs[ResolveBrickSlot(Hit.Level, Brick, Distance.BricksPerAxis)], Frame.FrameIndex);
}

@compute @workgroup_size(8, 8)
fn MarchInspection(@builtin(global_invocation_id) Invocation: vec3u)
{
    let Pixel = Invocation.xy;
    if (any(vec2f(Pixel) >= Frame.ScreenExtent))
    {
        return;
    }
    let Far = ReconstructWorldPosition(vec2f(Pixel) + 0.5, 1.0e-6);
    let Direction = normalize(Far - Frame.CameraPosition);
    var Colour = vec3f(0.0);

    if (FeatureEnabled(Frame, FeatureRayTracing))
    {
        let RtHit = TraceSceneRay(Frame.CameraPosition, Direction, 0.01, 600.0);
        switch (ResolveDistanceInspectionMode(Frame))
        {
            case 2u:
            {
                Colour = ProjectHeat(f32(RtHit.Steps) / 96.0);
            }
            case 3u:
            {
                if (RtHit.Hit)
                {
                    var Seed = ConstructPixelSeed(Pixel, Frame.FrameIndex, 53u);
                    let Surf = ProjectSurfaceSample(RayMaterials[RtHit.Family]);
                    Colour   = Surf.Emission * Frame.EmissionScale
                             + EvaluateDirectNextEvent(RtHit.Position, RtHit.Normal, -Direction, Surf, &Seed, true);
                }
                else
                {
                    Colour = SampleSkyRadiance(Direction);
                }
            }
            default:
            {
                if (RtHit.Hit)
                {
                    let Surf  = ProjectSurfaceSample(RayMaterials[RtHit.Family]);
                    let Shade = 0.35 + 0.65 * max(dot(RtHit.Normal, normalize(vec3f(0.4, 0.3, 0.85))), 0.0);
                    Colour = mix(Surf.Albedo, RtHit.Normal * 0.5 + 0.5, 0.55) * Shade;
                    if (RtHit.Identity < arrayLength(&Occupants))
                    {
                        let Occ = Occupants[RtHit.Identity];
                        if ((Occ.Roles & RoleDynamic) != 0u)
                        {
                            Colour = mix(Colour, vec3f(0.25, 0.90, 0.70), 0.30);
                        }
                    }
                    let Lattice = fract(RtHit.Position * 2.0);
                    let Edge = min(min(min(Lattice.x, 1.0 - Lattice.x), min(Lattice.y, 1.0 - Lattice.y)), min(Lattice.z, 1.0 - Lattice.z));
                    Colour *= mix(0.84, 1.0, smoothstep(0.0, 0.05, Edge));
                }
                else
                {
                    Colour = mix(vec3f(0.10, 0.11, 0.14), vec3f(0.22, 0.26, 0.34), clamp(Direction.z * 2.0 + 0.5, 0.0, 1.0));
                }
            }
        }
        textureStore(InspectionTarget, vec2i(Pixel), vec4f(Colour, 1.0));
        return;
    }

    let Hit = TraverseSceneDistance(Frame.CameraPosition, Direction, 0.0, 600.0, 0.0);
    switch (ResolveDistanceInspectionMode(Frame))
    {
        case 2u:
        {
            Colour = ProjectHeat(f32(Hit.Steps) / f32(MaximumTraversalSteps));
        }
        case 3u:
        {
            Colour = ResolveTraversalRadiance(Hit, Direction);
        }
        default:
        {
            if (Hit.Hit)
            {
                let Shade = 0.35 + 0.65 * max(dot(Hit.Normal, normalize(vec3f(0.4, 0.3, 0.85))), 0.0);
                Colour = (Hit.Normal * 0.5 + 0.5) * CascadeTints[min(Hit.Level, 4u)] * Shade;
                let Age = ResolveHitBrickAge(Hit);
                if (Age.y > 0.5 && Age.x < 24.0)
                {
                    Colour = mix(Colour, vec3f(1.0, 0.15, 0.85), 0.7 * (1.0 - Age.x / 24.0));
                }
                else if (Age.x < 6.0)
                {
                    Colour = mix(Colour, vec3f(0.2, 0.85, 0.9), 0.3 * (1.0 - Age.x / 6.0));
                }
                // 📝 Voxel lattice lines of the answering cascade, so its resolution reads at a glance.
                let Lattice = fract(Hit.Position / Distance.Clips[min(Hit.Level, Distance.ClipCount - 1u)].VoxelExtent);
                let Edge = min(min(min(Lattice.x, 1.0 - Lattice.x), min(Lattice.y, 1.0 - Lattice.y)), min(Lattice.z, 1.0 - Lattice.z));
                Colour *= mix(0.82, 1.0, smoothstep(0.0, 0.06, Edge));
            }
            else
            {
                Colour = mix(vec3f(0.10, 0.11, 0.14), vec3f(0.22, 0.26, 0.34), clamp(Direction.z * 2.0 + 0.5, 0.0, 1.0));
            }
        }
    }
    textureStore(InspectionTarget, vec2i(Pixel), vec4f(Colour, 1.0));
}
