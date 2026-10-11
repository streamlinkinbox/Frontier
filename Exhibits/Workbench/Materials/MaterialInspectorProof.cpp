//============================================================================================================================================
//                                                  MATERIALINSPECTORPROOF.CPP
//============================================================================================================================================
// M7b gate — the material-inspector proof. Nine archetype materials (opaque, metal, folded glass, cloth, subsurface,
//    emissive-only, unlit, coated+mask, one fully-loaded slab) walk the inspector: selection resolution, the
//    reflectance/complexity cross-check against Finalise's own records, all 20 Sultan rows (value/source/texture),
//    fold attribution, registry round-trip, headless layout (null AND recording surfaces), the selector menu flow,
//    host wiring, and the F-panel summary — then the M7b editing loop: drafts, clamps, Apply/Discard, retention,
//    cutout, the shaderball preview (deterministic + commit-reactive), the preview toggle, host commit flow, and
//    a synthetic slider drag. No GPU, no window: imgui runs context-only (draw lists accumulate in CPU
//    memory, never rendered) and Vulkan appears as headers only (RayTracingCapabilitySet declarations).

#include "MaterialInspector.h"
#include "ControlCentreHost.h"
#include "ConfigurationRegistry.h"
#include "DiagnosticInspector.h"
#include "MaterialIndex.h"
#include "TextureIndex.h"
#include "OrientationClassifier.h"
#include "ReSTIRIntegrator.h"
#include "InputExchange.h"
#include "ShaderballPreview.h"
#include "imgui.h"

#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstring>
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

bool Contains(const char* Haystack, const char* Needle)
{
    return Haystack && Needle && std::strstr(Haystack, Needle) != nullptr;
}

void Bind(Frontier::MaterialSlabDescriptor& S, Frontier::MaterialTextureChannel C, uint32_t Slot, uint8_t Uv,
          Frontier::TextureChannelSelection Ch, float Scalar = 1.0f)
{
    Frontier::TextureReference& T = S.Texture(C);
    T.Texture = Slot; T.UvSet = Uv; T.Channel = Ch; T.Scalar = Scalar;
}

} // namespace

int main()
{
    std::setlocale(LC_ALL, "C");
    using namespace Frontier;

    // ── Fixtures ────────────────────────────────────────────────────────────────────────────────────────────────
    MaterialIndex Index;
    {
        MaterialDescriptor Brick; Brick.Name = "brick-01";
        MaterialSlabDescriptor S; S.SpecularRoughness = 0.9f;
        Bind(S, MaterialTextureChannel::BaseColor, 3u, 0u, TextureChannelSelection::Rgb);
        Bind(S, MaterialTextureChannel::GeometryNormal, 4u, 0u, TextureChannelSelection::Rgb);
        Brick.Slabs.push_back(S); Index.Register(Brick);

        MaterialDescriptor Steel; Steel.Name = "steel-02";
        MaterialSlabDescriptor M; M.BaseMetalness = 1.0f; M.SpecularRoughnessAnisotropy = 0.6f; M.SlateAnisotropyRotation = 0.5f;
        Steel.Slabs.push_back(M); Index.Register(Steel);

        MaterialDescriptor Glass; Glass.Name = "glass-01";   // implicit 3-slab vertical chain → folds at limit 1
        for (float W : { 0.9f, 0.5f, 0.3f }) { MaterialSlabDescriptor T; T.TransmissionWeight = W; Glass.Slabs.push_back(T); }
        Index.Register(Glass);

        MaterialDescriptor Velvet; Velvet.Name = "velvet-03";
        MaterialSlabDescriptor F; F.FuzzWeight = 0.8f; F.SpecularWeight = 0.0f;
        Velvet.Slabs.push_back(F); Index.Register(Velvet);

        MaterialDescriptor Wax; Wax.Name = "wax-04";
        MaterialSlabDescriptor W; W.SubsurfaceWeight = 0.6f; W.SubsurfaceRadius = 0.05f;
        W.SubsurfaceColor[0] = 0.9f; W.SubsurfaceColor[1] = 0.7f; W.SubsurfaceColor[2] = 0.6f;
        Wax.Slabs.push_back(W); Index.Register(Wax);

        MaterialDescriptor Lamp; Lamp.Name = "lamp-05";
        MaterialSlabDescriptor E; E.BaseWeight = 0.0f; E.SpecularWeight = 0.0f; E.EmissionLuminance = 5.0f;
        Lamp.Slabs.push_back(E); Index.Register(Lamp);

        MaterialDescriptor Ui; Ui.Name = "ui-06"; Ui.Flags = MaterialFlagUnlit;
        MaterialSlabDescriptor U; U.BaseColor[0] = 1.0f; U.BaseColor[1] = 0.0f; U.BaseColor[2] = 0.0f;
        Ui.Slabs.push_back(U); Index.Register(Ui);

        MaterialDescriptor Helmet; Helmet.Name = "helmet-08"; Helmet.Flags = MaterialFlagAlphaMask; Helmet.AlphaCutoff = 0.4f;
        MaterialSlabDescriptor C; C.CoatWeight = 0.5f; C.CoatRoughness = 0.1f; C.ThinFilmWeight = 0.3f;
        Bind(C, MaterialTextureChannel::ThinFilm, 17u, 0u, TextureChannelSelection::Rgb);
        Bind(C, MaterialTextureChannel::GeometryOpacity, 9u, 1u, TextureChannelSelection::R);
        Helmet.Slabs.push_back(C); Index.Register(Helmet);

        MaterialDescriptor Rich; Rich.Name = "rich-00";   // every carrier loaded (the 20-row deep-dive)
        MaterialSlabDescriptor R;
        R.BaseColor[0] = 0.2f; R.BaseColor[1] = 0.4f; R.BaseColor[2] = 0.6f; R.BaseMetalness = 0.9f; R.BaseDiffuseRoughness = 0.3f;
        R.SpecularColor[0] = 1.0f; R.SpecularColor[1] = 0.9f; R.SpecularColor[2] = 0.8f;
        R.SpecularRoughness = 0.35f; R.SpecularRoughnessAnisotropy = 0.4f; R.SlateAnisotropyRotation = 1.2f;
        R.SlateHazinessWeight = 0.1f; R.SlateGlintDensity = 0.5f; R.ThinFilmWeight = 0.2f;
        R.EmissionLuminance = 2.5f; R.EmissionColor[0] = 1.0f; R.EmissionColor[1] = 0.5f; R.EmissionColor[2] = 0.25f;
        R.GeometryOpacity = 0.75f;
        R.CoatWeight = 0.3f; R.CoatRoughness = 0.15f; R.CoatIor = 1.7f;
        R.FuzzWeight = 0.2f; R.FuzzColor[0] = 0.9f; R.FuzzColor[1] = 0.1f; R.FuzzColor[2] = 0.1f; R.FuzzRoughness = 0.6f;
        R.SubsurfaceWeight = 0.1f; R.SubsurfaceColor[0] = 0.8f; R.SubsurfaceColor[1] = 0.6f; R.SubsurfaceColor[2] = 0.5f;
        R.SubsurfaceRadius = 0.02f;
        R.TransmissionWeight = 0.4f; R.TransmissionDepth = 0.1f;
        R.TransmissionScatter[0] = 0.1f; R.TransmissionScatter[1] = 0.2f; R.TransmissionScatter[2] = 0.3f;
        R.TransmissionDispersionScale = 0.5f; R.TransmissionDispersionAbbeNumber = 30.0f;
        Bind(R, MaterialTextureChannel::BaseColor, 1u, 0u, TextureChannelSelection::Rgb);
        Bind(R, MaterialTextureChannel::Metalness, 1u, 0u, TextureChannelSelection::B);
        Bind(R, MaterialTextureChannel::SpecularRoughness, 1u, 0u, TextureChannelSelection::G);
        Bind(R, MaterialTextureChannel::SpecularColor, 13u, 0u, TextureChannelSelection::Rgb);
        Bind(R, MaterialTextureChannel::GeometryNormal, 2u, 1u, TextureChannelSelection::Rgb, 0.8f);
        Bind(R, MaterialTextureChannel::Occlusion, 5u, 0u, TextureChannelSelection::R, 0.7f);
        Bind(R, MaterialTextureChannel::Emission, 14u, 0u, TextureChannelSelection::Rgb);
        Bind(R, MaterialTextureChannel::GeometryOpacity, 6u, 0u, TextureChannelSelection::A);
        Bind(R, MaterialTextureChannel::Anisotropy, 7u, 0u, TextureChannelSelection::B);
        Bind(R, MaterialTextureChannel::Coat, 8u, 0u, TextureChannelSelection::R);
        Bind(R, MaterialTextureChannel::GeometryCoatNormal, 10u, 0u, TextureChannelSelection::Rgb);
        Bind(R, MaterialTextureChannel::Fuzz, 11u, 0u, TextureChannelSelection::Rgb);
        Bind(R, MaterialTextureChannel::Subsurface, 15u, 0u, TextureChannelSelection::Rgb);
        Bind(R, MaterialTextureChannel::Transmission, 16u, 0u, TextureChannelSelection::R);
        Rich.Slabs.push_back(R); Index.Register(Rich);
    }
    std::vector<std::string> Report;
    Index.Finalise(1u, &Report);

    // ── A. Registry [material] ──────────────────────────────────────────────────────────────────────────────────
    {
        SlateConfiguration C;
        Check(C.Material.Selected.empty() && C.Material.Preview, "A1 defaults: empty selection, preview on");
        C.Material.Selected = "glass-01"; C.Material.Preview = false;
        const std::string Toml = ConfigurationRegistry::Serialise(C);
        Check(Contains(Toml.c_str(), "[material]") && Contains(Toml.c_str(), "glass-01"), "A2 serialise writes [material] selected");
        SlateConfiguration Back;
        std::string Error;
        Check(ConfigurationRegistry::Deserialise(Toml, Back, &Error) && Back.Material.Selected == "glass-01" && !Back.Material.Preview,
              "A3 round-trip preserves selected + preview");
        SlateConfiguration Missing;
        Check(ConfigurationRegistry::Deserialise("[render]\nquality = \"High\"\n", Missing, &Error) && Missing.Material.Selected.empty() && Missing.Material.Preview,
              "A4 old file without [material] keeps defaults");
        SlateConfiguration Unknown;
        Check(ConfigurationRegistry::Deserialise("[material]\nselected = \"x\"\nshaderball = true\n", Unknown, &Error) && Unknown.Material.Selected == "x",
              "A5 unknown keys ignored");
        Check(Back.Material == C.Material && !(Back.Material == SlateConfiguration{}.Material), "A6 round-trip equals source, differs from defaults");
    }

    // ── B. Fold retention ───────────────────────────────────────────────────────────────────────────────────────
    {
        Check(Report.size() == 1u, "B1 one fold line at limit 1 (glass only)");
        Check(Index.QueryFoldReport() == Report && !Index.QueryFoldReport().empty(), "B2 Finalise retains the report");
        Index.Finalise(8u, nullptr);
        Check(Index.QueryFoldReport().empty() && Report.size() == 1u, "B3 re-Finalise at 8 replaces retention, caller copy kept");
        Index.Finalise(1u, &Report);
        Check(Index.QueryFoldReport().size() == 1u, "B4 re-Finalise at 1 restores the fold line");
        MaterialIndex Empty;
        Empty.Finalise(1u, nullptr);
        Check(Empty.QueryFoldReport().empty(), "B5 empty index retains nothing");
    }

    // ── C. Walkthrough: selection × records cross-check ─────────────────────────────────────────────────────────
    MaterialInspector Insp;
    const char* Names[9] = { "brick-01", "steel-02", "glass-01", "velvet-03", "wax-04", "lamp-05", "ui-06", "helmet-08", "rich-00" };
    const MaterialReflectance Sels[9] = { MaterialReflectance::Standard, MaterialReflectance::Anisotropic, MaterialReflectance::Transmissive,
        MaterialReflectance::Cloth, MaterialReflectance::Subsurface, MaterialReflectance::EmissiveOnly, MaterialReflectance::Unlit,
        MaterialReflectance::ClearCoated, MaterialReflectance::Transmissive };
    const uint32_t Comps[9] = { MaterialComplexitySimple, MaterialComplexitySingle, MaterialComplexitySpecial, MaterialComplexitySingle,
        MaterialComplexitySpecial, MaterialComplexitySimple, MaterialComplexitySimple, MaterialComplexitySpecial, MaterialComplexitySpecial };
    const uint32_t Authored[9] = { 1u, 1u, 3u, 1u, 1u, 1u, 1u, 1u, 1u };
    for (uint32_t I = 0u; I < 9u; ++I)
    {
        char Label[96];
        Insp.SeedSelection(Names[I]);
        Insp.Rebuild(&Index);
        const MaterialRecord& Rec = Index.QueryRecords()[I];
        std::snprintf(Label, sizeof(Label), "C%u id resolves (%s)", I, Names[I]);
        Check(Insp.QuerySelectedId() == I && std::strcmp(Insp.QuerySelectedName(), Names[I]) == 0, Label);
        std::snprintf(Label, sizeof(Label), "C%u selection literal + record cross-check", I);
        Check(Insp.QuerySelection() == Sels[I] && static_cast<uint32_t>(Insp.QuerySelection()) == ((Rec.Flags >> kMaterialReflectanceShift) & 0xFu), Label);
        std::snprintf(Label, sizeof(Label), "C%u complexity literal + record cross-check", I);
        Check(Insp.QueryComplexity() == Comps[I] && Insp.QueryComplexity() == Rec.Complexity, Label);
        std::snprintf(Label, sizeof(Label), "C%u slab counts %u/%u + limit", I, Rec.SlabCount, Authored[I]);
        Check(Insp.QueryAuthoredSlabs() == Authored[I] && Insp.QueryResidentSlabs() == Rec.SlabCount && Insp.QuerySlabLimit() == 1u, Label);
        std::snprintf(Label, sizeof(Label), "C%u status line", I);
        Check(Contains(Insp.QueryStatusLine(), Names[I]) && Contains(Insp.QueryStatusLine(), MaterialSelectionName(Sels[I])), Label);
        std::snprintf(Label, sizeof(Label), "C%u summary line", I);
        Check(Contains(Insp.QuerySummaryLine(), Names[I]) && Contains(Insp.QuerySummaryLine(), MaterialComplexityName(Comps[I])), Label);
        std::snprintf(Label, sizeof(Label), "C%u fold attribution (%s)", I, I == 2u ? "one line" : "none");
        Check(Insp.QueryFoldLineCount() == (I == 2u ? 1u : 0u), Label);
    }
    Insp.SeedSelection("glass-01"); Insp.Rebuild(&Index);
    Check(Contains(Insp.QueryFoldLine(0), "material 'glass-01':") && Contains(Insp.QueryFoldLine(0), "3 slab(s) folded into 1") &&
          Contains(Insp.QueryFoldLine(0), "slab_limit 1"), "C10 glass fold line text + attribution");
    Check(std::strcmp(Insp.QueryFoldLine(7), "") == 0, "C11 fold accessor out-of-range returns empty");
    Check(std::strcmp(Insp.QuerySummaryLine(), "glass-01  \xC2\xB7  Transmissive  \xC2\xB7  Special  \xC2\xB7  1/3 slabs") == 0, "C12 glass summary exact");
    Check(std::strcmp(Insp.QueryStatusLine(), "9 materials - glass-01 - Transmissive") == 0, "C13 glass status exact");
    Check(Insp.QueryMaterialCount() == 9u, "C14 material count");

    // ── D. rich-00: all 20 rows ─────────────────────────────────────────────────────────────────────────────────
    Insp.SeedSelection("rich-00"); Insp.Rebuild(&Index);
    struct RowExpect { const char* Value; MaterialChannelSource Source; const char* Texture; };
    const RowExpect Rows[20] = {
        { "(0.2, 0.4, 0.6)", MaterialChannelSource::Imported, "tex 1 \xC2\xB7 uv0 \xC2\xB7 rgb" },
        { "0.9",             MaterialChannelSource::Imported, "tex 1 \xC2\xB7 uv0 \xC2\xB7 B" },
        { "0.35",            MaterialChannelSource::Imported, "tex 1 \xC2\xB7 uv0 \xC2\xB7 G" },
        { "ior 1.5",         MaterialChannelSource::Imported, "tex 13 \xC2\xB7 uv0 \xC2\xB7 rgb" },
        { "map",             MaterialChannelSource::Imported, "tex 2 \xC2\xB7 uv1 \xC2\xB7 rgb" },
        { "strength 0.7",    MaterialChannelSource::Imported, "tex 5 \xC2\xB7 uv0 \xC2\xB7 R" },
        { "2.5 nit",         MaterialChannelSource::Imported, "tex 14 \xC2\xB7 uv0 \xC2\xB7 rgb" },
        { "0.75",            MaterialChannelSource::Imported, "tex 6 \xC2\xB7 uv0 \xC2\xB7 A" },
        { "0.4",             MaterialChannelSource::Imported, "tex 7 \xC2\xB7 uv0 \xC2\xB7 B" },
        { "1.2 rad",         MaterialChannelSource::Imported, "tex 7 \xC2\xB7 uv0 \xC2\xB7 B" },
        { "0.3",             MaterialChannelSource::Imported, "tex 8 \xC2\xB7 uv0 \xC2\xB7 R" },
        { "0.15",            MaterialChannelSource::Constant, "\xE2\x80\x94" },
        { "map",             MaterialChannelSource::Imported, "tex 10 \xC2\xB7 uv0 \xC2\xB7 rgb" },
        { "(0.9, 0.1, 0.1)", MaterialChannelSource::Imported, "tex 11 \xC2\xB7 uv0 \xC2\xB7 rgb" },
        { "0.6",             MaterialChannelSource::Constant, "\xE2\x80\x94" },
        { "(0.8, 0.6, 0.5)", MaterialChannelSource::Imported, "tex 15 \xC2\xB7 uv0 \xC2\xB7 rgb" },
        { "0.02 m",          MaterialChannelSource::Imported, "tex 15 \xC2\xB7 uv0 \xC2\xB7 rgb" },
        { "0.4",             MaterialChannelSource::Imported, "tex 16 \xC2\xB7 uv0 \xC2\xB7 R" },
        { "1.5",             MaterialChannelSource::Constant, "\xE2\x80\x94" },
        { "no carrier",      MaterialChannelSource::Absent,   "\xE2\x80\x94" },
    };
    for (uint32_t I = 0u; I < 20u; ++I)
    {
        char Label[96];
        const MaterialChannelRow& Row = Insp.QueryRow(I);
        std::snprintf(Label, sizeof(Label), "D%u row %u value/source/texture", I, I + 1u);
        Check(std::strcmp(Row.Value, Rows[I].Value) == 0 && Row.Source == Rows[I].Source && std::strcmp(Row.Texture, Rows[I].Texture) == 0, Label);
    }
    Check(std::strcmp(Insp.QueryRow(8).Texture, Insp.QueryRow(9).Texture) == 0, "D20 anisotropy carrier shared by 09+10");
    Check(std::strcmp(Insp.QueryRow(15).Texture, Insp.QueryRow(16).Texture) == 0, "D21 subsurface carrier shared by 16+17");
    Check(std::strcmp(Insp.QueryRow(3).Value, "ior 1.5") == 0 && std::strcmp(Insp.QueryRow(18).Value, "1.5") == 0, "D22 IOR shared by 04+19");
    Check(Contains(Insp.QueryRow(0).Detail, "eon 0.3"), "D23 detail: EON roughness");
    Check(Contains(Insp.QueryRow(2).Detail, "glint d 0.5") && Contains(Insp.QueryRow(2).Detail, "uv 1"), "D24 detail: glint");
    Check(Contains(Insp.QueryRow(3).Detail, "tint (1, 0.9, 0.8)") && Contains(Insp.QueryRow(3).Detail, "haze w 0.1 r 0.6") &&
          Contains(Insp.QueryRow(3).Detail, "film w 0.2 t 0.5 \xC2\xB5m n 1.4"), "D25 detail: tint + haze + film");
    Check(Contains(Insp.QueryRow(4).Detail, "scale 0.8"), "D26 detail: normal scale");
    Check(Contains(Insp.QueryRow(6).Detail, "tint (1, 0.5, 0.25)"), "D27 detail: emission tint");
    Check(Contains(Insp.QueryRow(10).Detail, "ior 1.7"), "D28 detail: coat IOR");
    Check(Contains(Insp.QueryRow(13).Detail, "weight 0.2") && Contains(Insp.QueryRow(13).Detail, "rough 0.6"), "D29 detail: fuzz");
    Check(Contains(Insp.QueryRow(15).Detail, "weight 0.1"), "D30 detail: subsurface weight");
    Check(Contains(Insp.QueryRow(17).Detail, "depth 0.1 m") && Contains(Insp.QueryRow(17).Detail, "atten (0.1, 0.2, 0.3)") &&
          Contains(Insp.QueryRow(17).Detail, "disp x0.5 Abbe 30"), "D31 detail: transmission volume");
    Check(!Insp.QueryRow(1).HasDetail() && !Insp.QueryRow(19).HasDetail(), "D32 no detail where no sub-params");

    // Spot rows on the other archetypes (sources + retention, one assert each).
    Insp.SeedSelection("brick-01"); Insp.Rebuild(&Index);
    Check(std::strcmp(Insp.QueryRow(0).Value, "(0.8, 0.8, 0.8)") == 0 && Insp.QueryRow(0).Source == MaterialChannelSource::Imported, "D33 brick base imported");
    Check(std::strcmp(Insp.QueryRow(2).Value, "0.9") == 0 && Insp.QueryRow(2).Source == MaterialChannelSource::Constant, "D34 brick roughness constant");
    Check(Insp.QueryRow(7).Source == MaterialChannelSource::Absent, "D35 brick opacity-1-unbound absent");
    Insp.SeedSelection("steel-02"); Insp.Rebuild(&Index);
    Check(std::strcmp(Insp.QueryRow(9).Value, "0.5 rad") == 0 && Insp.QueryRow(9).Source == MaterialChannelSource::Constant, "D36 steel direction constant (active, unbound)");
    Insp.SeedSelection("velvet-03"); Insp.Rebuild(&Index);
    Check(Insp.QueryRow(13).Source == MaterialChannelSource::Constant && Contains(Insp.QueryRow(13).Detail, "weight 0.8"), "D37 velvet sheen constant via weight");
    Check(Insp.QueryRow(14).Source == MaterialChannelSource::Constant, "D38 velvet sheen roughness constant via weight");
    Insp.SeedSelection("lamp-05"); Insp.Rebuild(&Index);
    Check(std::strcmp(Insp.QueryRow(6).Value, "5 nit") == 0, "D39 lamp emission value");
    Check(std::strcmp(Insp.QueryRow(0).Value, "(0.8, 0.8, 0.8)") == 0 && Insp.QueryRow(0).Source == MaterialChannelSource::Constant,
          "D40 lamp base retained though unread (weight 0)");
    Insp.SeedSelection("helmet-08"); Insp.Rebuild(&Index);
    Check(Insp.QueryRow(3).Source == MaterialChannelSource::Imported && Contains(Insp.QueryRow(3).Texture, "tex 17") &&
          Contains(Insp.QueryRow(3).Detail, "film w 0.3"), "D41 helmet film texture feeds 04 (spec unbound)");
    Check(Insp.QueryRow(7).Source == MaterialChannelSource::Imported && std::strcmp(Insp.QueryRow(7).Value, "1") == 0,
          "D42 helmet opacity bound-but-1 is imported, not absent");
    Check(Insp.QueryRow(11).Source == MaterialChannelSource::Constant && Insp.QueryRow(12).Source == MaterialChannelSource::Constant &&
          std::strcmp(Insp.QueryRow(12).Value, "mesh frame") == 0,
          "D43 helmet coat rough constant, coat orientation mesh-frame (active, unbound)");
    Insp.SeedSelection("ui-06"); Insp.Rebuild(&Index);
    Check(std::strcmp(Insp.QueryRow(0).Value, "(1, 0, 0)") == 0, "D44 ui base red");

    // ── E. Selection edges ──────────────────────────────────────────────────────────────────────────────────────────
    {
        const uint32_t Before = Insp.QueryRevision();
        Insp.SeedSelection("no-such-material"); Insp.Rebuild(&Index);
        Check(Insp.QuerySelectedId() == 0u && Insp.QueryRevision() == Before + 1u, "E1 unknown name falls back to 0 + bumps");
        const uint32_t AtBrick = Insp.QueryRevision();
        Insp.Rebuild(&Index);
        Check(Insp.QueryRevision() == AtBrick, "E2 steady Rebuild never bumps");
        Check(!Insp.Select(0u) && Insp.QueryRevision() == AtBrick, "E3 Select(same) returns false, no bump");
        Check(Insp.Select(4u) && Insp.QuerySelectedId() == 4u && Insp.QueryRevision() == AtBrick + 1u, "E4 Select(other) bumps");
        Check(Insp.Select(99u) && Insp.QuerySelectedId() == 8u, "E5 Select clamps to last");
        const uint32_t AtRich = Insp.QueryRevision();
        Insp.SeedSelection("steel-02"); Insp.Rebuild(&Index);
        Check(Insp.QuerySelectedId() == 1u && Insp.QueryRevision() == AtRich, "E6 valid seed resolves silently (already persisted)");
        MaterialInspector Cleared;
        Cleared.SeedSelection("brick-01"); Cleared.Rebuild(&Index);
        Cleared.Rebuild(nullptr);
        Check(Cleared.QueryMaterialCount() == 0u && std::strcmp(Cleared.QueryStatusLine(), "no scene") == 0 &&
              Cleared.QuerySummaryLine()[0] == '\0' && Cleared.QueryRow(0).Source == MaterialChannelSource::Absent,
              "E7 null Rebuild clears to no-scene state");
        MaterialIndex Empty;
        Empty.Finalise(1u, nullptr);
        Cleared.Rebuild(&Empty);
        Check(Cleared.QueryMaterialCount() == 0u, "E8 empty index clears");
        MaterialInspector Fresh;
        Fresh.Rebuild(&Index);
        Check(Fresh.QuerySelectedId() == 0u && std::strcmp(Fresh.QuerySelectedName(), "brick-01") == 0, "E9 empty seed resolves to first");
    }

    // ── F/G/H/I need imgui (recording surface) ──────────────────────────────────────────────────────────────────────
    ImGui::CreateContext();
    {
        // A bare context never ran a frame: the draw lists' pixel density is 0 until NewFrame (without this,
        //    imgui asserts inside _SetPixelDensity on the first recording call).
        ImGuiIO& IO = ImGui::GetIO();
        unsigned char* AtlasPixels = nullptr; int AtlasW = 0, AtlasH = 0;
        IO.Fonts->GetTexDataAsRGBA32(&AtlasPixels, &AtlasW, &AtlasH);   // CPU-side atlas build (never uploaded)
        IO.DisplaySize = ImVec2(1280.0f, 800.0f);
        IO.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
    }
    {
        // F. Layout headless: recording vs null surfaces agree exactly.
        Insp.SeedSelection("rich-00"); Insp.Rebuild(&Index);
        PixelSpace Recording;
        Check(Recording.Begin(SurfaceLayer::Above, 1280.0f, 800.0f, 1.0f), "F1 recording Begin succeeds with a context");
        const PlaneExtent Body = Spanning(0.0f, 0.0f, 800.0f, 600.0f);
        ControlPointer Idle{};
        const float H = Insp.ConstructMaterialsLayout(Recording, Body, 0.0f, Idle, 1.0f);
        Check(H > 600.0f, "F2 content height sane (header + 20 rows)");
        Check(Insp.QuerySelectorExtent().Width() > 100.0f, "F3 selector extent recorded");
        bool Ordered = true;
        for (uint32_t I = 1u; I < 20u; ++I)
            Ordered = Ordered && Insp.QueryRowExtent(I).MinimumY > Insp.QueryRowExtent(I - 1u).MaximumY - 4.0f;
        Check(Ordered, "F4 row extents Y-ordered");
        bool Heights = true;
        for (uint32_t I = 0u; I < 20u; ++I)
        {
            // M7b: editable rows grow an editor block (6px pad + 40px scalar / 3x40px RGB).
            const MaterialEditKind K = MaterialRowEditKind(I);
            const float Want = 24.0f + (Insp.QueryRow(I).HasDetail() ? 16.0f : 0.0f)
                             + (K == MaterialEditKind::None ? 0.0f : (K == MaterialEditKind::Rgb ? 126.0f : 46.0f));
            Heights = Heights && std::fabs(Insp.QueryRowExtent(I).Height() - Want) < 0.01f;
        }
        Check(Heights, "F5 row heights match detail + editor presence");
        PixelSpace Null;   // never begun: every primitive is a no-op
        MaterialInspector Twin;
        Twin.SeedSelection("rich-00"); Twin.Rebuild(&Index);
        const float HNull = Twin.ConstructMaterialsLayout(Null, Body, 0.0f, Idle, 1.0f);
        Check(HNull == H && Twin.QueryRowExtent(7).MinimumY == Insp.QueryRowExtent(7).MinimumY, "F6 null surface lays out identically");
        MaterialInspector Bare;
        const float HBare = Bare.ConstructMaterialsLayout(Null, Body, 0.0f, Idle, 1.0f);
        Check(HBare > 0.0f, "F7 empty state lays out without crashing");

        // G. Selector menu flow with a synthetic pointer.
        Insp.SeedSelection("brick-01"); Insp.Rebuild(&Index);
        Insp.ConstructMaterialsLayout(Recording, Body, 0.0f, Idle, 1.0f);
        const PlaneExtent Button = Insp.QuerySelectorExtent();
        ControlPointer Tap{};
        Tap.X = (Button.MinimumX + Button.MaximumX) * 0.5f; Tap.Y = (Button.MinimumY + Button.MaximumY) * 0.5f;
        Tap.Released = true;
        Insp.ConstructMaterialsLayout(Recording, Body, 0.0f, Tap, 1.0f);
        Check(Insp.HasOpenMenu(), "G1 release on the selector opens the menu");
        const PlaneExtent Menu = ControlKit::DropdownMenuExtent(Button, 9u);
        const float OptionY = Menu.MinimumY + 6.0f + 3u * (ControlKit::DropdownOptionHeight + 2.0f) + ControlKit::DropdownOptionHeight * 0.5f;
        ControlPointer Pick{};
        Pick.X = (Menu.MinimumX + Menu.MaximumX) * 0.5f; Pick.Y = OptionY; Pick.Released = true;
        const uint32_t RevBefore = Insp.QueryRevision();
        Insp.ConstructFloatingLayout(Recording, Pick, 1.0f);
        Check(!Insp.HasOpenMenu() && Insp.QuerySelectedId() == 3u && std::strcmp(Insp.QuerySelectedName(), "velvet-03") == 0 &&
              Insp.QueryRevision() == RevBefore + 1u, "G2 option pick selects + bumps + closes");
        Insp.ConstructMaterialsLayout(Recording, Body, 0.0f, Tap, 1.0f);
        Check(Insp.HasOpenMenu(), "G3 menu reopens");
        ControlPointer Away{};
        Away.X = 5.0f; Away.Y = 595.0f; Away.Released = true;
        Insp.ConstructFloatingLayout(Recording, Away, 1.0f);
        Check(!Insp.HasOpenMenu() && Insp.QuerySelectedId() == 3u, "G4 release outside closes without picking");
        Bare.ConstructFloatingLayout(Recording, Pick, 1.0f);
        Check(!Bare.HasOpenMenu(), "G5 floating with no materials is a no-op");

        // H. Host wiring.
        ControlCentreHost Host;
        Check(Host.Initialize(1280u, 800u), "H1 host initializes headless");
        Host.OpenNotch();   // the sheet starts closed; pages record once it is open
        Host.NavigateToPage(ControlCentrePageCategory::SettingsHub);
        for (int I = 0; I < 240; ++I) Host.AdvanceLocomotion(1.0f / 60.0f);
        Check(std::fabs(Host.QueryCardExtent().Height() - 557.0f) < 2.0f && std::fabs(Host.QueryCardExtent().Width() - 420.0f) < 2.0f,
              "H2 hub card settles at 420x557 (five rows)");
        const PlaneExtent R3 = Host.QueryHubRowExtent(3u), R4 = Host.QueryHubRowExtent(4u);
        Check(R4.Height() == 76.0f && R4.MinimumY == R3.MinimumY + 77.0f, "H3 fifth hub row geometry follows row four");
        Host.NavigateToPage(ControlCentrePageCategory::Dashboard);
        for (int I = 0; I < 240; ++I) Host.AdvanceLocomotion(1.0f / 60.0f);
        Check(std::fabs(Host.QueryCardExtent().Height() - 480.0f) < 2.0f, "H4 dashboard card keeps 420x480");
        Host.NavigateToPage(ControlCentrePageCategory::Materials);
        Check(Host.QueryActivePage() == ControlCentrePageCategory::Materials, "H5 navigate to Materials");
        for (int I = 0; I < 60; ++I) Host.AdvanceLocomotion(1.0f / 60.0f);   // the swap draws PreviousPage until P >= 0.5
        Host.AccessMaterials().SeedSelection("wax-04");
        Host.AccessMaterials().Rebuild(&Index);
        Host.ConstructControlLayout(Recording);
        Check(Host.QueryMaterials().QuerySelectorExtent().Width() > 100.0f, "H6 page records the selector through the host");
        Check(!Host.IsPageDirty(), "H7 Materials clean when untouched (M7b editable)");
        Host.NavigateBack();
        Check(Host.QueryActivePage() == ControlCentrePageCategory::Dashboard, "H8 back navigates away");
        Host.NavigateToPage(ControlCentrePageCategory::Materials);
        for (int I = 0; I < 60; ++I) Host.AdvanceLocomotion(1.0f / 60.0f);
        Host.ConstructControlLayout(Null);
        Check(true, "H9 full host layout on a null surface does not crash");

        // I. F-panel summary.
        DiagnosticInspector Diagnostics;
        InputExchange Keys;
        Keys.AssignKeyState(VirtualKeyCategory::KeyF3, true);
        Check(!Diagnostics.AdvanceInteraction(Keys), "I1 first F3 opens without a settings change");
        Keys.AssignKeyState(VirtualKeyCategory::KeyF3, false);
        Diagnostics.AdvanceInteraction(Keys);
        Keys.AssignKeyState(VirtualKeyCategory::KeyF3, true);
        Check(Diagnostics.AdvanceInteraction(Keys), "I2 second F3 cycles the view (proves the first opened it)");
        Insp.SeedSelection("glass-01"); Insp.Rebuild(&Index);
        const VisibilityTelemetry Tele{};
        const ReSTIRIntegratorConfiguration ReSTIR{};
        const TextureIndexMetrics TexStats{};
        Diagnostics.ConstructInspectorLayout(Recording, 0.0f, 1280.0f, Tele, 0u, false, ReSTIR,
                                             Index.QueryMetrics(), TexStats, 1u, Insp.QuerySummaryLine());
        Check(true, "I3 inspector layout with a material summary does not crash");
        Diagnostics.ConstructInspectorLayout(Recording, 0.0f, 1280.0f, Tele, 0u, false, ReSTIR,
                                             Index.QueryMetrics(), TexStats, 1u, nullptr);
        Check(true, "I4 inspector layout with no summary (omitted row) does not crash");
        // Q. Synthetic slider drag (own fixtures — isolated from the shared index).
        MaterialIndex QIndex;
        {
            MaterialDescriptor QBrick; QBrick.Name = "qbrick";
            MaterialSlabDescriptor QS; QS.SpecularRoughness = 0.9f;
            QBrick.Slabs.push_back(QS); QIndex.Register(QBrick);
            MaterialDescriptor QWax; QWax.Name = "qwax";
            MaterialSlabDescriptor QW; QW.SubsurfaceWeight = 0.6f;
            QWax.Slabs.push_back(QW); QIndex.Register(QWax);
        }
        QIndex.Finalise(1u, nullptr);
        MaterialInspector Q;
        Q.SeedSelection("qbrick"); Q.Rebuild(&QIndex);
        PixelSpace QNull;   // never begun: hit-testing still runs (F6 precedent)
        ControlPointer QIdle{};
        Q.ConstructMaterialsLayout(QNull, Body, 0.0f, QIdle, 1.0f);
        const PlaneExtent QTrack = Q.QueryEditExtent(2u);
        Check(QTrack.Width() > 100.0f && QTrack.Height() == 40.0f, "Q1 roughness track extent recorded");
        Check(Q.QueryEditExtent(0u, 0u).MinimumY < Q.QueryEditExtent(0u, 1u).MinimumY &&
              Q.QueryEditExtent(0u, 1u).MinimumY < Q.QueryEditExtent(0u, 2u).MinimumY, "Q2 RGB tracks stack R/G/B");
        Check(Q.QueryEditExtent(19u).Width() == 0.0f && Q.QueryEditExtent(4u).Width() == 0.0f, "Q3 no editor on rows 05/20");
        Check(Q.QueryCutoutExtent().Width() > 100.0f, "Q4 cutout track recorded");
        // Press at 25% across the track: T=(X-MinX-Thumb/2)/Usable (ControlKit::Slider, verbatim).
        ControlPointer QPress{};
        QPress.X = QTrack.MinimumX + ControlKit::SliderThumb * 0.5f + (QTrack.Width() - ControlKit::SliderThumb) * 0.25f;
        QPress.Y = (QTrack.MinimumY + QTrack.MaximumY) * 0.5f;
        QPress.Pressed = true; QPress.Down = true;
        Q.ConstructMaterialsLayout(QNull, Body, 0.0f, QPress, 1.0f);
        Check(std::fabs(Q.QueryDraftSlab(0u).SpecularRoughness - 0.25f) < 1e-4f, "Q5 drag writes the draft (exact track map)");
        ControlPointer QRel{};
        QRel.X = QPress.X; QRel.Y = QPress.Y; QRel.Released = true;
        Q.ConstructMaterialsLayout(QNull, Body, 0.0f, QRel, 1.0f);
        ControlPointer QMove{};
        QMove.X = QTrack.MinimumX + QTrack.Width() * 0.9f; QMove.Y = QPress.Y; QMove.Down = true;
        Q.ConstructMaterialsLayout(QNull, Body, 0.0f, QMove, 1.0f);
        Check(std::fabs(Q.QueryDraftSlab(0u).SpecularRoughness - 0.25f) < 1e-4f, "Q6 release ends the drag (stale motion ignored)");
    }
    ImGui::DestroyContext();

    // ── J. Draft/edit API ─────────────────────────────────────────────────────────────────────────────────────────
    {
        Insp.SeedSelection("steel-02"); Insp.Rebuild(&Index);   // id 1, roughness 0.3, aniso 0.6
        Check(!Insp.IsDirty() && Insp.QueryDirtyCount() == 0u && Insp.QueryCommitRevision() == 0u, "J1 clean after Rebuild");
        Insp.SetDraftScalar(1u, 2u, 0.05f);
        Check(Insp.QueryDraftSlab(1u).SpecularRoughness == 0.05f, "J2 scalar writes the draft slab");
        Check(std::fabs(Insp.QueryDraftScalar(2u) - 0.05f) < 1e-6f, "J3 knob reader mirrors the write (selected)");
        Check(Insp.IsDirty() && Insp.QueryDirtyCount() == 1u, "J4 dirty + count follow the edit");
        Insp.Rebuild(&Index);   // steady Rebuild: drafts survive, status refreshes
        Check(Contains(Insp.QueryStatusLine(), "1 unsaved change: steel-02"), "J5 status names the dirty material");
        Check(Contains(Insp.QuerySummaryLine(), "1 unsaved"), "J6 summary carries the dirty suffix");
        Check(Insp.QueryDraftSlab(1u).SpecularRoughness == 0.05f, "J7 Rebuild preserves the draft");
        // Clamp pin per range (proof-side literals — the UX contract).
        Insp.SetDraftScalar(0u, 2u, 99.0f);
        Check(Insp.QueryDraftSlab(0u).SpecularRoughness == 1.0f, "J8 roughness clamps to 1");
        Insp.SetDraftScalar(0u, 3u, 99.0f);
        Check(Insp.QueryDraftSlab(0u).SpecularIor == 2.5f, "J9 IOR clamps to 2.5");
        Insp.SetDraftScalar(0u, 3u, 0.5f);
        Check(Insp.QueryDraftSlab(0u).SpecularIor == 1.0f, "J10 IOR clamps to 1.0");
        Insp.SetDraftScalar(1u, 8u, -99.0f);
        Check(Insp.QueryDraftSlab(1u).SpecularRoughnessAnisotropy == -1.0f, "J11 signed aniso clamps to -1");
        Insp.SetDraftScalar(1u, 9u, 99.0f);
        Check(std::fabs(Insp.QueryDraftSlab(1u).SlateAnisotropyRotation - 6.28318530717959f) < 1e-6f, "J12 rotation clamps to 2pi");
        Insp.SetDraftScalar(4u, 16u, 99.0f);
        Check(Insp.QueryDraftSlab(4u).SubsurfaceRadius == 2.0f, "J13 radius clamps to 2m");
        Insp.SetDraftScalar(4u, 6u, 99.0f);
        Check(Insp.QueryDraftSlab(4u).EmissionLuminance == 20.0f, "J14 emission clamps to 20 nit");
        Insp.SetDraftColor(0u, 0u, 1u, 0.5f);
        Check(Insp.QueryDraftSlab(0u).BaseColor[1] == 0.5f, "J15 colour writes the channel");
        Insp.SetDraftColor(0u, 0u, 1u, 99.0f);
        Check(Insp.QueryDraftSlab(0u).BaseColor[1] == 1.0f, "J16 colour clamps to 1");
        Insp.SetDraftColor(0u, 1u, 1u, 0.5f);   // row 1 is Scalar, not Rgb
        Check(Insp.QueryDraftSlab(0u).BaseMetalness == 0.0f, "J17 colour on a scalar row no-ops");
        const MaterialSlabDescriptor BeforeJ = Insp.QueryDraftSlab(0u);
        const uint32_t DirtyJ = Insp.QueryDirtyCount();
        Insp.SetDraftScalar(0u, 19u, 0.5f);
        Insp.SetDraftScalar(99u, 2u, 0.5f);
        Insp.SetDraftScalar(0u, 99u, 0.5f);
        Insp.SetDraftColor(0u, 0u, 9u, 0.5f);
        Insp.SetDraftColor(99u, 0u, 0u, 0.5f);
        Insp.SetDraftCutoff(99u, 0.9f);
        Check(Insp.QueryDraftSlab(0u) == BeforeJ && Insp.QueryDirtyCount() == DirtyJ, "J18 invalid ids/rows/components no-op");
        Insp.SetDraftCutoff(7u, 0.9f);
        Check(Insp.QueryDraftCutoff(7u) == 0.9f, "J19 cutoff writes the draft");
        Insp.SetDraftCutoff(7u, 99.0f);
        Check(Insp.QueryDraftCutoff(7u) == 1.0f, "J20 cutoff clamps to 1");
        // Setter/getter closure: every scalar row round-trips Set->Query on the selection (steel, id 1).
        Insp.SeedSelection("steel-02"); Insp.Rebuild(&Index);
        bool RoundTrip = true;
        const uint32_t ScalarRows[14] = { 1u, 2u, 3u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 14u, 16u, 17u, 18u };
        for (uint32_t Rr : ScalarRows)
        {
            const float V = (Rr == 3u || Rr == 18u) ? 1.5f : 0.123f;
            Insp.SetDraftScalar(1u, Rr, V);
            RoundTrip = RoundTrip && std::fabs(Insp.QueryDraftScalar(Rr) - V) < 1e-6f;
        }
        Check(RoundTrip, "J21 all 14 scalar rows Set->Query round-trip");
        bool RgbTrip = true;
        const uint32_t RgbRows[3] = { 0u, 13u, 15u };
        for (uint32_t Rr : RgbRows)
            for (uint32_t Cc = 0u; Cc < 3u; ++Cc)
            {
                const float V = 0.1f + 0.01f * static_cast<float>(Cc);
                Insp.SetDraftColor(1u, Rr, Cc, V);
                RgbTrip = RgbTrip && std::fabs(Insp.QueryDraftColor(Rr, Cc) - V) < 1e-6f;
            }
        Check(RgbTrip, "J22 all 3 RGB rows x channels Set->Query round-trip");
        Check(MaterialRowEditKind(0u) == MaterialEditKind::Rgb && MaterialRowEditKind(2u) == MaterialEditKind::Scalar &&
              MaterialRowEditKind(4u) == MaterialEditKind::None && MaterialRowEditKind(12u) == MaterialEditKind::None &&
              MaterialRowEditKind(19u) == MaterialEditKind::None && MaterialRowEditKind(99u) == MaterialEditKind::None,
              "J23 edit-kind table (Rgb/Scalar/None/OOB)");
    }

    // ── K. Apply/commit ─────────────────────────────────────────────────────────────────────────────────────────
    {
        const float SteelRough = Insp.QueryDraftSlab(1u).SpecularRoughness;
        const float HelmetCut = Insp.QueryDraftCutoff(7u);
        const uint32_t DirtyBefore = Insp.QueryDirtyCount();
        Check(DirtyBefore >= 3u, "K1 several drafts dirty (steel, brick, wax, helmet)");
        Insp.Apply();
        Check(Insp.QueryCommitRevision() == 1u && Insp.QueryLastCommitCount() == DirtyBefore, "K2 commit bumps + stamps the count");
        Check(!Insp.IsDirty() && Insp.QueryDirtyCount() == 0u, "K3 clean after Apply");
        Check(Index.QueryDescriptors()[1].Slabs[0].SpecularRoughness == SteelRough, "K4 descriptor carries the commit");
        Check(Index.QueryRecords()[1].Roughness == SteelRough, "K5 Finalise re-derived the record");
        Check(Index.QueryRecords()[7].AlphaCutoff == HelmetCut, "K6 cutoff committed through to the record");
        Insp.Apply();
        Check(Insp.QueryCommitRevision() == 1u && Insp.QueryLastCommitCount() == 0u, "K7 no-op Apply bumps nothing");
        Check(!Contains(Insp.QueryStatusLine(), "unsaved"), "K8 status clears inside Apply");
    }

    // ── L. Retention + Discard ──────────────────────────────────────────────────────────────────────────────────
    {
        (void)Insp.Select(0u); Insp.Rebuild(&Index);   // brick
        Insp.SetDraftColor(0u, 0u, 0u, 0.1f);
        (void)Insp.Select(4u); Insp.Rebuild(&Index);   // wax
        Insp.SetDraftScalar(4u, 16u, 0.9f);
        (void)Insp.Select(0u); Insp.Rebuild(&Index);
        Check(Insp.QueryDraftSlab(0u).BaseColor[0] == 0.1f, "L1 brick draft survives the round-trip switch");
        (void)Insp.Select(4u); Insp.Rebuild(&Index);
        Check(Insp.QueryDraftSlab(4u).SubsurfaceRadius == 0.9f, "L2 wax draft survives the round-trip switch");
        Check(Insp.QueryDirtyCount() == 2u, "L3 both drafts dirty");
        Insp.Apply();
        Check(Index.QueryDescriptors()[0].Slabs[0].BaseColor[0] == 0.1f, "L4 brick committed");
        Check(Index.QueryRecords()[0].AlbedoR == 0.1f, "L5 record albedo follows the commit");
        Check(Index.QueryDescriptors()[4].Slabs[0].SubsurfaceRadius == 0.9f, "L6 wax committed");
        Insp.SetDraftScalar(5u, 6u, 9.0f);   // lamp emission (applied 5.0)
        Check(Insp.IsDirty(), "L7 lamp edit dirties");
        Insp.Discard();
        Check(!Insp.IsDirty() && Insp.QueryDraftSlab(5u).EmissionLuminance == 5.0f, "L8 Discard re-snapshots, lamp back to 5");
        Check(!Contains(Insp.QueryStatusLine(), "unsaved"), "L9 status clears inside Discard");
    }

    // ── M. Cutoff on a non-mask material ────────────────────────────────────────────────────────────────────────
    {
        Insp.SetDraftCutoff(0u, 0.2f);   // brick is opaque (no mask flag)
        Insp.Apply();
        Check(Index.QueryDescriptors()[0].AlphaCutoff == 0.2f, "M1 cutoff commits without the mask flag");
        Check(Index.QueryRecords()[0].AlphaCutoff == 0.2f, "M2 record carries it");
    }

    // ── N. Shaderball preview ─────────────────────────────────────────────────────────────────────────────────
    {
        auto ReadFile = [](const char* Path, std::vector<unsigned char>& Out) -> bool {
            Out.clear();
            FILE* F = std::fopen(Path, "rb");
            if (!F) return false;
            unsigned char Buf[4096];
            size_t N = 0;
            while ((N = std::fread(Buf, 1, sizeof(Buf), F)) > 0) Out.insert(Out.end(), Buf, Buf + N);
            std::fclose(F);
            return !Out.empty();
        };
        const MaterialDescriptor& ND = Index.QueryDescriptors()[0];
        uint32_t NFolded = 0u;
        const std::vector<MaterialSlabDescriptor> NFlat = MaterialIndex::Flatten(ND, 1u, &NFolded, nullptr);
        static const MaterialSlabDescriptor kNDef{};
        const MaterialSlabDescriptor& NS = NFlat.empty() ? kNDef : NFlat.front();
        ShaderballPreviewRequest Req;
        Req.Material = &ND; Req.Selection = MaterialIndex::DeriveReflectance(ND, NS);
        Req.Size = 64; Req.Spp = 2; Req.OutPath = "/tmp/MaterialPreview_N1.png";
        ShaderballPreviewResult Res;
        Check(RenderShaderballPreview(Req, Res), "N1 preview renders");
        Check(Res.Bad == 0 && Res.Mean > 0.01 && Res.Tris > 15000, "N2 sane stats (no bad, lit, full mesh)");
        Req.OutPath = "/tmp/MaterialPreview_N2.png";
        ShaderballPreviewResult Res2;
        Check(RenderShaderballPreview(Req, Res2), "N3 re-render renders");
        std::vector<unsigned char> A, B;
        Check(ReadFile("/tmp/MaterialPreview_N1.png", A) && ReadFile("/tmp/MaterialPreview_N2.png", B) &&
              A.size() == B.size() && std::memcmp(A.data(), B.data(), A.size()) == 0, "N4 deterministic: byte-identical");
        // Close the loop: edit -> Apply -> pixels differ.
        Insp.SeedSelection("brick-01"); Insp.Rebuild(&Index);
        Insp.SetDraftScalar(0u, 2u, 0.0f);
        Insp.SetDraftColor(0u, 0u, 0u, 0.9f);
        Insp.Apply();
        Req.Material = &Index.QueryDescriptors()[0];
        Req.OutPath = "/tmp/MaterialPreview_N3.png";
        ShaderballPreviewResult Res3;
        Check(RenderShaderballPreview(Req, Res3), "N5 post-commit preview renders");
        std::vector<unsigned char> C;
        Check(ReadFile("/tmp/MaterialPreview_N3.png", C) && (C.size() != A.size() || std::memcmp(C.data(), A.data(), A.size()) != 0),
              "N6 commit changes the pixels");
        ShaderballPreviewRequest BadReq;
        ShaderballPreviewResult BadRes;
        Check(!RenderShaderballPreview(BadReq, BadRes), "N7 null request fails clean");
        std::remove("/tmp/MaterialPreview_N1.png"); std::remove("/tmp/MaterialPreview_N2.png"); std::remove("/tmp/MaterialPreview_N3.png");
    }

    // ── O. Preview toggle + request ─────────────────────────────────────────────────────────────────────────────
    {
        MaterialInspector P;
        P.SeedPreview(false);
        Check(!P.QueryPreviewEnabled() && P.QueryPreviewRevision() == 0u, "O1 seed sets without bumping");
        P.SeedSelection("brick-01"); P.Rebuild(&Index);
        P.SetPreviewEnabled(true);
        Check(P.QueryPreviewEnabled() && P.QueryPreviewRevision() == 1u, "O2 enable bumps");
        P.SetPreviewEnabled(true);
        Check(P.QueryPreviewRevision() == 1u, "O3 set-same bumps nothing");
        Check(!P.TakePreviewRequest(), "O4 no request initially");
        P.Apply();   // clean: no changes
        Check(!P.TakePreviewRequest(), "O5 clean Apply requests nothing");
        P.SetDraftScalar(0u, 2u, 0.5f);
        P.Apply();   // dirty + enabled
        Check(P.TakePreviewRequest(), "O6 commit-while-enabled requests");
        Check(!P.TakePreviewRequest(), "O7 take consumes");
        P.SetPreviewEnabled(false);
        P.SetDraftScalar(0u, 2u, 0.6f);
        P.Apply();
        Check(!P.TakePreviewRequest(), "O8 commit-while-disabled requests nothing");
        P.NotifyPreviewRendered(true, "brick-01", "/tmp/x.png", 1.5, 3u);
        Check(Contains(P.QueryPreviewStatus(), "brick-01") && Contains(P.QueryPreviewStatus(), "commit #3"), "O9 ok stamp");
        P.NotifyPreviewRendered(false, "brick-01", "write failed", 0.0, 3u);
        Check(Contains(P.QueryPreviewStatus(), "failed: write failed"), "O10 fail stamp");
    }

    // ── P. Host commit flow ─────────────────────────────────────────────────────────────────────────────────────
    {
        ControlCentreHost Host2;
        Check(Host2.Initialize(1280u, 800u), "P1 host initializes headless");
        Host2.NavigateToPage(ControlCentrePageCategory::Materials);
        Host2.AccessMaterials().SeedSelection("wax-04");
        Host2.AccessMaterials().Rebuild(&Index);
        Check(!Host2.IsPageDirty(), "P2 clean when untouched");
        Host2.AccessMaterials().SetDraftScalar(4u, 16u, 0.33f);
        Check(Host2.IsPageDirty(), "P3 edit dirties the page");
        // (Apply/DiscardActivePage are private tap targets — the harness drives the same page instance the footer calls.)
        Host2.AccessMaterials().Apply();
        Check(!Host2.IsPageDirty() && Index.QueryDescriptors()[4].Slabs[0].SubsurfaceRadius == 0.33f, "P4 host page Apply commits + clears");
        Check(Host2.AccessMaterials().QueryCommitRevision() == 1u, "P5 commit revision visible through the host");
        Host2.AccessMaterials().SetDraftScalar(4u, 16u, 0.44f);
        Host2.AccessMaterials().Discard();
        Check(!Host2.IsPageDirty() && Host2.AccessMaterials().QueryDraftSlab(4u).SubsurfaceRadius == 0.33f, "P6 host page Discard reverts");
    }


    if (Failed == 0) std::printf("MATERIAL INSPECTOR: PASS (%d/%d)\n", Passed, Passed + Failed);
    else std::printf("MATERIAL INSPECTOR: FAIL (%d passed, %d failed)\n", Passed, Failed);
    return Failed == 0 ? 0 : 1;
}
