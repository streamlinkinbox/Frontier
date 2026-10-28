//============================================================================================================================================
//                                                           DIRECTRADIANCE.WGSL
//============================================================================================================================================
// 🧩 Direct sun radiance and clustered lights at every primary surface, cascaded shadow depth, and sky background.

//! include "FrameDescriptor.wgsl"
//! include "CelestialSky.wgsl"
//! include "SurfaceReflectance.wgsl"
//! include "PackingCodec.wgsl"
//! include "SkyProjection.wgsl"
//! include "FireIllumination.wgsl"
//! include "ClusteredLights.wgsl"
//! include "RayTraversal.wgsl"

struct CascadeReceiver
{
    LightViewProjection : array<mat4x4f, 3>,
    Splits              : vec4f,   // [m]   xyz far distance of each cascade, w cascade count
    TexelWorld          : vec4f,   // [m]   world size of one shadow texel per cascade
}

@group(0) @binding(1) var<storage, read> Materials     : array<MaterialRecord>;
@group(0) @binding(2) var SurfaceAttributes            : texture_2d<f32>;
@group(0) @binding(3) var SurfaceDepth                 : texture_depth_2d;
@group(0) @binding(4) var ShadowDepth                  : texture_depth_2d_array;
@group(0) @binding(5) var ShadowSampler                : sampler_comparison;
@group(0) @binding(6) var<uniform> Receiver            : CascadeReceiver;
@group(0) @binding(7) var DirectSkyView                : texture_2d<f32>;
@group(0) @binding(8) var DirectSkySampler             : sampler;
@group(0) @binding(9)  var DirectTarget       : texture_storage_2d<rgba16float, write>;
@group(0) @binding(10) var<storage, read> SceneLights : ClusteredLightBuffer;

@group(0) @binding(11) var<uniform> Shadow:CloudShadowParameters;
@group(0) @binding(12) var CloudShadowMap:texture_2d<f32>;
@group(0) @binding(13) var CloudShadowSampler:sampler;
//! include "CloudShadowSample.wgsl"

// 📐 3×3 percentage-closer taps, each a hardware bilinear comparison, after a normal offset of 1.5 texels.
fn ResolveCascadeVisibility(Position: vec3f, Normal: vec3f) -> f32
{
    let ViewDistance = ResolveViewDepth(Position);
    let Count = u32(Receiver.Splits.w);
    var Cascade = 0u;
    while (Cascade + 1u < Count && ViewDistance > Receiver.Splits[Cascade])
    {
        Cascade++;
    }
    if (ViewDistance > Receiver.Splits[Count - 1u])
    {
        return 1.0;
    }
    let Offset = Position + Normal * (1.5 * Receiver.TexelWorld[Cascade]);
    let Clip = Receiver.LightViewProjection[Cascade] * vec4f(Offset, 1.0);
    let Ndc = Clip.xyz / Clip.w;
    let Coordinates = vec2f(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5);
    if (any(Coordinates < vec2f(0.0)) || any(Coordinates > vec2f(1.0)) || Ndc.z > 1.0)
    {
        return 1.0;
    }
    let Texel = 1.0 / vec2f(textureDimensions(ShadowDepth).xy);
    var Lit = 0.0;
    for (var Y = -1; Y <= 1; Y++)
    {
        for (var X = -1; X <= 1; X++)
        {
            Lit += textureSampleCompareLevel(ShadowDepth, ShadowSampler, Coordinates + vec2f(f32(X), f32(Y)) * Texel, Cascade, Ndc.z);
        }
    }
    return Lit / 9.0;
}

fn ResolveBackgroundRadiance(Direction: vec3f) -> vec3f
{
    var Radiance = textureSampleLevel(DirectSkyView, DirectSkySampler, ProjectSkyCoordinates(Direction), 0.0).rgb * Frame.SkyScale
                 + Frame.NightSkyRadiance;
    Radiance += CelestialSky(Direction, MoonAtlas, SkySampler);
    return Radiance;
}

@compute @workgroup_size(8, 8)
fn IntegrateDirectRadiance(@builtin(global_invocation_id) Invocation: vec3u)
{
    let Pixel = Invocation.xy;
    if (any(vec2f(Pixel) >= Frame.ScreenExtent))
    {
        return;
    }
    let Depth = textureLoad(SurfaceDepth, vec2i(Pixel), 0);
    let Centre = vec2f(Pixel) + 0.5;
    if (Depth <= 0.0)
    {
        let Far = ReconstructWorldPosition(Centre, 1.0e-6);
        textureStore(DirectTarget, vec2i(Pixel), vec4f(ResolveBackgroundRadiance(normalize(Far - Frame.CameraPosition)), 0.0));
        return;
    }
    let Position     = ReconstructWorldPosition(Centre, Depth);
    let Attributes   = textureLoad(SurfaceAttributes, vec2i(Pixel), 0);
    let Normal       = DecodeOctahedral(Attributes.xy);
    let Surface      = ProjectSurfaceSample(Materials[u32(Attributes.z + 0.5)]);
    let View         = normalize(Frame.CameraPosition - Position);
    let UseRtShadows = FeatureEnabled(Frame, FeatureRayTracing) && FeatureEnabled(Frame, FeatureRayTracedShadows);
    let RayOrigin    = Position + Normal * RayEpsilon;

    var Visibility = 1.0;
    var SunDir     = Frame.SunDirection;
    if (FeatureEnabled(Frame, FeatureDirectShadow))
    {
        if (UseRtShadows)
        {
            SunDir     = SampleSunConeDirection(Frame.SunDirection, ProjectTemporalPair(Pixel, Frame.FrameIndex, 17u));
            Visibility = TraceSceneVisibility(RayOrigin, SunDir, RayEpsilon, 400.0);
        }
        else
        {
            Visibility = ResolveCascadeVisibility(Position, Normal);
        }
    }
    Visibility *= CloudGroundTransmission(Position);
    var Radiance = Frame.SunIlluminance * Visibility * ResolveDirectReflectance(Surface, Normal, View, SunDir);
    let Fire = ResolveFireArrival(Position, Normal);
    if (any(Fire.Illuminance > vec3f(0.0)))
    {
        Radiance += Fire.Illuminance * ResolveDirectReflectance(Surface, Normal, View, Fire.Direction);
    }

    // Live HMI Display Rectangular Area-Light NEE (Tablet + Instrument Cluster) with BVH shadow visibility
    let HmiPair = ProjectTemporalPair(Pixel, Frame.FrameIndex, 43u);
    Radiance += EvaluateHmiAreaLightNee(Position, Normal, View, Surface, HmiPair, UseRtShadows) * InversePi;

    // 3D Clustered light evaluations (bitmask-culled point lights, roadway IES street lamps, and vehicle beams)
    let CellIdx = ResolveClusterCellIndex(SceneLights.GridMinimum, SceneLights.GridScale, Position);
    if (CellIdx != -1)
    {
        let Pair  = SceneLights.CellMasks[u32(max(CellIdx, 0)) >> 1u];
        let Mask2 = ResolveClusterMaskFromPair(CellIdx, Pair, SceneLights.GridMinimum.w);
        if ((Mask2.x | Mask2.y) != 0u)
        {
            var ClusterRad  = vec3f(0.0);
            var BestContrib = vec3f(0.0);
            var BestDir     = vec3f(0.0, 0.0, 1.0);
            var BestDist    = 0.0;
            var BestLum     = 0.0;

            for (var Word = 0u; Word < 2u; Word += 1u)
            {
                var Mask = select(Mask2.x, Mask2.y, Word == 1u);
                let Base = Word * 32u;
                while (Mask != 0u)
                {
                    let Bit   = firstTrailingBit(Mask);
                    Mask     &= Mask - 1u;
                    let Light = SceneLights.Lights[Base + Bit];
                    let LightIllum = EvaluateLightIlluminance(Light, Position, Normal);
                    if (any(LightIllum > vec3f(0.0)))
                    {
                        let Offset      = Light.PositionType.xyz - Position;
                        let LightDist   = length(Offset);
                        let TowardLight = Offset / max(LightDist, 1.0e-4);
                        let Contrib     = LightIllum * ResolveDirectReflectance(Surface, Normal, View, TowardLight);
                        ClusterRad     += Contrib;
                        let Lum         = dot(Contrib, vec3f(0.2126, 0.7152, 0.0722));
                        if (Lum > BestLum)
                        {
                            BestLum     = Lum;
                            BestContrib = Contrib;
                            BestDir     = TowardLight;
                            BestDist    = LightDist;
                        }
                    }
                }
            }

            let SunLum = dot(Frame.SunIlluminance, vec3f(0.2126, 0.7152, 0.0722));
            if (UseRtShadows && BestLum > 0.04 + 0.015 * SunLum)
            {
                let LightVis = TraceSceneVisibility(RayOrigin, BestDir, RayEpsilon, max(BestDist - 0.65, RayEpsilon));
                ClusterRad  += BestContrib * (LightVis - 1.0);
            }
            Radiance += ClusterRad;
        }
    }

    textureStore(DirectTarget, vec2i(Pixel), vec4f(Radiance, 1.0));
}
