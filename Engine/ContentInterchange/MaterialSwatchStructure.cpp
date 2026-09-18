//============================================================================================================================================
//                                                    MATERIALSWATCHSTRUCTURE.CPP
//============================================================================================================================================
// See MaterialSwatchStructure.h. Layout (camera at −Y looking +Y, Z up, metres):
//
//        backdrop   Y = +7.40, X ±5.0, Z 0 … 3.6, matte neutral; three 1.4 × 0.9 m sign panels on it at Z 2.1 … 3.0
//                   X = −2.9 … −1.5 emissive-only · −0.7 … +0.7 unlit · +1.5 … +2.9 alpha cutout
//        floor      X ±5.0, Y −2.0 … +7.6, matte grey EON (SpecularWeight 0)
//        swatches   7 × 6 grid, r = 0.40 m resting on Z = 0, pitch 1.10 m — X = −3.3 + 1.1·column, Y = 0.60 + 1.1·row
//                   row 0 dielectric · row 1 clear coat · row 2 metals · row 3 glass · row 4 subsurface · row 5 cloth
//        key        3 × 3 m ceiling panel at Z = 4.6 (60 nit), facing −Z
//        fill       1.8 × 2.4 m side panel at X = −4.6 (25 nit), facing +X
//
//    Ordinal = row · 7 + column, so swatch 0 (plastic_abs_matte) is the near-left sphere and swatch 41
//    (metal_mercury_liquid) the far-right one. Every swatch material is a single OpenPBR slab whose entry below names
//    only the channels it changes from the spec defaults — the diff IS the material's identity, which is what the
//    material inspector shows and what MaterialSwatchProof re-derives from the decoded glTF.

#include "MaterialSwatchStructure.h"
#include "SceneCodec.h"
#include "../DeviceExchange/OrientationClassifier.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier {

namespace {

constexpr float kPi = 3.14159265358979f;

MaterialDescriptor MakeMaterial(const char* Name)
{
    MaterialDescriptor D;
    D.Name = Name;
    D.Slabs.emplace_back();
    return D;
}

void SetColour(float* Target, float R, float G, float B)
{
    Target[0] = R; Target[1] = G; Target[2] = B;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SWATCH CATALOGUE
//------------------------------------------------------------------------------------------------------------------------
// One entry per library material: the OpenPBR/`slate_` channels that make it that material. Defaults are the spec's, so
//    an omitted channel is *deliberately* at its default and the census print shows the difference.

struct SwatchSpecification
{
    const char* Name                     = "";
    float BaseColor[3]                   = { 0.8f, 0.8f, 0.8f };
    float BaseMetalness                  = 0.0f;
    float BaseDiffuseRoughness           = 0.0f;
    float SpecularWeight                 = 1.0f;
    float SpecularColor[3]               = { 1.0f, 1.0f, 1.0f };
    float SpecularRoughness              = 0.3f;
    float SpecularRoughnessAnisotropy    = 0.0f;
    float SpecularIor                    = 1.5f;
    float AnisotropyRotation             = 0.0f;                  // [rad] slate_anisotropy_rotation
    float CoatWeight                     = 0.0f;
    float CoatColor[3]                   = { 1.0f, 1.0f, 1.0f };
    float CoatRoughness                  = 0.0f;
    float CoatIor                        = 1.6f;                  // ⚠️ must be ≠ 1.6 on every coated swatch
    float CoatDarkening                  = 1.0f;                  //    (KHR_materials_clearcoat hard-codes 1.5 on decode)
    float FuzzWeight                     = 0.0f;
    float FuzzColor[3]                   = { 1.0f, 1.0f, 1.0f };
    float FuzzRoughness                  = 0.5f;
    float ThinFilmWeight                 = 0.0f;
    float ThinFilmThickness              = 0.5f;                  // [µm]
    float ThinFilmIor                    = 1.4f;
    float SubsurfaceWeight               = 0.0f;
    float SubsurfaceColor[3]             = { 0.8f, 0.8f, 0.8f };
    float SubsurfaceRadius               = 1.0f;                  // [m]
    float SubsurfaceRadiusScale[3]       = { 1.0f, 0.5f, 0.25f };
    float SubsurfaceScatterAnisotropy    = 0.0f;
    float TransmissionWeight             = 0.0f;
    float TransmissionColor[3]           = { 1.0f, 1.0f, 1.0f };
    float TransmissionDepth              = 0.0f;                  // [m] 0 = no volume (attenuation colour then unused)
    float HazinessWeight                 = 0.0f;
    float HazinessRoughness              = 0.6f;
    float EmissionLuminance              = 0.0f;                  // [nit]
    float EmissionColor[3]               = { 1.0f, 1.0f, 1.0f };
    float GeometryOpacity                = 1.0f;
    uint32_t Flags                       = 0u;
};

// Ordinal = row · kSwatchColumns + column. Boundary between rows is marked; the proof asserts the grid this table implies.
const SwatchSpecification kSwatches[MaterialSwatchStructure::kSwatchCount] =
{
    // Designated initializers must follow the declaration order of SwatchSpecification (C++20) — each entry below
    //    lists its channels in that order, so reordering the struct reorders the table.
    //
    // ── Row 0 — plastics, rubber, ceramics, plaster (Standard) ──────────────────────────────────────────────────────
    { .Name = "plastic_abs_matte",       .BaseColor = { 0.42f, 0.40f, 0.38f }, .BaseDiffuseRoughness = 0.35f, .SpecularRoughness = 0.62f },
    { .Name = "plastic_abs_gloss",       .BaseColor = { 0.06f, 0.28f, 0.62f }, .SpecularRoughness = 0.13f },
    { .Name = "plastic_pvc_white",       .BaseColor = { 0.86f, 0.86f, 0.84f }, .SpecularRoughness = 0.30f, .SpecularIor = 1.53f },
    { .Name = "rubber_epdm_matte",       .BaseColor = { 0.022f, 0.022f, 0.024f }, .BaseDiffuseRoughness = 0.55f, .SpecularWeight = 0.35f, .SpecularRoughness = 0.88f },
    { .Name = "ceramic_porcelain_glazed",.BaseColor = { 0.92f, 0.90f, 0.85f }, .SpecularRoughness = 0.10f, .SpecularIor = 1.45f },
    { .Name = "concrete_cast",           .BaseColor = { 0.52f, 0.52f, 0.50f }, .BaseDiffuseRoughness = 0.70f, .SpecularWeight = 0.55f, .SpecularRoughness = 0.78f },
    { .Name = "plaster_lime_matte",      .BaseColor = { 0.78f, 0.76f, 0.70f }, .BaseDiffuseRoughness = 0.80f, .SpecularWeight = 0.30f, .SpecularRoughness = 0.92f, .SpecularIor = 1.45f },

    // ── Row 1 — clear coat: paints, varnishes, lacquer, enamel, tile (ClearCoated) ─────────────────────────────────
    { .Name = "car_paint_metallic_blue", .BaseColor = { 0.030f, 0.100f, 0.360f }, .BaseMetalness = 0.25f, .SpecularRoughness = 0.30f,
      .CoatWeight = 1.00f, .CoatRoughness = 0.030f, .CoatIor = 1.50f },
    { .Name = "car_paint_candy_red",     .BaseColor = { 0.550f, 0.020f, 0.030f }, .SpecularRoughness = 0.38f,
      .CoatWeight = 1.00f, .CoatRoughness = 0.060f, .CoatIor = 1.50f, .CoatDarkening = 0.80f },
    { .Name = "wood_oak_varnished",      .BaseColor = { 0.300f, 0.180f, 0.085f }, .BaseDiffuseRoughness = 0.25f, .SpecularRoughness = 0.44f,
      .CoatWeight = 0.70f, .CoatRoughness = 0.160f, .CoatIor = 1.47f },
    { .Name = "lacquer_piano_black",     .BaseColor = { 0.012f, 0.012f, 0.014f }, .SpecularRoughness = 0.35f,
      .CoatWeight = 1.00f, .CoatRoughness = 0.015f, .CoatIor = 1.50f },
    { .Name = "enamel_appliance_white",  .BaseColor = { 0.880f, 0.880f, 0.870f }, .SpecularRoughness = 0.26f,
      .CoatWeight = 0.55f, .CoatRoughness = 0.120f, .CoatIor = 1.50f, .CoatDarkening = 0.70f },
    { .Name = "canvas_varnish_satin",    .BaseColor = { 0.720f, 0.680f, 0.600f }, .BaseDiffuseRoughness = 0.60f, .SpecularRoughness = 0.62f,
      .CoatWeight = 0.35f, .CoatRoughness = 0.340f, .CoatIor = 1.45f },
    { .Name = "tile_ceramic_gloss",      .BaseColor = { 0.850f, 0.870f, 0.880f }, .SpecularRoughness = 0.14f,
      .CoatWeight = 0.80f, .CoatRoughness = 0.050f, .CoatIor = 1.52f },

    // ── Row 2 — metals (F0 in base colour, F82 tint in specular colour) ─────────────────────────────────────────────
    { .Name = "metal_gold_bullion",      .BaseColor = { 1.000f, 0.766f, 0.336f }, .BaseMetalness = 1.0f, .SpecularColor = { 1.00f, 0.96f, 0.82f }, .SpecularRoughness = 0.18f },
    { .Name = "metal_copper_aged",       .BaseColor = { 0.955f, 0.638f, 0.538f }, .BaseMetalness = 1.0f, .SpecularColor = { 1.00f, 0.95f, 0.90f }, .SpecularRoughness = 0.42f },
    { .Name = "metal_chrome_mirror",     .BaseColor = { 0.972f, 0.960f, 0.915f }, .BaseMetalness = 1.0f, .SpecularColor = { 1.00f, 1.00f, 1.00f }, .SpecularRoughness = 0.03f },
    { .Name = "metal_steel_brushed",     .BaseColor = { 0.600f, 0.610f, 0.630f }, .BaseMetalness = 1.0f, .SpecularColor = { 0.85f, 0.86f, 0.88f }, .SpecularRoughness = 0.28f,
      .SpecularRoughnessAnisotropy = 0.75f, .AnisotropyRotation = 0.5236f },
    { .Name = "metal_aluminium_cast",    .BaseColor = { 0.913f, 0.922f, 0.924f }, .BaseMetalness = 1.0f, .SpecularColor = { 1.00f, 1.00f, 1.00f }, .SpecularRoughness = 0.55f },
    { .Name = "metal_titanium_anodised", .BaseColor = { 0.160f, 0.220f, 0.400f }, .BaseMetalness = 1.0f, .SpecularColor = { 0.60f, 0.70f, 0.90f }, .SpecularRoughness = 0.35f,
      .SpecularRoughnessAnisotropy = 0.25f, .AnisotropyRotation = -0.2618f },
    { .Name = "metal_brass_polished",    .BaseColor = { 0.910f, 0.780f, 0.420f }, .BaseMetalness = 1.0f, .SpecularColor = { 1.00f, 0.94f, 0.78f }, .SpecularRoughness = 0.12f },

    // ── Row 3 — the glass family (Transmissive; acrylic is the thin-walled path) ────────────────────────────────────
    { .Name = "glass_soda_lime_clear",   .SpecularRoughness = 0.02f, .SpecularIor = 1.52f, .TransmissionWeight = 1.00f },
    { .Name = "glass_soda_lime_frosted", .SpecularRoughness = 0.38f, .SpecularIor = 1.52f, .TransmissionWeight = 1.00f },
    { .Name = "glass_amber_bottle",      .SpecularRoughness = 0.04f, .SpecularIor = 1.52f, .TransmissionWeight = 1.00f,
      .TransmissionColor = { 0.62f, 0.34f, 0.10f }, .TransmissionDepth = 0.060f },
    { .Name = "glass_lead_crystal",      .BaseColor = { 0.92f, 0.94f, 0.96f }, .SpecularRoughness = 0.01f, .SpecularIor = 1.68f, .TransmissionWeight = 1.00f,
      .TransmissionColor = { 0.98f, 0.99f, 1.00f }, .TransmissionDepth = 0.020f },
    { .Name = "ice_frozen_clear",        .SpecularRoughness = 0.22f, .SpecularIor = 1.31f, .SubsurfaceWeight = 0.15f,
      .SubsurfaceColor = { 0.90f, 0.95f, 1.00f }, .SubsurfaceRadius = 0.020f,
      .TransmissionWeight = 0.85f, .TransmissionColor = { 0.92f, 0.96f, 1.00f }, .TransmissionDepth = 0.050f },
    { .Name = "acrylic_thin_sheet",      .SpecularRoughness = 0.03f, .SpecularIor = 1.49f, .TransmissionWeight = 1.00f,
      .Flags = MaterialFlagThinWalled },
    { .Name = "water_still_ball",        .SpecularRoughness = 0.005f, .SpecularIor = 1.333f, .TransmissionWeight = 1.00f },

    // ── Row 4 — subsurface (M5 dipole family) ──────────────────────────────────────────────────────────────────────
    { .Name = "bone_ivory",              .BaseColor = { 0.86f, 0.82f, 0.70f }, .SpecularRoughness = 0.45f, .SubsurfaceWeight = 0.70f,
      .SubsurfaceColor = { 0.85f, 0.78f, 0.62f }, .SubsurfaceRadius = 0.015f, .SubsurfaceRadiusScale = { 1.00f, 0.60f, 0.40f } },
    { .Name = "jade_nephrite",           .BaseColor = { 0.30f, 0.55f, 0.34f }, .SpecularRoughness = 0.20f, .SubsurfaceWeight = 0.55f,
      .SubsurfaceColor = { 0.22f, 0.52f, 0.30f }, .SubsurfaceRadius = 0.030f },
    { .Name = "wax_paraffin",            .BaseColor = { 0.95f, 0.88f, 0.66f }, .SpecularRoughness = 0.35f, .SubsurfaceWeight = 1.00f,
      .SubsurfaceColor = { 0.95f, 0.85f, 0.60f }, .SubsurfaceRadius = 0.050f, .SubsurfaceScatterAnisotropy = 0.20f },
    { .Name = "marble_carrara_polished", .BaseColor = { 0.88f, 0.88f, 0.86f }, .SpecularRoughness = 0.09f, .SubsurfaceWeight = 0.35f,
      .SubsurfaceColor = { 0.90f, 0.88f, 0.84f }, .SubsurfaceRadius = 0.008f },
    { .Name = "skin_light",              .BaseColor = { 0.80f, 0.62f, 0.52f }, .SpecularRoughness = 0.38f, .SubsurfaceWeight = 0.60f,
      .SubsurfaceColor = { 0.72f, 0.44f, 0.35f }, .SubsurfaceRadius = 0.012f, .SubsurfaceRadiusScale = { 1.00f, 0.35f, 0.20f } },
    { .Name = "milk_dairy",              .BaseColor = { 0.97f, 0.96f, 0.92f }, .SpecularRoughness = 0.25f, .SubsurfaceWeight = 0.90f,
      .SubsurfaceColor = { 0.98f, 0.97f, 0.93f }, .SubsurfaceRadius = 0.020f },
    { .Name = "resin_honey",             .BaseColor = { 0.80f, 0.55f, 0.18f }, .SpecularRoughness = 0.15f, .SubsurfaceWeight = 0.80f,
      .SubsurfaceColor = { 0.72f, 0.42f, 0.10f }, .SubsurfaceRadius = 0.020f },

    // ── Row 5 — cloth (M3 sheen-primary) and the specials ──────────────────────────────────────────────────────────
    { .Name = "cloth_velvet_crimson",    .BaseColor = { 0.35f, 0.02f, 0.08f }, .SpecularWeight = 0.0f, .FuzzWeight = 1.00f,
      .FuzzColor = { 1.00f, 0.90f, 0.90f }, .FuzzRoughness = 0.78f },
    { .Name = "cloth_felt_charcoal",     .BaseColor = { 0.05f, 0.05f, 0.052f }, .BaseDiffuseRoughness = 1.0f, .SpecularWeight = 0.0f, .FuzzWeight = 0.90f,
      .FuzzColor = { 1.00f, 0.97f, 0.94f }, .FuzzRoughness = 0.90f },
    { .Name = "silk_satin_ivory",        .BaseColor = { 0.80f, 0.76f, 0.68f }, .SpecularRoughness = 0.22f, .FuzzWeight = 0.45f,
      .FuzzColor = { 1.00f, 0.95f, 0.90f } },
    { .Name = "film_soap_bubble",        .BaseColor = { 0.02f, 0.02f, 0.02f }, .SpecularRoughness = 0.02f, .ThinFilmWeight = 1.00f,
      .ThinFilmThickness = 0.32f, .ThinFilmIor = 1.34f },
    { .Name = "haze_frosted_acrylic",    .BaseColor = { 0.55f, 0.58f, 0.62f }, .SpecularRoughness = 0.12f, .HazinessWeight = 0.75f,
      .HazinessRoughness = 0.65f },
    { .Name = "gem_emerald",             .SpecularRoughness = 0.05f, .SpecularIor = 1.58f, .TransmissionWeight = 0.90f,
      .TransmissionColor = { 0.10f, 0.55f, 0.30f }, .TransmissionDepth = 0.030f },
    { .Name = "metal_mercury_liquid",    .BaseColor = { 0.780f, 0.780f, 0.800f }, .BaseMetalness = 1.0f, .SpecularColor = { 1.00f, 1.00f, 1.00f }, .SpecularRoughness = 0.02f },
};

MaterialDescriptor BuildSwatch(const SwatchSpecification& S)
{
    MaterialDescriptor D = MakeMaterial(S.Name);
    MaterialSlabDescriptor& M = D.Slabs[0];
    SetColour(M.BaseColor, S.BaseColor[0], S.BaseColor[1], S.BaseColor[2]);
    M.BaseMetalness            = S.BaseMetalness;
    M.BaseDiffuseRoughness     = S.BaseDiffuseRoughness;
    M.SpecularWeight           = S.SpecularWeight;
    SetColour(M.SpecularColor, S.SpecularColor[0], S.SpecularColor[1], S.SpecularColor[2]);
    M.SpecularRoughness        = S.SpecularRoughness;
    M.SpecularRoughnessAnisotropy = S.SpecularRoughnessAnisotropy;
    M.SpecularIor              = S.SpecularIor;
    M.SlateAnisotropyRotation  = S.AnisotropyRotation;
    M.CoatWeight               = S.CoatWeight;
    SetColour(M.CoatColor, S.CoatColor[0], S.CoatColor[1], S.CoatColor[2]);
    M.CoatRoughness            = S.CoatRoughness;
    M.CoatIor                  = S.CoatIor;
    M.CoatDarkening            = S.CoatDarkening;
    M.FuzzWeight               = S.FuzzWeight;
    SetColour(M.FuzzColor, S.FuzzColor[0], S.FuzzColor[1], S.FuzzColor[2]);
    M.FuzzRoughness            = S.FuzzRoughness;
    M.ThinFilmWeight           = S.ThinFilmWeight;
    M.ThinFilmThickness        = S.ThinFilmThickness;
    M.ThinFilmIor              = S.ThinFilmIor;
    M.SubsurfaceWeight         = S.SubsurfaceWeight;
    SetColour(M.SubsurfaceColor, S.SubsurfaceColor[0], S.SubsurfaceColor[1], S.SubsurfaceColor[2]);
    M.SubsurfaceRadius         = S.SubsurfaceRadius;
    SetColour(M.SubsurfaceRadiusScale, S.SubsurfaceRadiusScale[0], S.SubsurfaceRadiusScale[1], S.SubsurfaceRadiusScale[2]);
    M.SubsurfaceScatterAnisotropy = S.SubsurfaceScatterAnisotropy;
    M.TransmissionWeight       = S.TransmissionWeight;
    SetColour(M.TransmissionColor, S.TransmissionColor[0], S.TransmissionColor[1], S.TransmissionColor[2]);
    M.TransmissionDepth        = S.TransmissionDepth;
    M.SlateHazinessWeight      = S.HazinessWeight;
    M.SlateHazinessRoughness   = S.HazinessRoughness;
    M.EmissionLuminance        = S.EmissionLuminance;
    SetColour(M.EmissionColor, S.EmissionColor[0], S.EmissionColor[1], S.EmissionColor[2]);
    M.GeometryOpacity          = S.GeometryOpacity;
    if ((S.Flags & MaterialFlagThinWalled) != 0u) M.GeometryThinWalled = true;
    D.Flags = S.Flags;
    return D;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        SWATCH ADDRESSING
//------------------------------------------------------------------------------------------------------------------------

Vector3 MaterialSwatchStructure::QuerySwatchOrigin(uint32_t Ordinal) noexcept
{
    const uint32_t Column = Ordinal % kSwatchColumns;
    const uint32_t Row    = Ordinal / kSwatchColumns;
    return Vector3{ -3.3f + kSwatchPitch * static_cast<float>(Column),
                    kSwatchFirstRow + kSwatchPitch * static_cast<float>(Row),
                    kSwatchRadius };
}

const char* MaterialSwatchStructure::QuerySwatchName(uint32_t Ordinal) noexcept
{
    return Ordinal < kSwatchCount ? kSwatches[Ordinal].Name : "";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

MaterialSwatchStructure::SpanScope::~SpanScope() noexcept
{
    if (Spans == nullptr || Triangles == nullptr || Span >= Spans->size()) return;
    TriangleSpanRecord& S = (*Spans)[Span];
    const uint32_t Now = static_cast<uint32_t>(Triangles->size());
    S.TriangleCount = Now >= S.FirstTriangle ? Now - S.FirstTriangle : 0u;
}

MaterialSwatchStructure::SpanScope MaterialSwatchStructure::OpenSpan(const char* Name, bool Dynamic) noexcept
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

void MaterialSwatchStructure::Construct() noexcept
{
    Triangles.clear(); CornerNormals.clear(); Materials.clear(); Spans.clear();

    // ── Materials ────────────────────────────────────────────────────────────────────────────────────────────────
    {   // 0 — studio floor: matte, no specular lobe, so the swatches' own highlights are the only ones on the floor
        MaterialDescriptor D = MakeMaterial("studio_floor");
        SetColour(D.Slabs[0].BaseColor, 0.45f, 0.45f, 0.45f);
        D.Slabs[0].SpecularWeight = 0.0f; D.Slabs[0].SpecularRoughness = 1.0f; D.Slabs[0].BaseDiffuseRoughness = 0.80f;
        Materials.push_back(D);
    }
    {   // 1 — backdrop: neutral, semi-matte; the glass swatches read against it and the metals reflect it
        MaterialDescriptor D = MakeMaterial("studio_backdrop");
        SetColour(D.Slabs[0].BaseColor, 0.55f, 0.56f, 0.58f);
        D.Slabs[0].SpecularWeight = 0.30f; D.Slabs[0].SpecularRoughness = 0.65f;
        Materials.push_back(D);
    }
    for (uint32_t N = 0u; N < kSwatchCount; ++N) Materials.push_back(BuildSwatch(kSwatches[N]));

    {   // alpha cutout sign: geometry_opacity 0.62 > cutoff 0.5, so the mask PASSES — the discard arm of the same
        //    material is the shaderball level's alpha card; here the channel is seen in a render.
        MaterialDescriptor D = MakeMaterial("panel_cutout_open");
        SetColour(D.Slabs[0].BaseColor, 0.85f, 0.30f, 0.10f);
        D.Slabs[0].SpecularRoughness = 0.45f; D.Slabs[0].GeometryOpacity = 0.62f;
        D.Flags = MaterialFlagAlphaMask | MaterialFlagDoubleSided; D.AlphaCutoff = 0.5f;
        Materials.push_back(D);
    }
    {   // unlit sign: base colour IS the radiance (KHR_materials_unlit → MaterialFlagUnlit → the kernel's Unlit path)
        MaterialDescriptor D = MakeMaterial("panel_unlit_poster");
        SetColour(D.Slabs[0].BaseColor, 0.15f, 0.65f, 0.35f);
        D.Slabs[0].SpecularWeight = 0.0f;
        D.Flags = MaterialFlagUnlit | MaterialFlagDoubleSided;
        Materials.push_back(D);
    }
    {   // emissive-only sign: base_weight 0 and specular_weight 0, emission 25 nit — DeriveReflectance returns
        //    EmissiveOnly (the one selection glTF-authored content cannot reach; the extras carry base_weight).
        MaterialDescriptor D = MakeMaterial("panel_emissive_only");
        D.Slabs[0].BaseWeight = 0.0f; D.Slabs[0].SpecularWeight = 0.0f;
        D.Slabs[0].EmissionLuminance = 25.0f; SetColour(D.Slabs[0].EmissionColor, 1.00f, 0.72f, 0.35f);
        Materials.push_back(D);
    }
    {   // key luminaire: 3 × 3 m ceiling panel, 60 nit (9 m² × 60 = 540 nit·m², the shader ball's order)
        MaterialDescriptor D = MakeMaterial("luminaire_key");
        D.Slabs[0].SpecularWeight = 0.0f; D.Slabs[0].EmissionLuminance = 60.0f;
        Materials.push_back(D);
    }
    {   // fill luminaire: 2.4 × 1.8 m side panel, 25 nit, so metals and coats carry two distinct highlights
        MaterialDescriptor D = MakeMaterial("luminaire_fill");
        D.Slabs[0].SpecularWeight = 0.0f; D.Slabs[0].EmissionLuminance = 25.0f;
        Materials.push_back(D);
    }

    // ── Geometry ─────────────────────────────────────────────────────────────────────────────────────────────────
    {
        const auto FloorSpan = OpenSpan("Studio Floor");
        AppendQuad(Vector3{ -5.0f, -2.0f, 0.0f }, Vector3{ 5.0f, -2.0f, 0.0f }, Vector3{ 5.0f, 7.6f, 0.0f }, Vector3{ -5.0f, 7.6f, 0.0f }, kFloorMaterial, 0.25f);
    }
    {
        const auto BackdropSpan = OpenSpan("Studio Backdrop");
        AppendQuad(Vector3{ -5.0f, 7.40f, 0.0f }, Vector3{ 5.0f, 7.40f, 0.0f }, Vector3{ 5.0f, 7.40f, 3.6f }, Vector3{ -5.0f, 7.40f, 3.6f }, kBackdropMaterial, 0.25f);
    }

    for (uint32_t N = 0u; N < kSwatchCount; ++N)
    {
        char SwatchName[96];
        std::snprintf(SwatchName, sizeof(SwatchName), "Swatch %02u (%s)", N, QuerySwatchName(N));
        const auto SwatchSpan = OpenSpan(SwatchName, true);
        AppendSphere(QuerySwatchOrigin(N), kSwatchRadius, QuerySwatchMaterial(N), 20u, 40u);
    }

    // Sign panels: 1.4 × 0.9 m, 4 cm proud of the backdrop, facing −Y (winding: X then X×Z = −Y).
    {
        const auto CutoutSpan = OpenSpan("Panel Cutout");
        AppendQuad(Vector3{ 1.50f, 7.36f, 2.10f }, Vector3{ 2.90f, 7.36f, 2.10f }, Vector3{ 2.90f, 7.36f, 3.00f }, Vector3{ 1.50f, 7.36f, 3.00f }, kPanelCutoutMaterial, 1.0f);
    }
    {
        const auto UnlitSpan = OpenSpan("Panel Unlit");
        AppendQuad(Vector3{ -0.70f, 7.36f, 2.10f }, Vector3{ 0.70f, 7.36f, 2.10f }, Vector3{ 0.70f, 7.36f, 3.00f }, Vector3{ -0.70f, 7.36f, 3.00f }, kPanelUnlitMaterial, 1.0f);
    }
    {
        const auto EmissiveSpan = OpenSpan("Panel Emissive");
        AppendQuad(Vector3{ -2.90f, 7.36f, 2.10f }, Vector3{ -1.50f, 7.36f, 2.10f }, Vector3{ -1.50f, 7.36f, 3.00f }, Vector3{ -2.90f, 7.36f, 3.00f }, kPanelEmissionMaterial, 1.0f);
    }

    // Luminaires: facing −Z (key) and +X (fill) — LAST, the shared luminaire convention.
    {
        const auto KeySpan = OpenSpan("Luminaire Key");
        AppendQuad(Vector3{ 1.5f, 2.35f, 4.60f }, Vector3{ -1.5f, 2.35f, 4.60f }, Vector3{ -1.5f, 4.35f, 4.60f }, Vector3{ 1.5f, 4.35f, 4.60f }, kKeyLuminaireMaterial, 1.0f);
    }
    {
        const auto FillSpan = OpenSpan("Luminaire Fill");
        AppendQuad(Vector3{ -4.60f, -1.60f, 1.20f }, Vector3{ -4.60f, 0.60f, 1.20f }, Vector3{ -4.60f, 0.60f, 3.00f }, Vector3{ -4.60f, -1.60f, 3.00f }, kFillLuminaireMaterial, 1.0f);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      GEOMETRY HELPERS
//------------------------------------------------------------------------------------------------------------------------
// Identical construction to ShaderBallStructure's (CCW outward winding, poles as fans): one sphere builder shared by
//    every level that shows materials, so a swatch and a shader ball tessellate the same way.

void MaterialSwatchStructure::AppendTriangle(const Vector3 P[3], const Vector3 N[3], const float Uv[3][2], uint32_t Material) noexcept
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

void MaterialSwatchStructure::AppendQuad(const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D, uint32_t Material, float UvScale) noexcept
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

void MaterialSwatchStructure::AppendSphere(const Vector3& Centre, float Radius, uint32_t Material, uint32_t Rings, uint32_t Segments) noexcept
{
    // UV sphere, poles on ±Z, CCW outward winding, u = longitude / 2π, v = latitude from the north pole.
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

bool MaterialSwatchStructure::Export(const std::string& Path, std::string* Error) const noexcept
{
    SceneEncodeConfiguration Configuration;
    Configuration.Name = "Materials"; Configuration.CornerNormals = &CornerNormals; Configuration.WriteTexcoords = true;
    Configuration.Spans = &Spans;
    return SceneCodec::Encode(Path, Triangles, Materials, Error, Configuration);
}

} // namespace Frontier
