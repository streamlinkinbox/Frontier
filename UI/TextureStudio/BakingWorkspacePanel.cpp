/*============================================================================================================================================
                                                          BAKINGWORKSPACEPANEL.CPP
============================================================================================================================================*/
#include "UI/TextureStudio/BakingWorkspacePanel.h"
#include "UI/TextureStudio/PanelChrome.h"
#include "UI/TextureStudio/WorkspaceViewportDrawList.h"
#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/DropdownControls.h"
#include "Engine/Interface/GlyphSpace.h"
#include <stb_image_write.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace Frontier
{
// Shared panel UI primitives (PanelChrome.h) - one implementation for all five workspaces.
using PanelChrome::Caption;
using PanelChrome::CaptionWidth;
using PanelChrome::Spectrum;
using PanelChrome::SpectrumQuad;
using PanelChrome::ThemeQuad;
namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     MESH MAP METADATA (1:1 WITH meshMaps.js)
//------------------------------------------------------------------------------------------------------------------------

struct MeshMapMeta
{
    const char *Key;
    const char *Label;
    const char *Family;
    const char *Description;
    const char *Swatch; // 📝 1:1 with meshMaps.js's `swatch:` key - drives DrawMapSwatch's gradient family below.
    // 📝 1:1 with MeshMapGrid.jsx's <small> tile caption logic (m.derived ? "Derived mask / effect" : m.space ?
    // `${m.space} space` : m.type === "scalar" ? "Grayscale" : m.type === "color" ? "RGB" : "Vector data") - the
    // tile itself only ever shows this short classifier; the full Description reads in the inspector's Map
    // inspector section and the tooltip, exactly like the web reference's terse-tile/verbose-inspector split.
    const char *Hint;
};
// 📝 Order, grouping and swatch family all mirror Experimental/TextureStudio/src/baking/meshMaps.js exactly;
// families render as section headers in the tile grid in this same order (Normals, Surface, Coordinates & IDs,
// Masks, Color), and each tile's preview swatch uses the matching .bk-swatch-* gradient family from studioTheme.css.
const std::vector<MeshMapMeta> &MeshMapTable()
{
    static const std::vector<MeshMapMeta> Table{
        {"normal", "Tangent normal", "Normals", "Projected high-surface normal in the low mesh UV tangent frame.", "normal", "Tangent space"},
        {"world-normal", "World normal", "Normals", "Projected high-surface normal in scene/world axes.", "normal-world", "World space"},
        {"object-normal", "Object normal", "Normals", "Projected normal in the low target's original local axes.", "normal-object", "Object space"},
        {"bent-normal", "Bent tangent normal", "Normals", "Average unoccluded hemisphere direction, tangent frame.", "normal", "Tangent space"},
        {"bent-world-normal", "Bent world normal", "Normals", "Average unoccluded hemisphere direction, world axes.", "normal-world", "World space"},
        {"bent-object-normal", "Bent object normal", "Normals", "Average unoccluded hemisphere direction, object axes.", "normal-object", "Object space"},
        {"bevel-normal", "Bevel normal", "Normals", "Geometry-derived rounded-edge normal approximation.", "normal", "Derived mask / effect"},
        {"occlusion", "Ambient occlusion", "Surface", "Hemisphere visibility against the high mesh.", "ao", "Grayscale"},
        {"occlusion-secondary", "Secondary AO", "Surface", "A second hemisphere-visibility bake, its own distance.", "ao", "Grayscale"},
        {"curvature", "Curvature", "Surface", "Signed local normal variation estimate.", "curvature", "Grayscale"},
        {"convexity", "Convexity", "Surface", "Positive signed curvature isolated as a bright-edge mask.", "curvature", "Grayscale"},
        {"cavity", "Cavity / concavity", "Surface", "Negative signed curvature isolated as a bright-recess mask.", "ao", "Grayscale"},
        {"thickness", "Thickness", "Surface", "Inward high-mesh ray distance divided by thickness range.", "height", "Grayscale"},
        {"height", "Projection height", "Surface", "Signed high-to-low distance along the low normal.", "height", "Grayscale"},
        {"vector-displacement", "Vector displacement", "Surface", "Full projected high-minus-low XYZ offset.", "normal-object", "World space"},
        {"position", "Position", "Coordinates & IDs", "Projected high-surface XYZ normalized to the world bbox.", "position", "Vector data"},
        {"id", "Object ID", "Coordinates & IDs", "24-bit source-object identity, little-endian RGB.", "id", "RGB"},
        {"uv-island", "UV island ID", "Coordinates & IDs", "A distinct display colour per connected low UV chart.", "id", "RGB"},
        {"uv", "UV coordinates", "Coordinates & IDs", "Low target UV0 coordinates in R/G, blue = 1.", "position", "Vector data"},
        {"wireframe", "UV wireframe", "Coordinates & IDs", "Low triangle edges in texture space.", "wireframe", "Grayscale"},
        {"coverage", "UV coverage", "Masks", "White on covered low UV texels; black outside.", "coverage", "Grayscale"},
        {"alpha", "Alpha / hit mask", "Masks", "White where a high surface was projected, black for misses.", "coverage", "Grayscale"},
        {"bevel-mask", "Bevel mask", "Masks", "Angular change from rounded-edge normal averaging.", "curvature", "Derived mask / effect"},
        {"dust", "Dust mask", "Masks", "Derived upward-facing deposition mask.", "dust", "Derived mask / effect"},
        {"dirt", "Dirt mask", "Masks", "Derived occlusion plus concavity mask.", "ao", "Derived mask / effect"},
        {"edge-wear", "Edge wear mask", "Masks", "Derived convexity weighted by surface exposure.", "curvature", "Derived mask / effect"},
        {"base-color", "Vertex color", "Color", "Interpolated high vertex colour, sRGB; white if none.", "color", "RGB"},
    };
    return Table;
}
// 🎨 Approximates the matching .bk-swatch-* CSS gradient from studioTheme.css as a 2-4 stop linear blend drawn
// with AddRectFilledMultiColor (plus a couple of cheap overlay shapes for the conic/radial-looking families) -
// the same visual family per mesh-map group the web reference uses for its placeholder tile art before a real
// bake thumbnail exists.
void DrawMapSwatch(ImDrawList *Ink, ImVec2 Minimum, ImVec2 Maximum, const char *Swatch, const char *MapKey = nullptr) noexcept
{
    const auto Colour = [](uint32_t Hex) { return IM_COL32((Hex >> 16) & 0xff, (Hex >> 8) & 0xff, Hex & 0xff, 255); };
    const auto Diagonal = [&](uint32_t A, uint32_t B) { Ink->AddRectFilledMultiColor(Minimum, Maximum, Colour(A), Colour(B), Colour(B), Colour(A)); };
    // 🎨 Approximates a CSS radial-gradient() as concentric filled circles drawn largest-to-smallest (each one
    // painting over the middle of the last) - cheap with ImGui's draw list and visually equivalent to the
    // reference's banded ellipse stops for swatches this small (51px tall).
    const ImVec2 Centre((Minimum.x + Maximum.x) * .5f, (Minimum.y + Maximum.y) * .5f);
    const float Span = std::max(Maximum.x - Minimum.x, Maximum.y - Minimum.y);
    const auto Rings = [&](uint32_t Background, std::initializer_list<std::pair<float, uint32_t>> Stops)
    {
        Ink->AddRectFilled(Minimum, Maximum, Colour(Background));
        for (const auto &[Fraction, Hex] : Stops) Ink->AddCircleFilled(Centre, Span * Fraction, Colour(Hex), 28);
    };
    const std::string Key = Swatch ? Swatch : "";
    if (Key == "normal") Diagonal(0xed7ce5, 0x8397ff);
    else if (Key == "normal-world") Diagonal(0x93cf98, 0xcf779d);
    else if (Key == "normal-object") Diagonal(0xd37390, 0x81c996);
    else if (Key == "id") Diagonal(0xdc976f, 0x81a6cc);
    else if (Key == "position") Diagonal(0xa2bf93, 0x9dbac7);
    // .bk-swatch-ao: radial-gradient(ellipse at 50% 45%, #333 20%, #c5c5be 43%, #60635f 68%, #252625) - a dark
    // centre dot inside a light ring inside a mid ring on a dark field, i.e. the "donut" look the reference
    // screenshots show for AO/cavity/dirt, not a single flat light blob.
    else if (Key == "ao") Rings(0x252625, {{0.34f, 0x60635f}, {0.215f, 0xc5c5be}, {0.1f, 0x333333}});
    else if (Key == "height") Diagonal(0x343a37, 0xdadad1);
    // .bk-swatch-curvature: radial-gradient(ellipse at 50% 55%, #252824 30%, #d9dcd2 33% 36%, #585d53 40% 55%,
    // #cad0c4 58%, #333b31 62%) - a concentric double ring (dark hole, light ring, mid ring, light ring, dark edge).
    else if (Key == "curvature") Rings(0x333b31, {{0.29f, 0xcad0c4}, {0.2375f, 0x585d53}, {0.17f, 0xd9dcd2}, {0.15f, 0x252824}});
    // .bk-swatch-coverage / alpha: radial-gradient(ellipse at 50% 45%, #e4e8df 48% , #111811 50%) - a sharp-edged
    // light disc on a near-black field, no soft falloff.
    else if (Key == "coverage") Rings(0x111811, {{0.24f, 0xe4e8df}});
    else if (Key == "dust") Diagonal(0xd1cbb2, 0x2a3328);
    // .bk-swatch-wireframe layers TWO 45deg/-45deg repeating-linear-gradients, i.e. a genuine crosshatch grid,
    // not single-direction hatching - both diagonals are drawn here so the tile reads as an X pattern like the
    // reference, instead of parallel stripes.
    else if (Key == "wireframe")
    {
        Ink->AddRectFilled(Minimum, Maximum, Colour(0x1c251e));
        const float Height = Maximum.y - Minimum.y;
        for (float X = Minimum.x - Height; X < Maximum.x + Height; X += 10.f)
        {
            Ink->AddLine(ImVec2(X, Maximum.y), ImVec2(X + Height, Minimum.y), Colour(0x98a38d), 1.f);
            Ink->AddLine(ImVec2(X, Minimum.y), ImVec2(X + Height, Maximum.y), Colour(0x98a38d), 1.f);
        }
    }
    else if (Key == "color") Diagonal(0xac815c, 0x709a8e);
    else Diagonal(0x565958, 0x1b1d1c);
    // 🧩 1:1 with .bk-map-swatch span's watermark text - "XYZ" for any map carrying a `space` field in
    // meshMaps.js (the normal/normal-world/normal-object swatch families cover exactly that set), "ID" for
    // the Object ID map and "UV" for the UV island map, blank otherwise - matches the reference pixel-for-pixel
    // down to the faint #fff9 (60% white) overlay colour and letter-spaced small caps.
    const std::string MapKeyStr = MapKey ? MapKey : "";
    const char *Overlay = (Key == "normal" || Key == "normal-world" || Key == "normal-object") ? "XYZ"
        : MapKeyStr == "id" ? "ID"
        : MapKeyStr == "uv-island" ? "UV"
        : nullptr;
    if (Overlay)
    {
        const ImVec2 Size = ImGui::CalcTextSize(Overlay);
        const float Scale = 10.f / ImGui::GetFontSize() * 1.302f;
        Ink->AddText(ImGui::GetFont(), ImGui::GetFontSize() * Scale,
            ImVec2(Minimum.x + 8.f, Maximum.y - Size.y * Scale - 6.f), IM_COL32(255, 255, 255, 153), Overlay);
    }
}
const MeshMapMeta *FindMeshMap(const std::string &Key)
{
    for (const auto &Entry : MeshMapTable())
        if (Key == Entry.Key) return &Entry;
    return nullptr;
}
const std::map<std::string, std::vector<std::string>> &MeshMapPresets()
{
    static const std::map<std::string, std::vector<std::string>> Presets{
        {"Essential", {"normal", "world-normal", "occlusion", "curvature", "position", "thickness"}},
        {"Masks", {"occlusion", "curvature", "convexity", "cavity", "uv-island", "dust", "dirt", "edge-wear", "bevel-mask", "alpha"}},
        {"Normals",
            {"normal", "world-normal", "object-normal", "bent-normal", "bent-world-normal", "bent-object-normal", "bevel-normal"}},
    };
    return Presets;
}
constexpr const char *kDefaultChannels[]{"normal", "world-normal", "occlusion", "curvature", "thickness", "position", "id", "height",
    "coverage"};
// 📝 BakingStudio.jsx renders every triangle count through Number.toLocaleString() ("12,288", not "12288").
std::string FormatCount(uint64_t Value)
{
    const std::string Digits = std::to_string(Value);
    std::string Grouped;
    int Run = 0;
    for (auto It = Digits.rbegin(); It != Digits.rend(); ++It)
    {
        if (Run == 3) { Grouped += ','; Run = 0; }
        Grouped += *It;
        ++Run;
    }
    std::reverse(Grouped.begin(), Grouped.end());
    return Grouped;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     MINIMAL 3D CAMERA MATH
//------------------------------------------------------------------------------------------------------------------------

struct Vec3 { float X = 0, Y = 0, Z = 0; };
Vec3 Sub(Vec3 A, Vec3 B) noexcept { return {A.X - B.X, A.Y - B.Y, A.Z - B.Z}; }
Vec3 CrossV(Vec3 A, Vec3 B) noexcept { return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X}; }
float DotV(Vec3 A, Vec3 B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
Vec3 NormalizeV(Vec3 V) noexcept
{
    const float Length = std::sqrt(DotV(V, V));
    return Length > 1e-9f ? Vec3{V.X / Length, V.Y / Length, V.Z / Length} : V;
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

BakingWorkspacePanel::BakingWorkspacePanel()
{
    Settings.Channels.assign(std::begin(kDefaultChannels), std::end(kDefaultChannels));
    // 📝 Same reusable PanelDockspace every other TextureWorkspace panel registers with (Pattern/Stamp/
    // Material) instead of Baking's previous bespoke fixed-position three-pane layout - this is what makes
    // the Assets/Preview/Inspector docks actually dockable, resizable and rearrangeable like the rest of the
    // app, not a Baking-only quirk.
    (void)Panels.ConfigureDockspaceKey("TextureWorkspace Baking panels");
    Panels.ConfigureWorkspaceInsets(43, 32);
    Panels.ConfigureTabExtent(130, 26);
    Panels.Register({"baking-assets", "Bake project", [&]() { ConstructAssetsDock(); }});
    Panels.Register({"baking-preview", "Baking viewport", [&]() { ConstructPreviewDock(); }});
    Panels.Register({"baking-inspector", "Baking inspector", [&]() { ConstructInspector(); }});
    // 📝 studioTheme.css's .bk-layout grid is 278px / 1fr / 338px; Pattern/Stamp express that same grid against
    // the shared 1436px reference width, instead of the rounder 22/56/22 split this panel used before (which
    // made the assets dock ~38px too wide and the inspector ~22px too narrow at reference width).
    Panels.ConfigureColumns({{"baking-assets", 278.f / 1436}, {"baking-preview", 820.f / 1436}, {"baking-inspector", 338.f / 1436}});
}

bool BakingWorkspacePanel::InitializeContent() noexcept
{
    try
    {
        SelectDemo("Teapot");
        Info = "Source meshes stay in this session. Recipes contain settings, not geometry.";
        return true;
    }
    catch (...) { return false; }
}
void BakingWorkspacePanel::FinalizeContent() noexcept
{
    if (BakeFuture.valid())
    {
        try { BakeFuture.wait(); } catch (...) {}
    }
    if (GpuRequestFuture.valid())
    {
        try { GpuRequestFuture.wait(); } catch (...) {}
    }
    GpuRequest.reset();
    ControlExtents.clear();
}
void BakingWorkspacePanel::IntegrateContent(double DeltaSeconds) noexcept { SettingsCoalesceClockSeconds += DeltaSeconds; PollBake(); }

//------------------------------------------------------------------------------------------------------------------------
//                                                     DOCUMENT / BAKE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void BakingWorkspacePanel::SelectDemo(const std::string &Shape)
{
    if (BakeRunning.load()) return;
    Assets = BuildDemoBakeAssets(Shape);
    DemoShape = Shape;
    SelectedTargets.clear();
    for (const auto &Part : Assets.Low.Parts) SelectedTargets.push_back(Part.Name);
    PreviewSetName = Assets.Low.Parts.empty() ? "" : Assets.Low.Parts.front().Name;
    ClearResults();
    Info = "Built-in low meshes use non-overlapping UVs. Imported low meshes require their own UV layouts.";
    ResetCamera();
}
void BakingWorkspacePanel::ClearResults() noexcept
{
    Results.clear();
    ++BakeResultsRevision;
}

PlaneExtent BakingWorkspacePanel::QueryControlExtent(const std::string &Caption) const
{
    const auto Position = ControlExtents.find(Caption);
    return Position == ControlExtents.end() ? PlaneExtent{} : Position->second;
}

void BakingWorkspacePanel::Track(const std::string &Caption)
{
    const ImVec2 Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
    ControlExtents[Caption] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
}
// 🧩 1:1 behaviour with BakingStudio.jsx's travel()/setSettingsDocument() history: every inspector field change
// is coalesced into a single undo step while it is still being actively edited (continuous drags/typing), and a
// new step only starts once 650ms have passed since the last push - not one undo entry per dragged pixel.
bool BakingWorkspacePanel::SettingsFieldsEqual(const BakeSettings &A, const BakeSettings &B) noexcept
{
    return A.Resolution == B.Resolution && A.Padding == B.Padding && A.NormalPlusY == B.NormalPlusY &&
        A.ProjectionRay == B.ProjectionRay && A.Front == B.Front && A.Back == B.Back && A.AoDistance == B.AoDistance &&
        A.SecondaryAoDistance == B.SecondaryAoDistance && A.ThicknessRange == B.ThicknessRange &&
        A.CurvatureRadius == B.CurvatureRadius && A.Samples == B.Samples && A.BevelRadius == B.BevelRadius &&
        A.BevelStrength == B.BevelStrength && A.BevelSamples == B.BevelSamples && A.DustAxis == B.DustAxis &&
        A.DustFalloff == B.DustFalloff && A.WireWidth == B.WireWidth && A.MaskStrength == B.MaskStrength &&
        A.MatchByName == B.MatchByName && A.Channels == B.Channels;
}
void BakingWorkspacePanel::CoalesceSettingsUndo()
{
    if (SettingsFieldsEqual(Settings, SettingsAtFrameStart)) return;
    if (SettingsUndoStack.empty() || SettingsCoalesceClockSeconds - LastSettingsPushSeconds > 0.65)
        SettingsUndoStack.push_back(SettingsAtFrameStart);
    LastSettingsPushSeconds = SettingsCoalesceClockSeconds;
    SettingsRedoStack.clear();
}
void BakingWorkspacePanel::UndoSettings()
{
    if (SettingsUndoStack.empty()) return;
    SettingsRedoStack.push_back(Settings);
    Settings = SettingsUndoStack.back();
    SettingsUndoStack.pop_back();
    LastSettingsPushSeconds = -1000.0;
}
void BakingWorkspacePanel::RedoSettings()
{
    if (SettingsRedoStack.empty()) return;
    SettingsUndoStack.push_back(Settings);
    Settings = SettingsRedoStack.back();
    SettingsRedoStack.pop_back();
    LastSettingsPushSeconds = -1000.0;
}
void BakingWorkspacePanel::StartBake()
{
    if (BakeRunning.load() || SelectedTargets.empty() || Settings.Channels.empty()) return;
    std::vector<const BakePart *> Targets;
    for (const auto &Name : SelectedTargets)
        for (const auto &Part : Assets.Low.Parts)
            if (Part.Name == Name) Targets.push_back(&Part);
    if (Targets.empty()) return;
    BakeRunning.store(true);
    BakeDone.store(0);
    BakeTotal.store(1);
    Error.clear();
    SettingsAtLastBake = Settings;
    // 📝 Snapshot by value for the whole bake: workers and the GPU hooks must never read Assets/Settings the
    // user can still edit mid-bake, and a GPU refusal at any step restarts the CPU route from these exact
    // inputs instead of whatever the inspector happens to hold by then.
    BakeHighSnapshot = Assets.High;
    BakeSettingsSnapshot = Settings;
    BakeTargetSnapshots.clear();
    for (const auto *Part : Targets) BakeTargetSnapshots.push_back(*Part);
    GpuRequest.reset();

    const std::string GpuDevice = (PreferGpuBake && QueryGpuBakeDevice && SubmitGpuBake && PollGpuBake)
        ? QueryGpuBakeDevice() : std::string();
    if (!GpuDevice.empty())
    {
        BakeDeviceUsed = GpuDevice;
        ActiveBakeStep = BakeStep::GpuPreparing;
        BakeProgressLabel = "Preparing GPU bake…";
        // 📝 Flattening the BVH and rasterizing UV coverage is the one CPU-costly step of a GPU bake, so it
        // runs on a worker like the CPU bake does; only the finished request crosses to the host's GPU hooks.
        const BakeMeshSet HighCopy = BakeHighSnapshot;
        const BakeSettings SettingsCopy = BakeSettingsSnapshot;
        const std::vector<BakePart> TargetCopies = BakeTargetSnapshots;
        GpuRequestFuture = std::async(std::launch::async, [HighCopy, SettingsCopy, TargetCopies]() mutable
        {
            std::vector<const BakePart *> Pointers;
            for (const auto &Part : TargetCopies) Pointers.push_back(&Part);
            return std::make_shared<BakeFieldRequest>(ConstructBakeFieldRequest(Pointers, HighCopy, SettingsCopy));
        });
        return;
    }
    BakeDeviceUsed = PreferGpuBake ? "CPU (no Vulkan compute GPU)" : "CPU";
    StartCpuBake();
}
void BakingWorkspacePanel::StartCpuBake()
{
    ActiveBakeStep = BakeStep::CpuBaking;
    BakeProgressLabel = "Baking mesh maps…";
    // 📝 Copy by value into the worker: the snapshots must outlive the bake even if the panel is torn
    // down mid-bake (FinalizeContent waits on BakeFuture first, so this is belt-and-braces, not load-bearing).
    const BakeMeshSet HighCopy = BakeHighSnapshot;
    const BakeSettings SettingsCopy = BakeSettingsSnapshot;
    const std::vector<BakePart> TargetCopies = BakeTargetSnapshots;
    BakeFuture = std::async(std::launch::async, [this, HighCopy, SettingsCopy, TargetCopies]() mutable
    {
        std::vector<const BakePart *> Pointers;
        for (const auto &Part : TargetCopies) Pointers.push_back(&Part);
        return RunMeshBake(Pointers, HighCopy, SettingsCopy, [this](const BakeProgress &Progress)
        {
            BakeDone.store(Progress.Done);
            BakeTotal.store(std::max<uint64_t>(1, Progress.Total));
        });
    });
}
void BakingWorkspacePanel::FallbackToCpuBake(const std::string &Reason)
{
    // 📝 The refusal stays named in the completion line so a fallback bake is never mistaken for a GPU bake.
    BakeDeviceUsed = "CPU after GPU refusal: " + Reason;
    GpuRequest.reset();
    StartCpuBake();
}
void BakingWorkspacePanel::PollBake()
{
    if (!BakeRunning.load()) return;
    switch (ActiveBakeStep)
    {
    case BakeStep::GpuPreparing:
    {
        if (!GpuRequestFuture.valid() || GpuRequestFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        try { GpuRequest = GpuRequestFuture.get(); }
        catch (const std::exception &Failure) { FallbackToCpuBake(Failure.what()); return; }
        if (!SubmitGpuBake || !SubmitGpuBake(GpuRequest)) { FallbackToCpuBake("the GPU bake unit is busy or faulted"); return; }
        ActiveBakeStep = BakeStep::GpuComputing;
        BakeProgressLabel = "Baking mesh maps on GPU…";
        return;
    }
    case BakeStep::GpuComputing:
    {
        BakeGpuOutcome Outcome;
        if (PollGpuBake) Outcome = PollGpuBake();
        else { Outcome.Complete = true; Outcome.Failed = true; Outcome.Failure = "GPU bake hooks were detached mid-bake"; }
        BakeDone.store(Outcome.DoneTexels);
        BakeTotal.store(std::max<uint64_t>(1, Outcome.TotalTexels));
        if (!Outcome.Complete) return;
        if (Outcome.Failed)
        {
            FallbackToCpuBake(Outcome.Failure.empty() ? "unspecified GPU bake failure" : Outcome.Failure);
            return;
        }
        // 📝 Encoding is plain CPU arithmetic (no BVH queries), but at GPU-only resolutions it is still
        // worker-sized work, so it returns through the same BakeFuture the CPU route completes on.
        ActiveBakeStep = BakeStep::GpuEncoding;
        BakeProgressLabel = "Encoding baked maps…";
        BakeDone.store(0);
        BakeTotal.store(1);
        const std::shared_ptr<const BakeFieldRequest> Request = GpuRequest;
        const BakeSettings SettingsCopy = BakeSettingsSnapshot;
        std::vector<std::vector<BakeTexelField>> Fields = std::move(Outcome.Fields);
        BakeFuture = std::async(std::launch::async, [Request, SettingsCopy, Fields = std::move(Fields)]() mutable
        {
            return EncodeBakeResults(*Request, Fields, SettingsCopy);
        });
        return;
    }
    case BakeStep::CpuBaking:
    case BakeStep::GpuEncoding:
        if (!BakeFuture.valid() || BakeFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        AcceptBakeResults();
        return;
    case BakeStep::Idle:
    default:
        return;
    }
}
void BakingWorkspacePanel::AcceptBakeResults()
{
    try
    {
        Results = BakeFuture.get();
        ++BakeResultsRevision;
        uint64_t Misses = 0;
        for (const auto &Result : Results) Misses += Result.Stats.Misses;
        // 📝 The completion line names the device that actually baked and the resolution actually produced
        // (the CPU route clamps above 1024), so neither can be misread from the inspector's current values.
        const uint32_t BakedResolution = Results.empty() ? 0 : Results.front().Resolution;
        Info = std::to_string(Results.size()) + " texture set" + (Results.size() == 1 ? "" : "s") + " baked on " + BakeDeviceUsed +
            " at " + std::to_string(BakedResolution) + " x " + std::to_string(BakedResolution) + ". " +
            std::to_string(Misses) + " missed projection" + (Misses == 1 ? "" : "s") + (Misses ? " — check ray distance/alignment." : ".");
        if (!Results.empty())
        {
            PreviewSetName = Results.front().Name;
            PreviewMapKey = Settings.Channels.empty() ? "normal" : Settings.Channels.front();
            // 📝 Keep whichever viewport the user is already in when a bake lands: forcing Maps2D here yanked
            // them out of the 3D orbit view they were framing. The 3D/2D tabs remain the only view switch.
        }
    }
    catch (const std::exception &Failure) { Error = Failure.what(); }
    GpuRequest.reset();
    ActiveBakeStep = BakeStep::Idle;
    BakeRunning.store(false);
}
void BakingWorkspacePanel::ExportResults()
{
    if (Results.empty()) { Error = "Bake mesh maps before exporting."; return; }
    try
    {
        std::filesystem::path Directory = ExportDirectory.empty() ? std::filesystem::temp_directory_path() / "FrontierBakeExport" : ExportDirectory;
        std::filesystem::create_directories(Directory);
        uint32_t Written = 0;
        for (const auto &Result : Results)
            for (const auto &Map : Result.Maps)
            {
                const auto Path = Directory / (Result.Name + "-" + Map.Key + ".png");
                if (stbi_write_png(Path.string().c_str(), int(Result.Resolution), int(Result.Resolution), 4, Map.Rgba.data(), int(Result.Resolution) * 4))
                    ++Written;
            }
        LastExportDirectory = Directory.string();
        char Buffer[320];
        std::snprintf(Buffer, sizeof(Buffer), "%u map PNGs written to %s", Written, LastExportDirectory.c_str());
        Info = Buffer;
    }
    catch (const std::exception &Failure) { Error = std::string("Export failed: ") + Failure.what(); }
}
// 📝 1:1 with BakingStudio.jsx's saveRecipe(): writes the full bake-settings/channel-selection document to disk
// so it can be reloaded later - "Recipes contain settings, not geometry" is the exact footer copy the web
// reference prints below this same control, and it is honoured here too (no mesh data in the file).
void BakingWorkspacePanel::SaveRecipe()
{
    try
    {
        std::filesystem::path Directory = ExportDirectory.empty() ? std::filesystem::temp_directory_path() / "FrontierBakeExport" : ExportDirectory;
        std::filesystem::create_directories(Directory);
        const auto Path = Directory / (Assets.Low.Label + ".bake-recipe.json");
        std::string Body = "{\n  \"schema\": \"frontier-bake-recipe/1\",\n  \"target\": \"" + Assets.Low.Label + "\",\n  \"settings\": {\n";
        char Line[256];
        std::snprintf(Line, sizeof(Line), "    \"resolution\": %u,\n", Settings.Resolution); Body += Line;
        std::snprintf(Line, sizeof(Line), "    \"padding\": %u,\n", Settings.Padding); Body += Line;
        Body += std::string("    \"normalFormat\": \"") + (Settings.NormalPlusY ? "OpenGL +Y" : "DirectX -Y") + "\",\n";
        Body += std::string("    \"method\": \"") + (Settings.ProjectionRay ? "Normal ray" : "Nearest surface") + "\",\n";
        std::snprintf(Line, sizeof(Line), "    \"frontDistance\": %g,\n", double(Settings.Front)); Body += Line;
        std::snprintf(Line, sizeof(Line), "    \"backDistance\": %g,\n", double(Settings.Back)); Body += Line;
        std::snprintf(Line, sizeof(Line), "    \"aoDistance\": %g,\n", double(Settings.AoDistance)); Body += Line;
        std::snprintf(Line, sizeof(Line), "    \"secondaryAoDistance\": %g,\n", double(Settings.SecondaryAoDistance)); Body += Line;
        std::snprintf(Line, sizeof(Line), "    \"thicknessRange\": %g,\n", double(Settings.ThicknessRange)); Body += Line;
        std::snprintf(Line, sizeof(Line), "    \"curvatureRadius\": %g,\n", double(Settings.CurvatureRadius)); Body += Line;
        std::snprintf(Line, sizeof(Line), "    \"aoSamples\": %u,\n", Settings.Samples); Body += Line;
        std::snprintf(Line, sizeof(Line), "    \"bevelRadius\": %g,\n", double(Settings.BevelRadius)); Body += Line;
        std::snprintf(Line, sizeof(Line), "    \"bevelStrength\": %g,\n", double(Settings.BevelStrength)); Body += Line;
        std::snprintf(Line, sizeof(Line), "    \"bevelSamples\": %u,\n", Settings.BevelSamples); Body += Line;
        Body += "    \"dustAxis\": \"" + Settings.DustAxis + "\",\n";
        std::snprintf(Line, sizeof(Line), "    \"dustFalloff\": %g,\n", double(Settings.DustFalloff)); Body += Line;
        std::snprintf(Line, sizeof(Line), "    \"wireWidth\": %g,\n", double(Settings.WireWidth)); Body += Line;
        std::snprintf(Line, sizeof(Line), "    \"maskStrength\": %g,\n", double(Settings.MaskStrength)); Body += Line;
        Body += std::string("    \"matchObjectsByName\": ") + (Settings.MatchByName ? "true" : "false") + "\n  },\n  \"channels\": [";
        for (size_t Index = 0; Index < Settings.Channels.size(); ++Index)
            Body += (Index ? ", \"" : "\"") + Settings.Channels[Index] + "\"";
        Body += "]\n}\n";
        std::ofstream File(Path, std::ios::binary);
        File << Body;
        File.close();
        char Buffer[320];
        std::snprintf(Buffer, sizeof(Buffer), "Recipe saved to %s", Path.string().c_str());
        Info = Buffer;
    }
    catch (const std::exception &Failure) { Error = std::string("Save recipe failed: ") + Failure.what(); }
}
// 📝 There is no native file-picker library in this tree (the web reference uses an <input type="file">), so
// rather than silently no-op this reports the honest limitation instead of pretending a recipe was opened.
void BakingWorkspacePanel::OpenRecipe()
{
    Info = "Recipe loading needs a native file picker this build does not have wired up yet - Save recipe still writes the real settings file.";
}
void BakingWorkspacePanel::ApplyPreset(const std::string &Preset)
{
    const auto &Presets = MeshMapPresets();
    if (Preset == "Clear") { Settings.Channels.clear(); return; }
    if (Preset == "All")
    {
        Settings.Channels.clear();
        for (const auto &Entry : MeshMapTable())
            if (IsMeshMapImplemented(Entry.Key)) Settings.Channels.push_back(Entry.Key);
        return;
    }
    const auto Found = Presets.find(Preset);
    if (Found == Presets.end()) return;
    Settings.Channels = Found->second;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     HEADER (WORKSPACE SWITCHER)
//------------------------------------------------------------------------------------------------------------------------

void BakingWorkspacePanel::ConstructHeader()
{
    const auto *Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(Viewport->WorkSize.x, 43 * InterfaceScale));
    ImGui::Begin("Baking header", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoScrollbar);
    const auto Origin = ImGui::GetWindowPos();
    const float Width = ImGui::GetWindowWidth();
    ImGui::GetWindowDrawList()->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + 43 * InterfaceScale), ImGui::GetColorU32(Spectrum(0x0b0b0b)));
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 16 * InterfaceScale, Origin.y + 13 * InterfaceScale));
    ImGui::TextColored(Spectrum(0xc5c5b8), "%s", Assets.Low.Label.c_str());
    ImGui::SameLine();
    ImGui::TextColored(Spectrum(0x67675b), ".bake");

    Controls.ControlHeight = 24;
    const float NavigationX = Origin.x + std::max(230.f * InterfaceScale, Width - 620.f * InterfaceScale);
    const char *Workspaces[]{"Material", "Pattern", "Texture", "Stamp", "Baking"};
    const float Widths[]{64, 58, 54, 52, 64};
    float PositionX = NavigationX;
    for (size_t Index = 0; Index < 5; ++Index)
    {
        const bool Available = true;
        ImGui::SetCursorScreenPos(ImVec2(PositionX, Origin.y + 9 * InterfaceScale));
        ImGui::BeginDisabled((Index == 0 && !ActivateMaterial) || (Index == 1 && !ActivatePattern) || (Index == 2 && !ActivateTexture) || (Index == 3 && !ActivateStamp));
        if (Controls.Button(Workspaces[Index], Widths[Index] * InterfaceScale, Index == 4, {}, true))
        {
            if (Index == 0 && ActivateMaterial) ActivateMaterial();
            if (Index == 1 && ActivatePattern) ActivatePattern();
            if (Index == 2 && ActivateTexture) ActivateTexture();
            if (Index == 3 && ActivateStamp) ActivateStamp();
        }
        Controls.Tooltip(Workspaces[Index], Available ? "Switch workspace without losing your document." : "This native workspace is not hosted yet.");
        ImGui::EndDisabled();
        PositionX += Widths[Index] * InterfaceScale;
    }

    // 🧩 1:1 with StudioHeader's .studio-header-actions children for the Baking workspace: an icon-only
    // "Open bake recipe" button (transparent chrome) then a filled "Save recipe" pill, both sitting just
    // left of the close button exactly like Experimental/TextureStudio/src/BakingStudio.jsx wires them.
    const float S = InterfaceScale;
    auto *HeaderInk = ImGui::GetWindowDrawList();
    // 📝 BakingStudio.jsx disables both recipe actions while a bake/import is in flight
    // (`locked = busy || importing`); this native build has no importing state, so BakeRunning is the lock.
    const bool RecipeLocked = BakeRunning.load();
    float ActionX = Origin.x + Width - 44 * S;
    {
        const float PillWidth = 102 * S, PillHeight = 25 * S;
        ActionX -= PillWidth + 8 * S;
        const ImVec2 Minimum(ActionX, Origin.y + (43 * S - PillHeight) * .5f), Maximum(Minimum.x + PillWidth, Minimum.y + PillHeight);
        ImGui::SetCursorScreenPos(Minimum);
        ImGui::BeginDisabled(RecipeLocked);
        ImGui::InvisibleButton("SaveRecipe", ImVec2(PillWidth, PillHeight));
        ImGui::EndDisabled();
        const bool Hovered = ImGui::IsItemHovered();
        HeaderInk->AddRectFilled(Minimum, Maximum, ImGui::GetColorU32(Spectrum(RecipeLocked ? 0x1b1b19 : Hovered ? 0x2c2c29 : 0x232321)), 6 * S);
        HeaderInk->AddRect(Minimum, Maximum, ImGui::GetColorU32(Spectrum(0xffffff, 0.05f)), 6 * S);
        PixelSpace IconSurface;
        (void)IconSurface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
        GlyphSpace::Stroke(IconSurface, "M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8ZM14 2v6h6",
            {Minimum.x + 10 * S, Minimum.y + (PillHeight - 11 * S) * .5f, 11 * S, 1.6f, SpectrumQuad(0xc9c9bb)});
        Caption(ImVec2(Minimum.x + 26 * S, Minimum.y + (PillHeight - 9 * S) * .42f), "Save recipe", 9.5f * S, 0xc9c9bb);
        if (ImGui::IsItemClicked()) SaveRecipe();
        if (Hovered) ImGui::SetTooltip("Write the current bake settings and channel selection to disk.");
    }
    {
        const float ButtonSize = 28 * S;
        ActionX -= ButtonSize + 2 * S;
        const ImVec2 Minimum(ActionX, Origin.y + (43 * S - ButtonSize) * .5f), Maximum(Minimum.x + ButtonSize, Minimum.y + ButtonSize);
        ImGui::SetCursorScreenPos(Minimum);
        ImGui::BeginDisabled(RecipeLocked);
        ImGui::InvisibleButton("OpenRecipe", ImVec2(ButtonSize, ButtonSize));
        ImGui::EndDisabled();
        const bool Hovered = ImGui::IsItemHovered();
        if (Hovered) HeaderInk->AddRectFilled(Minimum, Maximum, ImGui::GetColorU32(Spectrum(0x1c1c1a)), 6 * S);
        PixelSpace IconSurface;
        (void)IconSurface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
        GlyphSpace::Stroke(IconSurface, "M3 7a2 2 0 0 1 2-2h3.5l1.7 1.7H19a2 2 0 0 1 2 2v7.3a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2Z",
            {Minimum.x + (ButtonSize - 14 * S) * .5f, Minimum.y + (ButtonSize - 11 * S) * .5f, 14 * S, 1.6f, SpectrumQuad(0xa8a89a)});
        if (ImGui::IsItemClicked()) OpenRecipe();
        if (Hovered) ImGui::SetTooltip("Open bake recipe");
    }
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 44 * InterfaceScale, Origin.y + 9 * InterfaceScale));
    ImGui::BeginDisabled(!ActivateMaterial);
    if (ImGui::Button("X", ImVec2(28 * InterfaceScale, 24 * InterfaceScale)) && ActivateMaterial) ActivateMaterial();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close baking studio");
    ImGui::EndDisabled();
    ImGui::End();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     MESH MAP GRID
//------------------------------------------------------------------------------------------------------------------------

// 🧩 Based on Experimental/TextureStudio/src/baking/MeshMapGrid.jsx + its .bk-map-tile/.bk-map-swatch chrome
// in studioTheme.css: a 2-column grid of rounded 13px tile cards (a green check badge when selected), a bold
// label and a small caption, grouped under uppercase family headers - not a row of plain ImGui buttons. The
// native preview area is deliberately square (user preference) rather than the web tile's 51px-tall rectangle:
// bake maps are square textures, so their previews should read square at any window/dock size.
void BakingWorkspacePanel::ConstructMeshMapGrid(float Width)
{
    const float S = InterfaceScale;
    // 📝 MeshMapGrid.jsx receives `disabled={locked}` from BakingStudio.jsx: while a bake runs, neither the
    // preset pills nor the tiles may mutate Settings.Channels underneath the in-flight snapshot.
    const bool Locked = BakeRunning.load();
    // 🧩 1:1 with MeshMapGrid.jsx's <h2><Layers3/> Mesh maps</h2> + right-aligned "{enabled} / {total} enabled"
    // counter, then the "Click a tile to enable or disable it. Green tiles will be baked." paragraph underneath -
    // this whole heading block was missing (only the bare "MESH MAPS" caption existed) before this pass.
    const ImVec2 HeadingOrigin = ImGui::GetCursorScreenPos();
    {
        PixelSpace IconSurface;
        (void)IconSurface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
        GlyphSpace::Stroke(IconSurface,
            "M12.83 2.18a2 2 0 0 0-1.66 0L2.6 6.08a1 1 0 0 0 0 1.83l8.58 3.91a2 2 0 0 0 1.66 0l8.58-3.9a1 1 0 0 0 0-1.83Z "
            "M2 12a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 12 "
            "M2 17a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 17",
            {HeadingOrigin.x, HeadingOrigin.y + 3 * S, 13 * S, 1.6f, SpectrumQuad(0xd0d0bb)});
    }
    Caption(ImVec2(HeadingOrigin.x + 20 * S, HeadingOrigin.y + 1 * S), "Mesh maps", 13 * S, 0xd0d0bb, LightTypeface ? LightTypeface : RegularTypeface);
    {
        const uint32_t Total = uint32_t(MeshMapTable().size()), Enabled = uint32_t(Settings.Channels.size());
        char Counter[32];
        std::snprintf(Counter, sizeof(Counter), "%u / %u enabled", Enabled, Total);
        Caption(ImVec2(HeadingOrigin.x + Width - CaptionWidth(Counter, 10 * S), HeadingOrigin.y + 2 * S), Counter, 10 * S, 0x8dbc9d);
    }
    ImGui::Dummy(ImVec2(1, 19 * S));
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + 1 * S));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + Width);
    ImGui::TextColored(Spectrum(0x96958b), "Click a tile to enable or disable it. Green tiles will be baked.");
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(1, 10 * S));
    // 📝 "All maps"/"Clear maps" are the exact trailing button labels MeshMapGrid.jsx hardcodes after the
    // Object.entries(MESH_MAP_PRESETS) buttons - not "All"/"Clear" - while the preset lookup key itself stays
    // "All"/"Clear" to match ApplyPreset()'s switch above.
    const char *PresetKeys[]{"Essential", "Masks", "Normals", "All", "Clear"};
    const char *PresetLabels[]{"Essential", "Masks", "Normals", "All maps", "Clear maps"};
    Controls.ControlHeight = 22;
    // 📝 .bk-map-presets is `flex-wrap: wrap` in the web reference, so at the dock's ~310px content width these
    // five pills genuinely spill onto a second row rather than overflowing/clipping - reproduced here by
    // tracking cumulative row width instead of always chaining ImGui::SameLine().
    const ImVec2 RowStart = ImGui::GetCursorScreenPos();
    float RowX = RowStart.x, RowY = RowStart.y;
    for (size_t Index = 0; Index < 5; ++Index)
    {
        const float ButtonWidth = std::max(Controls.ControlHeight * S, ImGui::CalcTextSize(PresetLabels[Index]).x + 24 * S);
        if (Index != 0 && RowX + ButtonWidth > RowStart.x + Width + 0.5f) { RowX = RowStart.x; RowY += Controls.ControlHeight * S + 6 * S; }
        ImGui::SetCursorScreenPos(ImVec2(RowX, RowY));
        // 📝 MeshMapGrid.jsx: every preset is `disabled={disabled}`, and Clear maps is additionally disabled
        // when nothing is enabled - there is nothing left for it to clear.
        ImGui::BeginDisabled(Locked || (Index == 4 && Settings.Channels.empty()));
        if (Controls.Button(PresetLabels[Index], ButtonWidth, false, {}, true)) ApplyPreset(PresetKeys[Index]);
        ImGui::EndDisabled();
        Track(std::string("Preset ") + PresetKeys[Index]);
        RowX += ButtonWidth + 5 * S;
    }
    ImGui::SetCursorScreenPos(ImVec2(RowStart.x, RowY + Controls.ControlHeight * S));
    ImGui::Dummy(ImVec2(1, 10 * S));

    const float Gap = 7 * S;
    const uint32_t Columns = 2;
    const float TileWidth = (Width - Gap * float(Columns - 1)) / float(Columns);
    // 📝 Square previews by construction: the swatch/image area is exactly TilePreviewSide x TilePreviewSide
    // (the tile's inner width), and the tile height is that side plus fixed chrome (8px top padding, the
    // label/hint lines, 8px bottom padding). Nothing in the preview's aspect derives from the dock height or
    // the rectangular window, so maps stay square however the workspace is resized.
    const float TilePreviewSide = std::max(1.f, TileWidth - 16 * S);
    const float TileHeight = TilePreviewSide + 46 * S;
    const char *LastFamily = "";
    uint32_t Column = 0;
    ImVec2 RowOrigin = ImGui::GetCursorScreenPos();
    auto *Ink = ImGui::GetWindowDrawList();
    // 📝 BakingStudio.jsx feeds MeshMapGrid only the previews of the active texture set
    // (`previews.filter(p => p.set === (previewSet || previews[0]?.set))`); after a bake those real thumbnails
    // replace the illustrative swatches tile-for-tile.
    const BakePartResult *PreviewResult = nullptr;
    for (const auto &Result : Results)
        if (Result.Name == PreviewSetName) { PreviewResult = &Result; break; }
    if (!PreviewResult && !Results.empty()) PreviewResult = &Results.front();
    for (const auto &Entry : MeshMapTable())
    {
        if (std::strcmp(LastFamily, Entry.Family) != 0)
        {
            if (Column != 0) { ImGui::NewLine(); Column = 0; }
            if (*LastFamily) ImGui::Dummy(ImVec2(1, 8 * S));
            const ImVec2 HeadingPosition = ImGui::GetCursorScreenPos();
            Caption(ImVec2(HeadingPosition.x, HeadingPosition.y + 2 * S), Entry.Family, 9 * S, 0xaaa999);
            ImGui::Dummy(ImVec2(1, 17 * S));
            LastFamily = Entry.Family;
            RowOrigin = ImGui::GetCursorScreenPos();
        }
        const bool Implemented = IsMeshMapImplemented(Entry.Key);
        const bool Enabled = std::find(Settings.Channels.begin(), Settings.Channels.end(), std::string(Entry.Key)) != Settings.Channels.end();
        const ImVec2 TileMinimum(RowOrigin.x + Column * (TileWidth + Gap), RowOrigin.y);
        const ImVec2 TileMaximum(TileMinimum.x + TileWidth, TileMinimum.y + TileHeight);

        ImGui::PushID(Entry.Key);
        ImGui::SetCursorScreenPos(TileMinimum);
        ImGui::InvisibleButton("Tile", ImVec2(TileWidth, TileHeight));
        const bool Hovered = Implemented && !Locked && ImGui::IsItemHovered();
        if (Implemented && !Locked && ImGui::IsItemClicked())
        {
            InspectMapKey = Entry.Key;
            if (Enabled) Settings.Channels.erase(std::remove(Settings.Channels.begin(), Settings.Channels.end(), std::string(Entry.Key)), Settings.Channels.end());
            else Settings.Channels.push_back(Entry.Key);
        }
        Track(std::string("Tile ") + Entry.Key);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n%s", Entry.Description, Implemented ? (Enabled ? "Enabled · will be baked" : "Disabled") : "Not yet implemented in this native build.");

        const uint32_t TileFill = Enabled ? 0x163427 : Hovered ? 0x272b26 : 0x181918;
        const uint32_t TileBorder = Enabled ? 0x63c68e : Hovered ? 0x6b8c75 : 0x3a3a35;
        const float Alpha = Implemented ? 1.f : 0.4f;
        Ink->AddRectFilled(TileMinimum, TileMaximum, ImGui::GetColorU32(Spectrum(TileFill, Alpha)), 13 * S);
        Ink->AddRect(TileMinimum, TileMaximum, ImGui::GetColorU32(Spectrum(TileBorder, Alpha)), 13 * S);

        const ImVec2 SwatchMinimum(TileMinimum.x + 8 * S, TileMinimum.y + 8 * S), SwatchMaximum(TileMaximum.x - 8 * S, TileMinimum.y + 8 * S + TilePreviewSide);
        Ink->PushClipRect(SwatchMinimum, SwatchMaximum, true);
        // 🧩 1:1 with MeshMapGrid.jsx's `{image ? <img> : <span>}`: once the active set has a baked image for
        // this key, the tile shows that image instead of the gradient swatch + watermark. The preview rect is
        // square and the baked texture is square, so the image fills it edge-to-edge (the #080a08 field only
        // shows through transparent texels).
        // Same coarse block-raster sampling as ConstructMapPreview, and skipped entirely for tiles the scroll
        // clip has already culled, so a 27-tile grid costs thumbnails only for the rows on screen.
        const BakeMapImage *TileImage = nullptr;
        if (PreviewResult)
            for (const auto &Map : PreviewResult->Maps)
                if (Map.Key == Entry.Key) { TileImage = &Map; break; }
        const bool SwatchVisible = SwatchMaximum.y >= Ink->GetClipRectMin().y && SwatchMinimum.y <= Ink->GetClipRectMax().y;
        if (TileImage && PreviewResult->Resolution > 0 && SwatchVisible)
        {
            const uint32_t Resolution = PreviewResult->Resolution;
            Ink->AddRectFilled(SwatchMinimum, SwatchMaximum, ImGui::GetColorU32(Spectrum(0x080a08, Alpha)));
            const float Side = std::min(SwatchMaximum.x - SwatchMinimum.x, SwatchMaximum.y - SwatchMinimum.y);
            const ImVec2 ImageMinimum(SwatchMinimum.x + (SwatchMaximum.x - SwatchMinimum.x - Side) * .5f, SwatchMinimum.y + (SwatchMaximum.y - SwatchMinimum.y - Side) * .5f);
            constexpr uint32_t Blocks = 24;
            const float Cell = Side / float(Blocks);
            for (uint32_t By = 0; By < Blocks; ++By)
                for (uint32_t Bx = 0; Bx < Blocks; ++Bx)
                {
                    const uint32_t SampleX = std::min(Resolution - 1, uint32_t((float(Bx) + 0.5f) / float(Blocks) * float(Resolution)));
                    const uint32_t SampleY = std::min(Resolution - 1, uint32_t((float(By) + 0.5f) / float(Blocks) * float(Resolution)));
                    const size_t Offset = (size_t(SampleY) * Resolution + SampleX) * 4;
                    const uint8_t TexelAlpha = TileImage->Rgba[Offset + 3];
                    if (!TexelAlpha) continue; // transparent texels read against the #080a08 img background
                    const ImU32 Colour = IM_COL32(TileImage->Rgba[Offset + 0], TileImage->Rgba[Offset + 1], TileImage->Rgba[Offset + 2],
                        uint8_t(float(TexelAlpha) * Alpha));
                    const float X0 = ImageMinimum.x + Bx * Cell, Y0 = ImageMinimum.y + By * Cell;
                    Ink->AddRectFilled(ImVec2(X0, Y0), ImVec2(X0 + Cell + 0.5f, Y0 + Cell + 0.5f), Colour);
                }
        }
        else if (!TileImage)
            DrawMapSwatch(Ink, SwatchMinimum, SwatchMaximum, Entry.Swatch, Entry.Key);
        else
            Ink->AddRectFilled(SwatchMinimum, SwatchMaximum, ImGui::GetColorU32(Spectrum(0x080a08, Alpha)));
        Ink->PopClipRect();
        Ink->AddRect(SwatchMinimum, SwatchMaximum, ImGui::GetColorU32(Spectrum(0x000000, 0.25f * Alpha)), 8 * S);
        if (Enabled)
        {
            const float Radius = 7 * S;
            const ImVec2 BadgeCentre(SwatchMaximum.x - 4 * S - Radius, SwatchMinimum.y + 4 * S + Radius);
            Ink->AddCircleFilled(BadgeCentre, Radius, ImGui::GetColorU32(Spectrum(0x72d79d)));
            Ink->AddLine(ImVec2(BadgeCentre.x - 3 * S, BadgeCentre.y), ImVec2(BadgeCentre.x - 1 * S, BadgeCentre.y + 2.5f * S), ImGui::GetColorU32(Spectrum(0x082412)), 1.6f * S);
            Ink->AddLine(ImVec2(BadgeCentre.x - 1 * S, BadgeCentre.y + 2.5f * S), ImVec2(BadgeCentre.x + 3.5f * S, BadgeCentre.y - 3 * S), ImGui::GetColorU32(Spectrum(0x082412)), 1.6f * S);
        }
        Caption(ImVec2(TileMinimum.x + 8 * S, SwatchMaximum.y + 6 * S), Entry.Label, 10 * S, Enabled ? 0xdbf6e4 : Implemented ? 0xb8b9ad : 0x6b6b61);
        Caption(ImVec2(TileMinimum.x + 8 * S, SwatchMaximum.y + 20 * S), Implemented ? Entry.Hint : "Not yet native", 8 * S,
            Enabled ? 0x99c9ab : 0x828a7f);
        ImGui::PopID();

        if (++Column >= Columns) { Column = 0; RowOrigin.y += TileHeight + Gap; }
    }
    if (Column != 0) RowOrigin.y += TileHeight + Gap;
    ImGui::SetCursorScreenPos(ImVec2(RowOrigin.x, RowOrigin.y));
    ImGui::Dummy(ImVec2(1, 10 * S));
    // 🧩 1:1 with MeshMapGrid.jsx's `{!channels.length && <p className="bk-selection-empty">...}` empty state.
    if (Settings.Channels.empty())
    {
        Caption(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + 1 * S),
            "No maps enabled. Click a tile to start a bake.", 9.5f * S, 0xb8b9ad);
        ImGui::Dummy(ImVec2(1, 18 * S));
    }
    // 🧩 1:1 with MeshMapGrid.jsx's closing <details className="bk-map-research"> - the non-geometry bake
    // families (material channels, material/group IDs, lighting passes) the web reference explicitly says it
    // does *not* fake with a substitute map, plus its two external reference links. Collapsed by default,
    // matching <details> with no `open` attribute.
    if (ImGui::CollapsingHeader("Other bake families & requirements"))
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + Width - 8 * S);
        ImGui::TextColored(Spectrum(0x96958b), "Reviewed against Substance, Toolbag and Blender. These are not geometry-only outputs and are not substituted with fake maps.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(1, 8 * S));
        struct OtherFamily { const char *Label, *Types, *Requirement; };
        static const OtherFamily Families[]{
            {"Material channels", "Albedo, roughness / gloss, metalness, specular, emission, material opacity, transferred textures",
                "Require high-material / texture evaluation. Material patch mode exports the current procedural material separately."},
            {"Material / group IDs", "Material ID, collection / group ID",
                "Require preserved material slots or grouping metadata. Object and UV-island IDs are available above; they are not substituted for these IDs."},
            {"Lighting passes", "Combined / complete, diffuse, specular / glossy, indirect, transmission, shadow, environment",
                "Require a lighting/material integrator; AO is visibility, not baked scene lighting."},
        };
        for (const auto &Family : Families)
        {
            ImGui::TextColored(Spectrum(0xd0d0bb), "%s", Family.Label);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + Width - 8 * S);
            ImGui::TextColored(Spectrum(0xa8a89a), "%s", Family.Types);
            ImGui::TextColored(Spectrum(0x7d7d72), "%s", Family.Requirement);
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(1, 7 * S));
        }
        ImGui::TextColored(Spectrum(0x8fb3d6), "Toolbag map reference \xe2\x86\x97");
        ImGui::TextColored(Spectrum(0x8fb3d6), "Blender bake reference \xe2\x86\x97");
    }
    ImGui::Dummy(ImVec2(1, 1));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ASSETS DOCK (LEFT)
//------------------------------------------------------------------------------------------------------------------------

// 🧩 1:1 with BakingStudio.jsx's <aside className="bk-dock bk-assets"> + studioTheme.css's .bk-pane-heading/
// .bk-mode-switch/.bk-card chrome: a big light-weight "Baking" heading, a pill-shaped 3-way mode switch, then
// rounded gradient "Low UV targets"/"High source" cards with a big triangle-count numeral - not plain
// sequential ImGui text/buttons.
void BakingWorkspacePanel::ConstructAssetsDock()
{
    // 📝 Docked by PanelDockspace (see the constructor's Panels.Register call) exactly like Pattern/Stamp's
    // own panel callbacks - no SetNextWindowPos/Size or ImGui::Begin/End here; the content below starts
    // straight from the cursor PanelDockspace has already placed inside its own managed, dockable window.
    const float S = InterfaceScale;
    Controls.Scale = S;

    // .bk-pane-heading: h1 23px/weight-300 + small 8px muted subtitle, baseline-aligned.
    const ImVec2 HeadingOrigin = ImGui::GetCursorScreenPos();
    Caption(ImVec2(HeadingOrigin.x + 20 * S, HeadingOrigin.y + 6 * S), "Baking", 23 * S, 0xe8e8db, LightTypeface ? LightTypeface : RegularTypeface);
    // 📝 .bk-pane-heading is `justify-content: space-between; align-items: baseline`: the small subtitle sits
    // on the heading's own baseline at the pane's right edge, not stacked underneath the title.
    {
        const char *Subtitle = "Map-only workflow";
        const float SubtitleWidth = ImGui::GetFont()->CalcTextSizeA(8 * S * 1.302f, FLT_MAX, 0.f, Subtitle).x;
        Caption(ImVec2(HeadingOrigin.x + ImGui::GetContentRegionAvail().x - 20 * S - SubtitleWidth, HeadingOrigin.y + 24 * S), Subtitle, 8 * S, 0x727264);
    }
    ImGui::Dummy(ImVec2(1, 48 * S));

    // .bk-mode-switch: a pill capsule bar with three equal-width segments.
    {
        const float BarX = ImGui::GetCursorScreenPos().x + 6 * S, BarY = ImGui::GetCursorScreenPos().y;
        const float BarWidth = ImGui::GetContentRegionAvail().x - 12 * S, BarHeight = 30 * S;
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(ImVec2(BarX, BarY), ImVec2(BarX + BarWidth, BarY + BarHeight), ImGui::GetColorU32(Spectrum(0x0f0f0d)), BarHeight * .5f);
        Ink->AddRect(ImVec2(BarX, BarY), ImVec2(BarX + BarWidth, BarY + BarHeight), ImGui::GetColorU32(Spectrum(0xffffff, 0.03f)), BarHeight * .5f);
        const char *Modes[]{"Mesh maps", "Material patch", "Volume SDF"};
        const float SegmentWidth = (BarWidth - 6 * S) / 3.f;
        for (int Index = 0; Index < 3; ++Index)
        {
            const ImVec2 SegMin(BarX + 3 * S + Index * SegmentWidth, BarY + 3 * S), SegMax(SegMin.x + SegmentWidth - 2 * S, BarY + BarHeight - 3 * S);
            const bool Selected = int(Mode) == Index;
            ImGui::SetCursorScreenPos(SegMin);
            ImGui::PushID(Index);
            ImGui::InvisibleButton("Mode", ImVec2(SegMax.x - SegMin.x, SegMax.y - SegMin.y));
            // 📝 BakingStudio.jsx: mode buttons are `disabled={busy}`, and switching to Material patch /
            // Volume SDF also forces `setView("mesh")` - those modes have no 2D map stage to stay on.
            if (ImGui::IsItemClicked() && !BakeRunning.load())
            {
                Mode = StudioMode(Index);
                if (Index != 0) View = ViewMode::Preview3D;
            }
            Track(Index == 0 ? "Mode Mesh" : Index == 1 ? "Mode Material" : "Mode Volume");
            if (Selected) Ink->AddRectFilled(SegMin, SegMax, ImGui::GetColorU32(Spectrum(0x2a2a26)), (SegMax.y - SegMin.y) * .5f);
            Caption(ImVec2(SegMin.x + ((SegMax.x - SegMin.x) - CaptionWidth(Modes[Index], 9 * S)) * .5f, SegMin.y + ((SegMax.y - SegMin.y) - 9 * 1.15f * S) * .5f), Modes[Index],
                9 * S, Selected ? 0xdddec7 : 0x8d8d7e);
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(ImVec2(HeadingOrigin.x, BarY + BarHeight + 15 * S));
    }

    // 📝 .bk-asset-scroll { flex: 1; overflow: auto; }: everything from the SOURCE GEOMETRY caption through
    // the map grid scrolls inside its own child (window height minus the pinned 39px .bk-pane-footer), the
    // same arrangement ConstructInspector already uses. Without it the 27-tile grid overflowed straight past
    // the dock's bottom edge and under the footer instead of scrolling.
    const float PaneFooterHeight = 39 * S;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("AssetScroll", ImVec2(0, std::max(1.f, ImGui::GetContentRegionAvail().y - PaneFooterHeight)), false);
    ImGui::PopStyleVar();
    const ImVec2 SectionPosition = ImGui::GetCursorScreenPos();
    Caption(ImVec2(SectionPosition.x + 17 * S, SectionPosition.y + 2 * S), "SOURCE GEOMETRY", 8 * S, 0x8e8e7b);
    // 📝 .bk-section-caption is a space-between flex row: the source label is right-aligned on the same line.
    {
        const float SourceWidth = ImGui::GetFont()->CalcTextSizeA(7 * S * 1.302f, FLT_MAX, 0.f, Assets.Low.Source.c_str()).x;
        Caption(ImVec2(SectionPosition.x + ImGui::GetContentRegionAvail().x - 20 * S - SourceWidth, SectionPosition.y + 3 * S), Assets.Low.Source.c_str(), 7 * S, 0x666657);
    }
    ImGui::SetCursorScreenPos(ImVec2(SectionPosition.x + 8 * S, SectionPosition.y + 26 * S));
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - 16 * S, 1));
    ImGui::PopStyleVar();

    const auto LowSummary = SummarizeMesh(Assets.Low);
    Controls.BeginCard("", true);
    {
        auto *Ink = ImGui::GetWindowDrawList();
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Ink->AddRectFilled(ImVec2(Origin.x - 14 * S, Origin.y - 14 * S), ImVec2(Origin.x - 11 * S, Origin.y - 2 * S), ImGui::GetColorU32(Spectrum(0x8b9b82)), 1.5f * S);
        Caption(ImVec2(Origin.x, Origin.y - 12 * S), "Low UV targets", 11 * S, 0xd0d0bb);
        char ObjectsLabel[32]; std::snprintf(ObjectsLabel, sizeof(ObjectsLabel), "%u objects", LowSummary.Objects);
        // 📝 .bk-card h2 small is `margin-left: auto` - the object count hugs the card's right edge.
        {
            const float ObjectsWidth = ImGui::GetFont()->CalcTextSizeA(8 * S * 1.302f, FLT_MAX, 0.f, ObjectsLabel).x;
            Caption(ImVec2(Origin.x + ImGui::GetContentRegionAvail().x - ObjectsWidth, Origin.y - 11 * S), ObjectsLabel, 8 * S, 0x7a7a69);
        }
        ImGui::Dummy(ImVec2(1, 14 * S));
        // 📝 .bk-mesh-stat: a 22px Box glyph in #969a84 beside the big count (11px gap), not the bare numeral.
        const ImVec2 StatOrigin = ImGui::GetCursorScreenPos();
        {
            PixelSpace IconSurface;
            (void)IconSurface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
            GlyphSpace::Stroke(IconSurface,
                "M21 8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16Z"
                "M3.3 7 12 12l8.7-5M12 22V12",
                {StatOrigin.x, StatOrigin.y + 8 * S, 22 * S, 1.6f, SpectrumQuad(0x969a84)});
        }
        Caption(ImVec2(StatOrigin.x + 33 * S, StatOrigin.y), FormatCount(LowSummary.Triangles).c_str(), 24 * S, 0xd3d6bf, LightTypeface ? LightTypeface : RegularTypeface);
        Caption(ImVec2(StatOrigin.x + 33 * S, StatOrigin.y + 30 * S), LowSummary.HasUv ? "triangles · UV0 present" : "triangles · UV0 missing", 7 * S, 0x82846c);
        ImGui::Dummy(ImVec2(1, 48 * S));
        ImGui::BeginDisabled(BakeRunning.load());
        Controls.ControlHeight = 30;
        if (Controls.Button("Import low mesh", ImGui::GetContentRegionAvail().x, false, "M12 15V3M17 8l-5-5-5 5M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4", false))
            Info = "Mesh import (OBJ/GLB/PLY/STL) is not implemented in this native build yet; use a built-in study mesh.";
        ImGui::EndDisabled();
        ImGui::Dummy(ImVec2(1, 10 * S));
        for (const auto &Part : Assets.Low.Parts)
        {
            bool Checked = std::find(SelectedTargets.begin(), SelectedTargets.end(), Part.Name) != SelectedTargets.end();
            ImGui::BeginDisabled(BakeRunning.load());
            ImGui::PushID(Part.Name.c_str());
            // 🧩 Hand-drawn checkbox tinted to `.bk-target-list input { accent-color: #a3aa8a; }` - a muted
            // sage green - instead of ImGui::Checkbox, which paints its default bright blue CheckMark/FrameBg
            // since nothing in this panel overrides those colours for raw widgets.
            const float BoxSize = 14 * S;
            const ImVec2 BoxMinimum = ImGui::GetCursorScreenPos();
            const ImVec2 BoxMaximum(BoxMinimum.x + BoxSize, BoxMinimum.y + BoxSize);
            ImGui::InvisibleButton("CheckBox", ImVec2(BoxSize, BoxSize));
            const bool BoxClicked = ImGui::IsItemClicked();
            auto *CheckInk = ImGui::GetWindowDrawList();
            if (Checked)
            {
                CheckInk->AddRectFilled(BoxMinimum, BoxMaximum, ImGui::GetColorU32(Spectrum(0xa3aa8a)), 3 * S);
                PixelSpace IconSurface;
                (void)IconSurface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
                GlyphSpace::Stroke(IconSurface, "M20 6L9 17l-5-5", {BoxMinimum.x + 2 * S, BoxMinimum.y + 3.5f * S, 10 * S, 1.8f, SpectrumQuad(0x14160f)});
            }
            else
            {
                CheckInk->AddRectFilled(BoxMinimum, BoxMaximum, ImGui::GetColorU32(Spectrum(0x1a1a18)), 3 * S);
                CheckInk->AddRect(BoxMinimum, BoxMaximum, ImGui::GetColorU32(Spectrum(0xa3aa8a, .55f)), 3 * S);
            }
            ImGui::SameLine(0, 7 * S);
            ImGui::TextColored(Spectrum(0xb9bca3), "%s", Part.Name.c_str());
            if (BoxClicked)
            {
                Checked = !Checked;
                if (Checked) SelectedTargets.push_back(Part.Name);
                else SelectedTargets.erase(std::remove(SelectedTargets.begin(), SelectedTargets.end(), Part.Name), SelectedTargets.end());
            }
            ImGui::PopID();
            ImGui::EndDisabled();
            // 📝 .bk-target-list label is a flex row whose <span> takes flex:1: the bare (grouped) triangle
            // count sits at the row's right edge - not a "N tris" suffix trailing the part name.
            {
                const std::string CountLabel = FormatCount(Part.TriangleCount());
                const float CountWidth = ImGui::GetFont()->CalcTextSizeA(7 * S * 1.302f, FLT_MAX, 0.f, CountLabel.c_str()).x;
                const float RowRight = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
                Caption(ImVec2(RowRight - CountWidth, BoxMinimum.y + 3 * S), CountLabel.c_str(), 7 * S, 0x757967);
            }
        }
    }
    Controls.EndCard();

    const auto HighSummary = SummarizeMesh(Assets.High);
    Controls.BeginCard("", true);
    {
        auto *Ink = ImGui::GetWindowDrawList();
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Ink->AddRectFilled(ImVec2(Origin.x - 14 * S, Origin.y - 14 * S), ImVec2(Origin.x - 11 * S, Origin.y - 2 * S), ImGui::GetColorU32(Spectrum(0xa79171)), 1.5f * S);
        Caption(ImVec2(Origin.x, Origin.y - 12 * S), "High source", 11 * S, 0xd0d0bb);
        char ObjectsLabel[32]; std::snprintf(ObjectsLabel, sizeof(ObjectsLabel), "%u objects", HighSummary.Objects);
        {
            const float ObjectsWidth = ImGui::GetFont()->CalcTextSizeA(8 * S * 1.302f, FLT_MAX, 0.f, ObjectsLabel).x;
            Caption(ImVec2(Origin.x + ImGui::GetContentRegionAvail().x - ObjectsWidth, Origin.y - 11 * S), ObjectsLabel, 8 * S, 0x7a7a69);
        }
        ImGui::Dummy(ImVec2(1, 14 * S));
        const ImVec2 StatOrigin = ImGui::GetCursorScreenPos();
        {
            PixelSpace IconSurface;
            (void)IconSurface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
            GlyphSpace::Stroke(IconSurface,
                "M21 8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16Z"
                "M3.3 7 12 12l8.7-5M12 22V12",
                {StatOrigin.x, StatOrigin.y + 8 * S, 22 * S, 1.6f, SpectrumQuad(0x969a84)});
        }
        Caption(ImVec2(StatOrigin.x + 33 * S, StatOrigin.y), FormatCount(HighSummary.Triangles).c_str(), 24 * S, 0xd3d6bf, LightTypeface ? LightTypeface : RegularTypeface);
        Caption(ImVec2(StatOrigin.x + 33 * S, StatOrigin.y + 30 * S), "triangles · world transforms retained", 7 * S, 0x82846c);
        ImGui::Dummy(ImVec2(1, 48 * S));
        ImGui::BeginDisabled(BakeRunning.load());
        Controls.ControlHeight = 30;
        if (Controls.Button("Import high mesh", ImGui::GetContentRegionAvail().x, false, "M12 15V3M17 8l-5-5-5 5M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4", false))
            Info = "Mesh import (OBJ/GLB/PLY/STL) is not implemented in this native build yet; use a built-in study mesh.";
        ImGui::EndDisabled();
        ImGui::Dummy(ImVec2(1, 8 * S));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(Spectrum(0x868872), "OBJ · GLB / embedded glTF · PLY · high-only STL. No external asset requests.");
        ImGui::PopTextWrapPos();
    }
    Controls.EndCard();
    ImGui::EndGroup();

    Caption(ImVec2(ImGui::GetCursorScreenPos().x + 8 * S, ImGui::GetCursorScreenPos().y + 2 * S), "Built-in study", 9 * S, 0xa3a68c);
    ImGui::Dummy(ImVec2(1, 18 * S));
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + 8 * S, ImGui::GetCursorScreenPos().y));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 16 * S);
    ImGui::BeginDisabled(BakeRunning.load());
    // 🧩 DropdownControlSurface (the same themed PushSurface/BeginCombo/PopSurface wrapper every other
    // workspace's selects use via Controls.Dropdown), not a raw ImGui::BeginCombo - the latter falls back to
    // ImGui's own default blue frame/highlight colours since this panel never pushes a themed style block raw
    // widgets would otherwise inherit. Called directly (not through Controls.Dropdown) so the already-drawn
    // "Built-in study" caption above stays the only label, instead of Controls.Dropdown printing its own.
    if (DropdownControlSurface::BeginSelection("##Demo", DemoShape.c_str(), S))
    {
        for (const char *Shape : {"Teapot", "Cube", "Sphere"})
            if (DropdownControlSurface::Option(Shape, DemoShape == Shape, S)) SelectDemo(Shape);
        DropdownControlSurface::EndSelection();
    }
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(1, 18 * S));

    if (Mode == StudioMode::Mesh)
    {
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + 8 * S, ImGui::GetCursorScreenPos().y));
        ConstructMeshMapGrid(ImGui::GetContentRegionAvail().x - 16 * S);
    }
    ImGui::EndChild();

    // .bk-pane-footer: a fixed 39px strip, status dot + "CPU BVH / WebGL preview" on the left, texture-set
    // count on the right - pinned to the window's own bottom edge, not inline with the scrolling content above.
    {
        const ImVec2 WindowPosition = ImGui::GetWindowPos(), WindowSize = ImGui::GetWindowSize();
        const float FooterHeight = 39 * S;
        const ImVec2 FooterMinimum(WindowPosition.x, WindowPosition.y + WindowSize.y - FooterHeight), FooterMaximum(WindowPosition.x + WindowSize.x, WindowPosition.y + WindowSize.y);
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(FooterMinimum, FooterMaximum, ImGui::GetColorU32(Spectrum(0x151515)));
        Ink->AddLine(FooterMinimum, ImVec2(FooterMaximum.x, FooterMinimum.y), ImGui::GetColorU32(Spectrum(0xffffff, 0.02f)));
        Ink->AddCircleFilled(ImVec2(FooterMinimum.x + 16 * S, (FooterMinimum.y + FooterMaximum.y) * .5f), 2.5f * S, ImGui::GetColorU32(Spectrum(0x8aa27e)));
        Caption(ImVec2(FooterMinimum.x + 26 * S, (FooterMinimum.y + FooterMaximum.y) * .5f - 5 * S), "CPU BVH / WebGL preview", 8 * S, 0x777d66);
        char SetsLabel[32]; std::snprintf(SetsLabel, sizeof(SetsLabel), "%zu texture sets", Assets.Low.Parts.size());
        Caption(ImVec2(FooterMaximum.x - 16 * S - CaptionWidth(SetsLabel, 7 * S), (FooterMinimum.y + FooterMaximum.y) * .5f - 4 * S), SetsLabel, 7 * S, 0x686e59);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     3D ORBIT VIEWPORT
//------------------------------------------------------------------------------------------------------------------------

// 🧩 1:1 with MaterialLibraryPanel::SynchronizePreviewCamera: Camera (the drag/wheel target) eases toward
// SmoothedCamera every frame with the same exp(-dt*8) blend, so Baking's orbit camera glides exactly like
// Material's instead of snapping straight to the dragged value.
void BakingWorkspacePanel::SynchronizeCamera()
{
    if (!SmoothedCameraReady)
    {
        SmoothedCamera = Camera;
        SmoothedCameraReady = true;
        return;
    }
    SmoothedCamera.Shape = Camera.Shape;
    SmoothedCamera.Environment = Camera.Environment;
    SmoothedCamera.Exposure = Camera.Exposure;
    SmoothedCamera.LightIntensity = Camera.LightIntensity;
    const float Blend = 1 - std::exp(-std::min(ImGui::GetIO().DeltaTime, .1f) * 8);
    SmoothedCamera.Yaw = std::remainder(SmoothedCamera.Yaw + std::remainder(Camera.Yaw - SmoothedCamera.Yaw, 6.2831853f) * Blend, 6.2831853f);
    SmoothedCamera.Pitch += (Camera.Pitch - SmoothedCamera.Pitch) * Blend;
    SmoothedCamera.Zoom += (Camera.Zoom - SmoothedCamera.Zoom) * Blend;
    SmoothedCamera.PanX += (Camera.PanX - SmoothedCamera.PanX) * Blend;
    SmoothedCamera.PanY += (Camera.PanY - SmoothedCamera.PanY) * Blend;
    // 📝 Snap once the lag closes so the preview stops dirtying every frame and settles exactly on target.
    if (std::abs(std::remainder(Camera.Yaw - SmoothedCamera.Yaw, 6.2831853f)) < .0002f &&
        std::abs(Camera.Pitch - SmoothedCamera.Pitch) < .0002f && std::abs(Camera.Zoom - SmoothedCamera.Zoom) < .0002f &&
        std::abs(Camera.PanX - SmoothedCamera.PanX) < .0002f && std::abs(Camera.PanY - SmoothedCamera.PanY) < .0002f)
        SmoothedCamera = Camera;
}

void BakingWorkspacePanel::ConstructViewport3D(PlaneExtent Region)
{
    ImGui::InvisibleButton("##BakeViewport", ImVec2(Region.Width(), Region.Height()),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
    const bool Hovered = ImGui::IsItemHovered();
    const auto &Input = ImGui::GetIO();
    if (ImGui::IsItemActive())
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (Input.KeyShift && ImGui::IsMouseDown(ImGuiMouseButton_Left)))
        {
            const float Span = std::max(64.f, std::min(Region.Width(), Region.Height()));
            Camera.PanX = std::clamp(Camera.PanX + Input.MouseDelta.x * 2.f / Span, -2.f, 2.f);
            Camera.PanY = std::clamp(Camera.PanY + Input.MouseDelta.y * 2.f / Span, -2.f, 2.f);
        }
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
            Camera.Zoom = std::clamp(Camera.Zoom * std::exp(-Input.MouseDelta.y * .01f), .5f, 5.f);
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            Camera.Yaw = std::remainder(Camera.Yaw - Input.MouseDelta.x * 0.01f, 6.2831853f);
            Camera.Pitch = std::clamp(Camera.Pitch + Input.MouseDelta.y * 0.01f, -1.45f, 1.45f);
        }
    }
    if (Hovered && Input.MouseWheel != 0.f)
        Camera.Zoom = std::clamp(Camera.Zoom + Input.MouseWheel * 0.1f, .5f, 5.f);
    SynchronizeCamera();

    const BakePart *Part = nullptr;
    for (const auto &Candidate : Assets.Low.Parts)
        if (Candidate.Name == (PreviewSetName.empty() ? SelectedTargets.empty() ? "" : SelectedTargets.front() : PreviewSetName)) { Part = &Candidate; break; }
    if (!Part && !Assets.Low.Parts.empty()) Part = &Assets.Low.Parts.front();

    auto *DrawList = ImGui::GetWindowDrawList();
    DrawList->PushClipRect(ImVec2(Region.MinimumX, Region.MinimumY), ImVec2(Region.MaximumX, Region.MaximumY), true);
    DrawList->AddRectFilledMultiColor(
        ImVec2(Region.MinimumX, Region.MinimumY), ImVec2(Region.MaximumX, Region.MaximumY),
        ImGui::GetColorU32(Spectrum(0x18191a)), ImGui::GetColorU32(Spectrum(0x18191a)),
        ImGui::GetColorU32(Spectrum(0x27292b)), ImGui::GetColorU32(Spectrum(0x27292b)));

    if (Part && !Part->Indices.empty())
    {
        const std::string MeshKey = DemoShape + "|" + Part->Name + "|" +
            std::to_string(Part->Vertices.size()) + "|" + std::to_string(Part->Indices.size());
        if (MeshKey != CachedViewportMeshKey)
        {
            SurfaceGeometry Converted;
            Converted.Corners.reserve(Part->Vertices.size());
            float BoundsMin[3]{FLT_MAX, FLT_MAX, FLT_MAX}, BoundsMax[3]{-FLT_MAX, -FLT_MAX, -FLT_MAX};
            for (const auto &Vtx : Part->Vertices)
            {
                SurfaceCorner Corner{};
                Corner.Position = {Vtx.Position[0], Vtx.Position[1], Vtx.Position[2]};
                Corner.Normal = {Vtx.Normal[0], Vtx.Normal[1], Vtx.Normal[2]};
                Corner.Coordinates = {Vtx.Uv[0], Vtx.Uv[1]};
                Corner.Tangent = {0.f, 0.f, 0.f, 1.f};
                Converted.Corners.push_back(Corner);
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    BoundsMin[Axis] = std::min(BoundsMin[Axis], Vtx.Position[Axis]);
                    BoundsMax[Axis] = std::max(BoundsMax[Axis], Vtx.Position[Axis]);
                }
            }
            Converted.TriangleCorners.assign(Part->Indices.begin(), Part->Indices.end());
            CachedViewportGeometry = SanitizeMaterialSurfaceGeometry(std::move(Converted));
            CachedViewportPivot = {
                (BoundsMin[0] + BoundsMax[0]) * .5f,
                (BoundsMin[1] + BoundsMax[1]) * .5f,
                (BoundsMin[2] + BoundsMax[2]) * .5f};
            const float ExtX = BoundsMax[0] - CachedViewportPivot[0];
            const float ExtY = BoundsMax[1] - CachedViewportPivot[1];
            const float ExtZ = BoundsMax[2] - CachedViewportPivot[2];
            CachedViewportRadius = std::max(.05f, std::sqrt(ExtX * ExtX + ExtY * ExtY + ExtZ * ExtZ));
            CachedViewportMeshKey = MeshKey;
            if (ConfigureViewportGeometry) ConfigureViewportGeometry(CachedViewportGeometry);
        }

        const std::string MaterialKey = MeshKey + "|" + Preview3DMapKey + "|" + std::to_string(BakeResultsRevision);
        if (MaterialKey != CachedViewportMaterialKey)
        {
            const BakePartResult *PartResult = nullptr;
            for (const auto &Res : Results)
                if (Res.Name == Part->Name) { PartResult = &Res; break; }

            const auto FlipBakeRows = [](const BakeMapImage &Map, uint32_t Res) -> SurfaceTextureImage
            {
                SurfaceTextureImage Img{Res, Res, std::vector<uint8_t>(size_t(Res) * Res * 4, 255)};
                if (Res == 0 || Map.Rgba.size() < size_t(Res) * Res * 4) return Img;
                for (uint32_t Row = 0; Row < Res; ++Row)
                    std::memcpy(&Img.Rgba[size_t(Row) * Res * 4],
                        &Map.Rgba[size_t(Res - 1 - Row) * Res * 4], size_t(Res) * 4);
                return Img;
            };

            MaterialDefinition Def;
            Def.Name = "Baking Preview";
            Def.Scattering.Illumination.Configuration = {2, 0, 1, 0};
            auto &Base = Def.Scattering.Illumination.Strata[0];
            Base.Classification[0] = 0;
            Base.AlbedoRoughness = {.42f, .44f, .48f, .34f};
            Base.Optics = {.08f, 1.52f, 1.f, 0.f};
            auto &Coat = Def.Scattering.Illumination.Strata[1];
            Coat.Classification[0] = 1;
            Coat.AlbedoRoughness = {1.f, 1.f, 1.f, .14f};
            Coat.Optics = {0.f, 1.5f, .18f, 0.f};

            const BakeMapImage *SelectedMap = nullptr;
            const BakeMapImage *BakedNormalMap = nullptr;
            const BakeMapImage *BakedAoMap = nullptr;
            if (PartResult && PartResult->Resolution > 0)
            {
                for (const auto &Map : PartResult->Maps)
                {
                    if (!Preview3DMapKey.empty() && Map.Key == Preview3DMapKey) SelectedMap = &Map;
                    if (Map.Key == "normal") BakedNormalMap = &Map;
                    if (Map.Key == "ao") BakedAoMap = &Map;
                }
            }
            if (SelectedMap && PartResult)
            {
                Base.AlbedoRoughness = {1.f, 1.f, 1.f, .55f};
                Base.Optics = {0.f, 1.5f, 1.f, 0.f};
                Def.Scattering.DirectionalOptics[0][3] = .35f;
                Coat.Optics[2] = 0.f;
                Def.Scattering.Illumination.Configuration[0] = 1;
                Def.Scattering.TextureUse[0][0] |= 1u;
                Def.TextureSources[0] = "procedural/bake-preview-albedo";
                Def.ProceduralMaps[{0, "procedural/bake-preview-albedo"}] = FlipBakeRows(*SelectedMap, PartResult->Resolution);
            }
            else if (PartResult && (BakedNormalMap || BakedAoMap))
            {
                if (BakedNormalMap)
                {
                    Def.Scattering.TextureUse[0][0] |= 4u;
                    Def.TextureSources[2] = "procedural/bake-preview-normal";
                    Def.ProceduralMaps[{2, "procedural/bake-preview-normal"}] = FlipBakeRows(*BakedNormalMap, PartResult->Resolution);
                }
                if (BakedAoMap)
                {
                    auto AoFlipped = FlipBakeRows(*BakedAoMap, PartResult->Resolution);
                    for (size_t Px = 0; Px + 3 < AoFlipped.Rgba.size(); Px += 4)
                    {
                        AoFlipped.Rgba[Px + 1] = 255; // neutral roughness multiplier
                        AoFlipped.Rgba[Px + 2] = 255; // neutral metallic multiplier
                    }
                    Def.Scattering.TextureUse[0][0] |= 2u;
                    Def.TextureSources[1] = "procedural/bake-preview-orm";
                    Def.ProceduralMaps[{1, "procedural/bake-preview-orm"}] = std::move(AoFlipped);
                }
            }

            CachedViewportMaterial = CompileSurfaceMaterial(Def, std::filesystem::current_path());
            CachedViewportMaterialKey = MaterialKey;
            if (ConfigureViewportMaterial) ConfigureViewportMaterial(CachedViewportMaterial);
        }

        const float ViewW = std::max(1.f, Region.Width());
        const float ViewH = std::max(1.f, Region.Height());
        const float FbScale = std::max(1.f, Input.DisplayFramebufferScale.x);
        const uint32_t TargetW = std::clamp((uint32_t(ViewW * FbScale * 1.5f) + 63u) / 64u * 64u, 256u, 2048u);
        const uint32_t TargetH = std::clamp((uint32_t(ViewH * FbScale * 1.5f) + 63u) / 64u * 64u, 256u, 2048u);
        if (ConfigureViewportExtent && (TargetW != SentViewportWidth || TargetH != SentViewportHeight))
        {
            SentViewportWidth = TargetW;
            SentViewportHeight = TargetH;
            ConfigureViewportExtent(TargetW, TargetH);
        }

        WorkspaceViewportOrbitCamera Orbit{};
        Orbit.Pivot = CachedViewportPivot;
        Orbit.Radius = CachedViewportRadius;
        Orbit.FrameRatio = 5.8f / 1.35f;
        Orbit.Yaw = SmoothedCamera.Yaw;
        Orbit.Pitch = SmoothedCamera.Pitch;
        Orbit.Zoom = SmoothedCamera.Zoom;
        Orbit.PanX = SmoothedCamera.PanX;
        Orbit.PanY = SmoothedCamera.PanY;
        if (!SentViewportCameraReady || SentViewportCamera != Orbit)
        {
            SentViewportCamera = Orbit;
            SentViewportCameraReady = true;
            if (ConfigureViewportCamera) ConfigureViewportCamera(Orbit);
        }

        const WorkspaceViewportImage GpuImage = QueryViewport ? QueryViewport() : WorkspaceViewportImage{};
        if (GpuImage.Texture)
        {
            DrawList->AddImage(ImTextureRef(GpuImage.Texture),
                ImVec2(Region.MinimumX, Region.MinimumY), ImVec2(Region.MaximumX, Region.MaximumY),
                ImVec2(GpuImage.MinimumU, GpuImage.MinimumV), ImVec2(GpuImage.MaximumU, GpuImage.MaximumV));
        }
        else if (!QueryViewport)
        {
            const float Aspect = ViewW / ViewH;
            const uint32_t CpuH = std::clamp(uint32_t(ViewH * .75f), 200u, 480u);
            const uint32_t CpuW = std::clamp(uint32_t(std::lround(float(CpuH) * Aspect)), 200u, 640u);
            if (CachedCpuRgba.empty() || CachedCpuStateKey != CachedViewportMaterialKey ||
                CachedCpuOrbit != Orbit || CachedCpuWidth != CpuW || CachedCpuHeight != CpuH)
            {
                WorkspaceSurfaceViewportRequest Request{};
                Request.Geometry = &CachedViewportGeometry;
                Request.Material = &CachedViewportMaterial;
                Request.Camera = ConstructWorkspaceOrbitProjection(Orbit, Aspect);
                Request.LightDirection = ConstructWorkspaceOrbitLightDirection(Orbit);
                Request.LightIntensity = 2.25f;
                Request.EnvironmentStrength = 1.f;
                Request.EnvironmentUp = ConstructWorkspaceOrbitEnvironmentUp(Orbit);
                Request.Exposure = 1.f;
                auto Rendered = RenderWorkspaceSurfaceViewportCpu(Request, CpuW, CpuH);
                CachedCpuRgba = std::move(Rendered.Rgba);
                CachedCpuStateKey = CachedViewportMaterialKey;
                CachedCpuOrbit = Orbit;
                CachedCpuWidth = CpuW;
                CachedCpuHeight = CpuH;
            }
            BlitWorkspaceViewportCpuFrame(DrawList, {CachedCpuWidth, CachedCpuHeight, CachedCpuRgba},
                ImVec2(Region.MinimumX, Region.MinimumY), ImVec2(Region.MaximumX, Region.MaximumY),
                ImVec2(Region.MinimumX, Region.MinimumY), ImVec2(Region.MaximumX, Region.MaximumY));
        }

        if (Wireframe)
        {
            const auto Proj = ConstructWorkspaceOrbitProjection(Orbit, ViewW / ViewH);
            const auto &Clip = Proj.ClipFromSurface;
            const auto ProjectVert = [&](const std::array<float, 3> &Pos, ImVec2 &OutScreen) -> bool
            {
                const float Cx = Clip[0] * Pos[0] + Clip[4] * Pos[1] + Clip[8] * Pos[2] + Clip[12];
                const float Cy = Clip[1] * Pos[0] + Clip[5] * Pos[1] + Clip[9] * Pos[2] + Clip[13];
                const float Cz = Clip[2] * Pos[0] + Clip[6] * Pos[1] + Clip[10] * Pos[2] + Clip[14];
                const float Cw = Clip[3] * Pos[0] + Clip[7] * Pos[1] + Clip[11] * Pos[2] + Clip[15];
                if (Cw <= 1e-4f) return false;
                const float InvW = 1.f / Cw;
                if (Cz * InvW < -.2f || Cz * InvW > 1.2f) return false;
                OutScreen = ImVec2(
                    Region.MinimumX + (Cx * InvW * .5f + .5f) * ViewW,
                    Region.MinimumY + (Cy * InvW * .5f + .5f) * ViewH);
                return true;
            };
            for (size_t Tri = 0; Tri + 2 < CachedViewportGeometry.TriangleCorners.size(); Tri += 3)
            {
                const auto &A = CachedViewportGeometry.Corners[CachedViewportGeometry.TriangleCorners[Tri + 0]].Position;
                const auto &B = CachedViewportGeometry.Corners[CachedViewportGeometry.TriangleCorners[Tri + 1]].Position;
                const auto &C = CachedViewportGeometry.Corners[CachedViewportGeometry.TriangleCorners[Tri + 2]].Position;
                ImVec2 Sa, Sb, Sc;
                if (ProjectVert(A, Sa) && ProjectVert(B, Sb) && ProjectVert(C, Sc))
                {
                    const float SignedArea = (Sb.x - Sa.x) * (Sc.y - Sa.y) - (Sb.y - Sa.y) * (Sc.x - Sa.x);
                    if (SignedArea < 0.f) DrawList->AddTriangle(Sa, Sb, Sc, IM_COL32(220, 226, 214, 72), 1.f);
                }
            }
        }
    }
    else DrawList->AddText(ImVec2(Region.MinimumX + 12, Region.MinimumY + 12), ImGui::GetColorU32(Spectrum(0x716f62)), "No mesh selected");
    DrawList->PopClipRect();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     2D MAP PREVIEW (BLOCK RASTER)
//------------------------------------------------------------------------------------------------------------------------

// 📝 Draws the selected bake result directly from its real RGBA8 pixels via a coarse block grid (ImGui's draw
// list, same technique as the 3D viewport) — a genuine representation of the baked data without requiring a
// new GPU-texture upload path. ExportResults() below writes the full-resolution PNGs to disk for pixel-exact use.
// 📝 1:1 with .bk-map-image's `repeating-conic-gradient(#23251f 0% 25%, #191c16 0% 50%) 50% / 16px 16px`
// transparency checkerboard plus its 1px #ffffff0d / 5px-radius border - the reference never shows a baked
// map floating on a flat panel background, so neither does this. Returns false (and draws nothing) when
// PreviewSetName/PreviewMapKey resolve to no map yet; the caller substitutes the full .bk-empty screen instead.
bool BakingWorkspacePanel::ConstructMapPreview(PlaneExtent Region)
{
    auto *DrawList = ImGui::GetWindowDrawList();
    const BakeMapImage *Image = nullptr;
    uint32_t Resolution = 0;
    // 📝 BakingStudio.jsx resolves the on-screen preview as `visiblePreviews.find(set && key) ||
    // visiblePreviews.find(set)` against the active set (falling back to the first baked set): switching to a
    // set that never baked the chosen map shows that set's first map instead of a blank stage.
    const BakePartResult *ActiveResult = nullptr;
    for (const auto &Result : Results)
        if (Result.Name == PreviewSetName) { ActiveResult = &Result; break; }
    if (!ActiveResult && !Results.empty()) ActiveResult = &Results.front();
    if (ActiveResult)
    {
        Resolution = ActiveResult->Resolution;
        for (const auto &Map : ActiveResult->Maps)
            if (Map.Key == PreviewMapKey) { Image = &Map; break; }
        if (!Image && !ActiveResult->Maps.empty()) Image = &ActiveResult->Maps.front();
    }
    if (!Image) return false;
    const float CheckerCell = 16 * InterfaceScale;
    DrawList->PushClipRect(ImVec2(Region.MinimumX, Region.MinimumY), ImVec2(Region.MaximumX, Region.MaximumY), true);
    for (float Cy = Region.MinimumY; Cy < Region.MaximumY; Cy += CheckerCell)
        for (float Cx = Region.MinimumX; Cx < Region.MaximumX; Cx += CheckerCell)
        {
            const int Parity = (int(std::round((Cx - Region.MinimumX) / CheckerCell)) + int(std::round((Cy - Region.MinimumY) / CheckerCell))) & 1;
            DrawList->AddRectFilled(ImVec2(Cx, Cy), ImVec2(Cx + CheckerCell, Cy + CheckerCell), ImGui::GetColorU32(Spectrum(Parity ? 0x191c16 : 0x23251f)));
        }
    constexpr uint32_t Blocks = 96; // coarse in-app preview grid; ExportResults() keeps the real resolution
    const float CellWidth = Region.Width() / float(Blocks), CellHeight = Region.Height() / float(Blocks);
    for (uint32_t By = 0; By < Blocks; ++By)
        for (uint32_t Bx = 0; Bx < Blocks; ++Bx)
        {
            const uint32_t SampleX = std::min(Resolution - 1, uint32_t((float(Bx) + 0.5f) / float(Blocks) * float(Resolution)));
            const uint32_t SampleY = std::min(Resolution - 1, uint32_t((float(By) + 0.5f) / float(Blocks) * float(Resolution)));
            const size_t Offset = (size_t(SampleY) * Resolution + SampleX) * 4;
            // 📝 Keep the texel's real alpha (and skip fully transparent cells outright): PNG alpha is zero
            // outside low coverage, so the checkerboard painted above is what should read there - forcing
            // alpha 255 both hid that and wasted a filled rect per transparent cell every frame.
            const uint8_t TexelAlpha = Image->Rgba[Offset + 3];
            if (!TexelAlpha) continue;
            const ImU32 Colour = IM_COL32(Image->Rgba[Offset + 0], Image->Rgba[Offset + 1], Image->Rgba[Offset + 2], TexelAlpha);
            const float X0 = Region.MinimumX + Bx * CellWidth, Y0 = Region.MinimumY + By * CellHeight;
            DrawList->AddRectFilled(ImVec2(X0, Y0), ImVec2(X0 + CellWidth + 0.5f, Y0 + CellHeight + 0.5f), Colour);
        }
    DrawList->PopClipRect();
    DrawList->AddRect(ImVec2(Region.MinimumX, Region.MinimumY), ImVec2(Region.MaximumX, Region.MaximumY), ImGui::GetColorU32(Spectrum(0xffffff, 0.05f)), 5 * InterfaceScale);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PREVIEW DOCK (CENTER)
//------------------------------------------------------------------------------------------------------------------------

// 🧩 1:1 with BakingStudio.jsx's <section className="bk-dock bk-preview"> + .bk-preview-toolbar/.bk-view-tabs/
// .bk-mesh-toolbar chrome: a 73px title strip (16px/weight-300 part name + muted subtitle, pill 3D/2D tabs on
// the right) and, in 3D mode, a second Frame/overlay/wireframe toolbar row - not inline SmallButtons.
void BakingWorkspacePanel::ConstructPreviewDock()
{
    // 📝 Docked by PanelDockspace, same contract as ConstructAssetsDock/ConstructInspector - no manual
    // SetNextWindowPos/Size or Begin/End. The preview dock's background is a slightly lighter 0x141414 than
    // the shared 0x121212 the dockspace's own windows paint (studioTheme.css's .bk-preview is one step
    // brighter than its siblings), so repaint that one rect explicitly instead of a WindowBg style push -
    // a push here would land after PanelDockspace's own ImGui::Begin() already baked this frame's background.
    const float S = InterfaceScale;
    Controls.Scale = S;
    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(),
        ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
        ImGui::GetColorU32(Spectrum(0x141414)));

    // .bk-preview-toolbar
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float ToolbarHeight = 73 * S, ToolbarWidth = ImGui::GetContentRegionAvail().x;
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(Origin, ImVec2(Origin.x + ToolbarWidth, Origin.y + ToolbarHeight), ImGui::GetColorU32(Spectrum(0x151515)));
        Ink->AddLine(ImVec2(Origin.x, Origin.y + ToolbarHeight), ImVec2(Origin.x + ToolbarWidth, Origin.y + ToolbarHeight), ImGui::GetColorU32(Spectrum(0xffffff, 0.02f)));
        const std::string PartLabel = PreviewSetName.empty() ? Assets.Low.Label : PreviewSetName;
        Caption(ImVec2(Origin.x + 19 * S, Origin.y + 20 * S), PartLabel.c_str(), 16 * S, 0xd0d4be, LightTypeface ? LightTypeface : RegularTypeface);
        const char *Subtitle = Mode == StudioMode::Volume ? "High mesh · 3D distance volume" : Mode == StudioMode::Material ? "Procedural flat-patch export" :
            SettingsStale() ? "Previous bake · settings changed" : "Mesh-map authoring";
        Caption(ImVec2(Origin.x + 19 * S, Origin.y + 45 * S), Subtitle, 8 * S, 0x7d856b);

        const float TabsWidth = 160 * S, TabsHeight = 34 * S;
        const ImVec2 TabsMinimum(Origin.x + ToolbarWidth - 19 * S - TabsWidth, Origin.y + (ToolbarHeight - TabsHeight) * .5f);
        Ink->AddRectFilled(TabsMinimum, ImVec2(TabsMinimum.x + TabsWidth, TabsMinimum.y + TabsHeight), ImGui::GetColorU32(Spectrum(0x0f100e)), TabsHeight * .5f);
        Ink->AddRect(TabsMinimum, ImVec2(TabsMinimum.x + TabsWidth, TabsMinimum.y + TabsHeight), ImGui::GetColorU32(Spectrum(0xffffff, 0.03f)), TabsHeight * .5f);
        const char *Tabs[]{"3D preview", "2D maps"};
        const float TabWidth = (TabsWidth - 8 * S) / 2.f;
        for (int Index = 0; Index < 2; ++Index)
        {
            const ImVec2 TabMinimum(TabsMinimum.x + 4 * S + Index * TabWidth, TabsMinimum.y + 4 * S);
            const ImVec2 TabMaximum(TabMinimum.x + TabWidth - 2 * S, TabsMinimum.y + TabsHeight - 4 * S);
            ImGui::SetCursorScreenPos(TabMinimum);
            ImGui::PushID(100 + Index);
            ImGui::InvisibleButton("Tab", ImVec2(TabMaximum.x - TabMinimum.x, TabMaximum.y - TabMinimum.y));
            const bool Selected = (Index == 0) == (View == ViewMode::Preview3D);
            if (ImGui::IsItemClicked()) View = Index == 0 ? ViewMode::Preview3D : ViewMode::Maps2D;
            Track(Index == 0 ? "View3D" : "View2D");
            if (Selected) Ink->AddRectFilled(TabMinimum, TabMaximum, ImGui::GetColorU32(Spectrum(0x2c3025)), (TabMaximum.y - TabMinimum.y) * .5f);
            Caption(ImVec2(TabMinimum.x + ((TabMaximum.x - TabMinimum.x) - CaptionWidth(Tabs[Index], 9 * S)) * .5f, TabMinimum.y + ((TabMaximum.y - TabMinimum.y) - 9 * 1.15f * S) * .5f), Tabs[Index],
                9 * S, Selected ? 0xdce4c9 : 0x8c907b);
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + ToolbarHeight));
    }

    if (View == ViewMode::Preview3D)
    {
        // .bk-mesh-toolbar
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float RowHeight = 38 * S, RowWidth = ImGui::GetContentRegionAvail().x;
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(Origin, ImVec2(Origin.x + RowWidth, Origin.y + RowHeight), ImGui::GetColorU32(Spectrum(0x151515)));
        float CursorX = Origin.x + 18 * S;
        const auto ToolbarButton = [&](const char *Label, bool Active, bool Toggle, const char *IconPath = nullptr) -> bool
        {
            const float IconGutter = IconPath ? 15 * S : 0.f;
            const float ButtonWidth = CaptionWidth(Label, 8 * S) + 18 * S + IconGutter, ButtonHeight = 24 * S;
            const ImVec2 Minimum(CursorX, Origin.y + (RowHeight - ButtonHeight) * .5f), Maximum(Minimum.x + ButtonWidth, Minimum.y + ButtonHeight);
            ImGui::SetCursorScreenPos(Minimum);
            ImGui::PushID(Label);
            ImGui::InvisibleButton("Mesh", ImVec2(ButtonWidth, ButtonHeight));
            const bool Clicked = ImGui::IsItemClicked();
            if (Toggle && Active) Ink->AddRectFilled(Minimum, Maximum, ImGui::GetColorU32(Spectrum(0x33392b)), 6 * S);
            const uint32_t LabelColour = Toggle && Active ? 0xd0dfb8 : 0xa8ab98;
            if (IconPath)
            {
                PixelSpace IconSurface;
                (void)IconSurface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
                GlyphSpace::Stroke(IconSurface, IconPath, {Minimum.x + 9 * S, Minimum.y + (ButtonHeight - 11 * S) * .5f, 11 * S, 1.7f, SpectrumQuad(LabelColour)});
            }
            Caption(ImVec2(Minimum.x + 9 * S + IconGutter, Minimum.y + (ButtonHeight - 8 * S) * .4f), Label, 8 * S, LabelColour);
            ImGui::PopID();
            CursorX += ButtonWidth + 6 * S;
            return Clicked;
        };
        if (ToolbarButton("Frame", false, false, "M15 3h6v6M9 21H3v-6M21 3l-7 7M3 21l7-7")) ResetCamera();
        if (ToolbarButton("High overlay", Overlay, true)) Overlay = !Overlay;
        if (ToolbarButton("Wireframe", Wireframe, true)) Wireframe = !Wireframe;

        // 🧩 Native-only addition (no .bk-mesh-toolbar counterpart - the web reference always hardwires the
        // bevel/tangent normal onto the 3D mesh with no picker): lets the user actually see a baked map's real
        // texels painted on the model, not just in the flat 2D grid. Only offers maps this part has actually
        // baked; falls back to "Shaded" the moment that set has nothing baked yet.
        {
            const std::string ActiveSetName = PreviewSetName.empty() ? (SelectedTargets.empty() ? std::string() : SelectedTargets.front()) : PreviewSetName;
            const BakePartResult *ActiveResult = nullptr;
            for (const auto &Result : Results)
                if (Result.Name == ActiveSetName) { ActiveResult = &Result; break; }
            const bool HasBakedMaps = ActiveResult && !ActiveResult->Maps.empty();
            if (!HasBakedMaps) Preview3DMapKey.clear();
            ImGui::SetCursorScreenPos(ImVec2(CursorX, Origin.y + (RowHeight - 24 * S) * .5f));
            ImGui::BeginDisabled(!HasBakedMaps);
            ImGui::SetNextItemWidth(142 * S);
            const auto *Current3DMeta = FindMeshMap(Preview3DMapKey);
            const char *Current3DLabel = Preview3DMapKey.empty() ? "Shaded" : (Current3DMeta ? Current3DMeta->Label : Preview3DMapKey.c_str());
            const bool Preview3DDropdownOpen = DropdownControlSurface::BeginSelection("##Preview3DTexture", Current3DLabel, S);
            // 📝 Track the combo's own closed-state trigger item immediately, before any Option() rows below
            // can overwrite ImGui's LastItemData - QueryControlExtent("Preview3DTexture") must always resolve
            // to the clickable dropdown button itself, open or closed, for headless evidence to find it.
            Track("Preview3DTexture");
            if (Preview3DDropdownOpen)
            {
                if (DropdownControlSurface::Option("Shaded", Preview3DMapKey.empty(), S)) Preview3DMapKey.clear();
                if (ActiveResult)
                    for (const auto &Map : ActiveResult->Maps)
                    {
                        const auto *Meta = FindMeshMap(Map.Key);
                        if (DropdownControlSurface::Option(Meta ? Meta->Label : Map.Key.c_str(), Map.Key == Preview3DMapKey, S)) Preview3DMapKey = Map.Key;
                    }
                DropdownControlSurface::EndSelection();
            }
            ImGui::EndDisabled();
            Controls.Tooltip("Preview texture", "Paints the 3D model with a baked map's real pixels instead of the plain shaded surface.");
            CursorX += 142 * S + 6 * S;
        }
        const char *Hint = "Orbit · scroll to zoom";
        const float HintWidth = CaptionWidth(Hint, 7 * S);
        if (Origin.x + RowWidth - 18 * S - HintWidth > CursorX + 8 * S)
            Caption(ImVec2(Origin.x + RowWidth - 18 * S - HintWidth, Origin.y + (RowHeight - 7 * S) * .5f), Hint, 7 * S, 0x697459);
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + RowHeight));

        const ImVec2 CursorScreen = ImGui::GetCursorScreenPos();
        const ImVec2 Available = ImGui::GetContentRegionAvail();
        const float ViewportHeight = std::max(80.f, Available.y - 92.f * InterfaceScale);
        // 📝 ConstructViewport3D's own ImGui::InvisibleButton reserves exactly (Available.x, ViewportHeight) of
        // layout space starting at the CURRENT cursor position - the Region passed in must start there too
        // (no manual offset) and no extra Dummy() may follow, or the reservation doubles and shoves the Bake
        // button/results/status strip below the window.
        ConstructViewport3D(Spanning(CursorScreen.x, CursorScreen.y, Available.x, ViewportHeight));
    }
    else
    {
        // .bk-map-toolbar: 11px/18px padded row - the set/map pickers on the left, a right-aligned icon-only
        // PNG download button for whichever single map is on screen (studioTheme.css's `margin-left: auto`
        // button), only present once a map actually exists to download - not a bare two-combo row.
        const ImVec2 ToolbarOrigin = ImGui::GetCursorScreenPos();
        const float ToolbarRowHeight = 11 * S + 30 * S + 11 * S;
        ImGui::SetCursorScreenPos(ImVec2(ToolbarOrigin.x + 18 * S, ToolbarOrigin.y + 11 * S));
        // 📝 BakingStudio.jsx derives both pickers from the baked previews themselves: sets are the distinct
        // baked sets, maps are the keys the ACTIVE set actually baked, and both selects are
        // `disabled={!visiblePreviews.length}`. (The previous native pass fed the map picker from
        // Settings.Channels, so it happily offered maps that had never been baked - and hid baked ones whose
        // tile had since been toggled off.)
        const auto ResolveActiveResult = [&]() -> const BakePartResult *
        {
            for (const auto &Result : Results)
                if (Result.Name == PreviewSetName) return &Result;
            return Results.empty() ? nullptr : &Results.front();
        };
        const BakePartResult *ActiveResult = ResolveActiveResult();
        ImGui::SetNextItemWidth(170 * S);
        // 🧩 Themed dropdowns (DropdownControlSurface), not raw ImGui::BeginCombo - see the "Built-in study"
        // dropdown above for why.
        ImGui::BeginDisabled(Results.empty());
        if (DropdownControlSurface::BeginSelection("##Set", ActiveResult ? ActiveResult->Name.c_str() : "", S))
        {
            for (const auto &Result : Results)
                if (DropdownControlSurface::Option(Result.Name.c_str(), ActiveResult && Result.Name == ActiveResult->Name, S)) PreviewSetName = Result.Name;
            DropdownControlSurface::EndSelection();
        }
        ImGui::EndDisabled();
        ActiveResult = ResolveActiveResult();
        const BakeMapImage *CurrentImage = nullptr;
        uint32_t CurrentResolution = 0;
        std::string ActiveMapKey = PreviewMapKey;
        if (ActiveResult)
        {
            CurrentResolution = ActiveResult->Resolution;
            for (const auto &Map : ActiveResult->Maps)
                if (Map.Key == PreviewMapKey) { CurrentImage = &Map; break; }
            if (!CurrentImage && !ActiveResult->Maps.empty()) CurrentImage = &ActiveResult->Maps.front();
            if (CurrentImage) ActiveMapKey = CurrentImage->Key;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(170 * S);
        ImGui::BeginDisabled(Results.empty());
        if (DropdownControlSurface::BeginSelection("##Map", FindMeshMap(ActiveMapKey) ? FindMeshMap(ActiveMapKey)->Label : ActiveMapKey.c_str(), S))
        {
            if (ActiveResult)
                for (const auto &Map : ActiveResult->Maps)
                {
                    const auto *Meta = FindMeshMap(Map.Key);
                    if (DropdownControlSurface::Option(Meta ? Meta->Label : Map.Key.c_str(), Map.Key == ActiveMapKey, S))
                    {
                        PreviewMapKey = Map.Key;
                        ActiveMapKey = Map.Key;
                        CurrentImage = &Map;
                    }
                }
            DropdownControlSurface::EndSelection();
        }
        ImGui::EndDisabled();
        if (CurrentImage)
        {
            const float ButtonSize = 26 * S;
            const float WindowRight = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x;
            ImGui::SetCursorScreenPos(ImVec2(WindowRight - 18 * S - ButtonSize, ToolbarOrigin.y + 11 * S + 2 * S));
            Controls.ControlHeight = 26;
            if (Controls.Button("Download this map as PNG", ButtonSize, false,
                "M12 15V3M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M7 10l5 5 5-5", true))
            {
                try
                {
                    std::filesystem::path Directory = ExportDirectory.empty() ? std::filesystem::temp_directory_path() / "FrontierBakeExport" : ExportDirectory;
                    std::filesystem::create_directories(Directory);
                    const auto Path = Directory / ((ActiveResult ? ActiveResult->Name : PreviewSetName) + "-" + ActiveMapKey + ".png");
                    if (stbi_write_png(Path.string().c_str(), int(CurrentResolution), int(CurrentResolution), 4, CurrentImage->Rgba.data(), int(CurrentResolution) * 4))
                    {
                        LastExportDirectory = Directory.string();
                        Info = "Map PNG written to " + Path.string();
                    }
                }
                catch (const std::exception &Failure) { Error = std::string("PNG export failed: ") + Failure.what(); }
            }
            Controls.Tooltip("Download this map as PNG", "Writes just the map on screen, not the whole bake's set.");
            Track("DownloadMapPNG");
        }
        ImGui::SetCursorScreenPos(ImVec2(ToolbarOrigin.x, ToolbarOrigin.y + ToolbarRowHeight));

        const ImVec2 CursorScreen = ImGui::GetCursorScreenPos();
        const ImVec2 Available = ImGui::GetContentRegionAvail();
        // 📝 Leave the same ~92*S margin below the image as the 3D branch leaves below its viewport, so the
        // Bake/Export row and result captions that follow (outside this if/else) always have room instead of
        // being clipped against the window's bottom edge.
        if (CurrentImage)
        {
            // .bk-map-image: 17px padding around a checkerboard-backed, bordered square, with a caption line
            // below it ("{res} × {res} · {colorSpace} · current/previous bake") - not a bare edge-to-edge raster.
            const float ImageSize = std::max(64.f, std::min(Available.x - 34.f * S, Available.y - 92.f * S - 26.f * S) - 8.f);
            const ImVec2 ImageOrigin(CursorScreen.x + (Available.x - ImageSize) * .5f, CursorScreen.y + 17 * S);
            ConstructMapPreview(Spanning(ImageOrigin.x, ImageOrigin.y, ImageSize, ImageSize));
            const bool Stale = SettingsStale();
            const char *ColourSpace = ActiveMapKey == "base-color" ? "sRGB" : "linear";
            char CaptionLine[160];
            std::snprintf(CaptionLine, sizeof(CaptionLine), "%u × %u · %s · %s", CurrentResolution, CurrentResolution, ColourSpace, Stale ? "previous recipe" : "current bake");
            Caption(ImVec2(CursorScreen.x + (Available.x - CaptionWidth(CaptionLine, 9 * S)) * .5f, ImageOrigin.y + ImageSize + 12 * S), CaptionLine, 9 * S, 0x7c8a6a);
            ImGui::Dummy(ImVec2(Available.x, ImageSize + 17 * S + 26 * S));
        }
        else
        {
            // .bk-empty: centred stack icon + "No baked maps yet" + the same explainer paragraph as the web
            // reference, filling the whole stage instead of a one-line draw-list caption in the corner.
            const float StageHeight = std::max(120.f, Available.y - 92.f * S);
            const ImVec2 StageCentre(CursorScreen.x + Available.x * .5f, CursorScreen.y + StageHeight * .5f);
            auto *Ink = ImGui::GetWindowDrawList();
            PixelSpace IconSurface;
            (void)IconSurface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
            GlyphSpace::Stroke(IconSurface, "M12 2L2 7L12 12L22 7ZM2 17L12 22L22 17M2 12L12 17L22 12",
                {StageCentre.x - 15 * S, StageCentre.y - 58 * S, 30 * S, 1.6f, SpectrumQuad(0x748463)});
            const char *Heading = "No baked maps yet";
            auto *HeadingFont = LightTypeface ? LightTypeface : RegularTypeface;
            Caption(ImVec2(StageCentre.x - CaptionWidth(Heading, 16 * S, HeadingFont) * .5f, StageCentre.y - 16 * S), Heading, 16 * S, 0xbdcba9, HeadingFont);
            ImGui::SetCursorScreenPos(ImVec2(StageCentre.x - 190 * S, StageCentre.y + 10 * S));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380 * S);
            ImGui::PushStyleColor(ImGuiCol_Text, Spectrum(0x748463));
            ImGui::TextWrapped("Choose your low UV targets and high source, then bake the selected maps. "
                "Material-patch mode downloads its own six-map ZIP.");
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            (void)Ink;
            ImGui::SetCursorScreenPos(CursorScreen);
            ImGui::Dummy(ImVec2(Available.x, StageHeight));
        }
    }

    ImGui::Dummy(ImVec2(1, 10 * S));
    if (BakeRunning.load())
    {
        // 🧩 1:1 with .bk-progress: a left-aligned status <span>, a thin green pill <progress> (accent-color:
        // #94aa7a in studioTheme.css), not ImGui::ProgressBar's default bright-yellow ImGuiCol_PlotHistogram
        // bar - this was the one remaining raw-ImGui-default-colour control left in the panel.
        const uint64_t Done = BakeDone.load(), Total = std::max<uint64_t>(1, BakeTotal.load());
        const float Fraction = std::clamp(float(double(Done) / double(Total)), 0.f, 1.f);
        char Counter[48];
        std::snprintf(Counter, sizeof(Counter), "%s %llu/%llu", BakeProgressLabel.c_str(), (unsigned long long)Done, (unsigned long long)Total);
        const auto LabelExtent = ImGui::CalcTextSize(Counter);
        const float BarHeight = 4 * S, RowHeight = std::max(BarHeight, LabelExtent.y);
        ImGui::TextColored(Spectrum(0xa9b993), "%s", Counter);
        ImGui::SameLine(0, 10 * S);
        const ImVec2 BarMinimum = ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + (RowHeight - BarHeight) * .5f);
        const float BarWidth = std::max(60.f * S, ImGui::GetContentRegionAvail().x);
        const ImVec2 BarMaximum(BarMinimum.x + BarWidth, BarMinimum.y + BarHeight);
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(BarMinimum, BarMaximum, ImGui::GetColorU32(Spectrum(0x24261f)), BarHeight * .5f);
        if (Fraction > 0.f)
        {
            const float FillWidth = std::max(BarHeight, BarWidth * Fraction);
            Ink->AddRectFilled(BarMinimum, ImVec2(BarMinimum.x + FillWidth, BarMaximum.y), ImGui::GetColorU32(Spectrum(0x94aa7a)), BarHeight * .5f);
        }
        ImGui::Dummy(ImVec2(1, RowHeight));
    }
    // .bk-run-actions: Bake (bk-primary) + Export on the left, .bk-settings-history's Undo/Redo pair pinned to
    // the row's far right via BakingStudio.jsx's travel("undo"/"redo") - backed by the real coalesced settings
    // history CoalesceSettingsUndo() builds in ConstructInspector, not decorative disabled buttons.
    // 📝 BakingStudio.jsx renders the primary bake button only in Mesh-maps mode (`{mode === "mesh" && ...}`)
    // - Material patch and Volume SDF run from their own inspector panels - and while busy the same button
    // (disabled via `locked`) reads "Baking mesh maps…". There is deliberately no Cancel button here: the
    // web app aborts a Worker, but this native bake has no cancellation channel yet, so a fake one would lie.
    if (Mode == StudioMode::Mesh)
    {
        ImGui::BeginDisabled(BakeRunning.load() || SelectedTargets.empty() || Settings.Channels.empty());
        Controls.ControlHeight = 32;
        if (Controls.Button(BakeRunning.load() ? "Baking mesh maps…" : SelectedTargets.size() > 1 ? "Bake all selected meshes" : "Bake mesh maps", 220 * S)) StartBake();
        Track("Bake");
        ImGui::EndDisabled();
        if (!Results.empty())
        {
            ImGui::SameLine();
            Controls.ControlHeight = 32;
            if (Controls.Button("Export maps (PNG)", 0)) ExportResults();
            Track("Export");
        }
    }
    else
        ImGui::Dummy(ImVec2(1, 32 * S));
    {
        const float HistoryWidth = 25 * S * 2 + 4 * S;
        ImGui::SameLine(0, 0);
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 8 * S, ImGui::GetWindowContentRegionMax().x - HistoryWidth));
        Controls.ControlHeight = 25;
        ImGui::BeginDisabled(BakeRunning.load() || SettingsUndoStack.empty());
        if (Controls.Button("Undo bake settings", 25 * S, false,
            "M9 14L4 9l5-5M4 9h10.5a5.5 5.5 0 0 1 5.5 5.5a5.5 5.5 0 0 1-5.5 5.5H11", true)) UndoSettings();
        ImGui::EndDisabled();
        Controls.Tooltip("Undo bake settings", "Steps back through the inspector's resolution/projection history.");
        Track("UndoSettings");
        ImGui::SameLine(0, 4 * S);
        ImGui::BeginDisabled(BakeRunning.load() || SettingsRedoStack.empty());
        if (Controls.Button("Redo bake settings", 25 * S, false,
            "M15 14l5-5-5-5M20 9H9.5A5.5 5.5 0 0 0 4 14.5A5.5 5.5 0 0 0 9.5 20H13", true)) RedoSettings();
        ImGui::EndDisabled();
        Controls.Tooltip("Redo bake settings", "Replays a step the undo button just stepped back from.");
        Track("RedoSettings");
    }
    // .bk-result-summary: one Check-prefixed line per bake result, below the actions row - "N results ready"
    // is not in the web reference, so this native build no longer invents that heading either.
    if (!Results.empty())
    {
        ImGui::Dummy(ImVec2(1, 10 * S));
        for (const auto &Result : Results)
        {
            const ImVec2 LineOrigin = ImGui::GetCursorScreenPos();
            PixelSpace IconSurface;
            (void)IconSurface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
            GlyphSpace::Stroke(IconSurface, "M20 6L9 17l-5-5", {LineOrigin.x, LineOrigin.y + 1 * S, 9 * S, 1.8f, SpectrumQuad(0x8b9d76)});
            char Line[200];
            std::snprintf(Line, sizeof(Line), "%s · %u charts · %llu hits · %llu misses", Result.Name.c_str(), Result.Stats.Charts,
                (unsigned long long)Result.Stats.Hits, (unsigned long long)Result.Stats.Misses);
            Caption(ImVec2(LineOrigin.x + 15 * S, LineOrigin.y), Line, 8.5f * S, 0x8b9d76);
            ImGui::Dummy(ImVec2(1, 14 * S));
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     INSPECTOR (RIGHT)
//------------------------------------------------------------------------------------------------------------------------

// 🧩 1:1 with BakingStudio.jsx's <aside className="bk-dock bk-inspector"> + studioTheme.css's
// .bk-inspector-identity/.bk-section-caption/.bk-card/.bk-field chrome: an icon-chip identity block, an
// "OUTPUT" section caption, then three accent-barred cards (Texture settings / Map inspector / High-to-low
// projection) built from Controls.Dropdown/Number/Range/Toggle field rows, a Bake-contract notes card, and a
// pinned .bk-pane-footer - not raw sequential ImGui combos/sliders/checkboxes.
void BakingWorkspacePanel::ConstructInspector()
{
    // 📝 Docked by PanelDockspace - no manual SetNextWindowPos/Size or Begin/End. The card stack can
    // comfortably exceed the inspector's height (especially with "Map inspector" populated); the inner
    // "InspectorScroll" child below (not the dock window itself) is what actually scrolls, so the
    // .bk-pane-footer strip drawn last against the window's own fixed bottom edge stays pinned on top
    // regardless of the child's scroll offset, exactly as it did before PanelDockspace owned this window.
    const float S = InterfaceScale;
    Controls.Scale = S;
    SettingsAtFrameStart = Settings;
    // 📝 Every inspector field in BakingStudio.jsx is `disabled={locked}` while a bake runs.
    const bool Locked = BakeRunning.load();
    const float FooterHeight = 39 * S;
    // 📝 The card stack scrolls inside its own child (height = window height minus the footer strip), clipped
    // to that child's rect - so it can never paint over the pinned .bk-pane-footer below it the way unclipped
    // auto-flow content drawn straight into the outer window could.
    ImGui::BeginChild("InspectorScroll", ImVec2(0, ImGui::GetContentRegionAvail().y - FooterHeight), false);
    ImGui::Indent(19 * S);
    ImGui::Dummy(ImVec2(1, 5 * S));

    if (Mode == StudioMode::Material || Mode == StudioMode::Volume)
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 19 * S);
        ImGui::TextWrapped("%s", Mode == StudioMode::Material ? "Material patch mode is not yet native - ported in a later pass."
                                                                : "Volume SDF mode is not yet native - ported in a later pass.");
        ImGui::PopTextWrapPos();
        ImGui::Unindent(19 * S);
        ImGui::EndChild();
        return;
    }

    // .bk-inspector-identity: a rounded icon chip + "STATIC HIGH -> LOW" eyebrow + big "Mesh map bake" title.
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        auto *Ink = ImGui::GetWindowDrawList();
        const float ChipSize = 51 * S;
        Ink->AddRectFilled(Origin, ImVec2(Origin.x + ChipSize, Origin.y + ChipSize), ImGui::GetColorU32(Spectrum(0x21231d)), 16 * S);
        Ink->AddRect(Origin, ImVec2(Origin.x + ChipSize, Origin.y + ChipSize), ImGui::GetColorU32(Spectrum(0xffffff, 0.03f)), 16 * S);
        const float Cx = Origin.x + ChipSize * .5f, Cy = Origin.y + ChipSize * .5f;
        Ink->AddTriangleFilled(ImVec2(Cx - 9 * S, Cy - 7 * S), ImVec2(Cx - 9 * S, Cy + 7 * S), ImVec2(Cx + 3 * S, Cy), ImGui::GetColorU32(Spectrum(0xa5b38a)));
        Ink->AddLine(ImVec2(Cx - 2 * S, Cy), ImVec2(Cx + 10 * S, Cy), ImGui::GetColorU32(Spectrum(0xa5b38a)), 2 * S);
        const float TextX = Origin.x + ChipSize + 12 * S;
        Caption(ImVec2(TextX, Origin.y + 1 * S), "STATIC HIGH -> LOW", 7 * S, 0x859271);
        Caption(ImVec2(TextX, Origin.y + 13 * S), "Mesh map bake", 22 * S, 0xd8dfc7, LightTypeface ? LightTypeface : RegularTypeface);
        char SetsLabel[64];
        std::snprintf(SetsLabel, sizeof(SetsLabel), "%zu selected texture set%s", SelectedTargets.size(), SelectedTargets.size() == 1 ? "" : "s");
        Caption(ImVec2(TextX, Origin.y + 40 * S), SetsLabel, 8 * S, 0x728060);
        ImGui::Dummy(ImVec2(1, ChipSize + 17 * S));
    }

    // .bk-section-caption "OUTPUT" + muted format subcaption, right-aligned.
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Caption(Origin, "OUTPUT", 8 * S, 0x8e8e7b);
        const char *Sub = "8-bit PNG + manifest";
        Caption(ImVec2(Origin.x + ImGui::GetContentRegionAvail().x - 19 * S - CaptionWidth(Sub, 7 * S), Origin.y + 0.5f * S), Sub, 7 * S, 0x666657);
        ImGui::Dummy(ImVec2(1, 19 * S));
    }

    const auto AccentBar = [&](uint32_t Colour, const char *Label)
    {
        auto *Ink = ImGui::GetWindowDrawList();
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Ink->AddRectFilled(ImVec2(Origin.x - 14 * S, Origin.y - 2 * S), ImVec2(Origin.x - 11 * S, Origin.y + 10 * S), ImGui::GetColorU32(Spectrum(Colour)), 1.5f * S);
        Caption(ImVec2(Origin.x, Origin.y), Label, 11 * S, 0xd0d0bb);
        ImGui::Dummy(ImVec2(1, 26 * S));
    };

    Controls.ControlHeight = 29;

    // "Texture settings" card.
    Controls.BeginCard("", true);
    {
        AccentBar(0x8b9b82, "Texture settings");
        const char *ResolutionLabels[]{"64 x 64", "128 x 128", "256 x 256", "512 x 512", "1024 x 1024", "2048 x 2048 (GPU)", "4096 x 4096 (GPU)"};
        const int Resolutions[]{64, 128, 256, 512, 1024, 2048, 4096};
        int ResolutionIndex = 2;
        for (int Index = 0; Index < 7; ++Index) if (Resolutions[Index] == int(Settings.Resolution)) ResolutionIndex = Index;
        ImGui::BeginDisabled(Locked);
        if (Controls.Dropdown("Resolution", ResolutionIndex, ResolutionLabels)) Settings.Resolution = uint32_t(Resolutions[ResolutionIndex]);
        float Padding = float(Settings.Padding);
        if (Controls.Number("Padding", Padding, 0, 32)) Settings.Padding = uint32_t(std::clamp(Padding, 0.f, 32.f));
        const char *NormalFormats[]{"OpenGL +Y", "DirectX -Y"};
        int NormalIndex = Settings.NormalPlusY ? 0 : 1;
        if (Controls.Dropdown("Normal format", NormalIndex, NormalFormats)) Settings.NormalPlusY = NormalIndex == 0;
        // 📝 Bake device (native-only extension, kept after the reference's Resolution/Padding/Normal-format
        // rows so the card still reads top-down like BakingStudio.jsx): the GPU route (host-owned
        // BakeRasterization over the BakeFieldCompute shader) evaluates the identical per-texel queries on
        // any Vulkan compute queue and is the only route past 1024 (the CPU route clamps there; the bake
        // completion line states what was actually baked). The host names its detected device through
        // QueryGpuBakeDevice; with none bound, the choice is disabled and says why instead of doing nothing.
        const std::string GpuDevice = QueryGpuBakeDevice ? QueryGpuBakeDevice() : std::string();
        const char *DeviceLabels[]{"CPU", "GPU (Vulkan compute)"};
        int DeviceIndex = (PreferGpuBake && !GpuDevice.empty()) ? 1 : 0;
        if (GpuDevice.empty()) ImGui::BeginDisabled(true);
        if (Controls.Dropdown("Bake device", DeviceIndex, DeviceLabels)) PreferGpuBake = DeviceIndex == 1;
        if (GpuDevice.empty()) ImGui::EndDisabled();
        Track("BakeDevice");
        Caption(ImGui::GetCursorScreenPos(), GpuDevice.empty() ? "No Vulkan compute GPU detected - CPU bake only." : GpuDevice.c_str(), 7.5f * S, 0x728060);
        ImGui::Dummy(ImVec2(1, 15 * S));
        if (Settings.Resolution > 1024 && !PreferGpuBake)
        {
            Caption(ImGui::GetCursorScreenPos(), "Above 1024 needs the GPU device - CPU bakes clamp to 1024.", 7.5f * S, 0xa79171);
            ImGui::Dummy(ImVec2(1, 15 * S));
        }
        ImGui::EndDisabled();
    }
    Controls.EndCard();

    // "Map inspector" card.
    Controls.BeginCard("", true);
    {
        AccentBar(0x82939e, "Map inspector");
        const auto *Meta = FindMeshMap(InspectMapKey);
        if (Meta)
        {
            Caption(ImGui::GetCursorScreenPos(), Meta->Label, 10 * S, 0xd9d4c4);
            ImGui::Dummy(ImVec2(1, 16 * S));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextColored(Spectrum(0xa3a68c), "%s", Meta->Description);
            ImGui::PopTextWrapPos();
            const bool Enabled = std::find(Settings.Channels.begin(), Settings.Channels.end(), InspectMapKey) != Settings.Channels.end();
            ImGui::TextColored(Spectrum(0x82846c), "%s", IsMeshMapImplemented(InspectMapKey) ? (Enabled ? "Enabled - will be baked" : "Disabled - click its tile to enable")
                                                                                              : "Not yet implemented in this native build.");
            ImGui::Dummy(ImVec2(1, 6 * S));
            // 🧩 1:1 with BakingStudio.jsx's .bk-selected-map conditional fields - the exact same key sets,
            // ranges and labels the web card shows under the description. These were missing wholesale from
            // the native card even though BakeSettings (and every evaluator) already carried the fields, so
            // bevel/dust/wireframe/mask behaviour could not be adjusted from this workspace at all.
            ImGui::BeginDisabled(Locked);
            if (InspectMapKey == "bevel-normal" || InspectMapKey == "bevel-mask")
            {
                Controls.Number("Bevel radius", Settings.BevelRadius, 0.f, 10.f);
                Controls.Number("Bevel strength", Settings.BevelStrength, 0.f, 1.f);
                const char *BevelSampleLabels[]{"4", "8", "12", "16"};
                const int BevelSampleOptions[]{4, 8, 12, 16};
                int BevelSampleIndex = 1;
                for (int Index = 0; Index < 4; ++Index) if (BevelSampleOptions[Index] == int(Settings.BevelSamples)) BevelSampleIndex = Index;
                if (Controls.Dropdown("Bevel samples", BevelSampleIndex, BevelSampleLabels)) Settings.BevelSamples = uint32_t(BevelSampleOptions[BevelSampleIndex]);
            }
            if (InspectMapKey == "dust")
            {
                const char *DustAxes[]{"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
                int DustAxisIndex = 2;
                for (int Index = 0; Index < 6; ++Index) if (Settings.DustAxis == DustAxes[Index]) DustAxisIndex = Index;
                if (Controls.Dropdown("Dust up axis", DustAxisIndex, DustAxes)) Settings.DustAxis = DustAxes[DustAxisIndex];
                Controls.Number("Slope falloff", Settings.DustFalloff, 0.1f, 16.f);
            }
            if (InspectMapKey == "wireframe")
                Controls.Number("Line width (px)", Settings.WireWidth, 0.25f, 8.f);
            if (InspectMapKey == "convexity" || InspectMapKey == "cavity" || InspectMapKey == "dirt" || InspectMapKey == "edge-wear")
                Controls.Number("Mask strength", Settings.MaskStrength, 0.f, 16.f);
            ImGui::EndDisabled();
        }
    }
    Controls.EndCard();

    // "High-to-low projection" card.
    Controls.BeginCard("", true);
    {
        AccentBar(0xa79171, "High-to-low projection");
        ImGui::BeginDisabled(Locked);
        const char *Methods[]{"Normal ray", "Nearest surface"};
        int MethodIndex = Settings.ProjectionRay ? 0 : 1;
        if (Controls.Dropdown("Method", MethodIndex, Methods)) Settings.ProjectionRay = MethodIndex == 0;
        Controls.Number("Front distance", Settings.Front, 0.0001f, 10.f);
        Controls.Number("Back distance", Settings.Back, 0.0001f, 10.f);
        Controls.Number("AO distance", Settings.AoDistance, 0.0001f, 10.f);
        Controls.Number("Secondary AO distance", Settings.SecondaryAoDistance, 0.0001f, 10.f);
        Controls.Number("Thickness range", Settings.ThicknessRange, 0.0001f, 10.f);
        Controls.Number("Curvature radius", Settings.CurvatureRadius, 0.0001f, 10.f);
        const char *SampleLabels[]{"4", "8", "16", "32", "64"};
        const int SampleOptions[]{4, 8, 16, 32, 64};
        int SampleIndex = 2;
        for (int Index = 0; Index < 5; ++Index) if (SampleOptions[Index] == int(Settings.Samples)) SampleIndex = Index;
        if (Controls.Dropdown("AO samples", SampleIndex, SampleLabels)) Settings.Samples = uint32_t(SampleOptions[SampleIndex]);
        Controls.Toggle("Match objects by name", Settings.MatchByName);
        ImGui::EndDisabled();
        ImGui::Dummy(ImVec2(1, 2 * S));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(Spectrum(0x868872), "Distances use source scene units. Ray front/back limits act as a uniform projection envelope, not a custom cage mesh.");
        ImGui::PopTextWrapPos();
    }
    Controls.EndCard();

    // "Bake contract" notes card.
    Controls.BeginCard("", true);
    {
        AccentBar(0x8b9b82, "Bake contract");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(Spectrum(0x8e9c7d), "Each low object is its own UV texture set. Imported UVs must be non-overlapping in 0-1. "
                            "Misses, chart count and coverage are reported rather than hidden.");
        ImGui::Dummy(ImVec2(1, 8 * S));
        ImGui::TextColored(Spectrum(0x8e9c7d), "Bevel softens normals only; curvature is an estimate. No full Substance/Blender parity, "
                            "MikkTSpace certification, UDIM, animated geometry or material-texture transfer is claimed.");
        ImGui::PopTextWrapPos();
    }
    Controls.EndCard();
    ImGui::Dummy(ImVec2(1, 14 * S));
    ImGui::Unindent(19 * S);
    CoalesceSettingsUndo();
    ImGui::EndChild();

    // .bk-pane-footer: pinned 39px strip - "Settings + selected maps" / "Source meshes: session only" - drawn
    // OUTSIDE the scrolling child above, against the outer window's own rect, so it can never be painted over
    // by (or clipped awkwardly against) the card stack's scroll content.
    {
        const ImVec2 WindowPosition = ImGui::GetWindowPos(), WindowSize = ImGui::GetWindowSize();
        const ImVec2 FooterMinimum(WindowPosition.x, WindowPosition.y + WindowSize.y - FooterHeight), FooterMaximum(WindowPosition.x + WindowSize.x, WindowPosition.y + WindowSize.y);
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(FooterMinimum, FooterMaximum, ImGui::GetColorU32(Spectrum(0x151515)));
        Ink->AddLine(FooterMinimum, ImVec2(FooterMaximum.x, FooterMinimum.y), ImGui::GetColorU32(Spectrum(0xffffff, 0.02f)));
        Caption(ImVec2(FooterMinimum.x + 19 * S, (FooterMinimum.y + FooterMaximum.y) * .5f - 5 * S), "Settings + selected maps", 8 * S, 0x777d66);
        const char *Sub = "Source meshes: session only";
        Caption(ImVec2(FooterMaximum.x - 19 * S - CaptionWidth(Sub, 7 * S), (FooterMinimum.y + FooterMaximum.y) * .5f - 4 * S), Sub, 7 * S, 0x686e59);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     TOP-LEVEL LAYOUT
//------------------------------------------------------------------------------------------------------------------------

void BakingWorkspacePanel::ConstructInterface()
{
    // 🧩 Same themed style-colour/style-var block Pattern/StampWorkspacePanel push around their own
    // ConstructInterface() bodies - without it, every raw ImGui widget in this panel (Controls.Number's
    // InputFloat, the demo/preview dropdowns' ImGui::BeginCombo frame, popup backgrounds, etc.) falls back to
    // ImGui's own default dark-theme colours, which are a visibly blue FrameBg/CheckMark/accent - exactly the
    // "default ImGui blue" this workspace was never actually themed against before this pass.
    const auto SavedPalette = ControlKit::Palette();
    const auto Palette = PanelChrome::StudioPalette(SavedPalette, {.20f, .208f, .173f, 1}, {.39f, .45f, .31f, 1});
    ControlKit::ConfigurePalette(Palette);
    // 📝 Single shared dock-window background, pushed once before Panels.ConstructInterface() the same way
    // Pattern/StampWorkspacePanel push it before their own Panels.ConstructInterface() call - PanelDockspace
    // owns the ImGui::Begin() for every registered dock window, so a push made per-panel inside e.g.
    // ConstructAssetsDock would land after that Begin() already baked this frame's background.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Spectrum(0x121212));
    ImGui::PushStyleColor(ImGuiCol_Text, ThemeQuad(Palette.Text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ThemeQuad(Palette.TextDim));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Spectrum(0x090909));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Spectrum(0x141412));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Spectrum(0x191916));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Spectrum(0x191919));
    ImGui::PushStyleColor(ImGuiCol_Header, Spectrum(0x191919));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Spectrum(0x272923));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, Spectrum(0x2c2e27));
    ImGui::PushStyleColor(ImGuiCol_Button, Spectrum(0x222222));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Spectrum(0x2e2e28));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Spectrum(0x33352c));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, Spectrum(0xa3aa8a));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, Spectrum(0xa3aa8a));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, Spectrum(0xb7bf9c));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .055f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, Spectrum(0x2a2a26));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, Spectrum(0x35352f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, Spectrum(0x3c3c34));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6 * InterfaceScale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * InterfaceScale, 6 * InterfaceScale));
    // 🧩 Shared dock chrome (near-black title/tab colours, square windows, hidden scrollbars, no tab
    // close buttons) - one implementation in PanelChrome for every workspace, pushed last here and
    // popped first in the tail below.
    const auto Chrome = PanelChrome::PushDockChrome(Spectrum(0x090909), Spectrum(0x101010), true, true, InterfaceScale);

    PollBake();
    ConstructHeader();
    // 📝 PanelDockspace itself now owns the Assets/Preview/Inspector docks - same ConfigureWorkspaceInsets(43,
    // 32) reservation above/below as before (header height / .bk-status-bar height), but the dockspace handles
    // placement, resize, drag-to-rearrange and (once the app wires Baking's QueryPanels() the way it already
    // does for Pattern/Stamp) layout persistence, instead of this function computing fixed Left/Top/Right/
    // Bottom rects and calling each dock function directly.
    Panels.ConstructInterface(InterfaceScale);
    const auto *Viewport = ImGui::GetMainViewport();
    const float StatusHeight = 32 * InterfaceScale;
    const float Bottom = Viewport->WorkPos.y + Viewport->WorkSize.y - StatusHeight;
    const float Left = Viewport->WorkPos.x, Right = Viewport->WorkPos.x + Viewport->WorkSize.x;

    ImGui::SetNextWindowPos(ImVec2(Left, Bottom));
    ImGui::SetNextWindowSize(ImVec2(Right - Left, StatusHeight));
    ImGui::Begin("Baking status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    {
        // 🧩 1:1 with .bk-status-bar: #0c0c0c strip with a hairline top border, a 5px sage status dot + 8px
        // single-line info text at 16px padding (ellipsised by the flex row before the right-hand caption),
        // and the 7px letter-spaced "BOUNDED STATIC MESH BAKER" pinned right - not default-size ImGui text.
        const float S = InterfaceScale;
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(ImVec2(Left, Bottom), ImVec2(Right, Bottom + StatusHeight), ImGui::GetColorU32(Spectrum(0x0c0c0c)));
        Ink->AddLine(ImVec2(Left, Bottom), ImVec2(Right, Bottom), ImGui::GetColorU32(Spectrum(0xffffff, 0.03f)));
        Ink->AddCircleFilled(ImVec2(Left + 16 * S + 2.5f * S, Bottom + StatusHeight * .5f), 2.5f * S, ImGui::GetColorU32(Spectrum(0x8aa27e)));
        const char *RightCaption = "BOUNDED STATIC MESH BAKER";
        const float RightWidth = ImGui::GetFont()->CalcTextSizeA(7 * S * 1.302f, FLT_MAX, 0.f, RightCaption).x;
        const float RightX = Right - 16 * S - RightWidth;
        Ink->PushClipRect(ImVec2(Left, Bottom), ImVec2(RightX - 12 * S, Bottom + StatusHeight), true);
        Caption(ImVec2(Left + 28 * S, Bottom + StatusHeight * .5f - 5 * S), Info.c_str(), 8 * S, 0x727c63);
        Ink->PopClipRect();
        Caption(ImVec2(RightX, Bottom + StatusHeight * .5f - 4.5f * S), RightCaption, 7 * S, 0x5e6f4d);
    }
    ImGui::End();

    if (!Error.empty())
    {
        // 🧩 1:1 with .bk-error: an amber-on-brown toast centred 55px below the studio top
        // (width min(600px, 92vw), 13px rounding, #896541 border on #34281b, #dec3a6 text) dismissed by an
        // icon-only X at its right edge - not a red label parked over the status bar.
        const float S = InterfaceScale;
        const float ToastWidth = std::min(600 * S, (Right - Left) * 0.92f);
        const float TextWidth = std::max(80 * S, ToastWidth - 66 * S);
        const float ToastHeight = std::max(48 * S, ImGui::CalcTextSize(Error.c_str(), nullptr, false, TextWidth).y + 24 * S);
        ImGui::SetNextWindowPos(ImVec2((Left + Right) * .5f, Viewport->WorkPos.y + 55 * S), ImGuiCond_Always, ImVec2(.5f, 0.f));
        ImGui::SetNextWindowSize(ImVec2(ToastWidth, ToastHeight));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Spectrum(0x34281b));
        ImGui::PushStyleColor(ImGuiCol_Border, Spectrum(0x896541));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 13 * S);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * S, 12 * S));
        ImGui::Begin("Baking error", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + TextWidth);
        ImGui::TextColored(Spectrum(0xdec3a6), "%s", Error.c_str());
        ImGui::PopTextWrapPos();
        ImGui::SetCursorPos(ImVec2(ToastWidth - 40 * S, 12 * S));
        Controls.ControlHeight = 24;
        if (Controls.Button("Dismiss baking error", 24 * S, false, "M18 6L6 18M6 6l12 12", true)) Error.clear();
        ImGui::End();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }
    PanelChrome::PopDockChrome(Chrome);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(20);
    ControlKit::ConfigurePalette(SavedPalette);
}
} // namespace Frontier
