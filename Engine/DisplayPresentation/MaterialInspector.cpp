//============================================================================================================================================
//                                                     MATERIALINSPECTOR.CPP
//============================================================================================================================================
// 🔍 M7b editable material inspector (see the header for the data + editing models). Rows read the
//    RESOLVED slab; sliders write per-material drafts; layout is ControlKit-native with custom 20-row
//    channel cards + editors.

#include "MaterialInspector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier {
namespace {

// Sultan-42 §5 channel names, verbatim from the M0 coverage registry (MaterialChannelCoverage kRows) so the page and
//    the coverage table cross-reference row for row.
const char* const kRowNames[kMaterialChannelRowCount] = {
    "01 base colour", "02 metallic", "03 roughness", "04 reflectance/IOR", "05 orientation",
    "06 occlusion", "07 emission", "08 opacity", "09 anisotropy", "10 anisotropy direction",
    "11 clear coat", "12 coat roughness", "13 coat orientation", "14 sheen colour", "15 sheen roughness",
    "16 subsurface colour", "17 subsurface thickness", "18 transmission", "19 IOR (refraction)", "20 displacement",
};

const char* const kSelectionNames[8] = {
    "Standard", "Anisotropic", "ClearCoated", "Cloth", "Subsurface", "Transmissive", "EmissiveOnly", "Unlit",
};

const char* const kComplexityNames[4] = { "Simple", "Single", "Complex", "Special" };
const char* const kSourceNames[3]     = { "constant", "imported", "absent" };
const char* const kChannelNames[5]    = { "rgb", "R", "G", "B", "A" };

[[nodiscard]] bool IsWhite(const float C[3]) noexcept { return C[0] == 1.0f && C[1] == 1.0f && C[2] == 1.0f; }
[[nodiscard]] bool IsZero(const float C[3]) noexcept { return C[0] == 0.0f && C[1] == 0.0f && C[2] == 0.0f; }

// Append a " · "-joined fragment to a detail buffer (snprintf-truncated, always NUL terminated).
void AppendDetail(char* Out, size_t Capacity, const char* Fragment) noexcept
{
    if (!Fragment || !*Fragment) return;
    const size_t Used = std::strlen(Out);
    if (Used + 1u >= Capacity) return;
    std::snprintf(Out + Used, Capacity - Used, "%s%s", Used ? " \xC2\xB7 " : "", Fragment);
}

void FormatColour(char* Out, size_t Capacity, const float C[3]) noexcept
{
    std::snprintf(Out, Capacity, "(%.3g, %.3g, %.3g)", static_cast<double>(C[0]), static_cast<double>(C[1]), static_cast<double>(C[2]));
}


// M7b editor ranges per editable row (None rows are never queried — the layout and SetDraft* route
// through MaterialRowEditKind first). Proof section J pins every range via the clamp asserts.
void EditRange(uint32_t Row, float& Min, float& Max) noexcept
{
    Min = 0.0f; Max = 1.0f;
    if (Row == 3u || Row == 18u) { Min = 1.0f; Max = 2.5f; }   // IOR (one carrier, two Sultan rows)
    else if (Row == 6u) Max = 20.0f;                            // emission [nit]
    else if (Row == 8u) { Min = -1.0f; Max = 1.0f; }             // signed aniso (negative flips the axis)
    else if (Row == 9u) Max = 6.28318530717959f;                // rotation [rad]
    else if (Row == 16u) Max = 2.0f;                            // SSS radius [m]
}

const MaterialEditKind kEditKinds[kMaterialChannelRowCount] = {
    MaterialEditKind::Rgb,    MaterialEditKind::Scalar, MaterialEditKind::Scalar, MaterialEditKind::Scalar,
    MaterialEditKind::None,   MaterialEditKind::Scalar, MaterialEditKind::Scalar, MaterialEditKind::Scalar,
    MaterialEditKind::Scalar, MaterialEditKind::Scalar, MaterialEditKind::Scalar, MaterialEditKind::Scalar,
    MaterialEditKind::None,   MaterialEditKind::Rgb,    MaterialEditKind::Scalar, MaterialEditKind::Rgb,
    MaterialEditKind::Scalar, MaterialEditKind::Scalar, MaterialEditKind::Scalar, MaterialEditKind::None,
};
} // namespace

const char* MaterialChannelRowName(uint32_t Row) noexcept { return kRowNames[Row < kMaterialChannelRowCount ? Row : 0u]; }
const char* MaterialSelectionName(MaterialReflectance Selection) noexcept
{
    const uint32_t I = static_cast<uint32_t>(Selection);
    return kSelectionNames[I < 8u ? I : 0u];
}
const char* MaterialComplexityName(uint32_t Complexity) noexcept { return kComplexityNames[Complexity < 4u ? Complexity : 0u]; }
const char* MaterialChannelSourceName(MaterialChannelSource Source) noexcept
{
    const uint32_t I = static_cast<uint32_t>(Source);
    return kSourceNames[I < 3u ? I : 2u];
}
const char* MaterialTextureChannelName(TextureChannelSelection Channel) noexcept
{
    const uint32_t I = static_cast<uint32_t>(Channel);
    return kChannelNames[I < 5u ? I : 0u];
}

// M7b: None = rows 05/13 (map-or-mesh-frame) + 20 (no carrier); Rgb = colour carriers (01/14/16).
MaterialEditKind MaterialRowEditKind(uint32_t Row) noexcept { return kEditKinds[Row < kMaterialChannelRowCount ? Row : 19u]; }

//------------------------------------------------------------------------------------------------------------------------
//                                                        SELECTION
//------------------------------------------------------------------------------------------------------------------------

void MaterialInspector::SeedSelection(const char* PersistedName) noexcept
{
    SelectedName[0] = '\0';
    if (PersistedName && *PersistedName)
    {
        std::strncpy(SelectedName, PersistedName, sizeof(SelectedName) - 1u);
        SelectedName[sizeof(SelectedName) - 1u] = '\0';
    }
}

bool MaterialInspector::Select(uint32_t Id) noexcept
{
    const uint32_t Count = static_cast<uint32_t>(Names.size());
    const uint32_t Clamped = Count ? std::min(Id, Count - 1u) : 0u;
    const char* Name = Clamped < Names.size() ? Names[Clamped].c_str() : "";
    if (Clamped == SelectedId && std::strcmp(Name, SelectedName) == 0) return false;
    SelectedId = Clamped;
    std::strncpy(SelectedName, Name, sizeof(SelectedName) - 1u);
    SelectedName[sizeof(SelectedName) - 1u] = '\0';
    ++Revision;
    return true;
}

void MaterialInspector::Rebuild(MaterialIndex* Index) noexcept
{
    CachedIndex = Index;   // the Apply target (retained, never owned)
    if (!Index || Index->QueryCount() == 0u) { Drafts.clear(); DraftNames.clear(); DraftIndex = nullptr; }
    Names.clear(); NamePointers.clear(); FoldLines.clear();
    for (uint32_t I = 0u; I < kMaterialChannelRowCount; ++I)
    {
        Rows[I] = MaterialChannelRow{};
        Rows[I].Name = kRowNames[I];
        std::strcpy(Rows[I].Value, "—");
        std::strcpy(Rows[I].Texture, "—");
    }
    Selection = MaterialReflectance::Standard; Complexity = MaterialComplexitySimple;
    AuthoredSlabs = 0u; ResidentSlabs = 0u; SlabLimit = 1u;
    SelectionText[0] = ComplexityText[0] = HeaderSlabs[0] = HeaderFlags[0] = '\0';

    if (Index && Index->QueryCount() > 0u)
    {
        SlabLimit = std::max(1u, Index->QueryMetrics().SlabLimit);
        const std::vector<MaterialDescriptor>& Descriptors = Index->QueryDescriptors();
        for (const MaterialDescriptor& D : Descriptors)
            Names.push_back(D.Name.empty() ? "(unnamed)" : D.Name);
        for (const std::string& N : Names) NamePointers.push_back(N.c_str());
        SyncDrafts(Descriptors);   // same scene: keep drafts (retention); new scene: re-snapshot

        // Resolve the seeded/persisted name (exact display-name match, else material 0).
        uint32_t Id = 0u;
        if (SelectedName[0])
            for (uint32_t I = 0u; I < Names.size(); ++I)
                if (Names[I] == SelectedName) { Id = I; break; }
        const MaterialDescriptor& D = Descriptors[Id];

        // Resolved slab = what the Tier A kernel samples (Slabs.front at the scene's limit — the same call Finalise
        //    makes, so the page can never disagree with the records).
        uint32_t Folded = 0u;
        const std::vector<MaterialSlabDescriptor> Slabs = MaterialIndex::Flatten(D, SlabLimit, &Folded, nullptr, nullptr);
        static const MaterialSlabDescriptor kDefaultSlab{};
        const MaterialSlabDescriptor& S = Slabs.empty() ? kDefaultSlab : Slabs.front();
        Selection = MaterialIndex::DeriveReflectance(D, S);
        Complexity = MaterialIndex::ClassifyComplexity(Slabs);
        AuthoredSlabs = static_cast<uint32_t>(D.Slabs.size());
        ResidentSlabs = static_cast<uint32_t>(Slabs.size());

        std::strncpy(SelectionText, MaterialSelectionName(Selection), sizeof(SelectionText) - 1u);
        std::strncpy(ComplexityText, MaterialComplexityName(Complexity), sizeof(ComplexityText) - 1u);
        std::snprintf(HeaderSlabs, sizeof(HeaderSlabs), "%u authored \xE2\x86\x92 %u resident (limit %u)",
                      AuthoredSlabs, ResidentSlabs, SlabLimit);
        {
            char Alpha[32] = {};
            if ((D.Flags & MaterialFlagAlphaMask) != 0u) std::snprintf(Alpha, sizeof(Alpha), "mask @ %.3g", static_cast<double>(D.AlphaCutoff));
            else if ((D.Flags & MaterialFlagAlphaTranslucent) != 0u) std::strcpy(Alpha, "blend");
            else std::strcpy(Alpha, "opaque");
            std::snprintf(HeaderFlags, sizeof(HeaderFlags), "alpha: %s \xC2\xB7 %s%s%s", Alpha,
                          (D.Flags & MaterialFlagDoubleSided) != 0u ? "double-sided" : "single-sided",
                          (D.Flags & MaterialFlagUnlit) != 0u ? " \xC2\xB7 unlit" : "",
                          (D.Flags & MaterialFlagThinWalled) != 0u || S.GeometryThinWalled ? " \xC2\xB7 thin-walled" : "");
        }

        BuildRows(D, S);

        // Fold attribution: Finalise writes `material '<name>': …` — keep this material's lines only.
        {
            char Prefix[160] = {};
            std::snprintf(Prefix, sizeof(Prefix), "material '%s':", D.Name.c_str());
            for (const std::string& Line : Index->QueryFoldReport())
                if (Line.compare(0, std::strlen(Prefix), Prefix) == 0) FoldLines.push_back(Line);
        }

        // Revision iff the resolved NAME changed (unknown seed → fallback, scene swap, or unload below).
        SelectedId = Id;
        if (std::strcmp(Names[Id].c_str(), SelectedName) != 0)
        {
            std::strncpy(SelectedName, Names[Id].c_str(), sizeof(SelectedName) - 1u);
            SelectedName[sizeof(SelectedName) - 1u] = '\0';
            ++Revision;
        }
    }
    else if (SelectedName[0] != '\0')
    {
        SelectedName[0] = '\0';   // scene unloaded: persist the empty (scene-default) name
        ++Revision;
    }
    BuildStatusAndSummary();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         EDITING
//------------------------------------------------------------------------------------------------------------------------

void MaterialInspector::SyncDrafts(const std::vector<MaterialDescriptor>& Descriptors) noexcept
{
    if (CachedIndex == DraftIndex && DraftNames == Names) return;   // same scene: retention (never touch drafts)
    static const MaterialSlabDescriptor kDefaultSlab{};
    Drafts.clear();
    DraftNames = Names;
    DraftIndex = CachedIndex;
    for (const MaterialDescriptor& D : Descriptors)
    {
        MaterialDraft Draft;
        Draft.Slab = D.Slabs.empty() ? kDefaultSlab : D.Slabs.front();
        Draft.AlphaCutoff = D.AlphaCutoff;
        Drafts.push_back(Draft);
    }
}

bool MaterialInspector::DraftDiffers(uint32_t Material) const noexcept
{
    if (!CachedIndex || Material >= Drafts.size() || Material >= CachedIndex->QueryDescriptors().size()) return false;
    static const MaterialSlabDescriptor kDefaultSlab{};
    const MaterialDescriptor& D = CachedIndex->QueryDescriptors()[Material];
    const MaterialSlabDescriptor& Applied = D.Slabs.empty() ? kDefaultSlab : D.Slabs.front();
    return !(Drafts[Material].Slab == Applied) || Drafts[Material].AlphaCutoff != D.AlphaCutoff;
}

bool MaterialInspector::IsDirty() const noexcept
{
    for (uint32_t I = 0u; I < Drafts.size(); ++I)
        if (DraftDiffers(I)) return true;
    return false;
}

uint32_t MaterialInspector::QueryDirtyCount() const noexcept
{
    uint32_t N = 0u;
    for (uint32_t I = 0u; I < Drafts.size(); ++I)
        N += DraftDiffers(I) ? 1u : 0u;
    return N;
}

const MaterialSlabDescriptor& MaterialInspector::QueryDraftSlab(uint32_t Material) const noexcept
{
    static const MaterialSlabDescriptor kDefaultSlab{};
    return Material < Drafts.size() ? Drafts[Material].Slab : kDefaultSlab;
}

float MaterialInspector::QueryDraftCutoff(uint32_t Material) const noexcept
{
    return Material < Drafts.size() ? Drafts[Material].AlphaCutoff : 0.5f;
}

// The knob readers (layout + proof share them; the switch mirrors SetDraft* field for field — section J loops every
//    row asserting Set-then-Query round-trips, so the two maps cannot drift apart silently).
float MaterialInspector::QueryDraftScalar(uint32_t Row) const noexcept
{
    if (SelectedId >= Drafts.size() || MaterialRowEditKind(Row) != MaterialEditKind::Scalar) return 0.0f;
    const MaterialSlabDescriptor& S = Drafts[SelectedId].Slab;
    switch (Row)
    {
        case 1u:  return S.BaseMetalness;
        case 2u:  return S.SpecularRoughness;
        case 3u:  return S.SpecularIor;
        case 5u:  return S.Texture(MaterialTextureChannel::Occlusion).Scalar;
        case 6u:  return S.EmissionLuminance;
        case 7u:  return S.GeometryOpacity;
        case 8u:  return S.SpecularRoughnessAnisotropy;
        case 9u:  return S.SlateAnisotropyRotation;
        case 10u: return S.CoatWeight;
        case 11u: return S.CoatRoughness;
        case 14u: return S.FuzzRoughness;
        case 16u: return S.SubsurfaceRadius;
        case 17u: return S.TransmissionWeight;
        case 18u: return S.SpecularIor;
        default:  return 0.0f;
    }
}

float MaterialInspector::QueryDraftColor(uint32_t Row, uint32_t Component) const noexcept
{
    if (SelectedId >= Drafts.size() || Component >= 3u || MaterialRowEditKind(Row) != MaterialEditKind::Rgb) return 0.0f;
    const MaterialSlabDescriptor& S = Drafts[SelectedId].Slab;
    if (Row == 0u) return S.BaseColor[Component];
    if (Row == 13u) return S.FuzzColor[Component];
    if (Row == 15u) return S.SubsurfaceColor[Component];
    return 0.0f;
}

const PlaneExtent& MaterialInspector::QueryEditExtent(uint32_t Row, uint32_t Component) const noexcept
{
    static const PlaneExtent kEmpty{};
    return (Row < kMaterialChannelRowCount && Component < 3u) ? EditExtents[Row][Component] : kEmpty;
}

void MaterialInspector::SetDraftScalar(uint32_t Material, uint32_t Row, float V) noexcept
{
    if (Material >= Drafts.size() || MaterialRowEditKind(Row) != MaterialEditKind::Scalar) return;
    float Min = 0.0f, Max = 1.0f;
    EditRange(Row, Min, Max);
    MaterialSlabDescriptor& S = Drafts[Material].Slab;
    switch (Row)
    {
        case 1u:  S.BaseMetalness = std::clamp(V, Min, Max); break;
        case 2u:  S.SpecularRoughness = std::clamp(V, Min, Max); break;
        case 3u:  S.SpecularIor = std::clamp(V, Min, Max); break;
        case 5u:  S.Texture(MaterialTextureChannel::Occlusion).Scalar = std::clamp(V, Min, Max); break;
        case 6u:  S.EmissionLuminance = std::clamp(V, Min, Max); break;
        case 7u:  S.GeometryOpacity = std::clamp(V, Min, Max); break;
        case 8u:  S.SpecularRoughnessAnisotropy = std::clamp(V, Min, Max); break;
        case 9u:  S.SlateAnisotropyRotation = std::clamp(V, Min, Max); break;
        case 10u: S.CoatWeight = std::clamp(V, Min, Max); break;
        case 11u: S.CoatRoughness = std::clamp(V, Min, Max); break;
        case 14u: S.FuzzRoughness = std::clamp(V, Min, Max); break;
        case 16u: S.SubsurfaceRadius = std::clamp(V, Min, Max); break;
        case 17u: S.TransmissionWeight = std::clamp(V, Min, Max); break;
        case 18u: S.SpecularIor = std::clamp(V, Min, Max); break;   // same carrier as row 04
        default: break;
    }
}

void MaterialInspector::SetDraftColor(uint32_t Material, uint32_t Row, uint32_t Component, float V) noexcept
{
    if (Material >= Drafts.size() || Component >= 3u || MaterialRowEditKind(Row) != MaterialEditKind::Rgb) return;
    V = std::clamp(V, 0.0f, 1.0f);
    MaterialSlabDescriptor& S = Drafts[Material].Slab;
    if (Row == 0u) S.BaseColor[Component] = V;
    else if (Row == 13u) S.FuzzColor[Component] = V;
    else if (Row == 15u) S.SubsurfaceColor[Component] = V;
}

void MaterialInspector::SetDraftCutoff(uint32_t Material, float V) noexcept
{
    if (Material >= Drafts.size()) return;
    Drafts[Material].AlphaCutoff = std::clamp(V, 0.0f, 1.0f);
}

void MaterialInspector::Apply() noexcept
{
    LastCommitCount = 0u;
    if (!CachedIndex) return;
    uint32_t Changed = 0u;
    for (uint32_t I = 0u; I < Drafts.size(); ++I)
    {
        if (!DraftDiffers(I)) continue;
        if (MaterialDescriptor* D = CachedIndex->AccessDescriptor(I))
        {
            if (D->Slabs.empty()) D->Slabs.push_back(Drafts[I].Slab);   // hand-built indexes only (codecs never emit empty)
            else D->Slabs.front() = Drafts[I].Slab;
            D->AlphaCutoff = Drafts[I].AlphaCutoff;
            ++Changed;
        }
    }
    if (Changed == 0u) { BuildStatusAndSummary(); return; }
    CachedIndex->Finalise(std::max(1u, CachedIndex->QueryMetrics().SlabLimit), nullptr);   // the index's own limit
    LastCommitCount = Changed;
    ++CommitRevision;
    if (PreviewEnabled) PreviewRequested = true;
    BuildStatusAndSummary();   // the footer reads the status right after Apply — never stale
}

void MaterialInspector::Discard() noexcept
{
    if (!CachedIndex) return;
    static const MaterialSlabDescriptor kDefaultSlab{};
    const std::vector<MaterialDescriptor>& Descriptors = CachedIndex->QueryDescriptors();
    for (uint32_t I = 0u; I < Drafts.size() && I < Descriptors.size(); ++I)
    {
        Drafts[I].Slab = Descriptors[I].Slabs.empty() ? kDefaultSlab : Descriptors[I].Slabs.front();
        Drafts[I].AlphaCutoff = Descriptors[I].AlphaCutoff;
    }
    BuildStatusAndSummary();   // same staleness contract as Apply
}

void MaterialInspector::NotifyPreviewRendered(bool Ok, const char* MaterialName, const char* Detail, double Seconds, uint32_t Commit) noexcept
{
    const char* Name = (MaterialName && *MaterialName) ? MaterialName : "(unnamed)";
    const char* Info = (Detail && *Detail) ? Detail : (Ok ? "?" : "failed");
    if (Ok)
        std::snprintf(PreviewStatus, sizeof(PreviewStatus), "%.64s \xC2\xB7 %.80s \xC2\xB7 %.1fs \xC2\xB7 commit #%u",
                      Name, Info, Seconds, Commit);
    else
        std::snprintf(PreviewStatus, sizeof(PreviewStatus), "%.64s \xC2\xB7 failed: %.96s", Name, Info);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          ROWS
//------------------------------------------------------------------------------------------------------------------------

void MaterialInspector::BuildRows(const MaterialDescriptor& D, const MaterialSlabDescriptor& S) noexcept
{
    (void)D;
    auto SetTexture = [&](uint32_t Row, const TextureReference& T) noexcept {
        if (T.IsBound())
            std::snprintf(Rows[Row].Texture, sizeof(Rows[Row].Texture), "tex %u \xC2\xB7 uv%u \xC2\xB7 %s",
                          T.Texture, T.UvSet, MaterialTextureChannelName(T.Channel));
        else
            std::strcpy(Rows[Row].Texture, "—");
    };
    char Frag[96];

    // 01 base colour — never absent (the constant IS the dielectric albedo).
    FormatColour(Rows[0].Value, sizeof(Rows[0].Value), S.BaseColor);
    SetTexture(0, S.Texture(MaterialTextureChannel::BaseColor));
    Rows[0].Source = S.Texture(MaterialTextureChannel::BaseColor).IsBound() ? MaterialChannelSource::Imported : MaterialChannelSource::Constant;
    if (S.BaseWeight != 1.0f) { std::snprintf(Frag, sizeof(Frag), "weight %.3g", static_cast<double>(S.BaseWeight)); AppendDetail(Rows[0].Detail, sizeof(Rows[0].Detail), Frag); }
    if (S.BaseDiffuseRoughness != 0.0f) { std::snprintf(Frag, sizeof(Frag), "eon %.3g", static_cast<double>(S.BaseDiffuseRoughness)); AppendDetail(Rows[0].Detail, sizeof(Rows[0].Detail), Frag); }

    // 02 metallic — 0 is a real dielectric, not an absence.
    std::snprintf(Rows[1].Value, sizeof(Rows[1].Value), "%.3g", static_cast<double>(S.BaseMetalness));
    SetTexture(1, S.Texture(MaterialTextureChannel::Metalness));
    Rows[1].Source = S.Texture(MaterialTextureChannel::Metalness).IsBound() ? MaterialChannelSource::Imported : MaterialChannelSource::Constant;

    // 03 roughness.
    std::snprintf(Rows[2].Value, sizeof(Rows[2].Value), "%.3g", static_cast<double>(S.SpecularRoughness));
    SetTexture(2, S.Texture(MaterialTextureChannel::SpecularRoughness));
    Rows[2].Source = S.Texture(MaterialTextureChannel::SpecularRoughness).IsBound() ? MaterialChannelSource::Imported : MaterialChannelSource::Constant;
    if (S.SlateGlintDensity != 0.0f)
    {
        std::snprintf(Frag, sizeof(Frag), "glint d %.3g \xC2\xB7 uv %.3g",
                      static_cast<double>(S.SlateGlintDensity), static_cast<double>(S.SlateGlintUvScale));
        AppendDetail(Rows[2].Detail, sizeof(Rows[2].Detail), Frag);
    }

    // 04 reflectance/IOR — spec weight/tint/D-f0/haze/film fold into the detail line.
    std::snprintf(Rows[3].Value, sizeof(Rows[3].Value), "ior %.3g", static_cast<double>(S.SpecularIor));
    {
        const TextureReference& T = S.Texture(MaterialTextureChannel::SpecularColor).IsBound()
            ? S.Texture(MaterialTextureChannel::SpecularColor) : S.Texture(MaterialTextureChannel::ThinFilm);
        SetTexture(3, T);
        Rows[3].Source = T.IsBound() ? MaterialChannelSource::Imported : MaterialChannelSource::Constant;
    }
    if (S.SpecularWeight != 1.0f) { std::snprintf(Frag, sizeof(Frag), "weight %.3g", static_cast<double>(S.SpecularWeight)); AppendDetail(Rows[3].Detail, sizeof(Rows[3].Detail), Frag); }
    if (!IsWhite(S.SpecularColor)) { char C[48]; FormatColour(C, sizeof(C), S.SpecularColor); std::snprintf(Frag, sizeof(Frag), "tint %s", C); AppendDetail(Rows[3].Detail, sizeof(Rows[3].Detail), Frag); }
    if (S.SlateDirectF0Weight != 0.0f) { std::snprintf(Frag, sizeof(Frag), "d-f0 %.3g", static_cast<double>(S.SlateDirectF0Weight)); AppendDetail(Rows[3].Detail, sizeof(Rows[3].Detail), Frag); }
    if (S.SlateHazinessWeight != 0.0f)
    {
        std::snprintf(Frag, sizeof(Frag), "haze w %.3g r %.3g",
                      static_cast<double>(S.SlateHazinessWeight), static_cast<double>(S.SlateHazinessRoughness));
        AppendDetail(Rows[3].Detail, sizeof(Rows[3].Detail), Frag);
    }
    if (S.ThinFilmWeight != 0.0f)
    {
        std::snprintf(Frag, sizeof(Frag), "film w %.3g t %.3g \xC2\xB5m n %.3g", static_cast<double>(S.ThinFilmWeight),
                      static_cast<double>(S.ThinFilmThickness), static_cast<double>(S.ThinFilmIor));
        AppendDetail(Rows[3].Detail, sizeof(Rows[3].Detail), Frag);
    }

    // 05 orientation — unbound = the mesh frame (a real constant, not an absence).
    const TextureReference& Nrm = S.Texture(MaterialTextureChannel::GeometryNormal);
    std::strcpy(Rows[4].Value, Nrm.IsBound() ? "map" : "mesh frame");
    SetTexture(4, Nrm);
    Rows[4].Source = Nrm.IsBound() ? MaterialChannelSource::Imported : MaterialChannelSource::Constant;
    if (Nrm.Scalar != 1.0f) { std::snprintf(Frag, sizeof(Frag), "scale %.3g", static_cast<double>(Nrm.Scalar)); AppendDetail(Rows[4].Detail, sizeof(Rows[4].Detail), Frag); }

    // 06 occlusion — no scalar carrier: unbound is Absent, strength rides the texture row.
    const TextureReference& Occ = S.Texture(MaterialTextureChannel::Occlusion);
    if (Occ.IsBound()) std::snprintf(Rows[5].Value, sizeof(Rows[5].Value), "strength %.3g", static_cast<double>(Occ.Scalar));
    SetTexture(5, Occ);
    Rows[5].Source = Occ.IsBound() ? MaterialChannelSource::Imported : MaterialChannelSource::Absent;

    // 07 emission.
    if (S.EmissionLuminance > 0.0f) std::snprintf(Rows[6].Value, sizeof(Rows[6].Value), "%.3g nit", static_cast<double>(S.EmissionLuminance));
    SetTexture(6, S.Texture(MaterialTextureChannel::Emission));
    Rows[6].Source = S.Texture(MaterialTextureChannel::Emission).IsBound() ? MaterialChannelSource::Imported
        : (S.EmissionLuminance > 0.0f ? MaterialChannelSource::Constant : MaterialChannelSource::Absent);
    if (!IsWhite(S.EmissionColor)) { char C[48]; FormatColour(C, sizeof(C), S.EmissionColor); std::snprintf(Frag, sizeof(Frag), "tint %s", C); AppendDetail(Rows[6].Detail, sizeof(Rows[6].Detail), Frag); }

    // 08 opacity.
    std::snprintf(Rows[7].Value, sizeof(Rows[7].Value), "%.3g", static_cast<double>(S.GeometryOpacity));
    SetTexture(7, S.Texture(MaterialTextureChannel::GeometryOpacity));
    Rows[7].Source = S.Texture(MaterialTextureChannel::GeometryOpacity).IsBound() ? MaterialChannelSource::Imported
        : (S.GeometryOpacity != 1.0f ? MaterialChannelSource::Constant : MaterialChannelSource::Absent);

    // 09 anisotropy + 10 direction (one shared carrier, shown on both rows).
    const TextureReference& Ani = S.Texture(MaterialTextureChannel::Anisotropy);
    std::snprintf(Rows[8].Value, sizeof(Rows[8].Value), "%.3g", static_cast<double>(S.SpecularRoughnessAnisotropy));
    SetTexture(8, Ani);
    Rows[8].Source = Ani.IsBound() ? MaterialChannelSource::Imported
        : (S.SpecularRoughnessAnisotropy != 0.0f ? MaterialChannelSource::Constant : MaterialChannelSource::Absent);
    const bool AnisoActive = S.SpecularRoughnessAnisotropy != 0.0f || Ani.IsBound();
    if (AnisoActive) std::snprintf(Rows[9].Value, sizeof(Rows[9].Value), "%.3g rad", static_cast<double>(S.SlateAnisotropyRotation));
    SetTexture(9, Ani);
    Rows[9].Source = !AnisoActive ? MaterialChannelSource::Absent
        : (Ani.IsBound() ? MaterialChannelSource::Imported : MaterialChannelSource::Constant);

    // 11 clear coat + 12 roughness + 13 orientation (coat weight gates 12/13).
    std::snprintf(Rows[10].Value, sizeof(Rows[10].Value), "%.3g", static_cast<double>(S.CoatWeight));
    SetTexture(10, S.Texture(MaterialTextureChannel::Coat));
    Rows[10].Source = S.Texture(MaterialTextureChannel::Coat).IsBound() ? MaterialChannelSource::Imported
        : (S.CoatWeight != 0.0f ? MaterialChannelSource::Constant : MaterialChannelSource::Absent);
    if (S.CoatIor != 1.6f) { std::snprintf(Frag, sizeof(Frag), "ior %.3g", static_cast<double>(S.CoatIor)); AppendDetail(Rows[10].Detail, sizeof(Rows[10].Detail), Frag); }
    if (S.CoatDarkening != 1.0f) { std::snprintf(Frag, sizeof(Frag), "darken %.3g", static_cast<double>(S.CoatDarkening)); AppendDetail(Rows[10].Detail, sizeof(Rows[10].Detail), Frag); }
    const bool CoatActive = S.CoatWeight != 0.0f;
    if (CoatActive) std::snprintf(Rows[11].Value, sizeof(Rows[11].Value), "%.3g", static_cast<double>(S.CoatRoughness));
    std::strcpy(Rows[11].Texture, "—");   // M6: coat roughness is untexturable
    Rows[11].Source = CoatActive ? MaterialChannelSource::Constant : MaterialChannelSource::Absent;
    if (S.CoatRoughnessAnisotropy != 0.0f) { std::snprintf(Frag, sizeof(Frag), "aniso %.3g", static_cast<double>(S.CoatRoughnessAnisotropy)); AppendDetail(Rows[11].Detail, sizeof(Rows[11].Detail), Frag); }
    const TextureReference& CoatNrm = S.Texture(MaterialTextureChannel::GeometryCoatNormal);
    if (CoatActive) std::strcpy(Rows[12].Value, CoatNrm.IsBound() ? "map" : "mesh frame");
    SetTexture(12, CoatNrm);
    Rows[12].Source = !CoatActive ? MaterialChannelSource::Absent
        : (CoatNrm.IsBound() ? MaterialChannelSource::Imported : MaterialChannelSource::Constant);
    if (CoatNrm.Scalar != 1.0f) { std::snprintf(Frag, sizeof(Frag), "scale %.3g", static_cast<double>(CoatNrm.Scalar)); AppendDetail(Rows[12].Detail, sizeof(Rows[12].Detail), Frag); }

    // 14 sheen colour + 15 sheen roughness (coated in M6: sheen roughness is untexturable).
    FormatColour(Rows[13].Value, sizeof(Rows[13].Value), S.FuzzColor);
    SetTexture(13, S.Texture(MaterialTextureChannel::Fuzz));
    Rows[13].Source = S.Texture(MaterialTextureChannel::Fuzz).IsBound() ? MaterialChannelSource::Imported
        : ((S.FuzzWeight != 0.0f || !IsWhite(S.FuzzColor)) ? MaterialChannelSource::Constant : MaterialChannelSource::Absent);
    if (S.FuzzWeight != 0.0f) { std::snprintf(Frag, sizeof(Frag), "weight %.3g", static_cast<double>(S.FuzzWeight)); AppendDetail(Rows[13].Detail, sizeof(Rows[13].Detail), Frag); }
    if (S.FuzzRoughness != 0.5f) { std::snprintf(Frag, sizeof(Frag), "rough %.3g", static_cast<double>(S.FuzzRoughness)); AppendDetail(Rows[13].Detail, sizeof(Rows[13].Detail), Frag); }
    std::snprintf(Rows[14].Value, sizeof(Rows[14].Value), "%.3g", static_cast<double>(S.FuzzRoughness));
    std::strcpy(Rows[14].Texture, "—");
    Rows[14].Source = (S.FuzzWeight != 0.0f || S.FuzzRoughness != 0.5f) ? MaterialChannelSource::Constant : MaterialChannelSource::Absent;

    // 16 subsurface colour + 17 thickness (one shared carrier, shown on both rows).
    const TextureReference& Sss = S.Texture(MaterialTextureChannel::Subsurface);
    FormatColour(Rows[15].Value, sizeof(Rows[15].Value), S.SubsurfaceColor);
    SetTexture(15, Sss);
    const bool SssColoured = S.SubsurfaceColor[0] != 0.8f || S.SubsurfaceColor[1] != 0.8f || S.SubsurfaceColor[2] != 0.8f;
    Rows[15].Source = Sss.IsBound() ? MaterialChannelSource::Imported
        : ((S.SubsurfaceWeight != 0.0f || SssColoured) ? MaterialChannelSource::Constant : MaterialChannelSource::Absent);
    if (S.SubsurfaceWeight != 0.0f) { std::snprintf(Frag, sizeof(Frag), "weight %.3g", static_cast<double>(S.SubsurfaceWeight)); AppendDetail(Rows[15].Detail, sizeof(Rows[15].Detail), Frag); }
    std::snprintf(Rows[16].Value, sizeof(Rows[16].Value), "%.3g m", static_cast<double>(S.SubsurfaceRadius));
    SetTexture(16, Sss);
    Rows[16].Source = Sss.IsBound() ? MaterialChannelSource::Imported
        : ((S.SubsurfaceWeight != 0.0f || S.SubsurfaceRadius != 1.0f) ? MaterialChannelSource::Constant : MaterialChannelSource::Absent);
    if (S.SubsurfaceRadiusScale[0] != 1.0f || S.SubsurfaceRadiusScale[1] != 0.5f || S.SubsurfaceRadiusScale[2] != 0.25f)
    {
        char C[48]; FormatColour(C, sizeof(C), S.SubsurfaceRadiusScale);
        std::snprintf(Frag, sizeof(Frag), "scale %s", C);
        AppendDetail(Rows[16].Detail, sizeof(Rows[16].Detail), Frag);
    }
    if (S.SubsurfaceScatterAnisotropy != 0.0f) { std::snprintf(Frag, sizeof(Frag), "aniso %.3g", static_cast<double>(S.SubsurfaceScatterAnisotropy)); AppendDetail(Rows[16].Detail, sizeof(Rows[16].Detail), Frag); }

    // 18 transmission — depth/attenuation/scatter/dispersion fold into the detail line.
    std::snprintf(Rows[17].Value, sizeof(Rows[17].Value), "%.3g", static_cast<double>(S.TransmissionWeight));
    SetTexture(17, S.Texture(MaterialTextureChannel::Transmission));
    Rows[17].Source = S.Texture(MaterialTextureChannel::Transmission).IsBound() ? MaterialChannelSource::Imported
        : (S.TransmissionWeight != 0.0f ? MaterialChannelSource::Constant : MaterialChannelSource::Absent);
    if (S.TransmissionDepth != 0.0f) { std::snprintf(Frag, sizeof(Frag), "depth %.3g m", static_cast<double>(S.TransmissionDepth)); AppendDetail(Rows[17].Detail, sizeof(Rows[17].Detail), Frag); }
    if (!IsZero(S.TransmissionScatter)) { char C[48]; FormatColour(C, sizeof(C), S.TransmissionScatter); std::snprintf(Frag, sizeof(Frag), "atten %s", C); AppendDetail(Rows[17].Detail, sizeof(Rows[17].Detail), Frag); }
    if (S.TransmissionScatterAnisotropy != 0.0f) { std::snprintf(Frag, sizeof(Frag), "aniso %.3g", static_cast<double>(S.TransmissionScatterAnisotropy)); AppendDetail(Rows[17].Detail, sizeof(Rows[17].Detail), Frag); }
    if (S.TransmissionDispersionScale != 0.0f)
    {
        std::snprintf(Frag, sizeof(Frag), "disp x%.3g Abbe %.3g",
                      static_cast<double>(S.TransmissionDispersionScale), static_cast<double>(S.TransmissionDispersionAbbeNumber));
        AppendDetail(Rows[17].Detail, sizeof(Rows[17].Detail), Frag);
    }

    // 19 refraction IOR — the same SpecularIor carrier row 04 shows (one number, two Sultan rows).
    std::snprintf(Rows[18].Value, sizeof(Rows[18].Value), "%.3g", static_cast<double>(S.SpecularIor));
    std::strcpy(Rows[18].Texture, "—");
    Rows[18].Source = MaterialChannelSource::Constant;

    // 20 displacement — no carrier anywhere in the stack (ACK:M6-none).
    std::strcpy(Rows[19].Value, "no carrier");
    std::strcpy(Rows[19].Texture, "—");
    Rows[19].Source = MaterialChannelSource::Absent;
}

void MaterialInspector::BuildStatusAndSummary() noexcept
{
    if (Names.empty())
    {
        std::strcpy(StatusLine, "no scene");
        SummaryLine[0] = '\0';
        return;
    }
    const uint32_t Dirty = QueryDirtyCount();
    if (Dirty > 0u)
    {
        // "N unsaved changes: a, b, ..." (house format — Appearance caps its field list at 8 the same way).
        std::snprintf(StatusLine, sizeof(StatusLine), "%u unsaved change%s: ", Dirty, Dirty == 1u ? "" : "s");
        uint32_t Shown = 0u;
        for (uint32_t I = 0u; I < Drafts.size() && I < Names.size(); ++I)
        {
            if (!DraftDiffers(I)) continue;
            if (Shown >= 8u) { std::strncat(StatusLine, ", ...", sizeof(StatusLine) - std::strlen(StatusLine) - 1u); break; }
            if (Shown > 0u) std::strncat(StatusLine, ", ", sizeof(StatusLine) - std::strlen(StatusLine) - 1u);
            std::strncat(StatusLine, Names[I].c_str(), sizeof(StatusLine) - std::strlen(StatusLine) - 1u);
            ++Shown;
        }
    }
    else
    {
        std::snprintf(StatusLine, sizeof(StatusLine), "%u material%s - %s - %s",
                      static_cast<uint32_t>(Names.size()), Names.size() == 1u ? "" : "s",
                      SelectedName, MaterialSelectionName(Selection));
    }
    // No "material" prefix — the F-panel adds the row label. The name is capped so the 160-byte panel row holds it.
    if (Dirty > 0u)
        std::snprintf(SummaryLine, sizeof(SummaryLine), "%.100s  \xC2\xB7  %s  \xC2\xB7  %s  \xC2\xB7  %u/%u slabs  \xC2\xB7  %u unsaved",
                      SelectedName, MaterialSelectionName(Selection), MaterialComplexityName(Complexity),
                      ResidentSlabs, AuthoredSlabs, Dirty);
    else
        std::snprintf(SummaryLine, sizeof(SummaryLine), "%.100s  \xC2\xB7  %s  \xC2\xB7  %s  \xC2\xB7  %u/%u slabs",
                      SelectedName, MaterialSelectionName(Selection), MaterialComplexityName(Complexity),
                      ResidentSlabs, AuthoredSlabs);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          LAYOUT
//------------------------------------------------------------------------------------------------------------------------

float MaterialInspector::ConstructMaterialsLayout(PixelSpace& Surface, const PlaneExtent& Body, float ScrollY, const ControlPointer& Pointer, float Opacity) noexcept
{
    const ControlKitPalette& P = ControlKit::Palette();
    const float X = Body.MinimumX, W = Body.Width();
    const float Pad = ControlKit::SectionPadding;
    float Y = Body.MinimumY - ScrollY;
    const float Top = Y;
    ControlPointer Local = Pointer;
    if (SelectorOpen) Local.Enabled = false;   // the open menu owns the pointer
    if (Pointer.Released) DraggingEdit = -1;   // sliders release like Appearance's (house pattern)

    // Header card: selector + selection / complexity / slabs / flags.
    {
        const float HeadingH = 24.0f + 16.0f + 24.0f;   // title + description + mb-6 (mirrors SectionHeading)
        const float H = Pad * 2.0f + HeadingH + ControlKitTokens::ControlHeight + 12.0f + 4.0f * 26.0f + 3.0f * 8.0f
                      + 8.0f + 40.0f + 10.0f + 30.0f + 10.0f + 40.0f;   // M7b: cutout + preview + render pill
        const PlaneExtent Card = Spanning(X, Y, W, H);
        const PlaneExtent Content = ControlKit::SectionCard(Surface, Card, ControlKitTokens::RadiusInset, Opacity);
        float R = Content.MinimumY + ControlKit::SectionHeading(Surface, Content.MinimumX, Content.MinimumY, Content.Width(),
            "Selected material", "Selection, complexity, and slab counts of the inspected material.",
            P.Text, P.TextDim, Opacity) + 24.0f;
        const PlaneExtent Value = ControlKit::ControlRow(Surface, Content.MinimumX, R, Content.Width(), "Material", P.TextDim, Opacity);
        if (!Names.empty())
        {
            const ControlHit Hit = ControlKit::Dropdown(Surface, Value, SelectedName, SelectorOpen, Local, Opacity);
            if (Hit.Clicked) SelectorOpen = true;
            SelectorButton = Value;
        }
        else
        {
            ControlKit::TextLeading(Surface, Value, 0.0f, ControlKit::Faded(P.TextFaint, Opacity), "—", 13.5f);
            SelectorButton = PlaneExtent{};
        }
        R += ControlKitTokens::ControlHeight + 12.0f;
        const char* Labels[4] = { "Selection", "Complexity", "Slabs", "Flags" };
        const char* Values[4] = { SelectionText, ComplexityText, HeaderSlabs, HeaderFlags };
        for (uint32_t I = 0u; I < 4u; ++I)
        {
            ControlKit::TextLeading(Surface, Spanning(Content.MinimumX, R, ControlKitTokens::LabelWidth, 26.0f), 0.0f,
                                    ControlKit::Faded(P.TextDim, Opacity), Labels[I], 13.0f);
            ControlKit::TextLeading(Surface, Spanning(Content.MinimumX + ControlKitTokens::LabelWidth + ControlKitTokens::RowGap, R,
                                                      Content.Width() - ControlKitTokens::LabelWidth - ControlKitTokens::RowGap, 26.0f),
                                    0.0f, ControlKit::Faded(Names.empty() ? P.TextFaint : P.Text, Opacity),
                                    Names.empty() ? "—" : Values[I], 13.0f);
            R += 26.0f + 8.0f;
        }
        // M7b: cutout threshold (draft AlphaCutoff of the selection) + preview controls.
        const bool CanEditHeader = !Names.empty() && SelectedId < Drafts.size();
        if (CanEditHeader)
        {
            ControlKit::TextLeading(Surface, Spanning(Content.MinimumX, R, ControlKitTokens::LabelWidth, 40.0f), 0.0f,
                                    ControlKit::Faded(P.TextDim, Opacity), "Cutout", 13.0f);
            char CutText[16];
            std::snprintf(CutText, sizeof(CutText), "%.3g", static_cast<double>(Drafts[SelectedId].AlphaCutoff));
            const float PillX = Content.MinimumX + ControlKitTokens::LabelWidth + ControlKitTokens::RowGap;
            ControlKit::ValuePill(Surface, PillX, R, CutText, "", Opacity);
            const float TrackX = PillX + ControlKit::ValuePillWidth + 12.0f;
            const PlaneExtent Track = Spanning(TrackX, R, Content.MinimumX + Content.Width() - TrackX, 40.0f);
            float V = Drafts[SelectedId].AlphaCutoff;
            const ControlHit CutHit = ControlKit::Slider(Surface, Track, 0.0f, 1.0f, V, DraggingEdit == 200, Local, V, false, false, Opacity);
            if (CutHit.Pressed) DraggingEdit = 200;
            if (DraggingEdit == 200) SetDraftCutoff(SelectedId, V);
            CutoutSlider = Track;
        }
        else
        {
            CutoutSlider = PlaneExtent{};
        }
        R += 40.0f + 10.0f;
        if (CanEditHeader)
        {
            ControlKit::TextLeading(Surface, Spanning(Content.MinimumX, R, ControlKitTokens::LabelWidth, 30.0f), 0.0f,
                                    ControlKit::Faded(P.TextDim, Opacity), "Preview", 13.0f);
            const float SwitchX = Content.MinimumX + ControlKitTokens::LabelWidth + ControlKitTokens::RowGap;
            if (ControlKit::Switch(Surface, SwitchX, R + 2.0f, PreviewEnabled, Local, Opacity).Clicked)
                SetPreviewEnabled(!PreviewEnabled);
            const char* PreviewText = PreviewStatus[0] ? PreviewStatus : (PreviewEnabled ? "renders on Apply" : "off");
            ControlKit::TextLeading(Surface, Spanning(SwitchX + ControlKit::SwitchWidth + 12.0f, R,
                                                      Content.MinimumX + Content.Width() - SwitchX - ControlKit::SwitchWidth - 12.0f, 30.0f),
                                    0.0f, ControlKit::Faded(P.TextFaint, Opacity), PreviewText, 12.0f);
        }
        R += 30.0f + 10.0f;
        if (CanEditHeader)
        {
            ButtonStructure PreviewButton;
            PreviewButton.Label = "Render preview";
            PreviewButton.Tone = ButtonToneCategory::Secondary;
            const PlaneExtent Btn = Spanning(Content.MinimumX, R, 220.0f, 40.0f);
            if (ControlKit::PillButton(Surface, Btn, PreviewButton, Local, Opacity).Clicked) PreviewRequested = true;
        }
        Y += H + 16.0f;
    }

    // Channels card: the 20 Sultan rows (label | value | source | texture + detail line).
    {
        float RowsH = 0.0f;
        const bool CanEdit = !Names.empty() && SelectedId < Drafts.size();
        for (uint32_t I = 0u; I < kMaterialChannelRowCount; ++I)
        {
            // M7b: editable rows grow an editor block (6px pad + 40px scalar / 3x40px RGB) under the value line.
            const MaterialEditKind KindH = CanEdit ? MaterialRowEditKind(I) : MaterialEditKind::None;
            RowsH += 24.0f + (Rows[I].HasDetail() ? 16.0f : 0.0f)
                   + (KindH == MaterialEditKind::None ? 0.0f : (KindH == MaterialEditKind::Rgb ? 6.0f + 120.0f : 6.0f + 40.0f));
            if (I + 1u < kMaterialChannelRowCount) RowsH += 4.0f;
        }
        const float HeadingH = 24.0f + 16.0f + 24.0f;
        const float H = Pad * 2.0f + HeadingH + RowsH;
        const PlaneExtent Card = Spanning(X, Y, W, H);
        const PlaneExtent Content = ControlKit::SectionCard(Surface, Card, ControlKitTokens::RadiusInset, Opacity);
        float R = Content.MinimumY + ControlKit::SectionHeading(Surface, Content.MinimumX, Content.MinimumY, Content.Width(),
            "Channels", "All 20 Sultan channels of the resolved slab — value, source, and texture binding.",
            P.Text, P.TextDim, Opacity) + 24.0f;
        const float LabelW = 176.0f, SourceW = 84.0f, TexW = 150.0f;
        const float ValueX = Content.MinimumX + LabelW;
        const float ValueW = Content.Width() - LabelW - SourceW - TexW - 2.0f * 12.0f;
        const float SourceX = ValueX + ValueW + 12.0f, TexX = SourceX + SourceW + 12.0f;
        for (uint32_t I = 0u; I < kMaterialChannelRowCount; ++I)
        {
            const MaterialChannelRow& Row = Rows[I];
            const MaterialEditKind Kind = CanEdit ? MaterialRowEditKind(I) : MaterialEditKind::None;
            const float RowH = 24.0f + (Row.HasDetail() ? 16.0f : 0.0f)
                             + (Kind == MaterialEditKind::None ? 0.0f : (Kind == MaterialEditKind::Rgb ? 6.0f + 120.0f : 6.0f + 40.0f));
            RowExtents[I] = Spanning(Content.MinimumX, R, Content.Width(), RowH);
            const bool Dim = Row.Source == MaterialChannelSource::Absent;
            ControlKit::TextLeading(Surface, Spanning(Content.MinimumX, R, LabelW, 24.0f), 0.0f,
                                    ControlKit::Faded(P.TextDim, Opacity), Row.Name, 13.0f);
            ControlKit::TextLeading(Surface, Spanning(ValueX, R, ValueW, 24.0f), 0.0f,
                                    ControlKit::Faded(Dim ? P.TextFaint : P.Text, Opacity), Row.Value, 13.0f);
            const ColorQuad SourceInk = Row.Source == MaterialChannelSource::Imported ? P.Info
                : (Row.Source == MaterialChannelSource::Constant ? P.TextDim : P.TextFaint);
            ControlKit::TextLeading(Surface, Spanning(SourceX, R, SourceW, 24.0f), 0.0f,
                                    ControlKit::Faded(SourceInk, Opacity), MaterialChannelSourceName(Row.Source), 13.0f);
            ControlKit::TextLeading(Surface, Spanning(TexX, R, TexW, 24.0f), 0.0f,
                                    ControlKit::Faded(Dim ? P.TextFaint : P.TextDim, Opacity), Row.Texture, 13.0f);
            if (Row.HasDetail())
                ControlKit::TextLeading(Surface, Spanning(ValueX, R + 24.0f, Content.Width() - LabelW, 16.0f), 0.0f,
                                        ControlKit::Faded(P.TextFaint, Opacity), Row.Detail, 11.0f);
            // The knob binds the draft (authoring top slab) — the knob is what you edit, the row text is what renders.
            const float EditY = R + 24.0f + (Row.HasDetail() ? 16.0f : 0.0f) + (Kind == MaterialEditKind::None ? 0.0f : 6.0f);
            if (Kind == MaterialEditKind::Scalar)
            {
                float Min = 0.0f, Max = 1.0f;
                EditRange(I, Min, Max);
                char Num[16];
                std::snprintf(Num, sizeof(Num), "%.3g", static_cast<double>(QueryDraftScalar(I)));
                ControlKit::ValuePill(Surface, ValueX, EditY, Num, "", Opacity);
                const PlaneExtent Track = Spanning(ValueX + ControlKit::ValuePillWidth + 12.0f, EditY,
                                                   ValueW - ControlKit::ValuePillWidth - 12.0f, 40.0f);
                float V = QueryDraftScalar(I);
                const ControlHit Hit = ControlKit::Slider(Surface, Track, Min, Max, V, DraggingEdit == static_cast<int>(I),
                                                           Local, V, false, false, Opacity);
                if (Hit.Pressed) DraggingEdit = static_cast<int>(I);
                if (DraggingEdit == static_cast<int>(I)) SetDraftScalar(SelectedId, I, V);
                EditExtents[I][0] = Track; EditExtents[I][1] = PlaneExtent{}; EditExtents[I][2] = PlaneExtent{};
            }
            else if (Kind == MaterialEditKind::Rgb)
            {
                static const char* const kTags[3] = { "R", "G", "B" };
                for (uint32_t C = 0u; C < 3u; ++C)
                {
                    const float Cy = EditY + static_cast<float>(C) * 40.0f;
                    ControlKit::TextLeading(Surface, Spanning(ValueX, Cy, 14.0f, 40.0f), 0.0f,
                                            ControlKit::Faded(P.TextFaint, Opacity), kTags[C], 12.0f);
                    char Num[16];
                    std::snprintf(Num, sizeof(Num), "%.3g", static_cast<double>(QueryDraftColor(I, C)));
                    ControlKit::ValuePill(Surface, ValueX + 18.0f, Cy, Num, "", Opacity);
                    const PlaneExtent Track = Spanning(ValueX + 18.0f + ControlKit::ValuePillWidth + 12.0f, Cy,
                                                       ValueW - 18.0f - ControlKit::ValuePillWidth - 12.0f, 40.0f);
                    float V = QueryDraftColor(I, C);
                    const int Key = 64 + static_cast<int>(I) * 3 + static_cast<int>(C);
                    const ControlHit Hit = ControlKit::Slider(Surface, Track, 0.0f, 1.0f, V, DraggingEdit == Key,
                                                               Local, V, false, false, Opacity);
                    if (Hit.Pressed) DraggingEdit = Key;
                    if (DraggingEdit == Key) SetDraftColor(SelectedId, I, C, V);
                    EditExtents[I][C] = Track;
                }
            }
            else { EditExtents[I][0] = PlaneExtent{}; EditExtents[I][1] = PlaneExtent{}; EditExtents[I][2] = PlaneExtent{}; }
            R += RowH + 4.0f;
        }
        Y += H + 16.0f;
    }

    // Fold card: only when the selection was folded (routine at slab limit 1 — a note, not a warning).
    if (!FoldLines.empty())
    {
        const float HeadingH = 24.0f + 24.0f;
        const float H = Pad * 2.0f + HeadingH + FoldLines.size() * 20.0f + (FoldLines.size() - 1u) * 4.0f;
        const PlaneExtent Card = Spanning(X, Y, W, H);
        const PlaneExtent Content = ControlKit::SectionCard(Surface, Card, ControlKitTokens::RadiusInset, Opacity);
        float R = Content.MinimumY + ControlKit::SectionHeading(Surface, Content.MinimumX, Content.MinimumY, Content.Width(),
            "Fold report", nullptr, P.Text, P.TextDim, Opacity) + 24.0f;
        for (const std::string& Line : FoldLines)
        {
            ControlKit::TextLeading(Surface, Spanning(Content.MinimumX, R, Content.Width(), 20.0f), 0.0f,
                                    ControlKit::Faded(P.TextDim, Opacity), Line.c_str(), 12.0f);
            R += 20.0f + 4.0f;
        }
        Y += H + 16.0f;
    }

    BuildStatusAndSummary();   // a drag updates the footer/summary without a Rebuild

    return Y - 16.0f - Top;   // content height (trailing gap excluded)
}

void MaterialInspector::ConstructFloatingLayout(PixelSpace& Surface, const ControlPointer& Pointer, float Opacity) noexcept
{
    if (!SelectorOpen || NamePointers.empty()) return;
    uint32_t Chosen = std::min(SelectedId, static_cast<uint32_t>(NamePointers.size() - 1u));
    const ControlHit Hit = ControlKit::DropdownMenu(Surface, SelectorButton, NamePointers.data(),
        static_cast<uint32_t>(NamePointers.size()), SelectedId, Pointer, Chosen, Opacity);
    if (Hit.Clicked) { (void)Select(Chosen); SelectorOpen = false; return; }
    // Release outside the menu and its button closes it.
    if (Pointer.Released && !Hit.Hovered && !SelectorButton.Encloses(Pointer.X, Pointer.Y)) SelectorOpen = false;
}

} // namespace Frontier
