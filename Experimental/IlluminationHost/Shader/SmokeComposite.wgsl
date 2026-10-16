//============================================================================================================================================
//                                                            SMOKECOMPOSITE.WGSL
//============================================================================================================================================
// 🧩 Joint-bilateral upsampling of the marched smoke onto every screen pixel, composed over the resolved scene radiance.

// 📝 Composition happens after temporal antialiasing, into its own target: the resolved history the next frame
//    reprojects never contains smoke, so smoke that moves through the world cannot smear into the opaque history.
//    Exposure and the present pass read the composed target only on frames where smoke is resident.

//! include "FrameDescriptor.wgsl"
//! include "SmokeRecords.wgsl"

@group(0) @binding(1) var<uniform> View         : SmokeView;
@group(0) @binding(2) var ResolvedRadiance      : texture_2d<f32>;
@group(0) @binding(3) var MarchRadiance         : texture_2d<f32>;
@group(0) @binding(4) var MarchDepth            : texture_2d<f32>;
@group(0) @binding(5) var SurfaceDepth          : texture_depth_2d;
@group(0) @binding(6) var ComposedTarget        : texture_storage_2d<rgba16float, write>;

@compute @workgroup_size(8, 8)
fn ComposeSmoke(@builtin(global_invocation_id) Invocation: vec3u)
{
    let Pixel = vec2i(Invocation.xy);
    if (any(vec2f(Pixel) >= Frame.ScreenExtent))
    {
        return;
    }
    let Scene = textureLoad(ResolvedRadiance, Pixel, 0);
    let Depth = textureLoad(SurfaceDepth, Pixel, 0);
    var ViewDepth = 1.0e4;
    if (Depth > 0.0)
    {
        ViewDepth = ResolveViewDepth(ReconstructWorldPosition(vec2f(Pixel) + 0.5, Depth));
    }
    // 📐 A 3×3 joint-bilateral Gaussian over the march texels around this pixel (σ = 0.85 march texel), each tap
    //    weighted by how well its recorded depth agrees with this pixel's; it hides the march's fixed per-pixel offset
    //    pattern. The nearest-depth tap backs up an edge where every weight collapses.
    let Scale = f32(View.MarchScale);
    let Continuous = (vec2f(Pixel) + 0.5) / Scale - 0.5;
    let Centre = vec2i(floor(Continuous + 0.5));
    let Highest = vec2i(View.MarchExtent) - vec2i(1);
    var Sum = vec4f(0.0);
    var WeightSum = 0.0;
    var Nearest = vec4f(0.0, 0.0, 0.0, 1.0);
    var NearestScore = 1.0e30;
    for (var Tap = 0; Tap < 9; Tap++)
    {
        let Texel = clamp(Centre + vec2i(Tap % 3 - 1, Tap / 3 - 1), vec2i(0), Highest);
        let Offset = vec2f(Texel) - Continuous;
        let Spatial = exp(-dot(Offset, Offset) / (2.0 * 0.85 * 0.85));
        let TapDepth = textureLoad(MarchDepth, Texel, 0).x;
        let Difference = abs(TapDepth - ViewDepth);
        let Agreement = exp(-Difference / (0.03 * min(TapDepth, ViewDepth) + 0.05));
        let Sample = textureLoad(MarchRadiance, Texel, 0);
        let Weight = Spatial * Agreement;
        Sum += Sample * Weight;
        WeightSum += Weight;
        if (Difference < NearestScore)
        {
            NearestScore = Difference;
            Nearest = Sample;
        }
    }
    let Smoke = select(Nearest, Sum / max(WeightSum, 1.0e-6), WeightSum > 1.0e-3);
    textureStore(ComposedTarget, Pixel, vec4f(Scene.rgb * Smoke.a + Smoke.rgb, 1.0));
}
