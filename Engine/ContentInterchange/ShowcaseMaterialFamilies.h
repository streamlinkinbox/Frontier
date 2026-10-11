//============================================================================================================================================
//                                                  SHOWCASEMATERIALFAMILIES.H
//============================================================================================================================================
// 📦 Procedural authors for the showcase grid's r7 material families: wood, paper, fabric and the Fresnel ladder.

/// Each author fills one MaterialSlabDescriptor for one column of one row. They are pure functions of
///    (column, sweep) — no RNG — so every export, every reference render and every parity run agree.
///
///    These four rows exist because the r6 grid had no entry for any of them: it covered metal, glass,
///    subsurface, thin film, fuzz, coat, haze, EON diffuse, emission and five flake-paint families, and nothing
///    made of wood, paper or cloth, and nothing that isolated Fresnel behaviour from everything else.
///
/// note: wood and fabric are authored as PARAMETER sets, not textures. The grid has no texture budget and the
///       point is the BSDF, not the pattern — grain direction is carried by specular anisotropy and its
///       rotation, which is the part of wood a renderer actually has to get right. A textured variant belongs
///       with the material-authoring tools, not here.
/// tag : showcase, materials, procedural

#pragma once

#include "MaterialDescriptor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Frontier {

// Row labels for the r7 grid, used for material names and by any viewer that wants to caption a row.
inline constexpr const char* kShowcaseRowNames[20] = {
    "Anisotropic metal", "Transmissive glass", "Subsurface",   "Thin film",
    "Cloth fuzz",        "Coat",               "Haziness",     "EON diffuse",
    "Emission",          "Rough metal",        "Metal morph",  "Polymer",
    "Glitter",           "Absorbing glass",    "Showpiece",    "Flake paint",
    "Wood",              "Paper",              "Fabric",       "Fresnel ladder"
};

namespace ShowcaseFamilyDetail {

inline void Tint(float (&Target)[3], float R, float G, float B) noexcept { Target[0] = R; Target[1] = G; Target[2] = B; }

constexpr float kPiOverTwo = 1.57079632679489662f;

} // namespace ShowcaseFamilyDetail

//------------------------------------------------------------------------------------------------------------------------
//                                                        WOOD
//------------------------------------------------------------------------------------------------------------------------

/// Twenty real timbers, each at its own albedo, gloss and grain strength.
///
/// in  : Column 0–19 selects the species; Sweep 0–1 runs the finish from raw to fully lacquered
/// note: the grain is carried by SpecularRoughnessAnisotropy with SlateAnisotropyRotation fixed along the
///       trunk axis, because that is what makes wood read as wood under a moving light — the highlight
///       stretches ALONG the grain. An isotropic lobe with a wood-coloured albedo reads as painted plastic.
inline void AuthorWoodGrain(MaterialSlabDescriptor& Slab, uint32_t Column, float Sweep) noexcept
{
    using namespace ShowcaseFamilyDetail;
    struct Timber { const char* Label; float R, G, B; float Rough; float Grain; float Ior; };
    static constexpr Timber kTimbers[20] = {
        { "Oak",          0.355f, 0.225f, 0.115f, 0.52f, 0.72f, 1.53f },
        { "Ash",          0.470f, 0.355f, 0.215f, 0.48f, 0.68f, 1.53f },
        { "Maple",        0.510f, 0.395f, 0.255f, 0.38f, 0.35f, 1.54f },
        { "Birch",        0.545f, 0.440f, 0.305f, 0.42f, 0.30f, 1.53f },
        { "Beech",        0.455f, 0.320f, 0.195f, 0.44f, 0.38f, 1.54f },
        { "Pine",         0.520f, 0.375f, 0.185f, 0.60f, 0.55f, 1.51f },
        { "Cedar",        0.390f, 0.215f, 0.125f, 0.64f, 0.60f, 1.51f },
        { "Walnut",       0.125f, 0.072f, 0.042f, 0.46f, 0.70f, 1.55f },
        { "Dark walnut",  0.072f, 0.041f, 0.026f, 0.44f, 0.74f, 1.55f },
        { "Mahogany",     0.195f, 0.072f, 0.041f, 0.40f, 0.66f, 1.55f },
        { "Cherry",       0.265f, 0.110f, 0.062f, 0.36f, 0.52f, 1.54f },
        { "Teak",         0.235f, 0.145f, 0.062f, 0.50f, 0.64f, 1.54f },
        { "Rosewood",     0.105f, 0.048f, 0.035f, 0.34f, 0.78f, 1.56f },
        { "Ebony",        0.022f, 0.019f, 0.017f, 0.30f, 0.45f, 1.57f },
        { "Wenge",        0.062f, 0.045f, 0.035f, 0.42f, 0.80f, 1.56f },
        { "Zebrano",      0.330f, 0.215f, 0.105f, 0.46f, 0.88f, 1.54f },
        { "Bamboo",       0.470f, 0.380f, 0.195f, 0.40f, 0.50f, 1.52f },
        { "Plywood",      0.420f, 0.315f, 0.175f, 0.70f, 0.42f, 1.52f },
        { "Weathered",    0.300f, 0.280f, 0.255f, 0.88f, 0.58f, 1.50f },
        { "Charred",      0.028f, 0.024f, 0.022f, 0.92f, 0.36f, 1.49f } };

    const Timber& T = kTimbers[Column < 20u ? Column : 19u];
    const float Finish = std::clamp(Sweep, 0.0f, 1.0f);

    Tint(Slab.BaseColor, T.R, T.G, T.B);
    Slab.BaseMetalness               = 0.0f;
    Slab.SpecularIor                 = T.Ior;
    Slab.SpecularWeight              = 1.0f;
    // A finish fills the pores: the base lobe tightens as it is applied, it does not merely gain a coat.
    Slab.SpecularRoughness           = T.Rough * (1.0f - 0.55f * Finish);
    Slab.SpecularRoughnessAnisotropy = T.Grain;
    Slab.SlateAnisotropyRotation     = kPiOverTwo;        // grain runs up the trunk, i.e. along the ball's axis
    // Light that enters the cell structure and leaves nearby. Small, but it is why wood is not a painted board.
    Slab.SubsurfaceWeight            = 0.12f;
    Tint(Slab.SubsurfaceColor, T.R * 1.35f, T.G * 1.20f, T.B * 1.05f);
    Slab.SubsurfaceRadius            = 0.0016f;           // [m] sub-millimetre: cell walls, not flesh
    Slab.BaseDiffuseRoughness        = 0.45f;             // EON: a sanded surface is not Lambertian
    Slab.CoatWeight                  = Finish;
    Slab.CoatRoughness               = 0.015f + 0.085f * (1.0f - Finish);
    Slab.CoatIor                     = 1.50f;
    Slab.CoatDarkening               = 1.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PAPER
//------------------------------------------------------------------------------------------------------------------------

/// Paper and board stocks. The family is defined by thin-walled translucency, not by whiteness.
///
/// in  : Column 0–19 selects the stock; Sweep 0–1 raises the coating from uncoated to gloss art
/// note: GeometryThinWalled is the whole point. A sheet of paper is a translucent slab a tenth of a millimetre
///       thick, so the transmission has to be the thin-walled kind — a volumetric transmission with a depth
///       would make it a block of resin. Newsprint held to a window is the reference, not a white card.
inline void AuthorPaperStock(MaterialSlabDescriptor& Slab, uint32_t Column, float Sweep) noexcept
{
    using namespace ShowcaseFamilyDetail;
    struct Stock { const char* Label; float R, G, B; float Rough; float Through; float Fuzz; };
    static constexpr Stock kStocks[20] = {
        { "Newsprint",     0.720f, 0.700f, 0.635f, 0.94f, 0.42f, 0.25f },
        { "Copier 80",     0.880f, 0.885f, 0.880f, 0.90f, 0.30f, 0.18f },
        { "Bond",          0.855f, 0.855f, 0.840f, 0.88f, 0.26f, 0.20f },
        { "Laid",          0.840f, 0.830f, 0.790f, 0.91f, 0.28f, 0.24f },
        { "Watercolour",   0.880f, 0.870f, 0.835f, 0.96f, 0.18f, 0.32f },
        { "Cartridge",     0.845f, 0.835f, 0.800f, 0.93f, 0.20f, 0.26f },
        { "Tracing",       0.820f, 0.830f, 0.825f, 0.55f, 0.82f, 0.08f },
        { "Glassine",      0.840f, 0.845f, 0.840f, 0.40f, 0.88f, 0.05f },
        { "Tissue",        0.900f, 0.900f, 0.895f, 0.92f, 0.76f, 0.38f },
        { "Kraft",         0.420f, 0.280f, 0.160f, 0.92f, 0.14f, 0.28f },
        { "Corrugated",    0.395f, 0.285f, 0.185f, 0.95f, 0.06f, 0.30f },
        { "Greyboard",     0.330f, 0.325f, 0.315f, 0.94f, 0.04f, 0.26f },
        { "Blotting",      0.870f, 0.860f, 0.830f, 0.98f, 0.22f, 0.44f },
        { "Rice",          0.885f, 0.875f, 0.840f, 0.90f, 0.70f, 0.34f },
        { "Parchment",     0.790f, 0.740f, 0.615f, 0.80f, 0.36f, 0.16f },
        { "Matte art",     0.895f, 0.895f, 0.890f, 0.62f, 0.16f, 0.08f },
        { "Silk art",      0.900f, 0.900f, 0.898f, 0.42f, 0.14f, 0.05f },
        { "Gloss art",     0.905f, 0.905f, 0.905f, 0.16f, 0.12f, 0.02f },
        { "Photo RC",      0.910f, 0.910f, 0.912f, 0.08f, 0.05f, 0.01f },
        { "Black card",    0.032f, 0.031f, 0.033f, 0.90f, 0.02f, 0.22f } };

    const Stock& P = kStocks[Column < 20u ? Column : 19u];
    const float Coating = std::clamp(Sweep, 0.0f, 1.0f);

    Tint(Slab.BaseColor, P.R, P.G, P.B);
    Slab.BaseMetalness        = 0.0f;
    Slab.SpecularIor          = 1.47f;                    // cellulose
    Slab.SpecularWeight       = 1.0f;
    Slab.SpecularRoughness    = P.Rough * (1.0f - 0.72f * Coating);
    Slab.BaseDiffuseRoughness = 0.85f;                    // EON: fibre felt is far from Lambertian
    Slab.GeometryThinWalled   = true;
    Slab.TransmissionWeight   = P.Through;
    Tint(Slab.TransmissionColor, P.R, P.G, P.B);
    Slab.TransmissionDepth    = 0.0f;                     // thin-walled: no volume, by definition
    // Loose surface fibres. This is what makes an uncoated stock glow along a grazing edge.
    Slab.FuzzWeight           = P.Fuzz * (1.0f - 0.85f * Coating);
    Tint(Slab.FuzzColor, 1.0f, 0.99f, 0.96f);
    Slab.FuzzRoughness        = 0.65f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        FABRIC
//------------------------------------------------------------------------------------------------------------------------

/// Woven and knitted cloth: sheen-dominant, retroreflective, and anisotropic along the weave.
///
/// in  : Column 0–19 selects the cloth; Sweep 0–1 runs the weave from loose to tight
/// note: separate from the r4 "cloth fuzz" row, which sweeps FuzzWeight on a velvet to show the lobe in
///       isolation. This row is twenty cloths that differ in more than one parameter at once, which is the
///       only way the difference between satin and denim is visible.
inline void AuthorFabricWeave(MaterialSlabDescriptor& Slab, uint32_t Column, float Sweep) noexcept
{
    using namespace ShowcaseFamilyDetail;
    struct Cloth { const char* Label; float R, G, B; float Fuzz, FuzzRough, Rough, Aniso, Diffuse; };
    static constexpr Cloth kCloths[20] = {
        { "Cotton",      0.620f, 0.600f, 0.565f, 0.55f, 0.60f, 0.82f, 0.25f, 0.80f },
        { "Linen",       0.600f, 0.575f, 0.495f, 0.48f, 0.55f, 0.78f, 0.45f, 0.78f },
        { "Denim",       0.105f, 0.145f, 0.245f, 0.52f, 0.62f, 0.84f, 0.40f, 0.82f },
        { "Canvas",      0.465f, 0.420f, 0.325f, 0.45f, 0.58f, 0.88f, 0.35f, 0.85f },
        { "Wool",        0.305f, 0.270f, 0.235f, 0.82f, 0.72f, 0.90f, 0.12f, 0.90f },
        { "Tweed",       0.245f, 0.225f, 0.175f, 0.78f, 0.70f, 0.92f, 0.20f, 0.88f },
        { "Felt",        0.355f, 0.335f, 0.320f, 0.88f, 0.80f, 0.95f, 0.05f, 0.95f },
        { "Fleece",      0.455f, 0.450f, 0.445f, 0.92f, 0.78f, 0.93f, 0.04f, 0.92f },
        { "Velvet",      0.135f, 0.035f, 0.055f, 1.00f, 0.35f, 0.70f, 0.08f, 0.70f },
        { "Velour",      0.185f, 0.095f, 0.125f, 0.95f, 0.42f, 0.74f, 0.10f, 0.74f },
        { "Satin",       0.520f, 0.135f, 0.185f, 0.30f, 0.22f, 0.22f, 0.88f, 0.35f },
        { "Silk",        0.605f, 0.505f, 0.265f, 0.34f, 0.26f, 0.26f, 0.85f, 0.38f },
        { "Taffeta",     0.265f, 0.335f, 0.465f, 0.28f, 0.24f, 0.28f, 0.82f, 0.40f },
        { "Chiffon",     0.700f, 0.640f, 0.690f, 0.42f, 0.45f, 0.48f, 0.55f, 0.60f },
        { "Organza",     0.740f, 0.735f, 0.700f, 0.38f, 0.40f, 0.40f, 0.62f, 0.55f },
        { "Corduroy",    0.300f, 0.185f, 0.095f, 0.80f, 0.55f, 0.80f, 0.78f, 0.84f },
        { "Jersey knit", 0.420f, 0.215f, 0.215f, 0.68f, 0.65f, 0.86f, 0.18f, 0.86f },
        { "Terry",       0.800f, 0.790f, 0.760f, 0.90f, 0.82f, 0.94f, 0.06f, 0.94f },
        { "Nylon shell", 0.120f, 0.180f, 0.150f, 0.22f, 0.30f, 0.34f, 0.50f, 0.42f },
        { "Leather",     0.160f, 0.098f, 0.062f, 0.18f, 0.45f, 0.52f, 0.14f, 0.62f } };

    const Cloth& C = kCloths[Column < 20u ? Column : 19u];
    const float Tight = std::clamp(Sweep, 0.0f, 1.0f);

    Tint(Slab.BaseColor, C.R, C.G, C.B);
    Slab.BaseMetalness               = 0.0f;
    Slab.SpecularIor                 = 1.46f;             // cellulose / protein fibre
    Slab.SpecularWeight              = 1.0f;
    Slab.SpecularRoughness           = C.Rough * (1.0f - 0.25f * Tight);
    Slab.SpecularRoughnessAnisotropy = C.Aniso;
    Slab.SlateAnisotropyRotation     = 0.0f;              // weft runs across the ball
    Slab.BaseDiffuseRoughness        = C.Diffuse;         // EON: the retroreflection that makes cloth flat-lit
    Slab.FuzzWeight                  = C.Fuzz * (1.0f - 0.35f * Tight);
    Tint(Slab.FuzzColor, 1.0f, 1.0f, 1.0f);
    Slab.FuzzRoughness               = C.FuzzRough;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              IOR · F0 · F82 · F90
//------------------------------------------------------------------------------------------------------------------------

/// The Fresnel row: twenty columns that isolate reflectance at normal incidence, at the F82 tint angle and at
///    grazing, with everything else held still.
///
///    Columns  0–9  DIELECTRICS by IOR, 1.00 → 2.42. F0 = ((n−1)/(n+1))², so this walks F0 from 0.000 to 0.172
///                  with F90 pinned at 1 by the physics. Water, glass, sapphire and diamond sit on this line.
///    Columns 10–19 METALS by F82 tint. Fresnel for a metal is not one number: F0 is the normal-incidence
///                  colour, F82 is the measured dip at ~82°, and F90 returns to white. SpecularColor carries
///                  the F82 edge tint (Kutz, Hašan, Edmondson 2021; OpenPBR §5.3), so this half sweeps the
///                  edge tint across real metals while F0 stays each metal's own.
///
/// in  : Column 0–19; Sweep 0–1 roughens the lobe so the Fresnel gradient can be seen both sharp and blurred
/// note: roughness is deliberately LOW at the start of the sweep. The Fresnel falloff toward grazing is only
///       legible on a tight lobe; at roughness 0.5 the multiple-scattering compensation has already filled the
///       edge in and every column looks the same.
inline void AuthorFresnelLadder(MaterialSlabDescriptor& Slab, uint32_t Column, float Sweep) noexcept
{
    using namespace ShowcaseFamilyDetail;
    const uint32_t Slot = Column < 20u ? Column : 19u;
    const float    Blur = std::clamp(Sweep, 0.0f, 1.0f);

    if (Slot < 10u)
    {
        // Dielectrics. The label is the real substance that carries this IOR.
        struct Dielectric { const char* Label; float Ior; };
        static constexpr Dielectric kDielectrics[10] = {
            { "Air 1.00",      1.000f }, { "Ice 1.31",      1.310f },
            { "Water 1.333",   1.333f }, { "Acrylic 1.49",  1.491f },
            { "Glass 1.52",    1.520f }, { "Quartz 1.544",  1.544f },
            { "Polycarb 1.585",1.585f }, { "Sapphire 1.77", 1.770f },
            { "Zircon 1.92",   1.920f }, { "Diamond 2.417", 2.417f } };
        const Dielectric& Entry = kDielectrics[Slot];

        Tint(Slab.BaseColor, 0.055f, 0.055f, 0.058f);     // near-black, so only the SPECULAR is visible
        Slab.BaseMetalness      = 0.0f;
        Slab.SpecularIor        = Entry.Ior;
        Slab.SpecularWeight     = 1.0f;
        Tint(Slab.SpecularColor, 1.0f, 1.0f, 1.0f);       // untinted: F0 comes from the IOR alone
        Slab.SpecularRoughness  = 0.02f + 0.30f * Blur;
        Slab.BaseDiffuseRoughness = 0.0f;
        return;
    }

    // Metals. BaseColor is F0 (normal incidence); SpecularColor is the F82 edge tint. F90 is white for every
    //    conductor — that is not authored, it is what the Fresnel model does at grazing, and the row exists so
    //    that can be seen happening on the ball's silhouette.
    struct Conductor { const char* Label; float F0R, F0G, F0B; float F82R, F82G, F82B; };
    static constexpr Conductor kConductors[10] = {
        { "Aluminium", 0.912f, 0.914f, 0.920f, 0.970f, 0.970f, 0.975f },
        { "Silver",    0.972f, 0.960f, 0.915f, 0.995f, 0.985f, 0.960f },
        { "Chromium",  0.549f, 0.556f, 0.554f, 0.760f, 0.780f, 0.800f },
        { "Nickel",    0.660f, 0.609f, 0.526f, 0.800f, 0.780f, 0.750f },
        { "Iron",      0.560f, 0.570f, 0.580f, 0.740f, 0.760f, 0.790f },
        { "Titanium",  0.542f, 0.497f, 0.449f, 0.700f, 0.690f, 0.680f },
        { "Brass",     0.887f, 0.789f, 0.434f, 0.950f, 0.900f, 0.700f },
        { "Gold",      1.000f, 0.766f, 0.336f, 1.000f, 0.900f, 0.620f },
        { "Copper",    0.955f, 0.638f, 0.538f, 0.985f, 0.800f, 0.720f },
        { "Cobalt",    0.662f, 0.655f, 0.634f, 0.820f, 0.820f, 0.810f } };
    const Conductor& Metal = kConductors[Slot - 10u];

    Tint(Slab.BaseColor, Metal.F0R, Metal.F0G, Metal.F0B);
    Slab.BaseMetalness      = 1.0f;
    Tint(Slab.SpecularColor, Metal.F82R, Metal.F82G, Metal.F82B);
    Slab.SpecularWeight     = 1.0f;
    Slab.SpecularIor        = 1.5f;                       // unused while metalness is 1; kept sane for readers
    Slab.SpecularRoughness  = 0.02f + 0.30f * Blur;
}

} // namespace Frontier
