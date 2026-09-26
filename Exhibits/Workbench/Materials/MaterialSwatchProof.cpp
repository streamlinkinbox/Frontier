//============================================================================================================================================
//                                                    MATERIALSWATCHPROOF.CPP
//============================================================================================================================================
// Material library gate — the level `--scene materials` (MaterialSwatchStructure) pinned as executable code. The M1–M8
//    gates proved the channels one lobe at a time (furnace, shaderball rows, glass proofs); this gate proves the
//    curated index of them: the level builds, exports through the real SceneCodec, decodes through the real codec
//    stack, and every swatch comes back with the channels it was authored with.
//
//        A  level builds → exports → decodes (49 materials + fallback)
//        B  census print — every swatch: name, selection, complexity, channels changed
//        C  uniqueness — 42 distinct names, 42 distinct resident records, zero folds at limits 1 / 2 / 8
//        D  channel coverage — 16 of 20 channels carried by the level, 4 acknowledged (texture-shaped + channel 20)
//        E  round-trip fidelity — authored descriptor vs decoded descriptor, field by field, for all 42 swatches
//        F  grid geometry — spheres on the declared pitch, spans, bounds, luminaires, mask/thin flags
//
//    No GPU, no window. Compile + run: Exhibits/Workbench/Materials/CheckMaterialSwatches.sh

#include "ContentCodec.h"
#include "SceneCodec.h"
#include "SceneStructure.h"
#include "MaterialIndex.h"
#include "MaterialSwatchStructure.h"

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {

int Passed = 0, Failed = 0, Serial = 0;

void Check(bool Condition, const char* Label)
{
    ++Serial;
    if (Condition) { ++Passed; std::printf("ok %d - %s\n", Serial, Label); }
    else           { ++Failed; std::printf("FAIL %d - %s\n", Serial, Label); }
    std::fflush(stdout);
}

const char* SelName(Frontier::MaterialReflectance S)
{
    static const char* kNames[8] = { "Standard", "Anisotropic", "ClearCoated", "Cloth", "Subsurface", "Transmissive", "EmissiveOnly", "Unlit" };
    const uint32_t I = static_cast<uint32_t>(S);
    return kNames[I < 8u ? I : 0u];
}

const char* CompName(uint32_t C)
{
    static const char* kNames[4] = { "Simple", "Single", "Complex", "Special" };
    return kNames[C < 4u ? C : 0u];
}

[[nodiscard]] bool NearlyEqual(float A, float B)
{
    const float Scale = std::max(1.0f, std::max(std::fabs(A), std::fabs(B)));
    return std::fabs(A - B) <= 1.0e-5f * Scale;
}

using Slab = Frontier::MaterialSlabDescriptor;

// Every float of the OpenPBR prefix (the 58 the GPU record mirrors byte for byte), by offset — the same table discipline
//    MaterialCodec uses, so a field added to the descriptor that this gate does not compare is itself a review item.
struct ChannelField { const char* Name; size_t Offset; uint8_t Components; };
constexpr ChannelField kSlabFields[] =
{
    { "base_weight", offsetof(Slab, BaseWeight), 1 },
    { "base_color", offsetof(Slab, BaseColor), 3 },
    { "base_metalness", offsetof(Slab, BaseMetalness), 1 },
    { "base_diffuse_roughness", offsetof(Slab, BaseDiffuseRoughness), 1 },
    { "specular_weight", offsetof(Slab, SpecularWeight), 1 },
    { "specular_color", offsetof(Slab, SpecularColor), 3 },
    { "specular_roughness", offsetof(Slab, SpecularRoughness), 1 },
    { "specular_roughness_anisotropy", offsetof(Slab, SpecularRoughnessAnisotropy), 1 },
    { "specular_ior", offsetof(Slab, SpecularIor), 1 },
    { "transmission_weight", offsetof(Slab, TransmissionWeight), 1 },
    { "transmission_color", offsetof(Slab, TransmissionColor), 3 },
    { "transmission_depth", offsetof(Slab, TransmissionDepth), 1 },
    { "transmission_scatter", offsetof(Slab, TransmissionScatter), 3 },
    { "transmission_scatter_anisotropy", offsetof(Slab, TransmissionScatterAnisotropy), 1 },
    { "transmission_dispersion_scale", offsetof(Slab, TransmissionDispersionScale), 1 },
    { "transmission_dispersion_abbe", offsetof(Slab, TransmissionDispersionAbbeNumber), 1 },
    { "subsurface_weight", offsetof(Slab, SubsurfaceWeight), 1 },
    { "subsurface_color", offsetof(Slab, SubsurfaceColor), 3 },
    { "subsurface_radius", offsetof(Slab, SubsurfaceRadius), 1 },
    { "subsurface_radius_scale", offsetof(Slab, SubsurfaceRadiusScale), 3 },
    { "subsurface_scatter_anisotropy", offsetof(Slab, SubsurfaceScatterAnisotropy), 1 },
    // fuzz_weight / fuzz_color are compared as a PRODUCT below (KHR_materials_sheen renormalises the split)
    { "fuzz_roughness", offsetof(Slab, FuzzRoughness), 1 },
    // emission_luminance / emission_color are compared as a PRODUCT below (emissiveFactor peak-normalises)
    { "coat_weight", offsetof(Slab, CoatWeight), 1 },
    { "coat_color", offsetof(Slab, CoatColor), 3 },
    { "coat_roughness", offsetof(Slab, CoatRoughness), 1 },
    { "coat_roughness_anisotropy", offsetof(Slab, CoatRoughnessAnisotropy), 1 },
    { "coat_ior", offsetof(Slab, CoatIor), 1 },
    { "coat_darkening", offsetof(Slab, CoatDarkening), 1 },
    { "thin_film_weight", offsetof(Slab, ThinFilmWeight), 1 },
    { "thin_film_thickness", offsetof(Slab, ThinFilmThickness), 1 },
    { "thin_film_ior", offsetof(Slab, ThinFilmIor), 1 },
    { "geometry_opacity", offsetof(Slab, GeometryOpacity), 1 },
    { "slate_haziness_weight", offsetof(Slab, SlateHazinessWeight), 1 },
    { "slate_haziness_roughness", offsetof(Slab, SlateHazinessRoughness), 1 },
    { "slate_glint_density", offsetof(Slab, SlateGlintDensity), 1 },
    { "slate_glint_uv_scale", offsetof(Slab, SlateGlintUvScale), 1 },
    { "slate_anisotropy_rotation", offsetof(Slab, SlateAnisotropyRotation), 1 },
    { "slate_direct_f0_weight", offsetof(Slab, SlateDirectF0Weight), 1 },
};

[[nodiscard]] const float* FieldPtr(const Slab& S, const ChannelField& F) noexcept
{
    return reinterpret_cast<const float*>(reinterpret_cast<const char*>(&S) + F.Offset);
}

// Returns the number of drifted fields; the first three are printed by the caller.
uint32_t CompareSlab(const Slab& A, const Slab& B, char* Report, size_t ReportSize)
{
    uint32_t Drift = 0;
    if (ReportSize > 0u) Report[0] = '\0';
    const auto Compare = [&](const char* Name, float L, float R)
    {
        if (NearlyEqual(L, R)) return;
        ++Drift;
        if (ReportSize > 0u && Report[0] == '\0')
            std::snprintf(Report, ReportSize, "%s %.6g vs %.6g", Name, static_cast<double>(L), static_cast<double>(R));
    };
    for (const ChannelField& F : kSlabFields)
    {
        const float* L = FieldPtr(A, F); const float* R = FieldPtr(B, F);
        for (uint8_t C = 0u; C < F.Components; ++C) Compare(F.Name, L[C], R[C]);
    }
    for (uint32_t C = 0u; C < 3u; ++C) Compare("fuzz_weight x fuzz_color", A.FuzzWeight * A.FuzzColor[C], B.FuzzWeight * B.FuzzColor[C]);
    for (uint32_t C = 0u; C < 3u; ++C) Compare("emission_luminance x color", A.EmissionLuminance * A.EmissionColor[C], B.EmissionLuminance * B.EmissionColor[C]);
    return Drift;
}

// Channels the level carries as constants, in the Sultan-42 order. Texture-shaped channels are named with the reason
//    they are not in this level; channel 20 is the plan's M6 decision (displacement: none).
const char* ChannelName(uint32_t Channel)
{
    static const char* kNames[20] =
    {
        "1 base colour", "2 metallic", "3 roughness", "4 reflectance (F0)", "5 surface orientation", "6 occlusion",
        "7 emission", "8 opacity", "9 anisotropy", "10 anisotropy direction", "11 clear coat", "12 clear coat roughness",
        "13 clear coat orientation", "14 sheen colour", "15 sheen roughness", "16 subsurface colour", "17 subsurface thickness",
        "18 transmission", "19 IOR", "20 displacement"
    };
    return Channel < 20u ? kNames[Channel] : "?";
}

bool ChannelSet(uint32_t Channel, const Slab& S)
{
    const float DefaultColor[3] = { 0.8f, 0.8f, 0.8f };
    switch (Channel)
    {
        case 0u:  return !NearlyEqual(S.BaseColor[0], DefaultColor[0]) || !NearlyEqual(S.BaseColor[1], DefaultColor[1]) || !NearlyEqual(S.BaseColor[2], DefaultColor[2]);
        case 1u:  return S.BaseMetalness != 0.0f;
        case 2u:  return !NearlyEqual(S.SpecularRoughness, 0.3f);
        case 3u:  return S.SpecularIor != 1.5f || S.SpecularWeight != 1.0f || S.SpecularColor[0] != 1.0f || S.SpecularColor[1] != 1.0f || S.SpecularColor[2] != 1.0f;
        case 6u:  return S.EmissionLuminance > 0.0f;
        case 7u:  return S.GeometryOpacity != 1.0f;
        case 8u:  return S.SpecularRoughnessAnisotropy != 0.0f;
        case 9u:  return S.SlateAnisotropyRotation != 0.0f;
        case 10u: return S.CoatWeight > 0.0f;
        case 11u: return S.CoatWeight > 0.0f && S.CoatRoughness != 0.0f;
        case 13u: return S.FuzzWeight > 0.0f && (S.FuzzColor[0] != 1.0f || S.FuzzColor[1] != 1.0f || S.FuzzColor[2] != 1.0f);
        case 14u: return S.FuzzWeight > 0.0f && !NearlyEqual(S.FuzzRoughness, 0.5f);
        case 15u: return S.SubsurfaceWeight > 0.0f && (S.SubsurfaceColor[0] != 0.8f || S.SubsurfaceColor[1] != 0.8f || S.SubsurfaceColor[2] != 0.8f);
        case 16u: return S.SubsurfaceWeight > 0.0f && (S.SubsurfaceRadius != 1.0f || S.SubsurfaceRadiusScale[0] != 1.0f ||
                                                       S.SubsurfaceRadiusScale[1] != 0.5f || S.SubsurfaceRadiusScale[2] != 0.25f);
        case 17u: return S.TransmissionWeight > 0.0f;
        case 18u: return S.SpecularIor != 1.5f;
        default:  return false;   // 4 normal · 5 occlusion · 12 coat normal (texture-shaped) · 19 displacement (none)
    }
}

} // namespace

int main()
{
    std::setlocale(LC_ALL, "C");
    using namespace Frontier;

    constexpr uint32_t kAuthored = MaterialSwatchStructure::kMaterialCount;          // 49
    constexpr uint32_t kSwatches = MaterialSwatchStructure::kSwatchCount;            // 42
    constexpr uint32_t kSphereTriangles = 20u * 40u * 2u - 40u * 2u;                 // 1520 at 20 rings × 40 segments

    // ── A. Level builds → exports → decodes ─────────────────────────────────────────────────────────────────────
    MaterialSwatchStructure Library;
    Library.Construct();
    Check(Library.QueryMaterials().size() == kAuthored, "A1 level authors 49 materials (42 swatches + 2 studio + 3 panels + 2 luminaires)");
    Check(Library.QueryTriangles().size() == kSwatches * kSphereTriangles + 7u * 2u,
          "A2 triangle count = 42 spheres + 7 quads (floor, backdrop, 3 panels, 2 luminaires)");
    std::string Error;
    Check(Library.Export("/tmp/MaterialSwatches.gltf", &Error), "A3 exports through SceneCodec::Encode");
    SceneStructure Level;
    SceneDecodeConfiguration Decode;
    Decode.SlabLimit = 1u;
    Check(ContentCodec::Decode("/tmp/MaterialSwatches.gltf", Level, nullptr, Decode, &Error), "A4 exported level decodes through ContentCodec");
    Check(Level.QueryMaterials().QueryCount() == kAuthored + 1u, "A5 49 authored materials + 1 fallback slot");
    {
        bool SameOrder = Level.QueryMaterials().QueryCount() == kAuthored + 1u;
        for (uint32_t I = 0u; SameOrder && I < kAuthored; ++I)
            SameOrder = Level.QueryMaterials().QueryDescriptors()[I].Name == Library.QueryMaterials()[I].Name;
        Check(SameOrder, "A6 decoded material names match the authored order exactly");
    }

    // ── B. Census print — the library as a lookdev sheet is the deliverable; the numbers are the review ──────────
    const std::vector<MaterialDescriptor>& Authored = Library.QueryMaterials();
    const std::vector<MaterialDescriptor>& Decoded  = Level.QueryMaterials().QueryDescriptors();
    const std::vector<MaterialRecord>&     Records  = Level.QueryMaterials().QueryRecords();
    std::printf("[swatch] --- material library census (42 swatches + 3 sign panels) ---\n");
    uint32_t SelectionTally[8] = {};
    std::vector<uint32_t> Inventory;   // the materials the level's selection story is told with: swatches then panels
    for (uint32_t N = 0u; N < kSwatches; ++N) Inventory.push_back(MaterialSwatchStructure::QuerySwatchMaterial(N));
    for (uint32_t M = MaterialSwatchStructure::kPanelCutoutMaterial; M <= MaterialSwatchStructure::kPanelEmissionMaterial; ++M) Inventory.push_back(M);
    for (size_t I = 0u; I < Inventory.size(); ++I)
    {
        const uint32_t Slot = Inventory[I];
        uint32_t Folded = 0u;
        const std::vector<MaterialSlabDescriptor> Flat = MaterialIndex::Flatten(Decoded[Slot], 1u, &Folded, nullptr);
        const MaterialSlabDescriptor& S = Flat.empty() ? MaterialSlabDescriptor{} : Flat.front();
        const MaterialReflectance Sel = MaterialIndex::DeriveReflectance(Decoded[Slot], S);
        ++SelectionTally[static_cast<uint32_t>(Sel)];
        uint32_t Channels = 0u;
        for (uint32_t C = 0u; C < 20u; ++C) Channels += ChannelSet(C, S) ? 1u : 0u;
        if (I < kSwatches)
            std::printf("[swatch]   %02u %-26s %-12s %-7s channels %2u\n", static_cast<uint32_t>(I), MaterialSwatchStructure::QuerySwatchName(static_cast<uint32_t>(I)),
                        SelName(Sel), CompName(Records[Slot].Complexity), Channels);
        else
            std::printf("[swatch]   -- %-26s %-12s %-7s channels %2u\n", Decoded[Slot].Name.c_str(), SelName(Sel), CompName(Records[Slot].Complexity), Channels);
    }
    std::printf("[swatch] selection tally:");
    for (uint32_t S = 0u; S < 8u; ++S) std::printf(" %s=%u", SelName(static_cast<MaterialReflectance>(S)), SelectionTally[S]);
    std::printf("\n");

    // ── C. Uniqueness + fold stability ───────────────────────────────────────────────────────────────────────────
    {
        std::set<std::string> Names;
        for (uint32_t N = 0u; N < kSwatches; ++N) Names.insert(MaterialSwatchStructure::QuerySwatchName(N));
        Check(Names.size() == kSwatches, "C1 42 distinct swatch names");

        std::set<std::string> Signatures;
        for (uint32_t N = 0u; N < kSwatches; ++N)
        {
            const uint32_t Slot = MaterialSwatchStructure::QuerySwatchMaterial(N);
            const MaterialSlabRecord& R = Level.QueryMaterials().QuerySlabRecords()[Records[Slot].SlabOffset];
            std::string Key;
            const uint32_t Floats = 58u;
            const float* Fields = reinterpret_cast<const float*>(&R);
            for (uint32_t F = 0u; F < Floats; ++F) { char Buf[32]; std::snprintf(Buf, sizeof(Buf), "%.9g|", static_cast<double>(Fields[F])); Key += Buf; }
            Signatures.insert(Key);
        }
        Check(Signatures.size() == kSwatches, "C2 42 distinct resident records (no two swatches share a value set)");

        bool Collapsed = false;
        const uint32_t Limits[3] = { 1u, 2u, 8u };
        for (uint32_t L : Limits)
        {
            std::vector<std::string> Report;
            Level.AccessMaterials().Finalise(L, &Report);
            if (!Report.empty()) Collapsed = true;
            for (const MaterialRecord& R : Level.QueryMaterials().QueryRecords())
                if (R.SlabCount != 1u) Collapsed = true;
        }
        Check(!Collapsed, "C3 zero folds and one resident slab per material at limits 1 / 2 / 8");
        Level.AccessMaterials().Finalise(1u, nullptr);
    }

    // ── D. Channel coverage — 16 carried, 4 acknowledged ─────────────────────────────────────────────────────────
    {
        uint32_t Covered = 0u, Gaps = 0u;
        for (uint32_t Channel = 0u; Channel < 20u; ++Channel)
        {
            const char* Carrier = nullptr;
            for (uint32_t N = 0u; N < kSwatches && Carrier == nullptr; ++N)
            {
                const uint32_t Slot = MaterialSwatchStructure::QuerySwatchMaterial(N);
                uint32_t Folded = 0u;
                const std::vector<MaterialSlabDescriptor> Flat = MaterialIndex::Flatten(Decoded[Slot], 1u, &Folded, nullptr);
                if (!Flat.empty() && ChannelSet(Channel, Flat.front())) Carrier = MaterialSwatchStructure::QuerySwatchName(N);
            }
            if (Carrier == nullptr)
            {
                for (uint32_t Slot = kSwatches + MaterialSwatchStructure::kFirstSwatchMaterial; Slot < kAuthored && Carrier == nullptr; ++Slot)
                {
                    uint32_t Folded = 0u;
                    const std::vector<MaterialSlabDescriptor> Flat = MaterialIndex::Flatten(Decoded[Slot], 1u, &Folded, nullptr);
                    if (!Flat.empty() && ChannelSet(Channel, Flat.front())) Carrier = Decoded[Slot].Name.c_str();
                }
            }
            if (Carrier != nullptr) { ++Covered; std::printf("[swatch] channel %-26s carried by %s\n", ChannelName(Channel), Carrier); }
            else
            {
                ++Gaps;
                std::printf("[swatch] channel %-26s %s\n", ChannelName(Channel),
                            Channel == 19u ? "ACKNOWLEDGED — channel 20 displacement: none (plan M6 decision)"
                                           : "ACKNOWLEDGED — texture-shaped; carried by imported content, not by this level");
            }
        }
        char Label[128];
        std::snprintf(Label, sizeof(Label), "D1 16 of 20 channels carried by the library, %u acknowledged gaps", Gaps);
        Check(Covered == 16u && Gaps == 4u, Label);

        bool AllSelections = true;
        for (uint32_t S = 0u; S < 8u; ++S) if (SelectionTally[S] == 0u) AllSelections = false;
        Check(AllSelections, "D2 all eight reflectance selections present (EmissiveOnly via extras base_weight 0, Unlit via KHR_materials_unlit)");
    }

    // ── E. Round-trip fidelity — authored vs decoded, every swatch, every field ──────────────────────────────────
    {
        uint32_t Drifted = 0u;
        for (uint32_t N = 0u; N < kSwatches; ++N)
        {
            const uint32_t Slot = MaterialSwatchStructure::QuerySwatchMaterial(N);
            char Report[160];
            const uint32_t Drift = CompareSlab(Authored[Slot].Slabs[0], Decoded[Slot].Slabs[0], Report, sizeof(Report));
            if (Drift != 0u)
            {
                ++Drifted;
                std::printf("[swatch] drift %-26s %s\n", MaterialSwatchStructure::QuerySwatchName(N), Report);
            }
            char Label[128];
            std::snprintf(Label, sizeof(Label), "E%u %s round-trips (58-float prefix, folds compared as products)", N, MaterialSwatchStructure::QuerySwatchName(N));
            Check(Drift == 0u, Label);
        }
        Check(Drifted == 0u, "E-summary no swatch drifted");

        const uint32_t ComparedFlags = MaterialFlagDoubleSided | MaterialFlagAlphaMask | MaterialFlagAlphaTranslucent | MaterialFlagUnlit | MaterialFlagThinWalled;
        bool FlagsOk = true, ThinOk = true;
        for (uint32_t I = 0u; I < kAuthored; ++I)
        {
            if ((Authored[I].Flags & ComparedFlags) != (Decoded[I].Flags & ComparedFlags)) { FlagsOk = false; std::printf("[swatch] flags drift %s\n", Authored[I].Name.c_str()); }
            if (Authored[I].Slabs[0].GeometryThinWalled != Decoded[I].Slabs[0].GeometryThinWalled) { ThinOk = false; std::printf("[swatch] thin drift %s\n", Authored[I].Name.c_str()); }
        }
        Check(FlagsOk, "E-flags double-sided / mask / unlit / thin-walled flags survive for all 49 materials");
        Check(ThinOk, "E-thin geometry_thin_walled survives for all 49 materials");
        Check(Authored[MaterialSwatchStructure::kPanelCutoutMaterial].AlphaCutoff == Decoded[MaterialSwatchStructure::kPanelCutoutMaterial].AlphaCutoff,
              "E-cutout alphaCutoff survives (0.5)");
    }

    // ── F. Grid geometry, spans, bounds, luminaires, flag inventory ──────────────────────────────────────────────
    {
        const std::vector<TriangleSpanRecord>& Spans = Library.QuerySpans();
        Check(Spans.size() == kSwatches + 7u, "F1 49 object spans (floor, backdrop, 42 swatches, 3 panels, 2 luminaires)");
        Check(Spans.back().Name == "Luminaire Fill" && Spans[Spans.size() - 2u].Name == "Luminaire Key",
              "F2 the two luminaires are the last spans (the shared luminaire convention)");

        const std::vector<TriangleIndex>& Soup = Library.QueryTriangles();
        bool GridOk = true;
        for (uint32_t N = 0u; N < kSwatches && GridOk; ++N)
        {
            const TriangleSpanRecord& Span = Spans[2u + N];
            GridOk = Span.TriangleCount == kSphereTriangles;
            const Vector3 Origin = MaterialSwatchStructure::QuerySwatchOrigin(N);
            float Lo[3] = { 1e30f, 1e30f, 1e30f }, Hi[3] = { -1e30f, -1e30f, -1e30f };
            for (uint32_t T = Span.FirstTriangle; T < Span.FirstTriangle + Span.TriangleCount; ++T)
            {
                const float P[3][3] =
                {
                    { Soup[T].VertexAlphaX, Soup[T].VertexAlphaY, Soup[T].VertexAlphaZ },
                    { Soup[T].VertexBetaX,  Soup[T].VertexBetaY,  Soup[T].VertexBetaZ  },
                    { Soup[T].VertexGammaX, Soup[T].VertexGammaY, Soup[T].VertexGammaZ }
                };
                for (const auto& V : P) for (uint32_t A = 0u; A < 3u; ++A) { Lo[A] = std::min(Lo[A], V[A]); Hi[A] = std::max(Hi[A], V[A]); }
            }
            const float Centre[3] = { (Lo[0] + Hi[0]) * 0.5f, (Lo[1] + Hi[1]) * 0.5f, (Lo[2] + Hi[2]) * 0.5f };
            const float Half[3]   = { (Hi[0] - Lo[0]) * 0.5f, (Hi[1] - Lo[1]) * 0.5f, (Hi[2] - Lo[2]) * 0.5f };
            GridOk = GridOk && NearlyEqual(Centre[0], Origin.x) && NearlyEqual(Centre[1], Origin.y) && NearlyEqual(Centre[2], Origin.z) &&
                     std::fabs(Half[0] - MaterialSwatchStructure::kSwatchRadius) < 0.002f &&
                     std::fabs(Half[1] - MaterialSwatchStructure::kSwatchRadius) < 0.002f &&
                     std::fabs(Half[2] - MaterialSwatchStructure::kSwatchRadius) < 0.002f;
        }
        Check(GridOk, "F3 every swatch sphere is centred on the declared 7 × 6 pitch at radius 0.40 m");

        bool Apart = true;
        for (uint32_t A = 0u; A < kSwatches && Apart; ++A)
            for (uint32_t B = A + 1u; B < kSwatches && Apart; ++B)
            {
                const Vector3 PA = MaterialSwatchStructure::QuerySwatchOrigin(A), PB = MaterialSwatchStructure::QuerySwatchOrigin(B);
                const Vector3 D = PA - PB;
                Apart = D.Length() >= MaterialSwatchStructure::kSwatchPitch - 1.0e-3f;
            }
        Check(Apart, "F4 no two swatches closer than the 1.10 m pitch (no interpenetration)");

        const Vector3 Lo = Level.QueryBoundsMinimum(), Hi = Level.QueryBoundsMaximum();
        Check(std::fabs(Lo.x + 5.0f) < 1.0e-3f && std::fabs(Hi.x - 5.0f) < 1.0e-3f &&
              std::fabs(Lo.y + 2.0f) < 1.0e-3f && std::fabs(Hi.y - 7.60f) < 1.0e-3f &&
              std::fabs(Lo.z - 0.0f) < 1.0e-3f && std::fabs(Hi.z - 4.60f) < 1.0e-3f,
              "F5 level bounds X ±5.0 · Y −2.0 … 7.60 · Z 0 … 4.60 m");

        Check(Level.QueryLuminaires().size() == 6u, "F6 three emissive quads → 6 luminaire triangles (key, fill, emissive panel)");
        uint32_t MaskCount = 0u, ThinCount = 0u, UnlitCount = 0u, EmissiveCount = 0u;
        for (const MaterialRecord& R : Level.QueryMaterials().QueryRecords())
        {
            MaskCount     += (R.Flags & MaterialFlagAlphaMask) != 0u ? 1u : 0u;
            ThinCount     += (R.Flags & MaterialFlagThinWalled) != 0u ? 1u : 0u;
            UnlitCount    += (R.Flags & MaterialFlagUnlit) != 0u ? 1u : 0u;
            EmissiveCount += (R.EmissiveR + R.EmissiveG + R.EmissiveB) > 0.0f ? 1u : 0u;
        }
        Check(MaskCount == 1u && ThinCount == 1u && UnlitCount == 1u && EmissiveCount == 3u,
              "F7 flag inventory: 1 alpha-mask, 1 thin-walled, 1 unlit, 3 emissive materials");

        bool Names = true;
        for (uint32_t N = 0u; N < kSwatches && Names; ++N)
        {
            char Expected[96];
            std::snprintf(Expected, sizeof(Expected), "Swatch %02u (%s)", N, MaterialSwatchStructure::QuerySwatchName(N));
            Names = Spans[2u + N].Name == Expected;
        }
        Check(Names, "F8 every swatch span is named \"Swatch NN (material)\" so the outliner reads as the census");
    }

    std::remove("/tmp/MaterialSwatches.gltf");
    std::remove("/tmp/MaterialSwatches.bin");

    if (Failed == 0) std::printf("MATERIAL SWATCHES: PASS (%d/%d)\n", Passed, Passed + Failed);
    else             std::printf("MATERIAL SWATCHES: FAIL (%d passed, %d failed)\n", Passed, Failed);
    return Failed == 0 ? 0 : 1;
}
