// 64-byte local light shared by cluster building, primary shading, mirrors and
// card relighting. Point: Outer=-1, Profile=0. Spot: Axis points FROM the
// fixture, Up orients its LM-63 C0 plane; inner/outer cosines feather the cone.
struct LocalLight
{
    PositionReach : vec4f,  // xyz [m], w cutoff radius [m]
    AxisInner     : vec4f,  // xyz unit optical axis, w inner cosine
    UpOuter       : vec4f,  // xyz C0 orientation, w outer cosine (-1 for point)
    ColourProfile : vec4f,  // rgb intensity [engine cd], w integer profile id
}

struct ClusterSettings
{
    TileCount    : vec2u,
    LightCount   : u32,
    SliceCount   : u32,
    ScreenExtent : vec2f,
    Tangents     : vec2f, // perspective half-FOV tangents (x, y)
    DepthRange   : vec2f, // near and far in positive view metres
    LogScale     : f32,   // slices / ln(far/near)
    Padding      : f32,
}
