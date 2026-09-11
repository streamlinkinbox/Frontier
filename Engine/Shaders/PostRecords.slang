//============================================================================================================================================
// 📦 Engine/Shaders/PostRecords.slang — stars, rainbow and lens flare as the ray-tracing kernel sees them
//============================================================================================================================================
// The third celestial seam: three transcriptions that must stay three transcriptions.
//    · StarAlong   ← the star loop in VisibilityRaster.cpp (same gate, same rotation, same cells, same profile)
//    · RainbowAlong← AtmosphericOptics::Rainbow (same Descartes physics, same 14-wavelength sweep, same band)
//    · FlareAlong  ← AtmosphericOptics::LensFlare (same ghosts, halo, streak, burst and bloom)
//
//    ⚠️ ARGUMENTS, NOT GLOBALS. Every per-pixel value these need arrives as a parameter — the sky luminance,
//    the sun direction, the screen UV, the ray distance — and every per-frame scalar rides the block, so this
//    file reads nothing but its own two bindings and can be included in any order. (MoonRecords showed the other
//    way: Textures[] forces its include position. This file samples no textures and declares no such debt.)
//
//    ⚠️ SMOOTHSTEP IS NEVER CALLED DIRECTLY. The C++ SmoothStep tolerates edge0 > edge1 (the ghost falloff uses
//    it); GLSL smoothstep with reversed edges is UNDEFINED. FrontierSmooth below is the C++ formula exactly, and
//    every falloff in this file goes through it.

#ifndef FRONTIER_POST_RECORDS
#define FRONTIER_POST_RECORDS

//------------------------------------------------------------------------------------------------------------------------
//                                              THE UNIFORM BLOCK (24)
//------------------------------------------------------------------------------------------------------------------------

// 128 bytes, std140 — the mirror is PostConstantRecord, and its static_asserts pin every offset. Per-frame scalars
//    only; the star tables are static and ride binding 23 instead.
layout(std140, binding = 24) uniform PostConstants
{
    vec4 PostStar;      // x = LST [deg], y = latitude [deg], z = point size [x], w = brightness (0 = off)
    vec4 PostFlare;     // x = category [0..3], y = ghost count, z = intensity, w = halo radius
    vec4 PostFlare2;    // x = chromatic [0..1], y = streak gain, z = aperture blades, w = sun visibility
    vec4 PostFlareUv;   // xy = sun UV [0..1] top-left origin, z = enabled, w unused
    vec4 PostBow;       // x = intensity, y = width, z = secondary gain, w = rain visibility
    vec4 PostBow2;      // x = alexander band 0/1, y = minimum path [m], z = enabled, w unused
    vec4 PostSpare0;    // reserved
    vec4 PostSpare1;    // reserved
};

//------------------------------------------------------------------------------------------------------------------------
//                                          THE STAR TABLES, STATIC (23)
//------------------------------------------------------------------------------------------------------------------------

// One star, 32 bytes: the shader's view of StarRecord (DirectionX/Y/Z, Luminance, ColourR/G/B, Padding).
struct StarEntry
{
    vec4 DirLum;   // xyz = unit, equatorial J2000; w = luminance (magnitude 0 is 1.0)
    vec4 ColPad;   // rgb = linear colour from B−V; w = padding
};

// Cells then binned stars in ONE buffer — the fixed 1 024-cell grid first (8 KiB), the unsized star array after,
//    exactly the layout StarCatalogueIndex wrote for. An empty catalogue uploads 1 024 zero cells and no stars,
//    and the packer zeroes the brightness alongside, so the loop below is doubly guarded.
layout(std430, binding = 23) readonly buffer StarTable
{
    uvec2     StarCells[1024];   // (First, Count) into Stars[] per octahedral cell
    StarEntry StarStars[];
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     HELPERS
//------------------------------------------------------------------------------------------------------------------------

// The C++ SmoothStep, exactly — including the reversed-edge falloffs GLSL smoothstep cannot express.
float FrontierSmooth(float Edge0, float Edge1, float V)
{
    float T = clamp((V - Edge0) / (Edge1 - Edge0), 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
}

// The ghosts' cheap spectral sweep — a cosine trio, not a colour space conversion (see AtmosphericOptics::Hue).
vec3 FlareHue(float T)
{
    const float kPi = 3.14159265358979323846;
    return 0.5 + 0.5 * cos(2.0 * kPi * (T + vec3(0.00, 0.33, 0.67)));
}

// Wavelength to linear RGB — the model's piecewise fit (the bow is a narrow saturated band; CIE curves would be
//    precision nobody can see).
vec3 BowSpectral(float Nanometres)
{
    float R = 0.0, G = 0.0, B = 0.0;
    if      (Nanometres < 440.0) { R = -(Nanometres - 440.0) / 60.0; B = 1.0; }
    else if (Nanometres < 490.0) { G = (Nanometres - 440.0) / 50.0;  B = 1.0; }
    else if (Nanometres < 510.0) { G = 1.0; B = -(Nanometres - 510.0) / 20.0; }
    else if (Nanometres < 580.0) { R = (Nanometres - 510.0) / 70.0;  G = 1.0; }
    else if (Nanometres < 645.0) { R = 1.0; G = -(Nanometres - 645.0) / 65.0; }
    else                         { R = 1.0; }
    float Falloff = Nanometres < 420.0 ? 0.3 + 0.7 * (Nanometres - 380.0) / 40.0
                  : Nanometres > 680.0 ? 0.3 + 0.7 * (700.0 - Nanometres) / 20.0
                  : 1.0;
    return vec3(R, G, B) * Falloff;
}

// The Descartes angles, primary then secondary, at 380 + i/13·320 nm — pure physics, constant forever. Computed
//    by AtmosphericOptics::RainbowAngle itself (the bow probe in CheckPostKernel.sh re-derives them and the gate
//    diffs), so these literals are pinned, not trusted. Primary red (700 nm) 0.74046 = 42.43°, secondary red
//    0.87730 = 50.27° — the published figures, and note the secondary runs BACKWARD (extra reflection).
const float kBowPrimary[14] = float[14](0.7038212, 0.7098362, 0.7148807, 0.7191508, 0.7227967, 0.7259333,
                                        0.7286513, 0.7310209, 0.7330999, 0.7349327, 0.7365577, 0.7380037,
                                        0.7392967, 0.7404578);
const float kBowSecondary[14] = float[14](0.9434741, 0.9325821, 0.9234555, 0.9157355, 0.9091494, 0.9034860,
                                          0.8985817, 0.8943064, 0.8905585, 0.8872540, 0.8843257, 0.8817198,
                                          0.8793910, 0.8772995);

//------------------------------------------------------------------------------------------------------------------------
//                                                        STARS
//------------------------------------------------------------------------------------------------------------------------

// The catalogue along a direction: the raster's star loop, transcribed. Gated on the SKY's brightness at this very
//    pixel (a star shows only when it outshines the air behind it — daylight removes the contrast, not the star),
//    so the caller passes the luminance of what it has rendered so far. The ray-cone spread rides PostSpare0.x,
//    packed per frame, because this file reads no push constants.
vec3 StarAlong(vec3 Direction, float SkyLuminance)
{
    // 0.09: the raster's kStarVisibilityCeiling, which is a restatement the gate watches (two copies of one
    //    threshold is how day skies grow stars — see the CheckPostKernel ceiling pin).
    if (PostStar.w <= 0.0 || SkyLuminance >= 0.09) return vec3(0.0);

    // Horizon frame → equatorial J2000: the raster's HorizonToEquatorial, both rotations. Float, where the CPU
    //    computes in double — the binning duplicates boundary stars precisely so edge flips cost nothing.
    const float kPi = 3.14159265358979323846;
    const float kDeg = kPi / 180.0;
    float Lat = PostStar.y * kDeg;
    float CosL = cos(Lat), SinL = sin(Lat);
    float East = Direction.x, North = Direction.y, Up = Direction.z;
    float X1 = North * SinL - Up * CosL;
    float Y1 = East;
    float Z1 = North * CosL + Up * SinL;
    float Theta = PostStar.x * kDeg;
    float CosT = cos(Theta), SinT = sin(Theta);
    vec3 Eq = vec3(X1 * CosT - Y1 * SinT, X1 * SinT + Y1 * CosT, Z1);

    // Octahedral map → 32×32 cell: DirectionToSquare + CellForDirection, exactly.
    float Norm = abs(Eq.x) + abs(Eq.y) + abs(Eq.z);
    float Px = Eq.x / max(Norm, 1e-9), Py = Eq.y / max(Norm, 1e-9);
    if (Norm < 1e-9) { Px = 0.0; Py = 0.0; }
    if (Eq.z < 0.0)
    {
        // Temps, not in-place: both folds read the ORIGINAL pair (see DirectionToSquare).
        float Ox = Px, Oy = Py;
        Px = (1.0 - abs(Oy)) * (Ox >= 0.0 ? 1.0 : -1.0);
        Py = (1.0 - abs(Ox)) * (Oy >= 0.0 ? 1.0 : -1.0);
    }
    uint Column = min(uint((Px * 0.5 + 0.5) * 32.0), 31u);
    uint Row    = min(uint((Py * 0.5 + 0.5) * 32.0), 31u);
    uint Cell   = Row * 32u + Column;

    // A star is a POINT: half-pixel core, third-pixel skirt, cut at 1.6 radii — the raster's profile, which keeps
    //    the energy inside roughly one pixel so the field reads as points, not smudges. Gain 1.0 (relative to
    //    magnitude 0) with the measured 5.73 energy normalisation.
    vec3 col = vec3(0.0);
    uvec2 Bucket = StarCells[Cell];
    float Radius = max(PostStar.z * 0.0002, PostSpare0.x * 0.5);
    for (uint I = 0u; I < Bucket.y; ++I)
    {
        StarEntry Star = StarStars[Bucket.x + I];
        float Dot = dot(Eq, Star.DirLum.xyz);
        if (Dot <= 0.0) continue;
        float Angle = acos(min(Dot, 1.0));
        if (Angle > Radius * 1.6) continue;
        float Falloff = (Angle - Radius) / (Radius * 0.35);
        float Core = Angle <= Radius ? 1.0 : exp(-Falloff * Falloff);
        col += Star.ColPad.rgb * (Star.DirLum.w * PostStar.w * Core * 5.73);
    }
    return col;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       RAINBOW
//------------------------------------------------------------------------------------------------------------------------

// The bow along a view direction: AtmosphericOptics::Rainbow, transcribed. The sun direction arrives from binding
//    21's block (same set — never restated); the distance is the ray's own travel, which is what keeps the arc off
//    nearby geometry (a short ray crosses no rain column, so it earns no bow — pass a large distance for the sky).
vec3 RainbowAlong(vec3 Direction, vec3 SunDirection, float DistanceMetres)
{
    vec3 col = vec3(0.0);
    if (PostBow2.z <= 0.0 || PostBow.w <= 0.0) return col;

    float PathFraction = FrontierSmooth(0.0, PostBow2.y, DistanceMetres);
    if (PathFraction <= 0.0) return col;

    // The bow needs the sun up and behind the viewer; below −2° the antisolar point climbs out of the sky.
    float SunElevation = degrees(asin(clamp(SunDirection.z, -1.0, 1.0)));
    float SunUp = FrontierSmooth(-2.0, 3.0, SunElevation);
    if (SunUp <= 0.0) return col;

    // Angle from the antisolar point; outside the 34°–57° window there is nothing to compute.
    float Angle = acos(clamp(-dot(Direction, SunDirection), -1.0, 1.0));
    if (Angle < 0.60 || Angle > 1.00) return col;

    float Width = max(PostBow.y, 0.05);
    vec3 Sum = vec3(0.0);
    for (int I = 0; I < 14; ++I)
    {
        float Wavelength = 380.0 + float(I) / 13.0 * 320.0;
        float D1 = (Angle - kBowPrimary[I]) / (0.0035 * Width);
        float D2 = (Angle - kBowSecondary[I]) / (0.0060 * Width);
        Sum += BowSpectral(Wavelength) * (exp(-D1 * D1) + exp(-D2 * D2) * 0.42 * PostBow.z);
    }
    Sum /= 14.0 * 0.42;

    // Alexander's dark band: no light leaves a drop between the two Descartes angles, so the sky between the bows
    //    really is darker — the absence of scattering, and the strongest cue the bow is physical.
    float Band = 1.0;
    if (PostBow2.x > 0.0)
    {
        Band = 1.0 - 0.18 * FrontierSmooth(0.7087893, 0.7087893 + 0.02, Angle)
                         * (1.0 - FrontierSmooth(0.8772995 - 0.02, 0.8772995, Angle));
    }
    return Sum * PostBow.x * PostBow.w * SunUp * Band * PathFraction;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      LENS FLARE
//------------------------------------------------------------------------------------------------------------------------

// The lens's answer to the sun: AtmosphericOptics::LensFlare, transcribed. Screen-space, legitimately — ghosts
//    march the sun-to-centre line because that IS the optical axis — and composited OVER geometry, correctly:
//    scatter inside the lens lies over everything the sensor recorded. Two behaviours separate it from a sprite:
//    occlusion kills it entirely (light that never entered cannot bounce) and it fades, never pops, at the frame
//    edge. Added to LINEAR radiance before the tone map: it is light in the lens, not ink on the print.
vec3 FlareAlong(vec2 ScreenUv, float Aspect)
{
    vec3 col = vec3(0.0);
    if (PostFlareUv.z <= 0.0 || PostFlare2.w <= 0.0) return col;

    // Off-screen suns produce nothing; the approach to the edge is a fade, not a pop.
    float EdgeFade = FrontierSmooth(-0.15, 0.05, PostFlareUv.x) * (1.0 - FrontierSmooth(0.95, 1.15, PostFlareUv.x))
                   * FrontierSmooth(-0.15, 0.05, PostFlareUv.y) * (1.0 - FrontierSmooth(0.95, 1.15, PostFlareUv.y));
    if (EdgeFade <= 0.0) return col;

    // Work about the frame centre, where the optical axis is.
    vec2 P = vec2((ScreenUv.x - 0.5) * Aspect, ScreenUv.y - 0.5);
    vec2 S = vec2((PostFlareUv.x - 0.5) * Aspect, PostFlareUv.y - 0.5);

    // The component mix for a lens type — the reference demo's weights, which are the whole definition of the
    //    difference between the four assemblies (see AtmosphericOptics::MixFor).
    uint Category = uint(PostFlare.x + 0.5);
    vec4 Mix = vec4(1.0, 1.0, 1.0, 0.35);   // Cinematic: everything present and balanced
    if      (Category == 1u) Mix = vec4(0.0, 0.0, 2.20, 0.0);    // Anamorphic: one long blue streak
    else if (Category == 2u) Mix = vec4(1.0, 0.50, 0.35, 1.0);   // Starburst: the aperture diffracts
    else if (Category == 3u) Mix = vec4(0.0, 1.0, 0.35, 0.0);    // Halo: one chromatic ring

    vec3 Accumulated = vec3(0.0);

    // ── Ghosts: internal reflections, strung along the sun-to-centre line at k = −1.35 + i·0.42 ────────────────
    uint Ghosts = Mix.x > 0.0 ? min(uint(PostFlare.y + 0.5), 8u) : 0u;
    for (uint I = 0u; I < Ghosts; ++I)
    {
        float Index = float(I);
        float K = -1.35 + Index * 0.42;
        vec2 Centre = S * K;
        float Radius = 0.035 + 0.07 * fract(Index * 0.618 + 0.31);
        float Distance = length(P - Centre);
        float Shape = (FrontierSmooth(Radius, Radius * 0.35, Distance) * 0.9
                     + FrontierSmooth(Radius * 1.6, Radius, Distance) * 0.25)
                    * (0.06 + 0.06 * fract(Index * 0.37));
        if (Shape <= 0.0) continue;
        vec3 Tint = FlareHue(fract(Index * 0.23 + 0.5));
        Accumulated += Shape * (1.0 + (Tint - 1.0) * PostFlare2.x) * Mix.x;
    }

    // ── Halo: a ring about the axis, chromatically smeared ─────────────────────────────────────────────────────
    vec2 HaloCentre = S * 0.25;
    float HaloDistance = length(P - HaloCentre);
    float RingOffset = abs(HaloDistance - PostFlare.w);
    float Halo = FrontierSmooth(0.045, 0.0, RingOffset) * 0.09 * Mix.y;
    if (Halo > 0.0)
    {
        float Angle = atan(P.y - HaloCentre.y, P.x - HaloCentre.x);
        const float kPi = 3.14159265358979323846;
        vec3 Tint = FlareHue(fract(Angle / (2.0 * kPi) + RingOffset * 8.0));
        Accumulated += Halo * (1.0 + (Tint - 1.0) * PostFlare2.x * 0.8);
    }

    // ── Anamorphic streak: the horizontal bar a wide lens throws, blue by coating ──────────────────────────────
    vec2 M = P - S;
    float Streak = exp(-abs(M.y) * 95.0) * exp(-abs(M.x) * 2.2) * 0.55 * PostFlare2.y * Mix.z;
    Accumulated += Streak * vec3(1.0 + (0.45 - 1.0) * PostFlare2.x,
                                 1.0 + (0.65 - 1.0) * PostFlare2.x,
                                 1.0);

    // ── Starburst: diffraction off the aperture blades (even blades 2N spokes, odd N) ───────────────────────────
    if (Mix.w > 0.0)
    {
        uint Blades = max(uint(PostFlare2.z + 0.5), 3u);
        float Spokes = (Blades % 2u == 0u) ? float(Blades) : float(Blades) * 0.5;
        float Angle = atan(M.y, M.x);
        float Distance = length(M);
        float Primary = pow(abs(sin(Angle * Spokes * 0.5 + 0.3)), 24.0) * exp(-Distance * 3.5) * 0.35;
        float Secondary = pow(abs(sin(Angle * Spokes * 0.875)), 40.0) * exp(-Distance * 6.0) * 0.25;
        Accumulated += (Primary + Secondary) * Mix.w * vec3(1.0, 0.95, 0.85);
    }

    // The ambient bloom around the sun's image — scatter in the glass, present for every lens.
    {
        float Distance = length(M);
        Accumulated += exp(-Distance * 1.6) * 0.04 * vec3(1.0, 0.90, 0.80);
    }

    return Accumulated * PostFlare.z * PostFlare2.w * EdgeFade;
}

#endif // FRONTIER_POST_RECORDS
