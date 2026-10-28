//============================================================================================================================================
//                                                         CLUSTEREDLIGHTS.WGSL
//============================================================================================================================================
// 🧩 3D Clustered Light Grid (16x16x4 cells with 64-bit per-cell light bitmasks) and photometric / IES evaluation for
//    omnidirectional point lights, roadway batwing street lamps, and automotive headlights / floodlights.

const LightTypeOmni       : u32 = 0u;
const LightTypeStreetLamp : u32 = 1u;
const LightTypeLowBeam    : u32 = 2u;
const LightTypeHighBeam   : u32 = 3u;
const LightTypeFog        : u32 = 4u;
const LightTypeFloodlight : u32 = 5u;

const MaxClusterLights    : u32 = 64u;

struct LightRecord
{
    PositionType    : vec4f, // .xyz = Position, .w = LightType
    DirectionReach  : vec4f, // .xyz = Direction, .w = Reach
    ColourIntensity : vec4f, // .xyz = Colour, .w = Intensity
    Params          : vec4f, // .x = InnerCos, .y = OuterCos, .z = IESParam, .w = Active (1.0 or 0.0)
}

struct ClusteredLightBuffer
{
    Lights      : array<LightRecord, 64>, // 64 light records (4,096 bytes)
    GridMinimum : vec4f,                  // .xyz = Minimum, .w = ActiveCount
    GridScale   : vec4f,                  // .xyz = Res / Extent, .w = CullEnabled
    CellMasks   : array<vec4u, 512>,      // 1,024 cells * vec2u (64-bit bitmask per cell)
}

// 📐 O(1) 3D cluster cell index lookup (-1 = empty/outside grid, -2 = culling disabled, 0..1023 = flat cell index).
fn ResolveClusterCellIndex(GridMinimum: vec4f, GridScale: vec4f, Position: vec3f) -> i32
{
    let Count = u32(GridMinimum.w);
    if (Count == 0u)
    {
        return -1;
    }
    if (GridScale.w < 0.5)
    {
        return -2;
    }
    let Rel = (Position - GridMinimum.xyz) * GridScale.xyz;
    if (any(Rel < vec3f(0.0)) || any(Rel >= vec3f(16.0, 16.0, 4.0)))
    {
        return -1;
    }
    let Cell = vec3i(clamp(vec3i(floor(Rel)), vec3i(0), vec3i(15, 15, 3)));
    return (Cell.z * 16 + Cell.y) * 16 + Cell.x;
}

// 📐 Extracts the 64-bit light bitmask (vec2u(lo, hi)) for CellIndex from the corresponding vec4u pair.
fn ResolveClusterMaskFromPair(CellIndex: i32, Pair: vec4u, ActiveCountF: f32) -> vec2u
{
    if (CellIndex == -1)
    {
        return vec2u(0u, 0u);
    }
    if (CellIndex == -2)
    {
        let Count = u32(ActiveCountF);
        let Lo = select(0xFFFFFFFFu, (1u << Count) - 1u, Count < 32u);
        let Hi = select(0u, select(0xFFFFFFFFu, (1u << (Count - 32u)) - 1u, Count < 64u), Count > 32u);
        return vec2u(Lo, Hi);
    }
    let Flat = u32(CellIndex);
    return select(Pair.xy, Pair.zw, (Flat & 1u) != 0u);
}

// 📐 Analytical IES photometric distribution profile based on polar angle cos(theta) from the light's primary axis.
fn EvaluateLightIES(CosAngle: f32, LightType: u32, InnerCos: f32, OuterCos: f32, ToSurface: vec3f, LightDir: vec3f) -> f32
{
    if (LightType == LightTypeOmni)
    {
        return 1.0;
    }

    if (LightType == LightTypeStreetLamp)
    {
        // Roadway batwing IES distribution (Type II / III cobrahead): peak intensity around 50-65 degrees off nadir
        if (CosAngle <= 0.05)
        {
            return 0.0;
        }
        let SinAngle = sqrt(max(0.0, 1.0 - CosAngle * CosAngle));
        let Batwing  = 0.38 + 1.35 * pow(SinAngle, 1.6) * smoothstep(0.05, 0.38, CosAngle);
        return Batwing * smoothstep(0.05, 0.22, CosAngle);
    }

    let SpotFactor = smoothstep(OuterCos, InnerCos, CosAngle);
    if (SpotFactor <= 0.0)
    {
        return 0.0;
    }

    if (LightType == LightTypeLowBeam)
    {
        // ECE/SAE sharp horizontal glare cutoff at Z = -0.02
        let Cutoff  = smoothstep(0.06, -0.04, ToSurface.z);
        let HotSpot = 1.0 + 1.4 * pow(max(0.0, CosAngle), 12.0);
        return SpotFactor * Cutoff * HotSpot;
    }

    if (LightType == LightTypeHighBeam)
    {
        let PencilCore = 1.0 + 2.8 * pow(max(0.0, CosAngle), 24.0);
        return SpotFactor * PencilCore;
    }

    if (LightType == LightTypeFog)
    {
        let VertDist = abs(ToSurface.z + 0.04);
        let BandMask = exp(-VertDist * VertDist * 28.0);
        return SpotFactor * (0.35 + 1.8 * BandMask);
    }

    if (LightType == LightTypeFloodlight)
    {
        let Lobe1 = pow(max(0.0, CosAngle), 4.0);
        let Lobe2 = smoothstep(OuterCos, mix(OuterCos, InnerCos, 0.5), CosAngle);
        return 0.65 * Lobe2 + 0.85 * Lobe1;
    }

    return SpotFactor;
}

// 📐 Incident illuminance arriving at SurfacePos from Light (before NdotL, matching SunIlluminance and Fire.Illuminance).
fn EvaluateLightIlluminance(Light: LightRecord, SurfacePos: vec3f, SurfaceNormal: vec3f) -> vec3f
{
    if (Light.Params.w < 0.5)
    {
        return vec3f(0.0);
    }

    let LightPos = Light.PositionType.xyz;
    let Offset   = LightPos - SurfacePos;
    let DistSq   = dot(Offset, Offset);
    let Reach    = Light.DirectionReach.w;
    if (DistSq >= Reach * Reach)
    {
        return vec3f(0.0);
    }

    let Dist     = sqrt(max(DistSq, 1.0e-4));
    let LightDir = Offset / Dist;
    if (dot(SurfaceNormal, LightDir) <= 0.0)
    {
        return vec3f(0.0);
    }

    let NormDist2 = DistSq / max(Reach * Reach, 1.0e-4);
    let Window    = max(0.0, 1.0 - NormDist2 * NormDist2);
    let Falloff   = (Window * Window) / (DistSq + 1.25);

    let ToSurface = -LightDir;
    let Axis      = normalize(Light.DirectionReach.xyz);
    let CosAngle  = dot(ToSurface, Axis);

    let Type      = u32(Light.PositionType.w);
    let InnerCos  = Light.Params.x;
    let OuterCos  = Light.Params.y;
    let IES       = EvaluateLightIES(CosAngle, Type, InnerCos, OuterCos, ToSurface, Axis);
    if (IES <= 0.0)
    {
        return vec3f(0.0);
    }

    let Colour    = Light.ColourIntensity.xyz;
    let Intensity = Light.ColourIntensity.w;
    return Colour * (Intensity * Falloff * IES);
}

// 📐 Lambertian surface irradiance (Illuminance * NdotL) for surface card direct injection (CardDirect.wgsl).
fn EvaluateLightArrival(Light: LightRecord, SurfacePos: vec3f, SurfaceNormal: vec3f) -> vec3f
{
    let Illuminance = EvaluateLightIlluminance(Light, SurfacePos, SurfaceNormal);
    if (all(Illuminance <= vec3f(0.0)))
    {
        return vec3f(0.0);
    }
    let LightDir = normalize(Light.PositionType.xyz - SurfacePos);
    return Illuminance * max(dot(SurfaceNormal, LightDir), 0.0);
}
