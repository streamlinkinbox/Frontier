//============================================================================================================================================
//                                                          LIGHTCLUSTERSPACE.WGSL
//============================================================================================================================================
// 🧩 Light records binned into a world-space grid: each cell's 32-bit mask names the lights whose reach touches it, and each
//    light's arrival is its beam profile with inverse-square falloff.

// 📝 Includer declares SceneLights as the LightClusterRecords storage buffer (LightClusterSpace.js lays it out).

struct LightRecord
{
    PositionType    : vec4f, // xyz = world position, w = light type (0: omni, 1: street lamp, 2: low beam, 3: high beam, 4: fog, 5: flood)
    DirectionReach  : vec4f, // xyz = forward direction, w = reach radius [m]
    ColourIntensity : vec4f, // rgb = linear colour, w = intensity
    Params          : vec4f, // x = inner angle cos, y = outer angle cos, z = profile variant, w = active
}

// 📝 The photometric atlas: 7 Type C profiles (IES LM-63, Photometry.js) of PhotometryAzimuthBins × PhotometryPolarBins bytes each, peak = 255, four to a u32.
const PhotometryAzimuthBins      : u32 = 37u;    // 10° steps of C-plane around the optical axis
const PhotometryPolarBins        : u32 = 91u;    // 2° steps from the axis
const PhotometryTexelsPerProfile : u32 = 3367u;

// 📝 The emissive region (EmissiveLightSpace.js): emissive triangles per facet set in local space, one record per cluster instance, and per grid cell eight cluster ids.
struct EmissiveTriangleRecord
{
    A  : vec4f,   // xyz local corner A
    E1 : vec4f,   // xyz edge B − A, w the local area
    E2 : vec4f,   // xyz edge C − A, w the material family (bitcast u32)
}

struct EmissiveClusterRecord
{
    CentreReach : vec4f,   // xyz world centre, w radius
    Meta        : vec4u,   // x occupant, y first triangle, z triangle count, w power (bitcast f32; area × luminance, world)
}

struct LightClusterRecords
{
    Minimum   : vec4f,            // xyz world minimum corner of the grid, w = 1 once the photometric atlas is uploaded
    Extent    : vec4f,            // xyz world size of the grid, w unused
    Lights    : array<LightRecord, 32>,
    CellMasks : array<vec4u, 256>, // 16 × 16 × 4 cells, x fastest: bit i of cell c is bit (c & 3) of lane CellMasks[c >> 2]; vec4u keeps the
                                   // record legal as a UNIFORM too (array strides there are multiples of 16), with the memory of array<u32, 1024>
    Atlas     : array<vec4u, 1474>, // 7 photometric profiles, one byte per texel (23 569 texels, padded to a vec4u)
    EmissiveTriangles : array<EmissiveTriangleRecord, 160>,
    EmissiveClusters  : array<EmissiveClusterRecord, 224>,
    EmissiveCells     : array<vec4u, 1024>,   // per cell eight u16 cluster ids, the strongest first (0x7FFF empty)
}

// 📝 Extent.w holds the number of cluster instances in the table, 0 when the emissive structure is off.

const LightClusterResolution : vec3u = vec3u(16u, 16u, 4u);

// 📝 Points outside the grid take every light: the mask is then all ones, so no light is skipped.
fn ResolveLightCandidates(Position: vec3f) -> u32
{
    // 📝 ExtendedLightGrid off: the reference mode, in which every receiver tests every light (inactive lights contribute nothing).
    if (!ExtendedEnabled(Frame, ExtendedLightGrid))
    {
        return 0xFFFFFFFFu;
    }
    let Local = (Position - SceneLights.Minimum.xyz) / SceneLights.Extent.xyz;
    if (any(Local < vec3f(0.0)) || any(Local >= vec3f(1.0)))
    {
        return 0xFFFFFFFFu;
    }
    let Cell = min(vec3u(Local * vec3f(LightClusterResolution)), LightClusterResolution - vec3u(1u));
    let Index = Cell.x + LightClusterResolution.x * (Cell.y + LightClusterResolution.y * Cell.z);
    return SceneLights.CellMasks[Index >> 2u][Index & 3u];
}

// 📝 Which photometric profile a light follows: its type picks the luminaire (roadway, low beam, high beam, fog beam), the flood type's variant picks flood or spot pods.
//    0 means none (an omnidirectional point, or a type without a file).
fn ResolvePhotometricProfile(Light: LightRecord) -> u32
{
    let Type = u32(Light.PositionType.w + 0.5);
    if (Type == 1u) { return 1u; }
    if (Type == 2u) { return 3u; }
    if (Type == 3u) { return 4u; }
    if (Type == 4u) { return 5u; }
    if (Type == 5u) { return select(6u, 7u, Light.Params.z > 0.5); }
    return 0u;
}

fn LoadPhotometricTexel(Profile: u32, Azimuth: u32, Polar: u32) -> f32
{
    let Texel = (Profile - 1u) * PhotometryTexelsPerProfile + Azimuth * PhotometryPolarBins + Polar;
    let Word  = SceneLights.Atlas[Texel >> 4u][(Texel >> 2u) & 3u];
    return f32((Word >> ((Texel & 3u) * 8u)) & 255u) * (1.0 / 255.0);
}

// 📝 The profile's relative intensity towards FromFixture (unit, from the light to the point). Type C: the polar angle is measured from the optical axis; C0 is up around it
//    (world +Z; +X for a fixture that looks straight up or down), C90 to its right. Bilinear reconstruction removes banding at a beam's cutoff.
fn SamplePhotometry(Light: LightRecord, Profile: u32, FromFixture: vec3f) -> f32
{
    let Axis      = normalize(Light.DirectionReach.xyz);
    let Reference = select(vec3f(0.0, 0.0, 1.0), vec3f(1.0, 0.0, 0.0), abs(Axis.z) > 0.99);
    let Right     = normalize(cross(Axis, Reference));
    let Up        = cross(Right, Axis);
    let Polar     = acos(clamp(dot(FromFixture, Axis), -1.0, 1.0)) * (f32(PhotometryPolarBins - 1u) / 3.141592653589793);
    var Angle     = atan2(dot(FromFixture, Right), dot(FromFixture, Up));
    if (Angle < 0.0) { Angle += 6.283185307179586; }
    let Azimuth = Angle * (f32(PhotometryAzimuthBins - 1u) / 6.283185307179586);
    let H0 = min(u32(floor(Azimuth)), PhotometryAzimuthBins - 2u);
    let V0 = min(u32(floor(Polar)), PhotometryPolarBins - 2u);
    let H  = clamp(Azimuth - f32(H0), 0.0, 1.0);
    let V  = clamp(Polar - f32(V0), 0.0, 1.0);
    let A  = mix(LoadPhotometricTexel(Profile, H0, V0), LoadPhotometricTexel(Profile, H0, V0 + 1u), V);
    let B  = mix(LoadPhotometricTexel(Profile, H0 + 1u, V0), LoadPhotometricTexel(Profile, H0 + 1u, V0 + 1u), V);
    return mix(A, B, H);
}

fn EvaluateBeamProfile(Light: LightRecord, TowardPoint: vec3f, Distance: f32) -> f32
{
    let Type = u32(Light.PositionType.w + 0.5);
    if (Type == 0u)
    {
        return 1.0; // Omnidirectional point
    }

    // 📝 Measured (or authored) photometry where the atlas is loaded and the switch is on: the file's shape, bounded by the same cone the analytic beams respect.
    let Profile = ResolvePhotometricProfile(Light);
    if (Profile != 0u && SceneLights.Minimum.w > 0.5 && ExtendedEnabled(Frame, ExtendedIesProfiles))
    {
        let FromFixture = TowardPoint / max(Distance, 1.0e-4);
        let Shape = SamplePhotometry(Light, Profile, FromFixture);
        if (Type == 5u)
        {
            return Shape * smoothstep(Light.Params.y, Light.Params.x, dot(FromFixture, normalize(Light.DirectionReach.xyz)));
        }
        return Shape;
    }

    let LDir = TowardPoint / max(Distance, 1.0e-4);
    let Forward = normalize(Light.DirectionReach.xyz);
    let CosTheta = dot(LDir, Forward);

    // Type 1: Street Lamp / Roadway Batwing (roadway luminaire, Type II / III spread)
    if (Type == 1u)
    {
        let CosDown = -LDir.z;
        if (CosDown <= 0.05) { return 0.0; }
        let Theta = acos(clamp(CosDown, 0.0, 1.0));
        // Smooth batwing distribution without sharp ring discontinuities
        let Batwing = sin(Theta * 1.8) * CosDown * 2.2;
        return max(0.0, Batwing);
    }

    // Type 2: Automotive Low Beam (Asymmetric sharp cutoff, right-side kick, wide horizontal spread)
    if (Type == 2u)
    {
        if (CosTheta <= 0.08) { return 0.0; }
        let Right = normalize(cross(Forward, vec3f(0.0, 0.0, 1.0)));
        let Up = cross(Right, Forward);
        let LocalY = dot(LDir, Right);
        let LocalZ = dot(LDir, Up);

        // Asymmetric beam cutoff (ECE / DOT standard: lower on oncoming lane, kicks up towards shoulder)
        let CutoffZ = -0.02 - 0.04 * max(0.0, LocalY);
        let TopFade = clamp((CutoffZ + 0.035 - LocalZ) / 0.035, 0.0, 1.0);
        let HorizAngle = atan2(LocalY, CosTheta);
        let HorizFade = max(0.0, cos(clamp(HorizAngle * 1.6, -1.57, 1.57)));
        return TopFade * HorizFade * (CosTheta * CosTheta);
    }

    // Type 3: Automotive High Beam (Collimated pencil spotlight with soft halo)
    if (Type == 3u)
    {
        if (CosTheta <= 0.15) { return 0.0; }
        return pow(max(0.0, CosTheta), 14.0);
    }

    // Type 4: Automotive Fog Light (Wide fan, razor-sharp top cutoff to prevent glare in fog/smoke)
    if (Type == 4u)
    {
        if (CosTheta <= 0.08) { return 0.0; }
        let Right = normalize(cross(Forward, vec3f(0.0, 0.0, 1.0)));
        let Up = cross(Right, Forward);
        let LocalY = dot(LDir, Right);
        let LocalZ = dot(LDir, Up);
        if (LocalZ > -0.005) { return 0.0; }
        let HorizAngle = atan2(LocalY, CosTheta);
        return max(0.0, cos(clamp(HorizAngle * 1.3, -1.57, 1.57))) * pow(CosTheta, 4.0);
    }

    // Type 5: Roof Floodlight (Multi-lens LED bar or dual rally pods)
    if (Type == 5u)
    {
        let InnerCos = Light.Params.x;
        let OuterCos = Light.Params.y;
        return smoothstep(OuterCos, InnerCos, CosTheta);
    }

    return 1.0;
}

// 📐 Inverse-square illuminance with a smooth window at the reach; the cosine of the surface is inside the result.
fn EvaluateLightArrival(Light: LightRecord, Position: vec3f, Normal: vec3f) -> vec3f
{
    if (Light.Params.w < 0.5)
    {
        return vec3f(0.0);
    }

    let LightPos = Light.PositionType.xyz;
    let Reach = Light.DirectionReach.w;
    let Toward = LightPos - Position;
    let D2 = dot(Toward, Toward);

    if (D2 >= Reach * Reach || Reach <= 0.0)
    {
        return vec3f(0.0);
    }

    let Distance = sqrt(D2);
    let LightDir = Toward / max(Distance, 1.0e-4);
    let NdotL = max(dot(Normal, LightDir), 0.0);

    if (NdotL <= 0.0)
    {
        return vec3f(0.0);
    }

    // Window function: C2 continuous polynomial smoothly dropping to 0 at Reach
    let Ratio = D2 / (Reach * Reach);
    let Window = (1.0 - Ratio * Ratio) * (1.0 - Ratio * Ratio);

    // Beam profile evaluation along the light direction
    let Profile = EvaluateBeamProfile(Light, -Toward, Distance);
    if (Profile <= 0.0)
    {
        return vec3f(0.0);
    }

    let Intensity = Light.ColourIntensity.w;
    let Colour = Light.ColourIntensity.rgb;

    // Standard physical inverse-square falloff with radius margin to avoid singularity
    let Falloff = Window / max(D2, 0.36);
    return Colour * (Intensity * Falloff * Profile * NdotL);
}
