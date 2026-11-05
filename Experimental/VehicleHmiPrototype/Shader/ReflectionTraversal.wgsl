//============================================================================================================================================
//                                                         REFLECTIONTRAVERSAL.WGSL
//============================================================================================================================================
// 🧩 Glossy reflection rays: a GGX visible-normal sample per resampling pixel, traced across the screen, then the distance field.

// 📝 The specular counterpart of the indirect candidate, traced the same two-part way: across the screen through the
//    nearest-depth pyramid, then through the scene distance field from the last point the screen proved unoccluded.
//    The ray reads the sample exitance where it lands and the sky where it escapes. Surfaces rougher than
//    RoughnessCutoff are left to the resolve, which substitutes the diffuse irradiance there exactly as Lumen reuses
//    its diffuse gather for rough reflections.

//! include "CardSampling.wgsl"
//! include "ScreenTraversal.wgsl"
//! include "SurfaceReflectance.wgsl"
//! include "PackingCodec.wgsl"
//! include "DiscrepancySequence.wgsl"
//! include "RayTraversal.wgsl"

@group(0) @binding(1) var<storage, read> Materials : array<MaterialRecord>;
@group(0) @binding(2) var SurfaceAttributes        : texture_2d<f32>;
@group(0) @binding(3) var SurfaceDepth             : texture_depth_2d;
@group(0) @binding(4) var ReflectionTarget         : texture_storage_2d<rgba16float, write>;

const RoughnessCutoff : f32 = 0.5;

@compute @workgroup_size(8, 8)
fn TraverseReflection(@builtin(global_invocation_id) Invocation: vec3u)
{
    let TracePixel = Invocation.xy;
    if (any(vec2f(TracePixel) >= Frame.TraceExtent))
    {
        return;
    }
    let ScreenPixel = min(vec2i(TracePixel * Frame.TraceScale), vec2i(Frame.ScreenExtent) - vec2i(1));
    let Depth = textureLoad(SurfaceDepth, ScreenPixel, 0);
    if (Depth <= 0.0)
    {
        textureStore(ReflectionTarget, vec2i(TracePixel), vec4f(0.0));
        return;
    }
    let Attributes = textureLoad(SurfaceAttributes, ScreenPixel, 0);
    let Surface = ProjectSurfaceSample(Materials[u32(Attributes.z + 0.5)]);
    let Roughness = select(Surface.Roughness, Surface.CoatRoughness, Surface.CoatWeight > 0.0);
    let UseRtReflections = FeatureEnabled(Frame, FeatureRayTracing) && FeatureEnabled(Frame, FeatureRayTracedReflections);
    let Cutoff = select(RoughnessCutoff, ResolveReflectionRoughness(Frame), UseRtReflections);
    if (Roughness > Cutoff)
    {
        textureStore(ReflectionTarget, vec2i(TracePixel), vec4f(0.0));
        return;
    }
    let Position = ReconstructWorldPosition(vec2f(ScreenPixel) + 0.5, Depth);
    let Normal = DecodeOctahedral(Attributes.xy);
    let View = normalize(Frame.CameraPosition - Position);
    let Frame3 = ConstructTangentFrame(Normal);
    let TangentView = transpose(Frame3) * View;
    let Pair = ProjectTemporalPair(TracePixel, Frame.FrameIndex, 29u);
    let Half = Frame3 * ProjectVisibleNormal(TangentView, Roughness * Roughness, Pair);
    var Direction = reflect(-View, Half);
    if (dot(Direction, Normal) <= 0.0)
    {
        Direction = reflect(-View, Normal);
    }

    // 📝 Lumen's two-part trace: the screen first (nearest-depth walk, contact detail finer than any voxel or BVH leaf needs), then, where
    //    the screen cannot settle the ray, the BVH multi-bounce tracer when the BVH renderer is active or the scene distance field resumed from the last point
    //    the screen proved unoccluded.
    let Bias = ResolveSurfaceBias(Position);
    var Strike: ScreenHit;
    if (ScreenConsumerEnabled(Frame, ExtendedScreenReflection))
    {
        Strike = TraverseScreenDepth(Position + Normal * ResolveScreenBias(ResolveViewDepth(Position)), Direction, 300.0, ScreenTraversalSteps);
    }
    var Radiance = vec3f(0.0);
    if (Strike.Hit)
    {
        Radiance = ResolveStrikeRadiance(Strike);
    }
    else if (UseRtReflections)
    {
        var Seed = ConstructPixelSeed(TracePixel, Frame.FrameIndex, 41u);
        Radiance = TraceMultiBounceReflection(Position + Normal * RayEpsilon, Direction, Roughness, &Seed);
    }
    else
    {
        let Resume = max(0.5 * Bias, Strike.Travel - Bias);
        let Hit    = TraverseSceneDistance(Position + Normal * Bias, Direction, Resume, 300.0, Roughness * 0.05);
        Radiance   = ResolveContinuedRadiance(Hit, Direction);
    }
    textureStore(ReflectionTarget, vec2i(TracePixel), vec4f(min(Radiance, vec3f(200.0)), 1.0));
}
