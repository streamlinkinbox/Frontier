//============================================================================================================================================
//                                                     SHOWCASESTRUCTURE.CPP
//============================================================================================================================================
// See ShowcaseStructure.h.
//
// Layout (top view, camera stands at −Y looking +Y):
//
//      Y = +7.2   row 5   emission · haze 0.2 · haze 0.9 · EON r0 · EON r1 · unlit-ish matte
//      Y = +5.4   row 4   cloth velvet · cloth felt · fuzz 0.4 · fuzz 1.0 · coat r0 · coat r0.35
//      Y = +3.6   row 3   thin film 0.25µm · film 0.55µm · film on gold · iridescent dark · soap · oil
//      Y = +1.8   row 2   SSS marble · SSS skin · SSS jade · SSS wax · SSS milk · SSS alabaster
//      Y =  0.0   row 1   glass IOR 1.33 · 1.45 · 1.52 · 1.77 · 2.42 · dense absorbing glass
//      Y = −1.8   row 0   anisotropic metal: gold · silver · copper · aluminium · iron F82 · brushed steel
//                 X = −4.5 … +4.5 in 1.8 m steps, spheres r = 0.7 m resting on Z = 0
//
//    Between and behind the rows sit scattered secondary shapes (boxes, cylinders, cones) carrying their own
//    materials, so the level is a scattered object field and not only a neat grid. Two 3×3 m area luminaires at
//    Z = 6 light it — they are what makes shadows and indirect light exist at all.

#include "ShowcaseStructure.h"
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

    // ── 1–6: anisotropic metals (row 0) ──────────────────────────────────────────────────────────────────────────
    //    Anisotropy is the point of the row: the same metal at the same roughness looks completely different when the
    //    highlight is stretched, and nothing in the old showcase could express it at all.
    {
        struct Metal { const char* Name; float F0[3]; float F82[3]; float Roughness; float Aniso; float Rotation; };
        const Metal Row[6] = {
            { "aniso_gold",      { 1.000f, 0.766f, 0.336f }, { 1.00f, 0.96f, 0.82f }, 0.22f, 0.0f,  0.0f        },
            { "aniso_silver",    { 0.972f, 0.960f, 0.915f }, { 1.00f, 1.00f, 1.00f }, 0.18f, 0.5f,  0.0f        },
            { "aniso_copper",    { 0.955f, 0.638f, 0.538f }, { 1.00f, 0.95f, 0.90f }, 0.28f, 0.8f,  0.0f        },
            { "aniso_aluminium", { 0.913f, 0.922f, 0.924f }, { 1.00f, 1.00f, 1.00f }, 0.30f, 0.9f,  kPi * 0.25f },
            { "aniso_iron_f82",  { 0.560f, 0.570f, 0.580f }, { 0.50f, 0.50f, 0.50f }, 0.38f, 0.6f,  kPi * 0.5f  },
            { "brushed_steel",   { 0.700f, 0.710f, 0.720f }, { 0.90f, 0.92f, 0.95f }, 0.34f, 0.95f, kPi * 0.5f  } };
        for (const Metal& M : Row)
        {
            MaterialDescriptor D = MakeMaterial(M.Name);
            SetColor(D.Slabs[0].BaseColor, M.F0[0], M.F0[1], M.F0[2]);
            SetColor(D.Slabs[0].SpecularColor, M.F82[0], M.F82[1], M.F82[2]);
            D.Slabs[0].BaseMetalness                 = 1.0f;
            D.Slabs[0].SpecularRoughness             = M.Roughness;
            D.Slabs[0].SpecularRoughnessAnisotropy   = M.Aniso;
            D.Slabs[0].SlateAnisotropyRotation       = M.Rotation;
            Materials.push_back(D);
        }
    }

    // ── 7–12: IOR-driven transmissive glass (row 1) ──────────────────────────────────────────────────────────────
    //    A transmission ramp by IOR, plus one dense absorbing glass with a real transmission depth.
    {
        struct Glass { const char* Name; float Ior; float Roughness; float Tint[3]; float Depth; float Dispersion; };
        const Glass Row[6] = {
            { "glass_water_133",  1.33f, 0.00f, { 0.95f, 0.99f, 1.00f }, 0.0f, 0.0f },
            { "glass_acrylic_145",1.45f, 0.00f, { 1.00f, 1.00f, 1.00f }, 0.0f, 0.0f },
            { "glass_crown_152",  1.52f, 0.02f, { 0.98f, 1.00f, 0.98f }, 0.0f, 0.2f },
            { "glass_flint_177",  1.77f, 0.00f, { 1.00f, 0.98f, 0.94f }, 0.0f, 0.6f },
            { "glass_diamond_242",2.42f, 0.00f, { 1.00f, 1.00f, 1.00f }, 0.0f, 1.0f },
            { "glass_absorbing",  1.52f, 0.06f, { 0.35f, 0.75f, 0.55f }, 0.4f, 0.0f } };
        for (const Glass& G : Row)
        {
            MaterialDescriptor D = MakeMaterial(G.Name);
            SetColor(D.Slabs[0].BaseColor, 1.0f, 1.0f, 1.0f);
            D.Slabs[0].TransmissionWeight                  = 1.0f;
            SetColor(D.Slabs[0].TransmissionColor, G.Tint[0], G.Tint[1], G.Tint[2]);
            D.Slabs[0].TransmissionDepth                   = G.Depth;
            D.Slabs[0].TransmissionDispersionScale         = G.Dispersion;
            D.Slabs[0].SpecularIor                         = G.Ior;
            D.Slabs[0].SpecularRoughness                   = G.Roughness;
            D.Slabs[0].SpecularWeight                      = 1.0f;
            D.VolumeThickness                              = 1.4f;   // sphere diameter: interchange fidelity
            Materials.push_back(D);
        }
    }

    // ── 13–18: subsurface (row 2) ────────────────────────────────────────────────────────────────────────────────
    {
        struct Sss { const char* Name; float Base[3]; float Scatter[3]; float Weight; float Radius; float Aniso; };
        const Sss Row[6] = {
            { "sss_marble",    { 0.90f, 0.89f, 0.85f }, { 1.00f, 0.95f, 0.90f }, 1.00f, 0.030f,  0.0f  },
            { "sss_skin",      { 0.85f, 0.62f, 0.52f }, { 1.00f, 0.40f, 0.30f }, 0.90f, 0.012f,  0.7f  },
            { "sss_jade",      { 0.35f, 0.70f, 0.50f }, { 0.40f, 1.00f, 0.65f }, 1.00f, 0.045f,  0.4f  },
            { "sss_wax",       { 0.92f, 0.86f, 0.70f }, { 1.00f, 0.90f, 0.65f }, 0.95f, 0.060f,  0.2f  },
            { "sss_milk",      { 0.96f, 0.96f, 0.94f }, { 1.00f, 1.00f, 1.00f }, 1.00f, 0.020f, -0.2f  },
            { "sss_alabaster", { 0.88f, 0.84f, 0.78f }, { 1.00f, 0.92f, 0.80f }, 0.85f, 0.080f,  0.1f  } };
        for (const Sss& S : Row)
        {
            MaterialDescriptor D = MakeMaterial(S.Name);
            SetColor(D.Slabs[0].BaseColor, S.Base[0], S.Base[1], S.Base[2]);
            D.Slabs[0].SubsurfaceWeight            = S.Weight;
            SetColor(D.Slabs[0].SubsurfaceColor, S.Base[0], S.Base[1], S.Base[2]);
            D.Slabs[0].SubsurfaceRadius            = S.Radius;
            D.Slabs[0].SubsurfaceRadiusScale[0]    = S.Scatter[0];
            D.Slabs[0].SubsurfaceRadiusScale[1]    = S.Scatter[1];
            D.Slabs[0].SubsurfaceRadiusScale[2]    = S.Scatter[2];
            D.Slabs[0].SubsurfaceScatterAnisotropy = S.Aniso;
            D.Slabs[0].SpecularRoughness           = 0.32f;
            D.Slabs[0].SpecularIor                 = 1.4f;
            Materials.push_back(D);
        }
    }

    // ── 19–24: thin film / iridescence (row 3) ───────────────────────────────────────────────────────────────────
    {
        struct Film { const char* Name; float Base[3]; float Metal; float Thickness; float Ior; float Roughness; };
        const Film Row[6] = {
            { "film_025",       { 0.02f, 0.02f, 0.02f }, 0.0f, 0.25f, 1.40f, 0.05f },
            { "film_055",       { 0.02f, 0.02f, 0.02f }, 0.0f, 0.55f, 1.40f, 0.05f },
            { "film_on_gold",   { 1.00f, 0.766f,0.336f}, 1.0f, 0.35f, 1.50f, 0.12f },
            { "film_dark_iri",  { 0.05f, 0.05f, 0.07f }, 0.0f, 0.80f, 2.00f, 0.15f },
            { "film_soap",      { 0.00f, 0.00f, 0.00f }, 0.0f, 0.42f, 1.33f, 0.02f },
            { "film_oil_slick", { 0.03f, 0.03f, 0.02f }, 0.0f, 1.10f, 1.45f, 0.25f } };
        for (const Film& F : Row)
        {
            MaterialDescriptor D = MakeMaterial(F.Name);
            SetColor(D.Slabs[0].BaseColor, F.Base[0], F.Base[1], F.Base[2]);
            D.Slabs[0].BaseMetalness      = F.Metal;
            D.Slabs[0].ThinFilmWeight     = 1.0f;
            D.Slabs[0].ThinFilmThickness  = F.Thickness;
            D.Slabs[0].ThinFilmIor        = F.Ior;
            D.Slabs[0].SpecularRoughness  = F.Roughness;
            Materials.push_back(D);
        }
    }

    // ── 25–30: cloth / fuzz / coat (row 4) ───────────────────────────────────────────────────────────────────────
    {
        MaterialDescriptor D = MakeMaterial("cloth_velvet");
        SetColor(D.Slabs[0].BaseColor, 0.35f, 0.02f, 0.08f); D.Slabs[0].SpecularWeight = 0.0f;
        D.Slabs[0].FuzzWeight = 1.0f; D.Slabs[0].FuzzRoughness = 0.8f; SetColor(D.Slabs[0].FuzzColor, 1.0f, 0.9f, 0.9f);
        Materials.push_back(D);

        D = MakeMaterial("cloth_felt");
        SetColor(D.Slabs[0].BaseColor, 0.5f, 0.5f, 0.48f); D.Slabs[0].SpecularWeight = 0.0f;
        D.Slabs[0].FuzzWeight = 1.0f; D.Slabs[0].FuzzRoughness = 0.9f; D.Slabs[0].BaseDiffuseRoughness = 1.0f;
        Materials.push_back(D);

        D = MakeMaterial("fuzz_04");
        SetColor(D.Slabs[0].BaseColor, 0.12f, 0.20f, 0.45f); D.Slabs[0].SpecularWeight = 0.2f;
        D.Slabs[0].FuzzWeight = 0.4f; D.Slabs[0].FuzzRoughness = 0.5f;
        Materials.push_back(D);

        D = MakeMaterial("fuzz_10");
        SetColor(D.Slabs[0].BaseColor, 0.12f, 0.20f, 0.45f); D.Slabs[0].SpecularWeight = 0.2f;
        D.Slabs[0].FuzzWeight = 1.0f; D.Slabs[0].FuzzRoughness = 0.85f;
        Materials.push_back(D);

        D = MakeMaterial("carpaint_coat_r0");
        SetColor(D.Slabs[0].BaseColor, 0.55f, 0.05f, 0.08f); D.Slabs[0].SpecularRoughness = 0.45f;
        D.Slabs[0].CoatWeight = 1.0f; D.Slabs[0].CoatRoughness = 0.0f; D.Slabs[0].CoatIor = 1.6f; D.Slabs[0].CoatDarkening = 1.0f;
        Materials.push_back(D);

        D = MakeMaterial("carpaint_coat_r035");
        SetColor(D.Slabs[0].BaseColor, 0.05f, 0.18f, 0.55f); D.Slabs[0].SpecularRoughness = 0.45f;
        D.Slabs[0].CoatWeight = 1.0f; D.Slabs[0].CoatRoughness = 0.35f; D.Slabs[0].CoatIor = 1.6f;
        Materials.push_back(D);
    }

    // ── 31–36: emission / haziness / EON (row 5) ─────────────────────────────────────────────────────────────────
    {
        MaterialDescriptor D = MakeMaterial("emitter_warm");
        SetColor(D.Slabs[0].BaseColor, 0.0f, 0.0f, 0.0f); D.Slabs[0].SpecularWeight = 0.0f;
        D.Slabs[0].EmissionLuminance = 24.0f; SetColor(D.Slabs[0].EmissionColor, 1.0f, 0.55f, 0.25f);
        Materials.push_back(D);

        D = MakeMaterial("haze_02");
        SetColor(D.Slabs[0].BaseColor, 0.10f, 0.10f, 0.11f); D.Slabs[0].SpecularRoughness = 0.10f;
        D.Slabs[0].SlateHazinessWeight = 0.2f; D.Slabs[0].SlateHazinessRoughness = 0.55f;
        Materials.push_back(D);

        D = MakeMaterial("haze_09");
        SetColor(D.Slabs[0].BaseColor, 0.10f, 0.10f, 0.11f); D.Slabs[0].SpecularRoughness = 0.10f;
        D.Slabs[0].SlateHazinessWeight = 0.9f; D.Slabs[0].SlateHazinessRoughness = 0.85f;
        Materials.push_back(D);

        D = MakeMaterial("eon_r0");
        SetColor(D.Slabs[0].BaseColor, 0.80f, 0.70f, 0.50f); D.Slabs[0].SpecularWeight = 0.0f; D.Slabs[0].BaseDiffuseRoughness = 0.0f;
        Materials.push_back(D);

        D = MakeMaterial("eon_r1");
        SetColor(D.Slabs[0].BaseColor, 0.80f, 0.70f, 0.50f); D.Slabs[0].SpecularWeight = 0.0f; D.Slabs[0].BaseDiffuseRoughness = 1.0f;
        Materials.push_back(D);

        D = MakeMaterial("glint_flakes");
        SetColor(D.Slabs[0].BaseColor, 0.20f, 0.20f, 0.22f); D.Slabs[0].SpecularRoughness = 0.25f;
        D.Slabs[0].BaseMetalness = 0.8f; D.Slabs[0].SlateGlintDensity = 4.0f; D.Slabs[0].SlateGlintUvScale = 8.0f;
        Materials.push_back(D);
    }

    const uint32_t kGridMaterialCount = static_cast<uint32_t>(Materials.size()) - 1u;   // everything but the ground

    // ── 37–44: the scattered field's own materials ───────────────────────────────────────────────────────────────
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

    // ── last: the luminaire ──────────────────────────────────────────────────────────────────────────────────────
    //    ⚠️ THE WHOLE LEVEL DEPENDS ON THIS EXISTING. The previous showcase had no emissive triangle anywhere, so the
    //    scene reported "0 luminaires": PlaceShadowTaps refused (empty emitter set ⇒ no shadow maps) and the ReSTIR
    //    direct-light loop had nothing to draw a candidate from, so there was no indirect bounce either. Two panels
    //    rather than one so objects catch a second, softer shadow and the GI has more than a single direction.
    const uint32_t LuminaireMaterial = static_cast<uint32_t>(Materials.size());
    {
        MaterialDescriptor D = MakeMaterial("luminaire_key");
        SetColor(D.Slabs[0].BaseColor, 1.0f, 1.0f, 1.0f); D.Slabs[0].SpecularWeight = 0.0f;
        D.Slabs[0].EmissionLuminance = 140.0f; SetColor(D.Slabs[0].EmissionColor, 1.0f, 0.97f, 0.92f);
        Materials.push_back(D);

        D = MakeMaterial("luminaire_fill");
        SetColor(D.Slabs[0].BaseColor, 1.0f, 1.0f, 1.0f); D.Slabs[0].SpecularWeight = 0.0f;
        D.Slabs[0].EmissionLuminance = 60.0f; SetColor(D.Slabs[0].EmissionColor, 0.80f, 0.88f, 1.0f);
        Materials.push_back(D);
    }

    //──────────────────────────────────────────────────────────────────────────────────────────────────────────────
    //                                                  GEOMETRY
    //──────────────────────────────────────────────────────────────────────────────────────────────────────────────

    // Ground. 60 × 60 m rather than the old 1000 × 1000: the previous plane put the scene bounds at ±500 m, which
    //    made the BVH's root node enormous next to 2 m objects and gave the shadow taps a far plane kilometres deep.
    {
        const auto GroundSpan = OpenSpan("Ground");
        AppendQuad(Vector3{ -30.0f, -30.0f, 0.0f }, Vector3{ 30.0f, -30.0f, 0.0f },
                   Vector3{ 30.0f, 30.0f, 0.0f }, Vector3{ -30.0f, 30.0f, 0.0f }, 0u, 0.15f);
    }

    // The material grid: 6 rows × 6 columns, one sphere per material.
    constexpr float kRadius = 0.7f;
    for (uint32_t Index = 0u; Index < kGridMaterialCount; ++Index)
    {
        const uint32_t Material = Index + 1u;                       // 0 is the ground
        const uint32_t Row = Index / 6u, Column = Index % 6u;
        const Vector3 Centre{ -4.5f + 1.8f * static_cast<float>(Column),
                              -1.8f + 1.8f * static_cast<float>(Row),
                              kRadius };
        char Name[96];
        std::snprintf(Name, sizeof(Name), "Sphere %02u (%s)", Material, Materials[Material].Name.c_str());
        const auto Span = OpenSpan(Name, true);
        AppendSphere(Centre, kRadius, Material, 24u, 48u);
    }

    // Small plinths under each sphere: they give the contact shadows something to land on, which is the cheapest
    //    way to see at a glance whether the shadow stage is actually running.
    for (uint32_t Index = 0u; Index < kGridMaterialCount; ++Index)
    {
        const uint32_t Row = Index / 6u, Column = Index % 6u;
        const Vector3 Centre{ -4.5f + 1.8f * static_cast<float>(Column),
                              -1.8f + 1.8f * static_cast<float>(Row),
                              0.03f };
        char Name[64];
        std::snprintf(Name, sizeof(Name), "Plinth %02u", Index + 1u);
        const auto Span = OpenSpan(Name);
        AppendCylinder(Vector3{ Centre.x, Centre.y, 0.0f }, kRadius * 1.15f, 0.06f, ScatterFirst + 1u, 24u);
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
            const float Angle  = Rng.Range(0.0f, 2.0f * kPi);
            const float Radial = Rng.Range(9.0f, 24.0f);
            const Vector3 Spot{ Radial * std::cos(Angle), 2.7f + Radial * std::sin(Angle), 0.0f };

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
