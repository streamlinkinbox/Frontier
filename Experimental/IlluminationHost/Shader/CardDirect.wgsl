//============================================================================================================================================
//                                                             CARDDIRECT.WGSL
//============================================================================================================================================
// 🧩 Direct sun irradiance on every scheduled card texel, shadowed by a penumbra traversal of the scene distance field.

//! include "FrameDescriptor.wgsl"
//! include "DistanceTraversal.wgsl"
//! include "CardProjection.wgsl"
//! include "CardIrradiance.wgsl"
//! include "FireIllumination.wgsl"

@group(0) @binding(1) var<storage, read> Cards     : array<CardRecord>;
@group(0) @binding(2) var<storage, read> Occupants : array<OccupantRecord>;
@group(0) @binding(3) var<storage, read> Tiles     : array<vec2u>;
@group(0) @binding(4) var<uniform> Work            : CardWorkRecord;
@group(0) @binding(5) var CardNormal               : texture_2d<f32>;
@group(0) @binding(6) var CardDepth                : texture_2d<f32>;
@group(0) @binding(7) var CardDirectTarget         : texture_storage_2d<rgba16float, write>;

@compute @workgroup_size(8, 8)
fn IntegrateCardDirect(@builtin(workgroup_id) Group: vec3u, @builtin(local_invocation_id) Local: vec3u)
{
    let WorkIndex = ResolveWorkIndex(Group);
    if (WorkIndex >= Work.TileCount)
    {
        return;
    }
    let Assignment = Tiles[WorkIndex];
    let Card = Cards[Assignment.x];
    let TileTexel = ResolveTileTexel(Assignment, Local.xy);
    if (any(TileTexel >= Card.TexelExtent))
    {
        return;
    }
    let Texel = vec2i(Card.TexelOrigin + TileTexel);
    let Depth = textureLoad(CardDepth, Texel, 0).x;
    if (Depth > 1.5)
    {
        textureStore(CardDirectTarget, Texel, vec4f(0.0));
        return;
    }
    let Occupant    = Occupants[Card.Occupant];
    let Coordinates = vec3f((vec2f(TileTexel) + 0.5) / vec2f(Card.TexelExtent), Depth);
    let Position    = (Occupant.LocalToWorld * vec4f(ReconstructCardLocal(Card, Coordinates), 1.0)).xyz;
    let LocalNormal = textureLoad(CardNormal, Texel, 0).xyz * 2.0 - 1.0;
    let Normal      = normalize((Occupant.LocalToWorld * vec4f(LocalNormal, 0.0)).xyz);
    let Cosine      = dot(Normal, Frame.SunDirection);
    var Irradiance  = vec3f(0.0);
    if (Cosine > 0.0 && any(Frame.SunIlluminance > vec3f(0.0)))
    {
        let Bias = ResolveSurfaceBias(Position);
        var Visibility = 1.0;
        if (FeatureEnabled(Frame, FeatureDirectShadow))
        {
            Visibility = TraverseSceneShadow(Position + Normal * Bias, Frame.SunDirection, 0.5 * Bias, 400.0, 6.0);
        }
        Irradiance = Frame.SunIlluminance * Cosine * Visibility;
    }
    let Fire = ResolveFireArrival(Position, Normal);
    Irradiance += Fire.Illuminance * max(dot(Normal, Fire.Direction), 0.0);
    textureStore(CardDirectTarget, Texel, vec4f(Irradiance, 1.0));
}
