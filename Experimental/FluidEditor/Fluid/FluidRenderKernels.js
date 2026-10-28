/*====================================================================================================================================
                                                   FLUIDRENDERKERNELS.JS
====================================================================================================================================*/
// 🧩 Density splat, surface raymarch, and the diagnostic inspection modes

//------------------------------------------------------------------------------------------------------------------------
//                                                   DENSITY RECONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Particles are splatted into a density field and the water surface is then the isosurface of that
//    field. Reconstructing a surface rather than drawing the particles is what separates liquid from a
//    point cloud: the isosurface merges neighbouring particles into one skin, so the render reads as a
//    continuous body of water instead of a swarm of beads.
//
//    Same fixed-point accumulation as the velocity scatter, and for the same reason — no f32 atomics.
export const DensityClearKernel = /* wgsl */`

@group(0) @binding(0) var<storage, read_write> DensityAccumulator : array<atomic<i32>>;
@group(0) @binding(1) var<uniform>             CellTally          : vec4<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) Invocation : vec3<u32>)
{
    if (Invocation.x >= CellTally.x) { return; }
    atomicStore(&DensityAccumulator[Invocation.x * 2u + 0u], 0);
    atomicStore(&DensityAccumulator[Invocation.x * 2u + 1u], 0);
}
`;

export const DensitySplatKernel = /* wgsl */`

struct Particle
{
    Position : vec3<f32>,
    Alive     : f32,
    Velocity : vec3<f32>,
    Foam      : f32,
};

struct SurfaceProfile
{
    GridExtent    : vec3<u32>,
    ParticleTally : u32,
    CellWidth     : f32,
    SplatRadius   : f32,
    Pad0          : f32,
    Pad1          : f32,
};

@group(0) @binding(0) var<storage, read>       Particles          : array<Particle>;
@group(0) @binding(1) var<storage, read_write> DensityAccumulator : array<atomic<i32>>;
@group(0) @binding(2) var<uniform>             Profile            : SurfaceProfile;

const FixedPointScale : f32 = 65536.0;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) Invocation : vec3<u32>)
{
    if (Invocation.x >= Profile.ParticleTally) { return; }

    let Subject = Particles[Invocation.x];
    if (Subject.Alive < 0.5) { return; }

    let GridPosition = Subject.Position / Profile.CellWidth;
    let Reach = i32(ceil(Profile.SplatRadius));
    let BaseCell = vec3<i32>(floor(GridPosition));

    // 📝 A smooth falloff, not a box. A hard-edged splat leaves the isosurface faceted along cell
    //    boundaries and the water reads as blocky however fine the grid is.
    for (var OffsetZ = -Reach; OffsetZ <= Reach; OffsetZ = OffsetZ + 1)
    {
        for (var OffsetY = -Reach; OffsetY <= Reach; OffsetY = OffsetY + 1)
        {
            for (var OffsetX = -Reach; OffsetX <= Reach; OffsetX = OffsetX + 1)
            {
                let Target = BaseCell + vec3<i32>(OffsetX, OffsetY, OffsetZ);
                if (any(Target < vec3<i32>(0)) || any(Target >= vec3<i32>(Profile.GridExtent))) { continue; }

                let Centre = vec3<f32>(Target) + vec3<f32>(0.5);
                let Separation = length(Centre - GridPosition) / Profile.SplatRadius;
                if (Separation >= 1.0) { continue; }

                // Cubic spline falloff — smooth to the first derivative at the support boundary.
                let Falloff = 1.0 - Separation * Separation;
                let Weight = Falloff * Falloff * Falloff;

                let Index = u32(Target.x)
                          + u32(Target.y) * Profile.GridExtent.x
                          + u32(Target.z) * Profile.GridExtent.x * Profile.GridExtent.y;

                atomicAdd(&DensityAccumulator[Index * 2u + 0u], i32(Weight * FixedPointScale));
                atomicAdd(&DensityAccumulator[Index * 2u + 1u], i32(Weight * Subject.Foam * FixedPointScale));
            }
        }
    }
}
`;

// Normalise the fixed-point accumulation into the sampled density/foam field.
export const DensityResolveKernel = /* wgsl */`

@group(0) @binding(0) var<storage, read>       DensityAccumulator : array<i32>;
@group(0) @binding(1) var<storage, read_write> DensityField       : array<vec2<f32>>;
@group(0) @binding(2) var<uniform>             CellTally          : vec4<u32>;

const FixedPointScale : f32 = 65536.0;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) Invocation : vec3<u32>)
{
    if (Invocation.x >= CellTally.x) { return; }

    let Density = f32(DensityAccumulator[Invocation.x * 2u + 0u]) / FixedPointScale;
    let Foam    = f32(DensityAccumulator[Invocation.x * 2u + 1u]) / FixedPointScale;

    // Foam is stored as a fraction of the density carrying it, so a thin spray reads as white rather
    // than being swamped by the density normalisation.
    let FoamFraction = select(0.0, clamp(Foam / max(Density, 1e-4), 0.0, 1.0), Density > 1e-4);
    DensityField[Invocation.x] = vec2<f32>(Density, FoamFraction);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                     SURFACE RAYMARCH
//------------------------------------------------------------------------------------------------------------------------

// 📝 One full-screen pass that raymarches the density field. Every shading mode and every diagnostic
//    inspection is a branch inside this shader rather than a separate pipeline, so they cannot disagree
//    about camera, domain extent, or where the surface is.
//
//    InspectMode: 0 surface · 1 particles · 2 velocity · 3 occupancy · 4 pressure · 5 divergence
//    ShadeMode:   0 refractive · 1 opaque · 2 thickness · 3 points
export const SurfaceRenderKernel = /* wgsl */`

struct RenderProfile
{
    CameraOrigin   : vec3<f32>,
    DomainExtent   : f32,

    CameraForward  : vec3<f32>,
    IsoLevel       : f32,

    CameraRight    : vec3<f32>,
    Smoothing      : f32,

    CameraUp       : vec3<f32>,
    AspectRatio    : f32,

    Absorption     : vec3<f32>,
    AbsorptionDepth: f32,

    SunDirection   : vec3<f32>,
    Roughness      : f32,

    GridExtent     : vec3<u32>,
    InspectMode    : u32,

    ShadeMode      : u32,
    OverlayBounds  : u32,
    OverlayCollider: u32,
    FogDensity     : f32,

    Turbidity      : f32,
    ElapsedSeconds : f32,
    Pad0           : f32,
    Pad1           : f32,
};

@group(0) @binding(0) var<storage, read> DensityField : array<vec2<f32>>;
@group(0) @binding(1) var<storage, read> Velocity     : array<vec4<f32>>;
@group(0) @binding(2) var<storage, read> Pressure     : array<f32>;
@group(0) @binding(3) var<storage, read> Divergence   : array<f32>;
@group(0) @binding(4) var<storage, read> CellKind     : array<u32>;
@group(0) @binding(5) var<uniform>       Profile      : RenderProfile;

struct Fragment
{
    @builtin(position) Position : vec4<f32>,
    @location(0)       Screen   : vec2<f32>,
};

@vertex
fn VertexStage(@builtin(vertex_index) VertexIndex : u32) -> Fragment
{
    // Full-screen triangle, no vertex buffer.
    var Corners = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0), vec2<f32>( 3.0, -1.0), vec2<f32>(-1.0,  3.0));

    var Output : Fragment;
    Output.Position = vec4<f32>(Corners[VertexIndex], 0.0, 1.0);
    Output.Screen   = Corners[VertexIndex];
    return Output;
}

fn GridIndex(Coordinate : vec3<i32>) -> u32
{
    return u32(Coordinate.x)
         + u32(Coordinate.y) * Profile.GridExtent.x
         + u32(Coordinate.z) * Profile.GridExtent.x * Profile.GridExtent.y;
}

fn WithinGrid(Coordinate : vec3<i32>) -> bool
{
    return all(Coordinate >= vec3<i32>(0)) && all(Coordinate < vec3<i32>(Profile.GridExtent));
}

// Trilinear sample of the density/foam field in domain space.
fn SampleDensity(Position : vec3<f32>) -> vec2<f32>
{
    let CellWidth = Profile.DomainExtent / f32(Profile.GridExtent.x);
    let GridPosition = Position / CellWidth - vec3<f32>(0.5);

    let BaseCell = vec3<i32>(floor(GridPosition));
    let Fraction = GridPosition - vec3<f32>(BaseCell);

    var Accumulated = vec2<f32>(0.0);

    for (var OffsetZ = 0; OffsetZ < 2; OffsetZ = OffsetZ + 1)
    {
        for (var OffsetY = 0; OffsetY < 2; OffsetY = OffsetY + 1)
        {
            for (var OffsetX = 0; OffsetX < 2; OffsetX = OffsetX + 1)
            {
                let Target = BaseCell + vec3<i32>(OffsetX, OffsetY, OffsetZ);
                if (!WithinGrid(Target)) { continue; }

                let AxisWeight = vec3<f32>(
                    select(1.0 - Fraction.x, Fraction.x, OffsetX == 1),
                    select(1.0 - Fraction.y, Fraction.y, OffsetY == 1),
                    select(1.0 - Fraction.z, Fraction.z, OffsetZ == 1));

                Accumulated = Accumulated
                            + DensityField[GridIndex(Target)] * AxisWeight.x * AxisWeight.y * AxisWeight.z;
            }
        }
    }
    return Accumulated;
}

// Central-difference gradient of the density field — the surface normal.
fn SurfaceNormal(Position : vec3<f32>) -> vec3<f32>
{
    let Probe = Profile.DomainExtent / f32(Profile.GridExtent.x) * Profile.Smoothing;

    let Gradient = vec3<f32>(
        SampleDensity(Position + vec3<f32>(Probe, 0.0, 0.0)).x - SampleDensity(Position - vec3<f32>(Probe, 0.0, 0.0)).x,
        SampleDensity(Position + vec3<f32>(0.0, Probe, 0.0)).x - SampleDensity(Position - vec3<f32>(0.0, Probe, 0.0)).x,
        SampleDensity(Position + vec3<f32>(0.0, 0.0, Probe)).x - SampleDensity(Position - vec3<f32>(0.0, 0.0, Probe)).x);

    // The density rises going inward, so the outward normal is the negated gradient.
    return normalize(-Gradient - vec3<f32>(1e-9));
}

// Slab intersection against the domain cube.
fn IntersectDomain(Origin : vec3<f32>, Direction : vec3<f32>) -> vec2<f32>
{
    let Inverse = 1.0 / (Direction + vec3<f32>(1e-9));
    let NearCorner = (vec3<f32>(0.0) - Origin) * Inverse;
    let FarCorner  = (vec3<f32>(Profile.DomainExtent) - Origin) * Inverse;

    let Lower = min(NearCorner, FarCorner);
    let Upper = max(NearCorner, FarCorner);

    let Entry = max(max(Lower.x, Lower.y), Lower.z);
    let Exit  = min(min(Upper.x, Upper.y), Upper.z);
    return vec2<f32>(max(Entry, 0.0), Exit);
}

// The sky the water reflects and the render sits against.
fn SampleSky(Direction : vec3<f32>) -> vec3<f32>
{
    let Height = clamp(Direction.y * 0.5 + 0.5, 0.0, 1.0);

    let Horizon = vec3<f32>(0.52, 0.58, 0.64);
    let Zenith  = vec3<f32>(0.10, 0.16, 0.28);
    var Colour = mix(Horizon, Zenith, pow(Height, 0.55));

    // Turbidity washes the sky toward the horizon tint, which is what a hazy day looks like.
    Colour = mix(Colour, Horizon * 1.1, clamp(Profile.Turbidity / 20.0, 0.0, 1.0) * 0.5);

    let SunAmount = pow(max(dot(Direction, Profile.SunDirection), 0.0), 220.0);
    return Colour + vec3<f32>(1.0, 0.94, 0.82) * SunAmount * 3.0;
}

// Distance to the nearest domain edge line, used to draw the bounding box overlay.
fn DomainEdgeProximity(Position : vec3<f32>) -> f32
{
    let Extent = Profile.DomainExtent;
    let Folded = min(Position, vec3<f32>(Extent) - Position);

    // Two of the three axes must be near a face for the point to sit on an edge.
    var Sorted = Folded;
    if (Sorted.x > Sorted.y) { let Swap = Sorted.x; Sorted.x = Sorted.y; Sorted.y = Swap; }
    if (Sorted.y > Sorted.z) { let Swap = Sorted.y; Sorted.y = Sorted.z; Sorted.z = Swap; }
    if (Sorted.x > Sorted.y) { let Swap = Sorted.x; Sorted.x = Sorted.y; Sorted.y = Swap; }

    return Sorted.y;
}

@fragment
fn FragmentStage(Input : Fragment) -> @location(0) vec4<f32>
{
    let Screen = vec2<f32>(Input.Screen.x * Profile.AspectRatio, Input.Screen.y);

    let Direction = normalize(Profile.CameraForward
                            + Profile.CameraRight * Screen.x
                            + Profile.CameraUp    * Screen.y);

    let Origin = Profile.CameraOrigin;
    var Colour = SampleSky(Direction);

    let Span = IntersectDomain(Origin, Direction);
    if (Span.x >= Span.y)
    {
        return vec4<f32>(Colour, 1.0);
    }

    let CellWidth = Profile.DomainExtent / f32(Profile.GridExtent.x);
    let StepLength = CellWidth * 0.5;

    //------------------------------------------------------------------------------------------------
    //  DIAGNOSTIC INSPECTION — accumulating modes, drawn instead of the surface
    //------------------------------------------------------------------------------------------------
    if (Profile.InspectMode != 0u)
    {
        var Accumulated = vec3<f32>(0.0);
        var Travelled = Span.x;
        var Samples = 0.0;

        loop
        {
            if (Travelled >= Span.y || Samples > 512.0) { break; }

            let Position = Origin + Direction * Travelled;
            let Coordinate = vec3<i32>(floor(Position / CellWidth));

            if (WithinGrid(Coordinate))
            {
                let Index = GridIndex(Coordinate);

                if (Profile.InspectMode == 1u)
                {
                    // Particles: raw density, no isosurface, so the point distribution is visible.
                    Accumulated = Accumulated + vec3<f32>(0.45, 0.72, 1.0) * SampleDensity(Position).x * 0.05;
                }
                else if (Profile.InspectMode == 2u)
                {
                    // Velocity: direction to colour, magnitude to intensity.
                    let Field = Velocity[Index].xyz;
                    let Speed = length(Field);
                    if (Speed > 1e-4)
                    {
                        Accumulated = Accumulated
                                    + (normalize(Field) * 0.5 + vec3<f32>(0.5)) * min(Speed * 0.12, 1.0) * 0.04;
                    }
                }
                else if (Profile.InspectMode == 3u)
                {
                    // Occupancy: the cell classification the pressure solve actually ran against.
                    let Kind = CellKind[Index];
                    if (Kind == 1u) { Accumulated = Accumulated + vec3<f32>(0.20, 0.55, 1.00) * 0.04; }
                    if (Kind == 2u) { Accumulated = Accumulated + vec3<f32>(0.85, 0.30, 0.25) * 0.05; }
                }
                else if (Profile.InspectMode == 4u)
                {
                    // Pressure: signed, blue negative through red positive.
                    let Amount = Pressure[Index] * 0.02;
                    Accumulated = Accumulated
                                + vec3<f32>(max(Amount, 0.0), 0.05, max(-Amount, 0.0)) * 0.35;
                }
                else if (Profile.InspectMode == 5u)
                {
                    // Divergence: the residual the projection is meant to have removed. A converged
                    // solve reads near black, so any glow here is the solver failing to close.
                    let Amount = abs(Divergence[Index]) * 0.5;
                    Accumulated = Accumulated + vec3<f32>(1.0, 0.55, 0.15) * Amount * 0.08;
                }
            }

            Travelled = Travelled + StepLength;
            Samples = Samples + 1.0;
        }

        Colour = mix(Colour * 0.15, Colour * 0.15 + Accumulated, 1.0);

        if (Profile.OverlayBounds == 1u)
        {
            let Entry = Origin + Direction * Span.x;
            if (DomainEdgeProximity(Entry) < CellWidth * 0.35)
            {
                Colour = mix(Colour, vec3<f32>(0.69, 0.98, 0.69), 0.8);
            }
        }
        return vec4<f32>(Colour, 1.0);
    }

    //------------------------------------------------------------------------------------------------
    //  SURFACE — march to the isosurface, then shade it
    //------------------------------------------------------------------------------------------------
    var Travelled = Span.x;
    var Struck = false;
    var StrikePosition = vec3<f32>(0.0);
    var StrikeFoam = 0.0;
    var Thickness = 0.0;
    var Steps = 0.0;

    loop
    {
        if (Travelled >= Span.y || Steps > 512.0) { break; }

        let Position = Origin + Direction * Travelled;
        let Sampled = SampleDensity(Position);

        if (Sampled.x > Profile.IsoLevel)
        {
            if (!Struck)
            {
                Struck = true;
                StrikePosition = Position;
                StrikeFoam = Sampled.y;
            }
            // Thickness keeps accumulating past the strike — it is what drives absorption depth.
            Thickness = Thickness + StepLength;
        }

        Travelled = Travelled + StepLength;
        Steps = Steps + 1.0;
    }

    if (Profile.ShadeMode == 3u)
    {
        // Points: no surface, just the accumulated density, so individual particles stay legible.
        var Accumulated = 0.0;
        var Cursor = Span.x;
        loop
        {
            if (Cursor >= Span.y) { break; }
            Accumulated = Accumulated + SampleDensity(Origin + Direction * Cursor).x * 0.04;
            Cursor = Cursor + StepLength;
        }
        Colour = mix(Colour, vec3<f32>(0.55, 0.80, 1.0), clamp(Accumulated, 0.0, 1.0));
        return vec4<f32>(Colour, 1.0);
    }

    if (Struck)
    {
        let Normal = SurfaceNormal(StrikePosition);
        let Lambert = max(dot(Normal, Profile.SunDirection), 0.0);

        if (Profile.ShadeMode == 2u)
        {
            // Thickness: how much water the ray passed through, as a heat ramp. Nothing to do with
            // lighting — it is the diagnostic for whether the body has volume or is a shell.
            let Depth = clamp(Thickness / max(Profile.AbsorptionDepth, 1e-3), 0.0, 1.0);
            Colour = mix(vec3<f32>(0.05, 0.10, 0.25), vec3<f32>(1.0, 0.92, 0.55), Depth);
        }
        else if (Profile.ShadeMode == 1u)
        {
            // Opaque: plain diffuse, so the surface geometry is readable without refraction confusing it.
            Colour = vec3<f32>(0.28, 0.52, 0.78) * (0.25 + Lambert * 0.85);
        }
        else
        {
            // ---- Refractive: Beer-Lambert absorption through the body, Fresnel-weighted reflection ----
            let Transmitted = exp(-Profile.Absorption * (Thickness / max(Profile.AbsorptionDepth, 1e-3)) * 4.0);

            let Refracted = refract(Direction, Normal, 1.0 / 1.333);
            let Behind = SampleSky(select(Direction, normalize(Refracted), length(Refracted) > 1e-5));

            let Reflected = reflect(Direction, Normal);
            let Mirror = SampleSky(Reflected);

            // Schlick, at water's 0.02 normal reflectance.
            let Grazing = pow(1.0 - max(dot(-Direction, Normal), 0.0), 5.0);
            let Fresnel = 0.02 + (1.0 - 0.02) * Grazing;

            let Specular = pow(max(dot(Reflected, Profile.SunDirection), 0.0),
                               2.0 / max(Profile.Roughness * Profile.Roughness, 1e-4));

            Colour = mix(Behind * Transmitted, Mirror, Fresnel)
                   + vec3<f32>(1.0, 0.96, 0.88) * Specular * 0.6;
        }

        // Foam sits on top of every lit mode — it is surface material, not a lighting term.
        Colour = mix(Colour, vec3<f32>(0.92, 0.95, 0.98), clamp(StrikeFoam, 0.0, 1.0) * 0.85);

        // Distance fog, matching the environment dial.
        let Range = length(StrikePosition - Origin);
        Colour = mix(Colour, SampleSky(Direction), 1.0 - exp(-Profile.FogDensity * Range * Range));
    }

    if (Profile.OverlayBounds == 1u)
    {
        let Entry = Origin + Direction * Span.x;
        if (DomainEdgeProximity(Entry) < CellWidth * 0.35)
        {
            Colour = mix(Colour, vec3<f32>(0.69, 0.98, 0.69), 0.75);
        }
    }

    return vec4<f32>(Colour, 1.0);
}
`;
