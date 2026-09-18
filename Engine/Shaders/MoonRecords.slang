//============================================================================================================================================
// 📦 Engine/Shaders/MoonRecords.slang — the moons as the ray-tracing kernel sees them
//============================================================================================================================================
// The Celestial port's moon seam: a transcription of the reference panel's moons() (Diagnostics/CelestialPanel.html
//    on the arena/01a08682-frontier branch), and it must stay one. The CPU raster, the headless proofs and this
//    kernel are three consumers of the same atlas, and the first time they disagree the night sky becomes two.
//
//    ⚠️ THREE CONVENTIONS CHANGE AT THIS FILE'S BORDER (see DisplayPresentation/MoonConstantRecord.h for the full
//    note): up is +Z, so the disc frame crosses with (0,0,1) and the ambient reads dir.z; phase 0 is FULL, because
//    the panel lights the disc fully at 0 and the host converts the ephemeris once, at pack time; albedos arrive
//    linear, because the bindless table uploads them SRGB and texture() decodes.
//
//    ⚠️ INCLUDE ORDER. MoonAlong samples the bindless Textures[] table, and SkyRecords.slang's SkyAlong calls
//    MoonAlong — so ReSTIRViewport declares Textures[] first, then includes THIS file, then SkyRecords. Binding
//    NUMBERS are unchanged (25 is still the highest binding in the set); only the source order moved, which the
//    "must be last" rule does not constrain.

#ifndef FRONTIER_MOON_RECORDS
#define FRONTIER_MOON_RECORDS

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE UNIFORM BLOCK
//------------------------------------------------------------------------------------------------------------------------

// 288 bytes, std140 — the mirror is MoonConstantRecord, and its static_asserts pin every offset. Round 9 vacated
//    21-24; the sky filled 21 and the moons fill 22. 23-24 stay reserved.
layout(std140, binding = 22) uniform MoonConstants
{
    uvec4 MoonControl;       // x = moons drawn [0..4]
    uvec4 MoonSlots;         // bindless sampler2D[] slot per moon
    vec4  MoonDirection[4];  // xyz = unit toward the moon, Z-up; w unused
    vec4  MoonTint[4];       // rgb tint; w unused
    vec4  MoonParams[4];     // x = angular RADIUS [rad], y = brightness, z = phase (0 = full), w = glow
    vec4  MoonSurface[4];    // x = spin [rad], y = tilt [rad], z = haze, w = gamma
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE DISCS AND THE LIGHT
//------------------------------------------------------------------------------------------------------------------------

// Textured UV-sphere moons along a direction, through the transmittance the caller integrated — the moon shines
//    THROUGH the air, not in front of it. Transcribed from the panel's moons(); the zenith guard is the one place
//    this deliberately differs (the panel's unguarded cross product goes NaN overhead).
vec3 MoonAlong(vec3 Direction, vec3 Transmittance)
{
    const float kPi = 3.14159265358979323846;
    vec3 col = vec3(0.0);
    for (int i = 0; i < 4; ++i)
    {
        if (uint(i) >= MoonControl.x) break;
        vec3 md = MoonDirection[i].xyz;
        float ang = MoonParams[i].x, bright = MoonParams[i].y, phase = MoonParams[i].z, glowAmt = MoonParams[i].w;
        float spin = MoonSurface[i].x, tilt = MoonSurface[i].y, haze = MoonSurface[i].z, gam = MoonSurface[i].w;
        // A zero-size moon is invisible, not a NaN: the glow divides by the radius, and at the exact disc centre
        //    that is 0/0. The host clamps too; this is the backstop for a hand-filled block.
        if (!(ang > 1e-6)) continue;
        float a = acos(clamp(dot(Direction, md), -1.0, 1.0));
        vec3 ax = (abs(md.z) > 0.9998477) ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
        vec3 u = normalize(cross(md, ax));
        vec3 v = cross(u, md);
        if (a < ang * 1.06)
        {
            vec2 lc = vec2(dot(Direction, u), dot(Direction, v)) / sin(ang);
            float r2 = dot(lc, lc);
            float z = sqrt(max(0.0, 1.0 - r2));
            // Sphere normal in the moon's local frame (x = u right, y = v up, z = toward viewer).
            vec3 n = vec3(lc.x, lc.y, z);
            // Axial tilt then spin -> UV.
            float ct = cos(tilt), st = sin(tilt);
            vec3 nt = vec3(n.x, n.y * ct - n.z * st, n.y * st + n.z * ct);
            float lon = atan(nt.x, nt.z) + spin;
            float lat = asin(clamp(nt.y, -1.0, 1.0));
            vec2 uv = vec2(fract(lon / (2.0 * kPi) + 0.5), 0.5 - lat / kPi);
            // LOD 0, explicitly: a compute shader has no derivatives, and the CPU raster samples level 0 too.
            vec3 alb = textureLod(Textures[nonuniformEXT(int(MoonSlots[i]))], uv, 0.0).rgb;
            alb = pow(alb, vec3(gam)) * MoonTint[i].xyz;
            // Phase lighting: the light direction rotates about v in the u/z plane.
            float ph = phase * 2.0 * kPi;
            vec3 lit = vec3(sin(ph), 0.0, cos(ph));
            float wrap = haze * 0.3;
            float ndl = max((dot(n, lit) + wrap) / (1.0 + wrap), 0.0);
            float edgeW = mix(0.975, 0.88, haze);
            float edge = 1.0 - smoothstep(ang * edgeW, ang * (1.0 + haze * 0.06), a);
            float limb = mix(1.0, 0.5, pow(1.0 - z, 2.0) * max(haze, 0.35));
            col += alb * bright * (pow(ndl, 0.8) * limb + 0.012) * edge;
            float rimA = smoothstep(ang * 0.8, ang, a) * (1.0 - smoothstep(ang, ang * 1.06, a)) * haze;
            col += rimA * MoonTint[i].xyz * bright * 0.35 * (0.3 + 0.7 * ndl);
        }
        float lum = 0.4 + 0.6 * (0.5 + 0.5 * cos(phase * 2.0 * kPi));
        float g = glowAmt * bright * (exp(-a / (ang * 1.4)) * 0.09 + exp(-a * 3.5) * 0.005) * lum;
        col += g * MoonTint[i].xyz;
    }
    return col * Transmittance;
}

// Moonlight as the surfaces see it: the panel's ambient loop, Z-up. Tiny (.0025) and phase-gated — a full moon
//    overhead against dark ground, not a second sun. The CPU raster adds the identical term to its sky ambient,
//    so a moonlit frame cannot be bright on one path and black on the other.
vec3 MoonAmbient()
{
    const float kPi = 3.14159265358979323846;
    vec3 amb = vec3(0.0);
    for (int i = 0; i < 4; ++i)
    {
        if (uint(i) >= MoonControl.x) break;
        amb += MoonTint[i].xyz * MoonParams[i].y * max(MoonDirection[i].z, 0.0) * 0.0025
             * (0.5 + 0.5 * cos(MoonParams[i].z * 2.0 * kPi));
    }
    return amb;
}

#endif // FRONTIER_MOON_RECORDS
