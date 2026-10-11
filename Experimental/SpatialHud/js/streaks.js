// The streak field — the one category with no native counterpart yet, so the one shader written by
// hand rather than generated from the .slang.
//
// 🔴 WHY THIS IS ANALYTIC AND NOT THE PARTICLE EDITOR'S ACTUAL PARTICLES.
//
//    The obvious build is: run Experimental/ParticleEditor's light-streak pass into a render target,
//    bind it as the panel's background. It would be pixel-exact and it is the wrong shape for this
//    system. The spatial interface binds NO texture at all — InterfacePanelSample.slang says so
//    outright, and gives the reason: a sampled background means a sampler binding, a texture
//    lifetime, and a second pass whose result a reflection ray cannot cheaply query. A panel that is
//    a lit surface in the room has to be answerable at a hit point, not just at a screen pixel.
//
//    So the streaks are a field: one more branch in the fragment shader that already runs, evaluated
//    wherever the panel is asked what it looks like — raster, reflection, or a CPU average.
//
// 🔴 WHAT WAS GIVEN UP TO GET THAT, STATED PLAINLY.
//
//    The preset's fibres are cubic Beziers in 3D, sampled per vertex over 72 segments — fine for
//    geometry, hopeless per fragment, where the nearest point on a Bezier has no closed form and the
//    sampled approximation would cost 200 strands x 72 distance tests on every pixel of the panel.
//
//    Each strand here is instead an EXPLICIT curve y = f(x): two summed harmonics, so the distance to
//    it is |Py - f(Px)| corrected by the slope — first order, exact on the curve, and about twenty
//    instructions. The visible consequence is that a strand cannot double back on itself. For a
//    backdrop of light running across a panel that is not a loss; for the preset's root-cluster-to-
//    reach spray it would be, which is why this is a sibling of that effect and not a port of it.
//
//    Everything that makes the preset READ as light streaks is kept, because none of it needed the
//    Bezier: the running head, the exponential tail behind it, the hard leading edge, the spark at
//    the head, per-strand phase and speed from a hash, and the additive tone map at the end.
//
// Reference: Experimental/ParticleEditor/js/shaders.js (FIBRE, fbHash / fbUnit / fbBezier) and the
//            "light-streaks" preset in js/presets.js.

export const STREAK_WGSL = String.raw`
//------------------------------------------------------------------------------------------------------------------------
//                                                   THE STREAK FIELD
//------------------------------------------------------------------------------------------------------------------------
// Parameters, carried in the figure's two streak vectors rather than in ScalarAlpha / ScalarBeta,
// because six of them do not fit in two floats and a parameter smuggled into another one's slot is
// how a slot stops meaning anything:
//
//    StreakA.x  strands     [cnt]  how many run across the panel
//    StreakA.y  amplitude   [m]    peak excursion of a strand from its own row
//    StreakA.z  waves       [-]    harmonics across the panel's width
//    StreakA.w  speed       [1/s]  loops per second of the running head
//    StreakB.x  tail        [-]    tail length as a fraction of the panel width
//    StreakB.y  seed        [-]    the hash salt; changing it re-deals every strand
//    StreakB.z  intensity   [-]    exposure before the tone map
//    StreakB.w  core        [m]    half width of a strand's bright core

const kCategoryStreakField: u32 = 7u;

// The particle editor's own integer avalanche (fbHash). Kept bit for bit so a strand dealt here and
// a fibre dealt there from the same seed land in the same place.
fn StreakHash(Key: u32) -> u32
{
    var k = Key;
    k ^= k >> 16u;
    k *= 0x7feb352du;
    k ^= k >> 15u;
    k *= 0x846ca68bu;
    k ^= k >> 16u;
    return k;
}

fn StreakUnit(Id: f32, Salt: f32, Seed: f32) -> f32
{
    let Key = u32(Id) * 747796405u + u32(Salt) * 2891336453u + u32(Seed) * 196613u;
    return f32(StreakHash(Key)) * (1.0 / 4294967296.0);
}

// One strand's height at a horizontal position, and the slope there. Two harmonics rather than one:
// a single sine reads as a graph of a sine, which is the first thing that gives a procedural
// background away.
fn StreakHeight(U: f32, Phase: f32, Waves: f32, Amplitude: f32, Drift: f32) -> vec2f
{
    let K1 = Waves * 3.14159265;
    let K2 = K1 * 2.17;                       // deliberately not a whole multiple, so it never repeats
    let A1 = Amplitude * 0.72;
    let A2 = Amplitude * 0.28;
    let Height = A1 * sin(K1 * U + Phase + Drift)
               + A2 * sin(K2 * U - 1.37 * Drift + Phase * 1.7);
    let Slope  = A1 * K1 * cos(K1 * U + Phase + Drift)
               - A2 * K2 * cos(K2 * U - 1.37 * Drift + Phase * 1.7) * -1.0;
    return vec2f(Height, Slope);
}

// Returns coverage in 0..1 — this category answers with light, not with a distance, because there is
// no edge to be a pixel wide. The fragment stage branches on that rather than pretending otherwise.
fn StreakFieldGlow(Point: vec2f, HalfExtent: vec2f, StreakA: vec4f, StreakB: vec4f, Time: f32) -> f32
{
    let Strands   = clamp(StreakA.x, 1.0, 48.0);
    let Amplitude = StreakA.y;
    let Waves     = StreakA.z;
    let Speed     = StreakA.w;
    let Tail      = max(StreakB.x, 1.0e-3);
    let Seed      = StreakB.y;
    let Intensity = StreakB.z;
    let Core      = max(StreakB.w, 1.0e-4);

    // Panel space, normalised across the width so the head's travel and the tail read the same on
    // any panel size. Height stays in metres: a strand's excursion is a real distance.
    let U = Point.x / max(HalfExtent.x, 1.0e-6);

    var Glow = 0.0;
    let Count = i32(Strands);
    for (var Index = 0; Index < Count; Index = Index + 1)
    {
        let Id        = f32(Index);
        let Row       = ((Id + 0.5) / Strands - 0.5) * 2.0 * HalfExtent.y;
        let Phase     = StreakUnit(Id, 1.0, Seed) * 6.28318530718;
        let Pace      = 0.55 + 0.9 * StreakUnit(Id, 2.0, Seed);      // every strand runs at its own rate
        let Swell     = 0.45 + 1.1 * StreakUnit(Id, 3.0, Seed);
        let Bright    = 0.35 + 0.65 * StreakUnit(Id, 4.0, Seed);

        let Curve = StreakHeight(U, Phase, Waves, Amplitude * Swell, Time * Speed * Pace);
        let Rise  = Row + Curve.x;

        // First-order distance to an explicit curve: the vertical gap, foreshortened by the slope.
        let Gap   = abs(Point.y - Rise) / sqrt(1.0 + Curve.y * Curve.y);
        let CoreGlow = exp(-(Gap * Gap) / (Core * Core));
        if (CoreGlow < 1.0e-4) { continue; }

        // The running head, and the tail it leaves. Behind the head the strand is lit and fading;
        // ahead of it there is nothing yet, and that edge is sharp — a streak with a symmetric
        // falloff reads as a smudge rather than as something travelling.
        let Head   = fract(Phase * 0.15915494 + Time * Speed * Pace * 0.25) * 2.0 - 1.0;
        let Behind = Head - U;
        let Wake   = select(exp(Behind * 26.0), exp(-Behind / Tail), Behind >= 0.0);

        // The spark at the head: the preset's brightest pixel, and the thing an eye tracks.
        let Spark = exp(-(Behind * Behind) / (0.0024)) * 2.4;

        Glow += CoreGlow * (Wake + Spark) * Bright;
    }

    // Additive light, then one tone map, so a dense field saturates instead of clipping to white
    // strand by strand.
    var Lit = 1.0 - exp(-Glow * max(Intensity, 0.0));

    // Fade out at the panel's edge. Strands that run off the side read as a texture that has been
    // cropped; strands that dim into the bezel read as light inside the glass.
    let EdgeX = 1.0 - smoothstep(0.72, 1.0, abs(Point.x) / max(HalfExtent.x, 1.0e-6));
    let EdgeY = 1.0 - smoothstep(0.70, 1.0, abs(Point.y) / max(HalfExtent.y, 1.0e-6));
    return clamp(Lit * EdgeX * EdgeY, 0.0, 1.0);
}
`;
