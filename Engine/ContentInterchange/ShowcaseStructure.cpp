//============================================================================================================================================
//                                                     SHOWCASESTRUCTURE.CPP
//============================================================================================================================================
// See ShowcaseStructure.h.
//
// Layout (top view, camera stands at −Y looking +Y). r4 widened the grid 6×6 → 15×15: 225 spheres, every one its
//    own generated material, 15 rows = 15 material FAMILIES, 15 columns = a parameter/hue sweep within the family:
//
//      Y = +19.2  row 14  showpieces: chrome · mirror · black gloss · pearl · coated velvet · … (hand-picked combos)
//      Y = +17.7  row 13  absorbing glass: hue sweep of the transmission colour at increasing depth
//      Y = +16.2  row 12  glint flakes: density × scale sweep over hue-tinted metal
//      Y = +14.7  row 11  matte ceramic/rubber: EON hue sweep alternating bright/dark
//      Y = +13.2  row 10  glossy plastic: clear coat over a hue sweep
//      Y = +11.7  row 9   rough metal: hue-tinted F0, roughness 0.05 → 0.90
//      Y = +10.2  row 8   emission: hue sweep at a modest 6 nit (low tessellation — these are luminaires)
//      Y =  +8.7  row 7   EON diffuse: hue rainbow, diffuse roughness 0 → 1
//      Y =  +7.2  row 6   haziness: weight 0 → 1 over a hue-tinted base
//      Y =  +5.7  row 5   coat / car paint: hue sweep, coat roughness ramp
//      Y =  +4.2  row 4   cloth / fuzz: velvet hue sweep, fuzz weight ramp
//      Y =  +2.7  row 3   thin film: thickness 0.15 → 1.30 µm over dark, gold and soap bases
//      Y =  +1.2  row 2   subsurface: the six authored SSS anchors + nine hue variants
//      Y =  −0.3  row 1   transmissive glass: IOR 1.30 → 2.42 with tint + dispersion ramps
//      Y =  −1.8  row 0   anisotropic metal: the six authored anchors + nine rotation/roughness variants
//                 X = −10.5 … +10.5 in 1.5 m steps, spheres r = 0.55 m resting on Z = 0
//
//    Between and around the grid sit scattered secondary shapes (boxes, cylinders, cones) carrying their own
//    materials, so the level is a scattered object field and not only a neat grid. Two 3×3 m area luminaires at
//    Z = 6 accent it — since the r4 rebalance the SUN is the key light (see the luminaire block), which is what
//    makes every sphere drop a visible shadow.

#include "ShowcaseStructure.h"
#include "AutomotiveShowcasePresets.h"
#include "SceneCodec.h"
#include "../DeviceExchange/OrientationClassifier.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace Frontier {

namespace {

constexpr float kPi = 3.14159265358979f;

MaterialDescriptor MakeMaterial(const char* Name)
{
    MaterialDescriptor D; D.Name = Name; D.Slabs.emplace_back(); return D;
}

void SetColor(float* Target, float R, float G, float B) { Target[0] = R; Target[1] = G; Target[2] = B; }

// The r4 grid's hue wheel: a linear-RGB rainbow (six 60° arcs, the HSV hexcone at full value) scaled by Sat/Val.
//    Deterministic and cheap — the generator calls it a few hundred times at export, never per frame.
void HueColor(float Hue, float Sat, float Val, float* Target)
{
    const float H = (Hue - std::floor(Hue)) * 6.0f;
    const int   Arc = static_cast<int>(H) % 6;
    const float F = H - std::floor(H);
    float R = 0.0f, G = 0.0f, B = 0.0f;
    switch (Arc)
    {
    case 0: R = 1.0f;     G = F;        B = 0.0f;     break;
    case 1: R = 1.0f - F; G = 1.0f;     B = 0.0f;     break;
    case 2: R = 0.0f;     G = 1.0f;     B = F;        break;
    case 3: R = 0.0f;     G = 1.0f - F; B = 1.0f;     break;
    case 4: R = F;        G = 0.0f;     B = 1.0f;     break;
    default:R = 1.0f;     G = 0.0f;     B = 1.0f - F; break;
    }
    Target[0] = Val * (1.0f - Sat * (1.0f - R));
    Target[1] = Val * (1.0f - Sat * (1.0f - G));
    Target[2] = Val * (1.0f - Sat * (1.0f - B));
}

// A small deterministic generator: the scattered shapes must land in the same places every run, or the accumulated
//    image and every kept reference render would differ frame to frame for no reason.
struct Lcg
{
    uint32_t State;
    float Next() noexcept { State = State * 1664525u + 1013904223u; return static_cast<float>(State >> 8) / 16777216.0f; }
    float Range(float Lo, float Hi) noexcept { return Lo + (Hi - Lo) * Next(); }
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        SPANS
//------------------------------------------------------------------------------------------------------------------------

ShowcaseStructure::SpanScope::~SpanScope() noexcept
{
    if (Spans == nullptr || Triangles == nullptr || Span >= Spans->size()) return;
    TriangleSpanRecord& S = (*Spans)[Span];
    const uint32_t Now = static_cast<uint32_t>(Triangles->size());
    S.TriangleCount = Now >= S.FirstTriangle ? Now - S.FirstTriangle : 0u;
}

ShowcaseStructure::SpanScope ShowcaseStructure::OpenSpan(const char* Name, bool Dynamic) noexcept
{
    TriangleSpanRecord S;
    S.FirstTriangle = static_cast<uint32_t>(Triangles.size());
    if (Name != nullptr) S.Name = Name;
    S.Dynamic = Dynamic;
    Spans.push_back(std::move(S));
    SpanScope Scope;
    Scope.Spans     = &Spans;
    Scope.Triangles = &Triangles;
    Scope.Span      = static_cast<uint32_t>(Spans.size()) - 1u;
    return Scope;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

void ShowcaseStructure::Construct() noexcept
{
    Triangles.clear(); CornerNormals.clear(); Materials.clear(); Spans.clear();

    // ── 0: ground ────────────────────────────────────────────────────────────────────────────────────────────────
    {
        MaterialDescriptor D = MakeMaterial("ground_concrete");
        SetColor(D.Slabs[0].BaseColor, 0.28f, 0.27f, 0.25f);
        D.Slabs[0].SpecularRoughness     = 0.55f;   // a real dielectric floor: it catches a sheen from the luminaires
        D.Slabs[0].SpecularWeight        = 1.0f;
        D.Slabs[0].SpecularIor           = 1.48f;
        D.Slabs[0].BaseDiffuseRoughness  = 0.6f;    // EON: dusty concrete, not a Lambertian card
        Materials.push_back(D);
    }

    // ── 1–225: the 15×15 grid's materials, generated ────────────────────────────────────────────────────────────
    //    r4 replaced the six hand-authored rows with a GENERATOR: 15 families × 15 sweeps = 225 distinct materials,
    //    one per sphere. Each row keeps one lobe family (so a row still reads as "the anisotropy row" from the
    //    default camera) and sweeps hue plus that family's defining parameter across its 15 columns. Deterministic:
    //    pure functions of (row, column), no RNG, so every export and every reference render agrees.
    for (uint32_t Row = 0u; Row < kShowcaseGridSide; ++Row)
        for (uint32_t Col = 0u; Col < kShowcaseGridSide; ++Col)
        {
            const float t   = static_cast<float>(Col) / static_cast<float>(kShowcaseGridSide - 1u);   // 0 → 1 sweep
            const float Hue = static_cast<float>(Col) / static_cast<float>(kShowcaseGridSide);        // hue wheel
            char Name[64];
            std::snprintf(Name, sizeof(Name), "grid_r%02u_c%02u", Row, Col);
            if (Row >= 15u) std::snprintf(Name,sizeof(Name),"%s %02u",kAutomotiveFamilyNames[Row-15u],Col+1u);
            MaterialDescriptor D = MakeMaterial(Name);
            MaterialSlabDescriptor& S = D.Slabs[0];
            switch (Row)
            {
            case 0:   // anisotropic metal: hue-tinted F0, anisotropy 0.2 → 0.95, brush direction swings 0 → 90°
                HueColor(Hue, 0.55f, 0.90f, S.BaseColor);
                SetColor(S.SpecularColor, 1.0f, 1.0f, 1.0f);
                S.BaseMetalness = 1.0f;
                S.SpecularRoughness = 0.14f + 0.26f * t;
                S.SpecularRoughnessAnisotropy = 0.2f + 0.75f * t;
                S.SlateAnisotropyRotation = t * (kPi * 0.5f);
                break;
            case 1:   // transmissive glass: IOR 1.30 → 2.42, tint sweeps the wheel, dispersion rises with the IOR
                SetColor(S.BaseColor, 1.0f, 1.0f, 1.0f);
                S.TransmissionWeight = 1.0f;
                HueColor(Hue, 0.18f, 1.0f, S.TransmissionColor);
                S.TransmissionDispersionScale = t;
                S.SpecularIor = 1.30f + 1.12f * t;
                S.SpecularRoughness = 0.01f;
                S.SpecularWeight = 1.0f;
                D.VolumeThickness = 1.1f;   // sphere diameter: interchange fidelity
                break;
            case 2:   // subsurface: scatter colour around the wheel, radius 12 → 80 mm, anisotropy −0.3 → +0.6
                HueColor(Hue, 0.35f, 0.88f, S.BaseColor);
                S.SubsurfaceWeight = 1.0f;
                HueColor(Hue, 0.35f, 0.88f, S.SubsurfaceColor);
                S.SubsurfaceRadius = 0.012f + 0.068f * t;
                HueColor(Hue, 0.55f, 1.0f, S.SubsurfaceRadiusScale);
                S.SubsurfaceScatterAnisotropy = -0.3f + 0.9f * t;
                S.SpecularRoughness = 0.32f;
                S.SpecularIor = 1.4f;
                break;
            case 3:   // thin film: thickness 0.15 → 1.30 µm over three cycling bases (dark, gold, soap-black)
            {
                const uint32_t Base = Col % 3u;
                if (Base == 0u)      { SetColor(S.BaseColor, 0.03f, 0.03f, 0.04f); S.BaseMetalness = 0.0f; }
                else if (Base == 1u) { SetColor(S.BaseColor, 1.0f, 0.766f, 0.336f); S.BaseMetalness = 1.0f; }
                else                 { SetColor(S.BaseColor, 0.0f, 0.0f, 0.0f);     S.BaseMetalness = 0.0f; }
                S.ThinFilmWeight = 1.0f;
                S.ThinFilmThickness = 0.15f + 1.15f * t;
                S.ThinFilmIor = 1.33f + 0.35f * static_cast<float>(Base);
                S.SpecularRoughness = 0.04f + 0.12f * static_cast<float>(Base);
                break;
            }
            case 4:   // cloth / fuzz: velvet hues, fuzz weight 0.4 → 1.0 and roughness rising with it
                HueColor(Hue, 0.75f, 0.45f, S.BaseColor);
                S.SpecularWeight = 0.0f;
                S.FuzzWeight = 0.4f + 0.6f * t;
                S.FuzzRoughness = 0.5f + 0.45f * t;
                HueColor(Hue, 0.25f, 1.0f, S.FuzzColor);
                break;
            case 5:   // coat / car paint: saturated base, clear coat whose roughness frosts 0 → 0.4
                HueColor(Hue, 0.85f, 0.50f, S.BaseColor);
                S.SpecularRoughness = 0.45f;
                S.CoatWeight = 1.0f;
                S.CoatRoughness = 0.4f * t;
                S.CoatIor = 1.6f;
                S.CoatDarkening = 1.0f;
                break;
            case 6:   // haziness: second specular lobe grows 0 → 0.95 over a dark tinted base
                HueColor(Hue, 0.45f, 0.22f, S.BaseColor);
                S.SpecularRoughness = 0.10f;
                S.SlateHazinessWeight = 0.95f * t;
                S.SlateHazinessRoughness = 0.50f + 0.35f * t;
                break;
            case 7:   // EON diffuse: the plainest row — a hue rainbow with diffuse roughness 0 → 1
                HueColor(Hue, 0.80f, 0.75f, S.BaseColor);
                S.SpecularWeight = 0.0f;
                S.BaseDiffuseRoughness = t;
                break;
            case 8:   // emission: a luminaire rainbow at a modest 6 nit (the sun stays the key light; these are
                      //    colour accents, tessellated low because every triangle is a luminaire table entry)
                SetColor(S.BaseColor, 0.0f, 0.0f, 0.0f);
                S.SpecularWeight = 0.0f;
                S.EmissionLuminance = 6.0f;
                HueColor(Hue, 0.85f, 1.0f, S.EmissionColor);
                break;
            case 9:   // rough metal: hue-tinted, roughness 0.05 → 0.90 — the classic roughness ladder, in colour
                HueColor(Hue, 0.40f, 0.85f, S.BaseColor);
                S.BaseMetalness = 1.0f;
                S.SpecularRoughness = 0.05f + 0.85f * t;
                break;
            case 10:  // dielectric → metal morph: metalness itself is the sweep, hue fixed per column
                HueColor(Hue, 0.70f, 0.65f, S.BaseColor);
                S.BaseMetalness = t;
                S.SpecularRoughness = 0.22f;
                break;
            case 11:  // matte ceramic / rubber, alternating: even columns bright satin ceramic, odd dark rubber
                if ((Col & 1u) == 0u) { HueColor(Hue, 0.30f, 0.90f, S.BaseColor); S.SpecularWeight = 0.4f; S.SpecularRoughness = 0.55f; S.BaseDiffuseRoughness = 0.5f; }
                else                  { HueColor(Hue, 0.55f, 0.10f, S.BaseColor); S.SpecularWeight = 0.5f; S.SpecularRoughness = 0.85f; S.BaseDiffuseRoughness = 1.0f; }
                break;
            case 12:  // glint flakes: Deliot–Belcour density 1 → 8 over hue-tinted metal
                HueColor(Hue, 0.50f, 0.30f, S.BaseColor);
                S.BaseMetalness = 0.8f;
                S.SpecularRoughness = 0.25f;
                S.SlateGlintDensity = 1.0f + 7.0f * t;
                S.SlateGlintUvScale = 4.0f + 8.0f * t;
                break;
            case 13:  // absorbing glass: fixed IOR, hue-tinted absorption deepening 0.10 → 0.60 m
                SetColor(S.BaseColor, 1.0f, 1.0f, 1.0f);
                S.TransmissionWeight = 1.0f;
                HueColor(Hue, 0.70f, 0.85f, S.TransmissionColor);
                S.TransmissionDepth = 0.10f + 0.50f * t;
                S.SpecularIor = 1.52f;
                S.SpecularRoughness = 0.05f;
                D.VolumeThickness = 1.1f;
                break;
            case 14:  // row 14 — showpieces: five hand-picked hero combinations cycling with a hue accent
            {
                const uint32_t Kind = Col % 5u;
                if (Kind == 0u)      { SetColor(S.BaseColor, 0.95f, 0.96f, 0.97f); S.BaseMetalness = 1.0f; S.SpecularRoughness = 0.03f; }                                   // chrome mirror
                else if (Kind == 1u) { HueColor(Hue, 0.90f, 0.06f, S.BaseColor); S.SpecularRoughness = 0.30f; S.CoatWeight = 1.0f; S.CoatRoughness = 0.0f; }                // black-gloss coat
                else if (Kind == 2u) { HueColor(Hue, 0.15f, 0.95f, S.BaseColor); S.SubsurfaceWeight = 0.6f; HueColor(Hue, 0.15f, 0.95f, S.SubsurfaceColor);
                                       S.SubsurfaceRadius = 0.02f; S.CoatWeight = 1.0f; S.CoatRoughness = 0.05f; S.ThinFilmWeight = 0.4f; S.ThinFilmThickness = 0.35f; }    // pearl
                else if (Kind == 3u) { SetColor(S.BaseColor, 1.0f, 0.766f, 0.336f); S.BaseMetalness = 1.0f; S.SpecularRoughness = 0.06f; }                                   // gold mirror
                else                 { SetColor(S.BaseColor, 1.0f, 1.0f, 1.0f); S.TransmissionWeight = 1.0f; HueColor(Hue, 0.10f, 1.0f, S.TransmissionColor);
                                       S.SpecularIor = 1.5f; S.SpecularRoughness = 0.30f; D.VolumeThickness = 1.1f; }                                                        // frosted glass
                break;
            }
            case 15: case 16: case 17: case 18: case 19:
                AuthorAutomotiveShowcase(S, Row - 14u, t);
                break;
            }
            Materials.push_back(D);
        }

    const uint32_t kGridMaterialCount = static_cast<uint32_t>(Materials.size()) - 1u;   // everything but the ground

    // ── 226–233: the scattered field's own materials ─────────────────────────────────────────────────────────────
    const uint32_t ScatterFirst = static_cast<uint32_t>(Materials.size());
    {
        struct Scatter { const char* Name; float Base[3]; float Rough; float Metal; float Coat; float Trans; };
        const Scatter Set[8] = {
            { "scatter_terracotta", { 0.55f, 0.25f, 0.15f }, 0.75f, 0.0f, 0.0f, 0.0f },
            { "scatter_slate",      { 0.12f, 0.13f, 0.15f }, 0.60f, 0.0f, 0.0f, 0.0f },
            { "scatter_brass",      { 0.85f, 0.65f, 0.30f }, 0.30f, 1.0f, 0.0f, 0.0f },
            { "scatter_plastic",    { 0.15f, 0.45f, 0.35f }, 0.35f, 0.0f, 1.0f, 0.0f },
            { "scatter_chrome",     { 0.90f, 0.91f, 0.92f }, 0.08f, 1.0f, 0.0f, 0.0f },
            { "scatter_amber",      { 0.85f, 0.50f, 0.10f }, 0.10f, 0.0f, 0.0f, 0.9f },
            { "scatter_chalk",      { 0.85f, 0.84f, 0.80f }, 0.95f, 0.0f, 0.0f, 0.0f },
            { "scatter_rubber",     { 0.05f, 0.05f, 0.05f }, 0.85f, 0.0f, 0.0f, 0.0f } };
        for (const Scatter& S : Set)
        {
            MaterialDescriptor D = MakeMaterial(S.Name);
            SetColor(D.Slabs[0].BaseColor, S.Base[0], S.Base[1], S.Base[2]);
            D.Slabs[0].SpecularRoughness   = S.Rough;
            D.Slabs[0].BaseMetalness       = S.Metal;
            D.Slabs[0].CoatWeight          = S.Coat;
            D.Slabs[0].TransmissionWeight  = S.Trans;
            if (S.Trans > 0.0f) { D.Slabs[0].SpecularIor = 1.55f; SetColor(D.Slabs[0].TransmissionColor, S.Base[0], S.Base[1], S.Base[2]); }
            Materials.push_back(D);
        }
    }

    // ── the interface panel's berth: stand + housing slab ───────────────────────────────────────────────────────
    //    The panel itself (figures, materials, its light) is the PROJECT's — this level only guarantees the panel a
    //    physical body: a slab the figures visually sit on, and a stand holding it at eye height. Dark satin
    //    dielectric, the fascia every consumer of the level sees and occludes against.
    const uint32_t PanelBodyMaterial = static_cast<uint32_t>(Materials.size());
    {
        MaterialDescriptor D = MakeMaterial("interface_panel_body");
        SetColor(D.Slabs[0].BaseColor, 0.020f, 0.022f, 0.026f);
        D.Slabs[0].SpecularWeight     = 1.0f;
        D.Slabs[0].SpecularIor        = 1.5f;
        D.Slabs[0].SpecularRoughness  = 0.35f;   // satin, not gloss: a mirror bezel would fight the figures
        Materials.push_back(D);

        D = MakeMaterial("interface_panel_stand");
        SetColor(D.Slabs[0].BaseColor, 0.10f, 0.10f, 0.11f);
        D.Slabs[0].BaseMetalness      = 1.0f;
        D.Slabs[0].SpecularRoughness  = 0.45f;   // brushed dark steel
        Materials.push_back(D);
    }

    // ── last: the luminaire ──────────────────────────────────────────────────────────────────────────────────────
    //    ⚠️ THE WHOLE LEVEL DEPENDS ON THIS EXISTING. The previous showcase had no emissive triangle anywhere, so the
    //    scene reported "0 luminaires": PlaceShadowTaps refused (empty emitter set ⇒ no shadow maps) and the ReSTIR
    //    direct-light loop had nothing to draw a candidate from, so there was no indirect bounce either. Two panels
    //    rather than one so objects catch a second, softer shadow and the GI has more than a single direction.
    const uint32_t LuminaireMaterial = static_cast<uint32_t>(Materials.size());
    {
        // ⚠️ LUMINANCE REBALANCED (2026-09-19 shadow diagnosis, r4). The panels shipped at 140/60 nits, which put
        //    ~35 lux on the floor beneath them while the sun's panel-calibrated direct term delivered ~0.95 — a
        //    37:1 ratio that made the ReSTIR sun shadows a 2–3% modulation nobody could see ("no shadows" reports,
        //    GTX 1650 SUPER run 2026-09-19). This is an OUTDOOR level: the sun is the key light. 6/3 nits keeps the
        //    panels reading as bright emitters (20–30× the concrete's reflected luminance) while the sun, at the
        //    raised SunDirect default (2.5), out-lights them ~2:1 even directly underneath and ≥3:1 elsewhere —
        //    so every sphere finally drops the crisp sun shadow the level was built to prove.
        MaterialDescriptor D = MakeMaterial("luminaire_key");
        SetColor(D.Slabs[0].BaseColor, 1.0f, 1.0f, 1.0f); D.Slabs[0].SpecularWeight = 0.0f;
        D.Slabs[0].EmissionLuminance = 6.0f; SetColor(D.Slabs[0].EmissionColor, 1.0f, 0.97f, 0.92f);
        Materials.push_back(D);

        D = MakeMaterial("luminaire_fill");
        SetColor(D.Slabs[0].BaseColor, 1.0f, 1.0f, 1.0f); D.Slabs[0].SpecularWeight = 0.0f;
        D.Slabs[0].EmissionLuminance = 3.0f; SetColor(D.Slabs[0].EmissionColor, 0.80f, 0.88f, 1.0f);
        Materials.push_back(D);
    }

    // r5 — SPOT LIGHTS AS FIXTURES. The engine's punctual-light records (KHR_lights_punctual) are imported but the
    //    kernel does not light from them yet (SceneStructure.h: "stored only in R4a"), so a spot is built the way a
    //    physical one is: a small, BRIGHT emissive quad recessed inside an open hood. The hood's plates shadow the
    //    emission into a cone — no kernel change, and the ReSTIR path treats it like any other luminaire: the alias
    //    table weights it by power, shadow rays give the cone its edge, and the denoiser sees a normal light. The
    //    150-nit disc is small (0.13 m²), so its POWER stays modest next to the 9 m² panels — high contrast pool,
    //    low sampling weight.
    const uint32_t SpotMaterial = static_cast<uint32_t>(Materials.size());
    {
        MaterialDescriptor D = MakeMaterial("spot_warm");
        SetColor(D.Slabs[0].BaseColor, 1.0f, 1.0f, 1.0f); D.Slabs[0].SpecularWeight = 0.0f;
        D.Slabs[0].EmissionLuminance = 150.0f; SetColor(D.Slabs[0].EmissionColor, 1.0f, 0.85f, 0.60f);
        Materials.push_back(D);

        D = MakeMaterial("spot_cool");
        SetColor(D.Slabs[0].BaseColor, 1.0f, 1.0f, 1.0f); D.Slabs[0].SpecularWeight = 0.0f;
        D.Slabs[0].EmissionLuminance = 150.0f; SetColor(D.Slabs[0].EmissionColor, 0.65f, 0.80f, 1.0f);
        Materials.push_back(D);
    }

    //──────────────────────────────────────────────────────────────────────────────────────────────────────────────
    //                                                  GEOMETRY
    //──────────────────────────────────────────────────────────────────────────────────────────────────────────────

    // Ground. 80 × 80 m (r4: the 15×15 grid spans 21 m and its scatter ring reaches ~27 m from the grid centre, so
    //    the old ±30 plane would have dropped the ring's far side off the edge). Still bounded — the 1000 × 1000
    //    plane this level originally had put the scene bounds at ±500 m, which made the BVH root enormous next to
    //    1 m objects and gave the shadow taps a far plane kilometres deep.
    {
        const auto GroundSpan = OpenSpan("Ground");
        AppendQuad(Vector3{ -40.0f, -40.0f, 0.0f }, Vector3{ 40.0f, -40.0f, 0.0f },
                   Vector3{ 40.0f, 40.0f, 0.0f }, Vector3{ -40.0f, 40.0f, 0.0f }, 0u, 0.15f);
    }

    // The material grid: kShowcaseGridSide² spheres, one per generated material. r4 geometry budget: 15×15 at
    //    16 rings × 32 segments ≈ 992 tris/sphere ≈ 223 k for the grid — a deliberate step down from the old 24×48
    //    (2 208 tris) so 225 spheres cost ~2.4× the old 36, not 15×. The EMISSION row tessellates far lower still
    //    (8×16 ≈ 240 tris): every emissive triangle is a luminaire-table entry the ReSTIR light pick must consider,
    //    and 15 rainbow luminaires at 992 tris each would put ~15 k rows in the alias table for no visual gain.
    constexpr float kRadius   = 0.55f;
    constexpr float kSpacing  = 1.5f;
    constexpr float kGridEdge = kSpacing * static_cast<float>(kShowcaseGridSide - 1u) * 0.5f;   // 10.5 m
    constexpr uint32_t kEmissionRow = 8u;   // matches the generator's case 8
    for (uint32_t Index = 0u; Index < kGridMaterialCount; ++Index)
    {
        const uint32_t Material = Index + 1u;                       // 0 is the ground
        const uint32_t Row = Index / kShowcaseGridSide, Column = Index % kShowcaseGridSide;
        const Vector3 Centre{ -kGridEdge + kSpacing * static_cast<float>(Column),
                              -1.8f + kSpacing * static_cast<float>(Row),
                              kRadius };
        char Name[96];
        std::snprintf(Name, sizeof(Name), "Sphere %03u (%s)", Material, Materials[Material].Name.c_str());
        const auto Span = OpenSpan(Name, true);
        if (Row == kEmissionRow) AppendSphere(Centre, kRadius, Material, 8u, 16u);
        else                     AppendSphere(Centre, kRadius, Material, 16u, 32u);
    }

    // Small plinths under each sphere: they give the contact shadows something to land on, which is the cheapest
    //    way to see at a glance whether the shadow stage is actually running.
    for (uint32_t Index = 0u; Index < kGridMaterialCount; ++Index)
    {
        const uint32_t Row = Index / kShowcaseGridSide, Column = Index % kShowcaseGridSide;
        const Vector3 Centre{ -kGridEdge + kSpacing * static_cast<float>(Column),
                              -1.8f + kSpacing * static_cast<float>(Row),
                              0.03f };
        char Name[64];
        std::snprintf(Name, sizeof(Name), "Plinth %03u", Index + 1u);
        const auto Span = OpenSpan(Name);
        AppendCylinder(Vector3{ Centre.x, Centre.y, 0.0f }, kRadius * 1.15f, 0.06f, ScatterFirst + 1u, 16u);
    }

    // The scattered field: boxes, cylinders and cones ringing the grid, deterministic placement, each on its own
    //    material from the scatter set. This is what keeps the level a scattered object field rather than a bare grid.
    {
        Lcg Rng{ 20260918u };
        uint32_t Built = 0u;
        uint32_t Guard = 0u;
        while (Built < 40u && Guard < 4000u)
        {
            ++Guard;
            // r4: the ring is centred on the GRID's centre (0, 8.7) and starts past its corner reach (√2·10.5 ≈
            //    14.8 m + the largest shape), so no scattered body can land inside the 15×15 field.
            const float Angle  = Rng.Range(0.0f, 2.0f * kPi);
            const float Radial = Rng.Range(std::sqrt(2.0f)*kGridEdge+2.5f, 25.0f);
            const Vector3 Spot{ Radial * std::cos(Angle), (-1.8f+kGridEdge) + Radial * std::sin(Angle), 0.0f };

            const float Size     = Rng.Range(0.5f, 1.9f);
            const float Spin     = Rng.Range(0.0f, 2.0f * kPi);
            const uint32_t Material = ScatterFirst + (Built % 8u);
            const uint32_t Shape    = Built % 4u;

            char Name[64];
            static const char* const ShapeNames[4] = { "Box", "Cylinder", "Cone", "Sphere" };
            std::snprintf(Name, sizeof(Name), "Scatter %02u (%s)", Built, ShapeNames[Shape]);
            const auto Span = OpenSpan(Name);
            switch (Shape)
            {
            case 0: AppendBox(Vector3{ Spot.x, Spot.y, Size * 0.5f }, Vector3{ Size * 0.5f, Size * 0.4f, Size * 0.5f }, Spin, Material); break;
            case 1: AppendCylinder(Spot, Size * 0.4f, Size * 1.4f, Material, 20u); break;
            case 2: AppendCone(Spot, Size * 0.5f, Size * 1.5f, Material, 20u); break;
            default: AppendSphere(Vector3{ Spot.x, Spot.y, Size * 0.5f }, Size * 0.5f, Material, 16u, 28u); break;
            }
            ++Built;
        }
    }

    // The interface panel's body. The FACE plane the project seats its figures on is published in the header
    //    (kShowcasePanelCentre*, kShowcasePanelScale); the slab here sits 5 mm behind it so the figures read as
    //    laminated onto glass rather than floating. Half extents follow the trial panel's authored 0.180 × 0.110
    //    at the published scale, with a 20 mm bezel margin all round.
    {
        constexpr float FaceX = kShowcasePanelCentreX, FaceY = kShowcasePanelCentreY, FaceZ = kShowcasePanelCentreZ;
        constexpr float HalfW = 0.180f * kShowcasePanelScale + 0.02f;   // [m] slab half width
        constexpr float HalfH = 0.110f * kShowcasePanelScale + 0.02f;   // [m] slab half height
        {
            const auto SlabSpan = OpenSpan("Interface Panel Housing");
            AppendBox(Vector3{ FaceX, FaceY + 0.030f, FaceZ }, Vector3{ HalfW, 0.025f, HalfH }, 0.0f, PanelBodyMaterial);
        }
        {
            const auto StandSpan = OpenSpan("Interface Panel Stand");
            AppendCylinder(Vector3{ FaceX, FaceY + 0.030f, 0.0f }, 0.06f, FaceZ - HalfH + 0.01f, PanelBodyMaterial + 1u, 16u);
            AppendCylinder(Vector3{ FaceX, FaceY + 0.030f, 0.0f }, 0.30f, 0.02f, PanelBodyMaterial + 1u, 24u);
        }
    }

    // r5 — the two spot fixtures' HOUSINGS: pole + open-bottomed square shroud, flanking the grid's front corners.
    //    ⚠️ AppendBox and AppendCylinder build CLOSED solids — a closed hood would trap the light entirely — so the
    //    shroud is four side plates and a top cap from AppendQuad, open at the bottom. The emissive disc sits
    //    recessed 0.05 m inside the opening; the walls (0.35 m deep on a 0.44 m mouth) shadow it into a ~32°
    //    half-angle cone, so each spot throws a crisp ~2.3 m pool on the ground in the default shot. The housings
    //    are ordinary geometry; the DISCS join the span list at the very end (luminaires-last convention).
    constexpr float kSpotX[2]   = { -7.0f, 7.0f };
    constexpr float kSpotY      = -4.5f;
    constexpr float kSpotHeadZ  = 4.0f;    // [m] underside of the top cap
    constexpr float kSpotHalf   = 0.22f;   // [m] shroud mouth half-width
    constexpr float kSpotDepth  = 0.35f;   // [m] shroud wall depth
    for (uint32_t SpotIndex = 0u; SpotIndex < 2u; ++SpotIndex)
    {
        const float X = kSpotX[SpotIndex], Y = kSpotY;
        char Name[64];
        std::snprintf(Name, sizeof(Name), "Spot Fixture %u", SpotIndex + 1u);
        const auto Span = OpenSpan(Name);
        // Pole and a small base plate, sharing the scatter field's dark-metal material.
        AppendCylinder(Vector3{ X, Y, 0.0f }, 0.05f, kSpotHeadZ + 0.05f, ScatterFirst + 1u, 12u);
        AppendCylinder(Vector3{ X, Y, 0.0f }, 0.22f, 0.02f, ScatterFirst + 1u, 16u);
        // The shroud: four walls (outward normals; the shadowing is geometric, not shading-dependent) + top cap.
        const float H = kSpotHalf, Z0 = kSpotHeadZ - kSpotDepth, Z1 = kSpotHeadZ + 0.02f;
        AppendQuad(Vector3{ X - H, Y - H, Z0 }, Vector3{ X + H, Y - H, Z0 },
                   Vector3{ X + H, Y - H, Z1 }, Vector3{ X - H, Y - H, Z1 }, ScatterFirst + 1u, 1.0f);   // −Y wall
        AppendQuad(Vector3{ X + H, Y + H, Z0 }, Vector3{ X - H, Y + H, Z0 },
                   Vector3{ X - H, Y + H, Z1 }, Vector3{ X + H, Y + H, Z1 }, ScatterFirst + 1u, 1.0f);   // +Y wall
        AppendQuad(Vector3{ X - H, Y + H, Z0 }, Vector3{ X - H, Y - H, Z0 },
                   Vector3{ X - H, Y - H, Z1 }, Vector3{ X - H, Y + H, Z1 }, ScatterFirst + 1u, 1.0f);   // −X wall
        AppendQuad(Vector3{ X + H, Y - H, Z0 }, Vector3{ X + H, Y + H, Z0 },
                   Vector3{ X + H, Y + H, Z1 }, Vector3{ X + H, Y - H, Z1 }, ScatterFirst + 1u, 1.0f);   // +X wall
        AppendQuad(Vector3{ X - H, Y - H, Z1 }, Vector3{ X + H, Y - H, Z1 },
                   Vector3{ X + H, Y + H, Z1 }, Vector3{ X - H, Y + H, Z1 }, ScatterFirst + 1u, 1.0f);   // top cap
    }

    // Luminaires LAST (the convention every other level follows: the emissive spans close the list).
    {
        const auto KeySpan = OpenSpan("Luminaire Key");
        AppendQuad(Vector3{ -3.5f, 4.0f, 6.0f }, Vector3{ -0.5f, 4.0f, 6.0f },
                   Vector3{ -0.5f, 1.0f, 6.0f }, Vector3{ -3.5f, 1.0f, 6.0f }, LuminaireMaterial, 1.0f);
    }
    {
        const auto FillSpan = OpenSpan("Luminaire Fill");
        AppendQuad(Vector3{ 1.0f, 4.0f, 6.0f }, Vector3{ 4.0f, 4.0f, 6.0f },
                   Vector3{ 4.0f, 1.0f, 6.0f }, Vector3{ 1.0f, 1.0f, 6.0f }, LuminaireMaterial + 1u, 1.0f);
    }
    // The spot DISCS: down-facing emissive quads recessed inside each shroud (winding chosen so the derived normal
    //    is −Z — PHatFull samples the normal side, so the emission goes down into the cone, not up into the cap).
    for (uint32_t SpotIndex = 0u; SpotIndex < 2u; ++SpotIndex)
    {
        const float X = kSpotX[SpotIndex], Y = kSpotY;
        const float H = kSpotHalf * 0.82f, Z = kSpotHeadZ - 0.05f;
        char Name[64];
        std::snprintf(Name, sizeof(Name), "Spot Disc %u", SpotIndex + 1u);
        const auto Span = OpenSpan(Name);
        AppendQuad(Vector3{ X - H, Y - H, Z }, Vector3{ X - H, Y + H, Z },
                   Vector3{ X + H, Y + H, Z }, Vector3{ X + H, Y - H, Z }, SpotMaterial + SpotIndex, 1.0f);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

void ShowcaseStructure::AppendTriangle(const Vector3 P[3], const Vector3 N[3], const float Uv[3][2], uint32_t Material) noexcept
{
    TriangleIndex T{};
    T.VertexAlphaX = P[0].x; T.VertexAlphaY = P[0].y; T.VertexAlphaZ = P[0].z;
    T.VertexBetaX  = P[1].x; T.VertexBetaY  = P[1].y; T.VertexBetaZ  = P[1].z;
    T.VertexGammaX = P[2].x; T.VertexGammaY = P[2].y; T.VertexGammaZ = P[2].z;
    std::memcpy(&T.MaterialSlot, &Material, sizeof(Material));
    T.TextureAlphaU = Uv[0][0]; T.TextureAlphaV = Uv[0][1];
    T.TextureBetaU  = Uv[1][0]; T.TextureBetaV  = Uv[1][1];
    T.TextureGammaU = Uv[2][0]; T.TextureGammaV = Uv[2][1];
    Triangles.push_back(T);
    CornerNormals.push_back(N[0]); CornerNormals.push_back(N[1]); CornerNormals.push_back(N[2]);
}

void ShowcaseStructure::AppendQuad(const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D, uint32_t Material, float UvScale) noexcept
{
    const Vector3 Cross = OrientationClassifier::CrossProduct(B - A, C - A);
    const float   Len   = Cross.Length();
    const Vector3 N     = Len > 0.0f ? Cross / Len : Vector3{ 0.0f, 0.0f, 1.0f };
    const Vector3 Ns[3] = { N, N, N };
    const float   SizeU = (B - A).Length() * UvScale, SizeV = (D - A).Length() * UvScale;
    const Vector3 P0[3] = { A, B, C }; const float U0[3][2] = { { 0.0f, 0.0f }, { SizeU, 0.0f }, { SizeU, SizeV } };
    const Vector3 P1[3] = { A, C, D }; const float U1[3][2] = { { 0.0f, 0.0f }, { SizeU, SizeV }, { 0.0f, SizeV } };
    AppendTriangle(P0, Ns, U0, Material);
    AppendTriangle(P1, Ns, U1, Material);
}

void ShowcaseStructure::AppendSphere(const Vector3& Centre, float Radius, uint32_t Material, uint32_t Rings, uint32_t Segments) noexcept
{
    const auto Point = [&](uint32_t Ring, uint32_t Segment, Vector3& P, Vector3& N, float Uv[2])
    {
        const float V     = static_cast<float>(Ring) / static_cast<float>(Rings);
        const float U     = static_cast<float>(Segment) / static_cast<float>(Segments);
        const float Theta = V * kPi, Phi = U * 2.0f * kPi;
        N  = Vector3{ std::sin(Theta) * std::cos(Phi), std::sin(Theta) * std::sin(Phi), std::cos(Theta) };
        P  = Centre + N * Radius;
        Uv[0] = U; Uv[1] = V;
    };
    for (uint32_t Ring = 0u; Ring < Rings; ++Ring)
        for (uint32_t Segment = 0u; Segment < Segments; ++Segment)
        {
            Vector3 P00, P01, P10, P11, N00, N01, N10, N11; float U00[2], U01[2], U10[2], U11[2];
            Point(Ring,      Segment,      P00, N00, U00);
            Point(Ring,      Segment + 1u, P01, N01, U01);
            Point(Ring + 1u, Segment,      P10, N10, U10);
            Point(Ring + 1u, Segment + 1u, P11, N11, U11);
            if (Ring != 0u)         { const Vector3 P[3] = { P00, P10, P01 }; const Vector3 N[3] = { N00, N10, N01 }; const float Uv[3][2] = { { U00[0], U00[1] }, { U10[0], U10[1] }, { U01[0], U01[1] } }; AppendTriangle(P, N, Uv, Material); }
            if (Ring + 1u != Rings) { const Vector3 P[3] = { P01, P10, P11 }; const Vector3 N[3] = { N01, N10, N11 }; const float Uv[3][2] = { { U01[0], U01[1] }, { U10[0], U10[1] }, { U11[0], U11[1] } }; AppendTriangle(P, N, Uv, Material); }
        }
}

void ShowcaseStructure::AppendBox(const Vector3& Centre, const Vector3& HalfExtent, float RotationRadians, uint32_t Material) noexcept
{
    const float C = std::cos(RotationRadians), S = std::sin(RotationRadians);
    const auto Rotate = [&](const Vector3& V) { return Vector3{ V.x * C - V.y * S, V.x * S + V.y * C, V.z }; };
    const float X = HalfExtent.x, Y = HalfExtent.y, Z = HalfExtent.z;
    const Vector3 K[8] = {
        Centre + Rotate(Vector3{ -X, -Y, -Z }), Centre + Rotate(Vector3{  X, -Y, -Z }),
        Centre + Rotate(Vector3{  X,  Y, -Z }), Centre + Rotate(Vector3{ -X,  Y, -Z }),
        Centre + Rotate(Vector3{ -X, -Y,  Z }), Centre + Rotate(Vector3{  X, -Y,  Z }),
        Centre + Rotate(Vector3{  X,  Y,  Z }), Centre + Rotate(Vector3{ -X,  Y,  Z }) };
    AppendQuad(K[4], K[5], K[6], K[7], Material, 1.0f);   // +Z
    AppendQuad(K[0], K[3], K[2], K[1], Material, 1.0f);   // −Z
    AppendQuad(K[1], K[2], K[6], K[5], Material, 1.0f);   // +X
    AppendQuad(K[0], K[4], K[7], K[3], Material, 1.0f);   // −X
    AppendQuad(K[2], K[3], K[7], K[6], Material, 1.0f);   // +Y
    AppendQuad(K[0], K[1], K[5], K[4], Material, 1.0f);   // −Y
}

void ShowcaseStructure::AppendCylinder(const Vector3& Base, float Radius, float Height, uint32_t Material, uint32_t Segments) noexcept
{
    const Vector3 Up{ 0.0f, 0.0f, 1.0f }, Down{ 0.0f, 0.0f, -1.0f };
    for (uint32_t I = 0u; I < Segments; ++I)
    {
        const float A0 = 2.0f * kPi * static_cast<float>(I)        / static_cast<float>(Segments);
        const float A1 = 2.0f * kPi * static_cast<float>(I + 1u)   / static_cast<float>(Segments);
        const Vector3 N0{ std::cos(A0), std::sin(A0), 0.0f }, N1{ std::cos(A1), std::sin(A1), 0.0f };
        const Vector3 B0 = Base + N0 * Radius,             B1 = Base + N1 * Radius;
        const Vector3 T0 = B0 + Vector3{ 0.0f, 0.0f, Height }, T1 = B1 + Vector3{ 0.0f, 0.0f, Height };
        const float U0 = static_cast<float>(I) / static_cast<float>(Segments);
        const float U1 = static_cast<float>(I + 1u) / static_cast<float>(Segments);
        {   const Vector3 P[3] = { B0, B1, T1 }; const Vector3 N[3] = { N0, N1, N1 }; const float Uv[3][2] = { { U0, 0.0f }, { U1, 0.0f }, { U1, 1.0f } }; AppendTriangle(P, N, Uv, Material); }
        {   const Vector3 P[3] = { B0, T1, T0 }; const Vector3 N[3] = { N0, N1, N0 }; const float Uv[3][2] = { { U0, 0.0f }, { U1, 1.0f }, { U0, 1.0f } }; AppendTriangle(P, N, Uv, Material); }
        const Vector3 TopCentre = Base + Vector3{ 0.0f, 0.0f, Height };
        {   const Vector3 P[3] = { TopCentre, T0, T1 }; const Vector3 N[3] = { Up, Up, Up }; const float Uv[3][2] = { { 0.5f, 0.5f }, { U0, 1.0f }, { U1, 1.0f } }; AppendTriangle(P, N, Uv, Material); }
        {   const Vector3 P[3] = { Base, B1, B0 };      const Vector3 N[3] = { Down, Down, Down }; const float Uv[3][2] = { { 0.5f, 0.5f }, { U1, 0.0f }, { U0, 0.0f } }; AppendTriangle(P, N, Uv, Material); }
    }
}

void ShowcaseStructure::AppendCone(const Vector3& Base, float Radius, float Height, uint32_t Material, uint32_t Segments) noexcept
{
    const Vector3 Apex = Base + Vector3{ 0.0f, 0.0f, Height };
    const Vector3 Down{ 0.0f, 0.0f, -1.0f };
    const float Slant = std::sqrt(Radius * Radius + Height * Height);
    const float Nz    = Slant > 0.0f ? Radius / Slant : 0.0f;
    const float Nr    = Slant > 0.0f ? Height / Slant : 1.0f;
    for (uint32_t I = 0u; I < Segments; ++I)
    {
        const float A0 = 2.0f * kPi * static_cast<float>(I)      / static_cast<float>(Segments);
        const float A1 = 2.0f * kPi * static_cast<float>(I + 1u) / static_cast<float>(Segments);
        const Vector3 R0{ std::cos(A0), std::sin(A0), 0.0f }, R1{ std::cos(A1), std::sin(A1), 0.0f };
        const Vector3 B0 = Base + R0 * Radius, B1 = Base + R1 * Radius;
        const Vector3 N0{ R0.x * Nr, R0.y * Nr, Nz }, N1{ R1.x * Nr, R1.y * Nr, Nz };
        const Vector3 NA{ (R0.x + R1.x) * 0.5f * Nr, (R0.y + R1.y) * 0.5f * Nr, Nz };
        const float U0 = static_cast<float>(I) / static_cast<float>(Segments);
        const float U1 = static_cast<float>(I + 1u) / static_cast<float>(Segments);
        {   const Vector3 P[3] = { B0, B1, Apex }; const Vector3 N[3] = { N0, N1, NA }; const float Uv[3][2] = { { U0, 0.0f }, { U1, 0.0f }, { (U0 + U1) * 0.5f, 1.0f } }; AppendTriangle(P, N, Uv, Material); }
        {   const Vector3 P[3] = { Base, B1, B0 }; const Vector3 N[3] = { Down, Down, Down }; const float Uv[3][2] = { { 0.5f, 0.5f }, { U1, 0.0f }, { U0, 0.0f } }; AppendTriangle(P, N, Uv, Material); }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       EXPORT
//------------------------------------------------------------------------------------------------------------------------

// The marker the revision check looks for. It rides the scene name, which SceneCodec::Encode writes into the glTF as
//    the node/mesh/scene name — no new format field, and any codec that round-trips names carries it for free.
namespace {
std::string ShowcaseRevisionName()
{
    char Buffer[32];
    std::snprintf(Buffer, sizeof(Buffer), "Showcase_r%u", kShowcaseRevision);
    return Buffer;
}
} // namespace

bool ShowcaseIsCurrent(const std::string& Path) noexcept
{
    std::ifstream File(Path, std::ios::binary);
    if (!File) return false;

    // The name appears in the JSON chunk, which glTF puts first. Reading a bounded prefix keeps this O(1) rather than
    //    pulling a multi-megabyte embedded buffer into memory just to answer a yes/no question.
    std::string Head(262144u, '\0');
    File.read(Head.data(), static_cast<std::streamsize>(Head.size()));
    Head.resize(static_cast<size_t>(File.gcount()));
    return Head.find(ShowcaseRevisionName()) != std::string::npos;
}

bool ShowcaseStructure::Export(const std::string& Path, std::string* Error) const noexcept
{
    SceneEncodeConfiguration Configuration;
    Configuration.Name           = ShowcaseRevisionName();   // stamps the revision so a stale file is detected
    Configuration.CornerNormals  = &CornerNormals;
    Configuration.WriteTexcoords = true;
    Configuration.Spans          = &Spans;
    return SceneCodec::Encode(Path, Triangles, Materials, Error, Configuration);
}

} // namespace Frontier
