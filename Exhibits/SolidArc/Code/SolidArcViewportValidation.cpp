/*====================================================================================================================================
                                                    SOLIDARCVIEWPORTVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Step 9: SolidArc viewport gates on a real Vulkan device — GPU identity vs CPU pick, tints, views, determinism

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Document/SolidArcMatcap.h"
#include "UI/SolidArc/Document/SolidArcScene.h"
#include "UI/SolidArc/Document/SolidArcSpecimen.h"
#include "UI/SolidArc/Viewport/SolidArcCamera.h"
#include "UI/SolidArc/Viewport/SolidArcDevice.h"
#include "UI/SolidArc/Viewport/SolidArcPortableImage.h"
#include "UI/SolidArc/Viewport/SolidArcRasterization.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace Frontier;
using namespace Frontier::SolidArc;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr int SkipCode = 77;                                                             // [-] - ctest SKIP_RETURN_CODE (no Vulkan)

    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - V1 …
        const char* Subject  = "";                                                           // [-] - What is measured
        const char* Limit    = "";                                                           // [-] - Pass condition (text)
        double      Worst    = 0.0;                                                          // [-] - Worst value
        long        Checks   = 0;                                                            // [-] - Measurements
        long        Failures = 0;                                                            // [-] - Measurements over the limit
    };

    GateTally DeviceGate      = { "V1", "Vulkan 1.3 device, SPIR-V modules from the Slang archive, seven pipelines (opaque, x-ray depth / ghost / occluded strokes, edges, grid, image planes)", "created" };
    GateTally FaceGate        = { "V2", "Face pixels away from edges: GPU identity = CPU ray pick (7 views × perspective / orthographic)", "mismatch ≤ 0.1 %" };
    GateTally EdgeGate        = { "V3", "Edge-stroke pixels: the GPU edge passes within the stroke reach of the pixel ray and is not hidden behind the CPU face hit (orthographic)", "explained ≥ 99.5 %" };
    GateTally ProbeGate       = { "V4", "Single-texel identity probe (hover path) = full readback = CPU pick", "all equal" };
    GateTally TintGate        = { "V5", "Selected face blue (#4099ff), active edge green (#33ff73), hovered face lighter; others unchanged", "all hold" };
    GateTally ViewGate        = { "V6", "View presets: centre pixel names the expected face (Top → top cap, Front → front side, …)", "all equal" };
    GateTally DeterminismGate = { "V7", "Same frame twice → identical colour and identity digests", "equal" };
    GateTally XRayGate        = { "V9", "X-ray: ghost faces = α · opaque + (1 − α) · background; every revealed stroke pixel is a CPU-hidden edge; no other identity changes; rim spill; probe and CPU pick through; box visibility = GPU edges; off → bit-identical", "≤ 2 / 255 · ≥ 99.5 % · 0 · ≤ 2 % · equal" };
    GateTally SectionGate     = { "V10", "Section (Alt+S): interior pixels = CPU pick with the clip (removed side gone, caps where the material is cut); every cap pixel within 1 px of a CPU cap (multisampled silhouettes aside); probe; grid never through an interior cap pixel; off → bit-identical", "≤ 0.1 % · all · equal" };
    GateTally ResizeGate      = { "V11", "Target rebuilds: the generation advances on every real resize and holds on an equal size (the interface re-registers its texture on it, never on the recycled VkImageView handle); a resize away and back renders the identical frame", "all hold · equal" };
    GateTally MatcapGate      = { "V8", "Matcap layers: planar face interiors = the bilinear texel at their view-space normal (body + 2 face overrides); Studio shader = CPU mirror; clearing restores the Studio frame bit-exact", "≤ 2 / 255" };

    std::vector<std::string> Journal;                                                        // [-] - Narrative rows

    void Tally(GateTally& Gate, double Value, bool PassedCondition, const std::string& Note)
    {
        Gate.Worst = std::max(Gate.Worst, Value);
        ++Gate.Checks;
        if (!PassedCondition) ++Gate.Failures;
        char Row[600];
        std::snprintf(Row, sizeof(Row), "| %s | %s | %.4g | %s |", Gate.Code, Note.c_str(), Value, PassedCondition ? "🟢" : "🔴");
        Journal.push_back(Row);
    }

    [[nodiscard]] std::string Compact(double Value)
    {
        char Text[32];
        std::snprintf(Text, sizeof(Text), "%.3g", Value);
        return Text;
    }

    struct Rendered
    {
        std::vector<uint32_t> Colour   = {};
        std::vector<uint32_t> Identity = {};
        uint32_t              Probe    = 0;
        double                Milliseconds = 0.0;
    };

    Rendered RenderFrame(const SolidArcDevice& Device, SolidArcRasterization& Raster, const OrbitCameraFrame& Frame, const SolidArcViewportStyle& Style,
                         int ProbeX = -1, int ProbeY = -1)
    {
        const auto Start = std::chrono::steady_clock::now();
        Device.SubmitImmediately([&](VkCommandBuffer Commands) { Raster.Record(Commands, Frame, Style, true, ProbeX, ProbeY); });
        Rendered Result;
        Result.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
        const size_t Count  = size_t(Raster.Width()) * Raster.Height();
        Result.Colour.assign(Raster.ReadbackColour(), Raster.ReadbackColour() + Count);
        Result.Identity.assign(Raster.ReadbackIdentity(), Raster.ReadbackIdentity() + Count);
        Result.Probe = Raster.ProbedIdentity();
        return Result;
    }

    // ① every 3rd pixel whose 5 × 5 neighbourhood is one single face (or all background) is compared with the CPU pick
    void CompareFaces(const SolidArcScene& Scene, const OrbitCameraFrame& Frame, const Rendered& Image, uint32_t Width, uint32_t Height,
                      const std::string& Label)
    {
        long Compared = 0, Mismatched = 0;
        for (uint32_t Row = 3; Row + 3 < Height; Row += 3)
            for (uint32_t Column = 3; Column + 3 < Width; Column += 3)
            {
                const uint32_t Centre = Image.Identity[size_t(Row) * Width + Column];
                bool UniformCondition = (Centre & IdentityEdgeBit) == 0u;
                for (int DeltaRow = -2; DeltaRow <= 2 && UniformCondition; ++DeltaRow)
                    for (int DeltaColumn = -2; DeltaColumn <= 2 && UniformCondition; ++DeltaColumn)
                        UniformCondition = Image.Identity[size_t(Row + DeltaRow) * Width + Column + DeltaColumn] == Centre;
                if (!UniformCondition) continue;
                const std::optional<SolidArcPick> Hit = PickSolidArcScene(Scene, CastOrbitRay(Frame, Column + 0.5, Row + 0.5), 0.0);
                const uint32_t Expected = Hit ? Hit->Identity : 0u;
                ++Compared;
                if (Expected != Centre) ++Mismatched;
            }
        const double Rate = Compared ? 100.0 * double(Mismatched) / double(Compared) : 100.0;
        Tally(FaceGate, Rate, Compared > 1000 && Rate <= 0.1, Label + ": " + std::to_string(Mismatched) + " / " + std::to_string(Compared) + " sampled pixels differ (%)");
    }

    [[nodiscard]] std::array<double, 3> MeanColour(const Rendered& Image, uint32_t Identity)
    {
        std::array<double, 3> Sum{};
        long Count = 0;
        for (size_t Index = 0; Index < Image.Identity.size(); ++Index)
        {
            if (Image.Identity[Index] != Identity) continue;
            const uint32_t Pixel = Image.Colour[Index];
            Sum[0] += (Pixel & 0xff) / 255.0; Sum[1] += ((Pixel >> 8) & 0xff) / 255.0; Sum[2] += ((Pixel >> 16) & 0xff) / 255.0;
            ++Count;
        }
        for (double& Channel : Sum) Channel /= std::max<long>(Count, 1);
        return Sum;
    }

    [[nodiscard]] uint32_t IdentityNamed(const SolidArcScene& Scene, const std::string& Fragment, bool EdgeCondition)
    {
        if (EdgeCondition)
        {
            for (size_t Index = 0; Index < Scene.Edges.size(); ++Index) if (Scene.Edges[Index].Name.find(Fragment) != std::string::npos) return IdentityEdgeBit | uint32_t(Index + 1);
            return 0u;
        }
        for (size_t Index = 0; Index < Scene.Faces.size(); ++Index) if (Scene.Faces[Index].Name == Fragment) return uint32_t(Index + 1);
        return 0u;
    }

    void WriteExhibit(const std::string& Directory, const std::string& Stem, const Rendered& Image, uint32_t Width, uint32_t Height)
    {
        if (Directory.empty()) return;
        WriteSolidArcPng(Directory + "/" + Stem + ".png", Width, Height, Image.Colour.data());
        const std::vector<uint32_t> Painted = PaintSolidArcIdentity(Image.Identity.data(), Width, Height);
        WriteSolidArcPng(Directory + "/" + Stem + "Identity.png", Width, Height, Painted.data());
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    const auto  Start = std::chrono::steady_clock::now();
    std::string Driver = std::getenv("SOLIDARC_VULKAN_DRIVER") ? std::getenv("SOLIDARC_VULKAN_DRIVER") : "";
    std::string Exhibits;
    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        const std::string Argument = Arguments[Index];
        if (Argument == "--vulkan-driver" && Index + 1 < ArgumentCount) Driver = Arguments[++Index];
        if (Argument == "--exhibits" && Index + 1 < ArgumentCount) Exhibits = Arguments[++Index];
    }

    // ---- V1
    SolidArcDevice         Device;
    SolidArcDeviceSettings Settings;
    Settings.DriverPath = Driver;
    std::string Refusal;
    if (!Device.Open(Settings, Refusal))
    {
        std::printf("# SolidArcViewportValidation\n\n⚠️ skipped: %s\n", Refusal.c_str());
        return SkipCode;
    }
    SolidArcRasterization Raster;
    const bool CreatedCondition = Raster.Create(Device, Refusal);
    Tally(DeviceGate, 0.0, CreatedCondition, Device.Describe() + (CreatedCondition ? "" : " — " + Refusal));
    if (!CreatedCondition) return 1;

    std::vector<std::string> Notes;
    SolidArcDocument Document = BuildSolidArcSpecimen(&Notes);
    for (const std::string& Line : Notes) std::printf("⚠️ specimen: %s\n", Line.c_str());
    const SolidArcScene Scene = BuildSolidArcScene(Document);
    constexpr uint32_t Width = 960, Height = 640;
    Raster.Resize(Width, Height);
    Raster.UploadScene(Scene);
    OrbitCamera Camera;
    FitSolidArcCamera(Camera, Scene);
    SolidArcViewportStyle Style;

    // ---- V2 · V6 over every preset, perspective and orthographic
    struct Expectation { OrbitViewPreset Preset; const char* Face; };
    const Expectation Views[] = { { OrbitViewPreset::Isometric, "" }, { OrbitViewPreset::Top, "Extrude#2.Cap(End)" },
                                  { OrbitViewPreset::Bottom, "Extrude#2.Cap(Start)" }, { OrbitViewPreset::Front, "Extrude#2.Side(Line1)" },
                                  { OrbitViewPreset::Back, "Extrude#2.Side(Line3)" }, { OrbitViewPreset::Right, "" }, { OrbitViewPreset::Left, "" } };
    double SlowestMs = 0.0;
    for (const bool OrthographicCondition : { false, true })
        for (const Expectation& View : Views)
        {
            Camera.OrthographicEnabled = OrthographicCondition;
            PresentOrbitView(Camera, View.Preset);
            const OrbitCameraFrame Frame = FrameOrbitCamera(Camera, Width, Height);
            const Rendered            Image = RenderFrame(Device, Raster, Frame, Style);
            SlowestMs = std::max(SlowestMs, Image.Milliseconds);
            const std::string Label = std::string(OrbitViewLabel(View.Preset)) + (OrthographicCondition ? " ortho" : " persp");
            CompareFaces(Scene, Frame, Image, Width, Height, Label);
            if (View.Face[0] != '\0')
            {
                // 📝 Sample just off-centre: the Top / Bottom centre lies between the boss and the hole.
                const uint32_t Probe = Image.Identity[size_t(Height / 2) * Width + Width / 2 + 40];
                const std::string Seen = NameSolidArcIdentity(Scene, Probe);
                Tally(ViewGate, 0.0, Seen == View.Face, Label + " centre → " + (Seen.empty() ? "background" : Seen));
            }
            if (!OrthographicCondition && (View.Preset == OrbitViewPreset::Isometric || View.Preset == OrbitViewPreset::Top || View.Preset == OrbitViewPreset::Front))
                WriteExhibit(Exhibits, std::string("Viewport") + OrbitViewLabel(View.Preset), Image, Width, Height);
        }

    // ---- V3 edge strokes against the CPU nearest edge (orthographic: one world reach for every pixel)
    {
        Camera.OrthographicEnabled = true;
        PresentOrbitView(Camera, OrbitViewPreset::Isometric);
        const OrbitCameraFrame Frame = FrameOrbitCamera(Camera, Width, Height);
        const Rendered            Image = RenderFrame(Device, Raster, Frame, Style);
        long Compared = 0, Agreed = 0;
        for (uint32_t Row = 0; Row < Height; ++Row)
            for (uint32_t Column = 0; Column < Width; ++Column)
            {
                const uint32_t Identity = Image.Identity[size_t(Row) * Width + Column];
                if (!(Identity & IdentityEdgeBit)) continue;
                // 📝 At a vertex two strokes overlap and either identity is right, so the test is "explained", not "nearest".
                const OrbitRay                 Ray   = CastOrbitRay(Frame, Column + 0.5, Row + 0.5);
                const SolidArcEdgeApproach        Edge  = ApproachSolidArcEdge(Scene, Ray, (Identity & ~IdentityEdgeBit) - 1u);
                const std::optional<SolidArcPick> Face  = PickSolidArcScene(Scene, Ray, 0.0);
                const double                      Reach = (0.5 * 2.0 * Style.EdgeStroke + 1.0) * Frame.WorldPerPixel * 1.05;
                ++Compared;
                if (Edge.Separation <= Reach && (!Face || Edge.Reach <= Face->Distance + 4.0 * Reach)) ++Agreed;
            }
        const double Rate = Compared ? 100.0 * double(Agreed) / double(Compared) : 0.0;
        Tally(EdgeGate, 100.0 - Rate, Compared > 500 && Rate >= 99.5, "isometric ortho: " + std::to_string(Agreed) + " / " + std::to_string(Compared) + " edge pixels explained (unexplained %)");
    }

    // ---- V4 probe path
    {
        Camera.OrthographicEnabled = false;
        PresentOrbitView(Camera, OrbitViewPreset::Isometric);
        const OrbitCameraFrame Frame = FrameOrbitCamera(Camera, Width, Height);
        const int Spots[][2] = { { 480, 320 }, { 300, 400 }, { 700, 260 }, { 10, 10 }, { 520, 360 } };
        for (const auto& Spot : Spots)
        {
            const Rendered Image = RenderFrame(Device, Raster, Frame, Style, Spot[0], Spot[1]);
            const uint32_t Whole = Image.Identity[size_t(Spot[1]) * Width + Spot[0]];
            // 📝 Faces and background are compared with the face-only CPU pick; edge texels with the edge approach.
            const OrbitRay                 Ray  = CastOrbitRay(Frame, Spot[0] + 0.5, Spot[1] + 0.5);
            const std::optional<SolidArcPick> Face = PickSolidArcScene(Scene, Ray, 0.0);
            const std::string Name = NameSolidArcIdentity(Scene, Image.Probe);
            bool CpuCondition = Whole == 0u ? !Face : (Face && Face->Identity == Whole);
            if (Whole & IdentityEdgeBit) CpuCondition = ApproachSolidArcEdge(Scene, Ray, (Whole & ~IdentityEdgeBit) - 1u).Separation <= 3.0 * Frame.WorldPerPixel * 2.0;
            const bool AgreeCondition = Image.Probe == Whole && CpuCondition;
            Tally(ProbeGate, 0.0, AgreeCondition, "(" + std::to_string(Spot[0]) + ", " + std::to_string(Spot[1]) + ") → " + (Name.empty() ? "background" : Name));
        }
    }

    // ---- V5 tints: select the top cap and the fillet's x = 4 boundary edge (active), hover the front face
    {
        Camera.OrthographicEnabled = false;
        PresentOrbitView(Camera, OrbitViewPreset::Isometric);
        const OrbitCameraFrame Frame = FrameOrbitCamera(Camera, Width, Height);
        const Rendered Plain = RenderFrame(Device, Raster, Frame, Style);
        const uint32_t Top   = IdentityNamed(Scene, "Extrude#2.Cap(End)", false);
        const uint32_t Front = IdentityNamed(Scene, "Extrude#2.Side(Line1)", false);
        const uint32_t Side  = IdentityNamed(Scene, "Extrude#2.Side(Line2)", false);
        uint32_t FilletEdge = 0u;
        for (size_t Index = 0; Index < Scene.Edges.size() && !FilletEdge; ++Index)
            if (Scene.Edges[Index].Name.find("Blend#") != std::string::npos && Scene.Edges[Index].Name.find("Cap(End)") != std::string::npos) FilletEdge = IdentityEdgeBit | uint32_t(Index + 1);
        SolidArcSelection Selection;
        Selection.Faces    = { NameSolidArcIdentity(Scene, Top) };
        Selection.Edges    = { NameSolidArcIdentity(Scene, FilletEdge) };
        Selection.LastKind = SolidArcSelectionKind::Edge;
        const SolidArcSceneStates States = ComposeSolidArcStates(Scene, Selection, Front);
        Raster.AssignStates(States.Faces, States.Edges);
        const Rendered Tinted = RenderFrame(Device, Raster, Frame, Style);
        Raster.AssignStates(std::vector<uint32_t>(Scene.Faces.size(), 0u), std::vector<uint32_t>(Scene.Edges.size(), 0u));
        const auto TopBefore = MeanColour(Plain, Top), TopAfter = MeanColour(Tinted, Top);
        const auto FrontBefore = MeanColour(Plain, Front), FrontAfter = MeanColour(Tinted, Front);
        const auto SideBefore = MeanColour(Plain, Side), SideAfter = MeanColour(Tinted, Side);
        const auto EdgeAfter = MeanColour(Tinted, FilletEdge);
        const double Blueness = TopAfter[2] - TopAfter[0];
        Tally(TintGate, Blueness, Blueness > 0.25 && std::abs(TopBefore[2] - TopBefore[0]) < 0.1, "selected top cap: B − R " + std::to_string(TopBefore[2] - TopBefore[0]) + " → " + std::to_string(Blueness));
        const double Greenness = EdgeAfter[1] - std::max(EdgeAfter[0], EdgeAfter[2]) * 0.5;
        Tally(TintGate, Greenness, FilletEdge != 0u && EdgeAfter[1] > 0.6 && EdgeAfter[1] > EdgeAfter[0] + 0.3, "active fillet edge: mean RGB (" + std::to_string(EdgeAfter[0]) + ", " + std::to_string(EdgeAfter[1]) + ", " + std::to_string(EdgeAfter[2]) + ")");
        const double Lift = (FrontAfter[0] + FrontAfter[1] + FrontAfter[2]) - (FrontBefore[0] + FrontBefore[1] + FrontBefore[2]);
        Tally(TintGate, Lift, Lift > 0.05, "hovered front face brightened by " + std::to_string(Lift));
        const double Drift = std::abs(SideAfter[0] - SideBefore[0]) + std::abs(SideAfter[1] - SideBefore[1]) + std::abs(SideAfter[2] - SideBefore[2]);
        Tally(TintGate, Drift, Drift < 1.0e-6, "unselected right side unchanged (Σ|ΔRGB|)");
        WriteExhibit(Exhibits, "ViewportSelection", Tinted, Width, Height);
    }

    // ---- V8 matcaps: planar faces shade with exactly the texel their view-space normal addresses (bilinear, clamp)
    {
        Camera.OrthographicEnabled = false;
        PresentOrbitView(Camera, OrbitViewPreset::Isometric);
        const OrbitCameraFrame Frame = FrameOrbitCamera(Camera, Width, Height);
        // 📝 Single-sampled: V8 measures shading, not coverage. Under 4× MSAA SwiftShader lets a near edge-on hole-wall
        //    sliver win a sample 0.13 behind the cap (CPU rays at the four sample positions all meet the cap first), which
        //    tints a few cap pixels; V2 / V6 / V10 cover the multisampled frame.
        SolidArcRasterization Single;
        std::string           SingleRefusal;
        if (!Single.Create(Device, SingleRefusal, 1)) std::printf("⚠️ single-sampled raster: %s\n", SingleRefusal.c_str());
        Single.Resize(Width, Height);
        Single.UploadScene(Scene);
        const Rendered Plain = RenderFrame(Device, Single, Frame, Style);
        SolidArcAppearance Appearance;
        Appearance.BodyMatcap                             = "BrushedSteel";
        Appearance.FaceMatcaps["Extrude#2.Cap(End)"]      = "Brass";
        Appearance.FaceMatcaps["Extrude#2.Side(Line2)"]   = "RedPlastic";
        const SolidArcMatcapLibrary Library = AssembleSolidArcMatcaps(Appearance);
        Single.UploadMatcaps(Library.Pictures);
        Single.AssignStates(ComposeSolidArcMatcapLayers(Scene, Appearance, Library), std::vector<uint32_t>(Scene.Edges.size(), 0u));
        const Rendered Shaded = RenderFrame(Device, Single, Frame, Style);

        // Bilinear clamp-to-edge sample of a picture at uv, as the sampler does it (texel centres at (i + ½) / E).
        auto Sample = [](const SolidArcPicture& Picture, double U, double V)
        {
            const double X = std::clamp(U * Picture.Width - 0.5, 0.0, double(Picture.Width - 1));
            const double Y = std::clamp(V * Picture.Height - 0.5, 0.0, double(Picture.Height - 1));
            const uint32_t Left = uint32_t(X), Top = uint32_t(Y);
            const uint32_t Right = std::min(Left + 1, Picture.Width - 1), Bottom = std::min(Top + 1, Picture.Height - 1);
            const double Across = X - Left, Down = Y - Top;
            std::array<double, 3> Value{};
            for (int Channel = 0; Channel < 3; ++Channel)
            {
                auto At = [&](uint32_t Column, uint32_t Row) { return Picture.Pixels[(size_t(Row) * Picture.Width + Column) * 4 + Channel] / 255.0; };
                Value[Channel] = (1 - Down) * ((1 - Across) * At(Left, Top) + Across * At(Right, Top)) + Down * ((1 - Across) * At(Left, Bottom) + Across * At(Right, Bottom));
            }
            return Value;
        };
        // Worst per-channel deviation over the interior pixels of the face (5 × 5 of its identity: multisampled borders and
        // the soft stroke fringe blend the neighbours by design) from the expected colour, in 1 / 255.
        auto Deviation = [&](const Rendered& Image, uint32_t Identity, const std::array<double, 3>& Expected, long& Count)
        {
            double Worst = 0.0;
            Count = 0;
            for (size_t Index = 0; Index < Image.Identity.size(); ++Index)
            {
                if (Image.Identity[Index] != Identity) continue;
                const int Row = int(Index / Width), Column = int(Index % Width);
                if (Row < 2 || Column < 2 || Row + 2 >= int(Height) || Column + 2 >= int(Width)) continue;
                bool InteriorCondition = true;
                for (int DeltaRow = -2; DeltaRow <= 2 && InteriorCondition; ++DeltaRow)
                    for (int DeltaColumn = -2; DeltaColumn <= 2 && InteriorCondition; ++DeltaColumn)
                        InteriorCondition = Image.Identity[size_t(Row + DeltaRow) * Width + size_t(Column + DeltaColumn)] == Identity;
                if (!InteriorCondition) continue;
                const uint32_t Pixel = Image.Colour[Index];
                for (int Channel = 0; Channel < 3; ++Channel) Worst = std::max(Worst, std::abs(((Pixel >> (8 * Channel)) & 0xff) - Expected[Channel] * 255.0));
                ++Count;
            }
            return Worst;
        };
        const SolidArcPicture Studio = ComposeSolidArcStudioMatcap(SolidArcMatcapExtent);
        struct Probe { const char* Face; const char* Matcap; };
        for (const Probe& Expect : { Probe{ "Extrude#2.Cap(End)", "Brass" }, Probe{ "Extrude#2.Side(Line2)", "RedPlastic" }, Probe{ "Extrude#2.Side(Line1)", "BrushedSteel" } })
        {
            const uint32_t Identity = IdentityNamed(Scene, Expect.Face, false);
            const SolidArcSceneCorner& Corner = Scene.Corners[Scene.Faces[Identity - 1].FirstCorner];
            const double View[2] = { Corner.Normal[0] * Frame.Right[0] + Corner.Normal[1] * Frame.Right[1] + Corner.Normal[2] * Frame.Right[2],
                                     Corner.Normal[0] * Frame.Up[0] + Corner.Normal[1] * Frame.Up[1] + Corner.Normal[2] * Frame.Up[2] };
            const uint32_t Layer = SolidArcMatcapLayer(Library, Expect.Matcap);
            const std::array<double, 3> Expected = Sample(Library.Pictures[Layer - 1], 0.5 + 0.5 * View[0], 0.5 - 0.5 * View[1]);
            long Count = 0;
            const double Worst = Deviation(Shaded, Identity, Expected, Count);
            char Note[300];
            std::snprintf(Note, sizeof(Note), "%s → %s (layer %u): view normal (%.3f, %.3f), %ld pixels within %.2f / 255 of the texel (RGB %.0f %.0f %.0f)", Expect.Face, Expect.Matcap,
                          Layer, View[0], View[1], Count, Worst, Expected[0] * 255, Expected[1] * 255, Expected[2] * 255);
            Tally(MatcapGate, Worst, Count > 500 && Worst <= 2.0, Note);
            // Studio (layer 0) is analytic in the shader; its CPU mirror sampled at the same normal must agree.
            const std::array<double, 3> Analytic = Sample(Studio, 0.5 + 0.5 * View[0], 0.5 - 0.5 * View[1]);
            const double StudioWorst = Deviation(Plain, Identity, Analytic, Count);
            Tally(MatcapGate, StudioWorst, Count > 500 && StudioWorst <= 2.0, std::string(Expect.Face) + " without matcap: Studio shader = CPU Studio mirror within " + Compact(StudioWorst) + " / 255");
        }
        // Back to Studio: the frame is bit-identical to the one before any matcap.
        Single.AssignStates(std::vector<uint32_t>(Scene.Faces.size(), 0u), std::vector<uint32_t>(Scene.Edges.size(), 0u));
        const Rendered Restored = RenderFrame(Device, Single, Frame, Style);
        Tally(MatcapGate, 0.0, Restored.Colour == Plain.Colour && Shaded.Identity == Plain.Identity,
              "layers cleared → colour bit-identical to the Studio frame; identity unchanged by matcaps");
        WriteExhibit(Exhibits, "ViewportMatcaps", Shaded, Width, Height);
    }

    // ---- V9 x-ray: ghost faces, revealed hidden strokes, pick-through, box visibility, bit-exact restore
    {
        SolidArcViewportStyle Plain = Style;
        Plain.GridEnabled = false;                                                           // Flat background behind the ghost faces
        SolidArcViewportStyle Ghost = Plain;
        Ghost.XRayEnabled = true;
        const double α = Ghost.XRayOpacity;
        for (const bool OrthographicCondition : { true, false })
        {
            Camera.OrthographicEnabled = OrthographicCondition;
            PresentOrbitView(Camera, OrbitViewPreset::Isometric);
            const OrbitCameraFrame Frame  = FrameOrbitCamera(Camera, Width, Height);
            const std::string         Label  = OrthographicCondition ? "isometric ortho" : "isometric persp";
            const Rendered            Opaque = RenderFrame(Device, Raster, Frame, Plain);
            const Rendered            XRay   = RenderFrame(Device, Raster, Frame, Ghost);

            // ① ghost faces: interior face pixels (5 × 5 one face in both frames) blend the opaque colour over the background
            double GhostWorst = 0.0;
            long   GhostCount = 0;
            for (uint32_t Row = 2; Row + 2 < Height; ++Row)
                for (uint32_t Column = 2; Column + 2 < Width; ++Column)
                {
                    const size_t   Index  = size_t(Row) * Width + Column;
                    const uint32_t Centre = Opaque.Identity[Index];
                    if (Centre == 0u || (Centre & IdentityEdgeBit) || XRay.Identity[Index] != Centre) continue;
                    bool UniformCondition = true;
                    for (int DeltaRow = -2; DeltaRow <= 2 && UniformCondition; ++DeltaRow)
                        for (int DeltaColumn = -2; DeltaColumn <= 2 && UniformCondition; ++DeltaColumn)
                        {
                            const size_t Near = size_t(int(Row) + DeltaRow) * Width + size_t(int(Column) + DeltaColumn);
                            UniformCondition = Opaque.Identity[Near] == Centre && XRay.Identity[Near] == Centre;
                        }
                    if (!UniformCondition) continue;
                    for (int Channel = 0; Channel < 3; ++Channel)
                    {
                        const double Shaded   = ((Opaque.Colour[Index] >> (8 * Channel)) & 0xff) / 255.0;
                        const double Expected = α * Shaded + (1.0 - α) * Plain.Background[Channel];
                        GhostWorst = std::max(GhostWorst, std::abs(((XRay.Colour[Index] >> (8 * Channel)) & 0xff) - Expected * 255.0));
                    }
                    ++GhostCount;
                }
            Tally(XRayGate, GhostWorst, GhostCount > 5000 && GhostWorst <= 2.0,
                  Label + ": " + std::to_string(GhostCount) + " interior face pixels = α · opaque + (1 − α) · background within " + Compact(GhostWorst) + " / 255 (α " + Compact(α) + ")");

            // ② revealed pixels: the identity changed → it must be a hidden edge (CPU: within the stroke reach, behind the face hit)
            long Revealed = 0, Explained = 0, Other = 0, Spilled = 0;
            const double Reach = (0.5 * 2.0 * Ghost.EdgeStroke + 1.0) * Frame.WorldPerPixel * 1.05;
            for (uint32_t Row = 0; Row < Height; ++Row)
                for (uint32_t Column = 0; Column < Width; ++Column)
                {
                    const size_t   Index    = size_t(Row) * Width + Column;
                    const uint32_t Identity = XRay.Identity[Index];
                    if (Identity == Opaque.Identity[Index]) continue;
                    if (!(Identity & IdentityEdgeBit)) { ++Other; continue; }
                    ++Revealed;
                    // Self-spill: the same edge has a visible stroke pixel within 2 px in the opaque frame (a rim, not a hidden run).
                    for (int DeltaRow = -2; DeltaRow <= 2; ++DeltaRow)
                    {
                        bool SpillCondition = false;
                        for (int DeltaColumn = -2; DeltaColumn <= 2 && !SpillCondition; ++DeltaColumn)
                        {
                            const int Near = int(Row) + DeltaRow, Across = int(Column) + DeltaColumn;
                            SpillCondition = Near >= 0 && Across >= 0 && Near < int(Height) && Across < int(Width) && Opaque.Identity[size_t(Near) * Width + Across] == Identity;
                        }
                        if (SpillCondition) { ++Spilled; break; }
                    }
                    if (!OrthographicCondition) continue;                                    // One world reach per pixel: orthographic only
                    const OrbitRay                 Ray  = CastOrbitRay(Frame, Column + 0.5, Row + 0.5);
                    const SolidArcEdgeApproach        Edge = ApproachSolidArcEdge(Scene, Ray, (Identity & ~IdentityEdgeBit) - 1u);
                    const std::optional<SolidArcPick> Face = PickSolidArcScene(Scene, Ray, 0.0);
                    if (Edge.Separation <= Reach && Face && Edge.Reach >= Face->Distance - 4.0 * Reach) ++Explained;
                }
            if (OrthographicCondition)
            {
                const double Rate = Revealed ? 100.0 * double(Explained) / double(Revealed) : 0.0;
                Tally(XRayGate, 100.0 - Rate, Revealed > 300 && Rate >= 99.5, Label + ": " + std::to_string(Explained) + " / " + std::to_string(Revealed) + " revealed stroke pixels are hidden edges (unexplained %)");
            }
            Tally(XRayGate, double(Other), Other == 0 && Revealed > 300, Label + ": " + std::to_string(Revealed) + " pixels changed identity to a hidden edge, " + std::to_string(Other) + " changed otherwise");
            // Rim spill: revealed pixels of an edge whose visible stroke lies within 2 px — what remains are the visible ↔ hidden
            // transitions along one edge (the occluded draw's 8 px world margin removes the rims of steep faces).
            const double SpillRate = Revealed ? 100.0 * double(Spilled) / double(Revealed) : 100.0;
            Tally(XRayGate, SpillRate, SpillRate <= 2.0, Label + ": " + std::to_string(Spilled) + " / " + std::to_string(Revealed) + " revealed pixels lie within 2 px of the same edge's visible stroke (%)");

            // ③ pick-through at the middle of the hidden back-bottom edge (y = 2.5, z = 0): the revealed pixel of that edge
            //    nearest its middle segment's midpoint; GPU probe = that edge, the x-ray CPU pick finds it, the opaque one does not
            if (OrthographicCondition)
            {
                const uint32_t ProbeEdge = IdentityNamed(Scene, "Edge(Extrude#2.Cap(Start)|Extrude#2.Side(Line3))", true);
                int ProbeX = -1, ProbeY = -1;
                if (ProbeEdge != 0u)
                {
                    const SolidArcSceneEdge&    Hidden  = Scene.Edges[(ProbeEdge & ~IdentityEdgeBit) - 1u];
                    const SolidArcSceneSegment& Segment = Scene.Segments[Hidden.FirstSegment + Hidden.SegmentCount / 2];
                    double Middle[4] = {};
                    for (int Row = 0; Row < 4; ++Row)
                        for (int Axis = 0; Axis < 3; ++Axis) Middle[Row] += Frame.ClipFromWorld[Axis * 4 + Row] * 0.5 * (Segment.Start[Axis] + Segment.End[Axis]);
                    for (int Row = 0; Row < 4; ++Row) Middle[Row] += Frame.ClipFromWorld[12 + Row];
                    const double CentreX = (Middle[0] / Middle[3] * 0.5 + 0.5) * Width, CentreY = (Middle[1] / Middle[3] * 0.5 + 0.5) * Height;
                    double Nearest = 1.0e9;
                    for (int Row = int(CentreY) - 3; Row <= int(CentreY) + 3; ++Row)
                        for (int Column = int(CentreX) - 3; Column <= int(CentreX) + 3; ++Column)
                        {
                            if (Row < 0 || Column < 0 || Row >= int(Height) || Column >= int(Width) || XRay.Identity[size_t(Row) * Width + Column] != ProbeEdge) continue;
                            const double Gap = std::hypot(Column + 0.5 - CentreX, Row + 0.5 - CentreY);
                            if (Gap < Nearest) { Nearest = Gap; ProbeX = Column; ProbeY = Row; }
                        }
                }
                const Rendered  Probed  = RenderFrame(Device, Raster, Frame, Ghost, ProbeX, ProbeY);
                const OrbitRay Ray   = CastOrbitRay(Frame, ProbeX + 0.5, ProbeY + 0.5);
                const std::optional<SolidArcPick> Through = PickSolidArcScene(Scene, Ray, 3.0 * Frame.WorldPerPixel, true);
                const std::optional<SolidArcPick> Solid   = PickSolidArcScene(Scene, Ray, 3.0 * Frame.WorldPerPixel, false);
                const bool ThroughCondition = ProbeEdge != 0u && Probed.Probe == ProbeEdge && Through && Through->Identity == ProbeEdge && Solid && Solid->Identity != ProbeEdge;
                Tally(XRayGate, 0.0, ThroughCondition, Label + ": probe (" + std::to_string(ProbeX) + ", " + std::to_string(ProbeY) + ") → " + NameSolidArcIdentity(Scene, Probed.Probe)
                      + "; CPU x-ray pick → " + (Through ? NameSolidArcIdentity(Scene, Through->Identity) : std::string("nothing")) + "; opaque pick → "
                      + (Solid ? NameSolidArcIdentity(Scene, Solid->Identity) : std::string("nothing")));

                WriteExhibit(Exhibits, "ViewportXRay", XRay, Width, Height);

                // ④ box over the whole viewport, from a slightly orbited view (the exact isometric looks edge-on at the
                //    chamfer, whose two long edges then coincide on screen): x-ray encloses every edge; without x-ray the
                //    CPU-visible edges = the edges the GPU drew. An edge drawn only as end caps where it meets a visible vertex
                //    (≤ CapPixels: two stroke ends of reach + 1 px) counts as touching, either verdict accepted.
                OrbitCamera Orbited = Camera;
                RevolveOrbitCamera(Orbited, 37.0, 11.0);
                const OrbitCameraFrame Turned = FrameOrbitCamera(Orbited, Width, Height);
                const Rendered            Seen   = RenderFrame(Device, Raster, Turned, Plain);
                constexpr long CapPixels = 16;
                std::vector<long> Drawn(Scene.Edges.size(), 0);
                for (const uint32_t Identity : Seen.Identity) if (Identity & IdentityEdgeBit) ++Drawn[(Identity & ~IdentityEdgeBit) - 1u];
                const std::vector<uint32_t> Visible = EncloseSolidArcEdges(Scene, Turned, { 0.0, 0.0 }, { double(Width), double(Height) }, false);
                const std::vector<uint32_t> Every   = EncloseSolidArcEdges(Scene, Turned, { 0.0, 0.0 }, { double(Width), double(Height) }, true);
                long Disagreements = 0, DrawnCount = 0, Touching = 0;
                std::string Missed;
                for (uint32_t Index = 0; Index < Scene.Edges.size(); ++Index)
                {
                    const bool CpuCondition = std::find(Visible.begin(), Visible.end(), Index) != Visible.end();
                    DrawnCount += Drawn[Index] > CapPixels ? 1 : 0;
                    if (Drawn[Index] > 0 && Drawn[Index] <= CapPixels) { ++Touching; continue; }
                    if (CpuCondition != (Drawn[Index] > 0)) { ++Disagreements; Missed += " " + Scene.Edges[Index].Name + " (" + std::to_string(Drawn[Index]) + " px, CPU " + (CpuCondition ? "visible)" : "hidden)"); }
                }
                Tally(XRayGate, double(Disagreements), Disagreements == 0 && Every.size() == Scene.Edges.size() && Visible.size() < Every.size(),
                      "orbited ortho: box encloses " + std::to_string(Visible.size()) + " visible edges (GPU drew " + std::to_string(DrawnCount) + ", " + std::to_string(Touching)
                      + " touching by end caps) and " + std::to_string(Every.size()) + " / " + std::to_string(Scene.Edges.size()) + " in x-ray; disagreements" + (Missed.empty() ? std::string(" none") : Missed));
            }

            // ⑤ determinism and restore: the x-ray frame twice; x-ray off again → the opaque frame bit for bit
            const Rendered Again    = RenderFrame(Device, Raster, Frame, Ghost);
            const Rendered Restored = RenderFrame(Device, Raster, Frame, Plain);
            Tally(XRayGate, 0.0, Again.Colour == XRay.Colour && Again.Identity == XRay.Identity && Restored.Colour == Opaque.Colour && Restored.Identity == Opaque.Identity,
                  Label + ": x-ray frame repeatable; off → colour and identity bit-identical to the opaque frame");
        }
    }

    // ---- V10 section: kept-side clip and caps against the CPU pick, cap mask within 1 px, grid behind caps, bit-exact restore
    {
        SolidArcViewportStyle Plain = Style;
        Plain.GridEnabled = false;
        struct Cut { const char* Label; std::array<double, 4> Plane; };
        const Cut Cuts[] = { { "y = 1.25 (keep y ≥ 1.25)", { 0.0, 1.0, 0.0, -1.25 } }, { "x = 1 (keep x ≤ 1)", { -1.0, 0.0, 0.0, 1.0 } } };
        for (const bool OrthographicCondition : { true, false })
        {
            Camera.OrthographicEnabled = OrthographicCondition;
            PresentOrbitView(Camera, OrbitViewPreset::Isometric);
            const OrbitCameraFrame Frame  = FrameOrbitCamera(Camera, Width, Height);
            const Rendered            Opaque = RenderFrame(Device, Raster, Frame, Plain);
            for (const Cut& Plane : Cuts)
            {
                const std::string Label = std::string(OrthographicCondition ? "ortho " : "persp ") + Plane.Label;
                SolidArcViewportStyle Sectioned = Plain;
                Sectioned.SectionEnabled = true;
                Sectioned.SectionPlane   = Plane.Plane;
                const SolidArcSectionClip Clip  = ComposeSolidArcSectionClip(Scene, Document, Plane.Plane);
                const Rendered            Image = RenderFrame(Device, Raster, Frame, Sectioned);
                const auto CpuIdentity = [&](double Column, double Row)
                {
                    const std::optional<SolidArcPick> Hit = PickSolidArcScene(Scene, CastOrbitRay(Frame, Column + 0.5, Row + 0.5), 0.0, false, &Clip);
                    return Hit ? Hit->Identity : 0u;
                };

                // ① interior pixels (5 × 5 one identity, no stroke) against the sectioned CPU pick
                long Compared = 0, Mismatched = 0, CapCompared = 0;
                for (uint32_t Row = 3; Row + 3 < Height; Row += 3)
                    for (uint32_t Column = 3; Column + 3 < Width; Column += 3)
                    {
                        const uint32_t Centre = Image.Identity[size_t(Row) * Width + Column];
                        bool UniformCondition = (Centre & IdentityEdgeBit) == 0u;
                        for (int DeltaRow = -2; DeltaRow <= 2 && UniformCondition; ++DeltaRow)
                            for (int DeltaColumn = -2; DeltaColumn <= 2 && UniformCondition; ++DeltaColumn)
                                UniformCondition = Image.Identity[size_t(Row + DeltaRow) * Width + Column + DeltaColumn] == Centre;
                        if (!UniformCondition) continue;
                        ++Compared;
                        if (Centre == IdentitySectionCap) ++CapCompared;
                        if (CpuIdentity(Column, Row) != Centre) ++Mismatched;
                    }
                const double Rate = Compared ? 100.0 * double(Mismatched) / double(Compared) : 100.0;
                Tally(SectionGate, Rate, Compared > 1000 && CapCompared > 100 && Rate <= 0.1,
                      Label + ": " + std::to_string(Mismatched) + " / " + std::to_string(Compared) + " interior pixels differ (" + std::to_string(CapCompared) + " on the cap) (%)");

                // ② every GPU cap pixel has a CPU cap within 1 px (the cap outline is exact up to rasterisation)
                long CapPixels = 0, Stray = 0;
                std::array<double, 3> CapSum{};
                size_t ProbeIndex = 0;
                for (uint32_t Row = 1; Row + 1 < Height; ++Row)
                    for (uint32_t Column = 1; Column + 1 < Width; ++Column)
                    {
                        const size_t Index = size_t(Row) * Width + Column;
                        if (Image.Identity[Index] != IdentitySectionCap) continue;
                        ++CapPixels;
                        const uint32_t Pixel = Image.Colour[Index];
                        CapSum[0] += (Pixel & 0xff) / 255.0; CapSum[1] += ((Pixel >> 8) & 0xff) / 255.0; CapSum[2] += ((Pixel >> 16) & 0xff) / 255.0;
                        if (CpuIdentity(Column, Row) == IdentitySectionCap) { if (ProbeIndex == 0 && CapPixels > 50) ProbeIndex = Index; continue; }
                        bool NearCondition = false;
                        for (int DeltaRow = -1; DeltaRow <= 1 && !NearCondition; ++DeltaRow)
                            for (int DeltaColumn = -1; DeltaColumn <= 1 && !NearCondition; ++DeltaColumn)
                                NearCondition = CpuIdentity(double(Column) + DeltaColumn, double(Row) + DeltaRow) == IdentitySectionCap;
                        // A multisampled border pixel names the surface under sample 0: where the CPU pick changes within
                        // 1 px (a silhouette), any surface covering part of the pixel is a correct code.
                        bool BorderCondition = false;
                        const uint32_t Middle = CpuIdentity(Column, Row);
                        for (int DeltaRow = -1; DeltaRow <= 1 && !BorderCondition && !NearCondition; ++DeltaRow)
                            for (int DeltaColumn = -1; DeltaColumn <= 1 && !BorderCondition; ++DeltaColumn)
                                BorderCondition = CpuIdentity(double(Column) + DeltaColumn, double(Row) + DeltaRow) != Middle;
                        if (!NearCondition && !BorderCondition) ++Stray;
                    }
                for (double& Channel : CapSum) Channel /= std::max<long>(CapPixels, 1);
                Tally(SectionGate, double(Stray), CapPixels > 500 && Stray == 0 && CapSum[0] > CapSum[1] + 0.15,
                      Label + ": " + std::to_string(CapPixels) + " cap pixels, mean colour (" + Compact(CapSum[0]) + ", " + Compact(CapSum[1]) + ", " + Compact(CapSum[2]) + "); without a CPU cap within 1 px (silhouettes aside)");

                // ③ the single-texel probe (hover path) on a cap pixel reads the cap
                const int      ProbeX = int(ProbeIndex % Width), ProbeY = int(ProbeIndex / Width);
                const Rendered Probed = RenderFrame(Device, Raster, Frame, Sectioned, ProbeX, ProbeY);
                Tally(SectionGate, 0.0, ProbeIndex != 0 && Probed.Probe == IdentitySectionCap && CpuIdentity(ProbeX, ProbeY) == IdentitySectionCap,
                      Label + ": probe at (" + std::to_string(ProbeX) + ", " + std::to_string(ProbeY) + ") → " + (Probed.Probe == IdentitySectionCap ? "cap" : "not the cap"));

                // ④ with the grid on, the grid (drawn first while sectioning) never shows through a cap: interior cap pixels
                //    (3 × 3 cap) unchanged; a multisampled cap border blends what lies beside it, the grid included
                SolidArcViewportStyle Gridded = Sectioned;
                Gridded.GridEnabled = true;
                const Rendered WithGrid = RenderFrame(Device, Raster, Frame, Gridded);
                long Changed = 0;
                for (uint32_t Row = 1; Row + 1 < Height; ++Row)
                    for (uint32_t Column = 1; Column + 1 < Width; ++Column)
                    {
                        bool InteriorCondition = true;
                        for (int DeltaRow = -1; DeltaRow <= 1 && InteriorCondition; ++DeltaRow)
                            for (int DeltaColumn = -1; DeltaColumn <= 1 && InteriorCondition; ++DeltaColumn)
                                InteriorCondition = Image.Identity[size_t(Row + DeltaRow) * Width + Column + DeltaColumn] == IdentitySectionCap;
                        const size_t Index = size_t(Row) * Width + Column;
                        if (InteriorCondition && (WithGrid.Identity[Index] != IdentitySectionCap || WithGrid.Colour[Index] != Image.Colour[Index])) ++Changed;
                    }
                Tally(SectionGate, double(Changed), Changed == 0, Label + ": cap pixels changed by switching the grid on");

                if (!OrthographicCondition)
                    WriteExhibit(Exhibits, std::string("ViewportSection") + (Plane.Plane[1] > 0.5 ? "Y" : "X"), WithGrid, Width, Height);
            }

            // ⑤ section off again → the unsectioned frame bit for bit
            const Rendered Restored = RenderFrame(Device, Raster, Frame, Plain);
            Tally(SectionGate, 0.0, Restored.Colour == Opaque.Colour && Restored.Identity == Opaque.Identity,
                  std::string(OrthographicCondition ? "ortho" : "persp") + ": section off → colour and identity bit-identical to the unsectioned frame");
        }
    }

    // ---- V11 target rebuilds (the interface keys its viewport texture on TargetGeneration, not on the view handle)
    {
        PresentOrbitView(Camera, OrbitViewPreset::Isometric);
        const OrbitCameraFrame Frame  = FrameOrbitCamera(Camera, Width, Height);
        const Rendered         Before = RenderFrame(Device, Raster, Frame, Style);
        const uint64_t         Built  = Raster.TargetGeneration();
        const VkImageView      View   = Raster.ColourView();
        Raster.Resize(Width, Height);
        Tally(ResizeGate, 0.0, Raster.TargetGeneration() == Built, "equal size " + std::to_string(Width) + " × " + std::to_string(Height) + ": generation holds at " + std::to_string(Built));
        Raster.Resize(Width - 317, Height - 211);
        const uint64_t Shrunk = Raster.TargetGeneration();
        Raster.Resize(Width, Height);
        const bool RecycledCondition = Raster.ColourView() == View;
        Tally(ResizeGate, 0.0, Shrunk == Built + 1 && Raster.TargetGeneration() == Built + 2,
              "resize away and back: generation " + std::to_string(Built) + " → " + std::to_string(Shrunk) + " → " + std::to_string(Raster.TargetGeneration()) +
              (RecycledCondition ? " (this driver recycled the colour view handle)" : " (new colour view handle)"));
        const Rendered After = RenderFrame(Device, Raster, Frame, Style);
        Tally(ResizeGate, 0.0, After.Colour == Before.Colour && After.Identity == Before.Identity, "frame after the rebuild = frame before (colour and identity)");
    }

    // ---- V7 determinism
    {
        PresentOrbitView(Camera, OrbitViewPreset::Isometric);
        const OrbitCameraFrame Frame = FrameOrbitCamera(Camera, Width, Height);
        const Rendered First  = RenderFrame(Device, Raster, Frame, Style);
        const Rendered Second = RenderFrame(Device, Raster, Frame, Style);
        const std::string ColourDigest   = DigestSolidArcPixels(First.Colour.data(), First.Colour.size());
        const std::string IdentityDigest = DigestSolidArcPixels(First.Identity.data(), First.Identity.size());
        const bool EqualCondition = ColourDigest == DigestSolidArcPixels(Second.Colour.data(), Second.Colour.size()) &&
                                    IdentityDigest == DigestSolidArcPixels(Second.Identity.data(), Second.Identity.size());
        Tally(DeterminismGate, 0.0, EqualCondition, "colour " + ColourDigest + " · identity " + IdentityDigest);
    }

    // ---- report
    const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
    std::printf("# SolidArcViewportValidation\n\n");
    std::printf("Viewport of SolidArc on %s — specimen: %zu faces, %zu edges, %zu triangles; %u × %u; slowest frame %.1f ms (%.1f s total).\n\n",
                Device.Describe().c_str(), Scene.Faces.size(), Scene.Edges.size(), Scene.Corners.size() / 3, Width, Height, SlowestMs, Seconds);
    std::printf("| Gate | Subject | Limit | Worst | Checks | Verdict |\n|------|---------|-------|-------|--------|---------|\n");
    long Failures = 0;
    for (const GateTally* Gate : { &DeviceGate, &FaceGate, &EdgeGate, &ProbeGate, &TintGate, &ViewGate, &DeterminismGate, &MatcapGate, &XRayGate, &SectionGate, &ResizeGate })
    {
        std::printf("| %s | %s | %s | %.4g | %ld | %s |\n", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures ? "🔴" : "🟢");
        Failures += Gate->Failures;
    }
    std::printf("\n## Walkthrough\n\n| Gate | Check | Value | Verdict |\n|------|-------|-------|---------|\n");
    for (const std::string& Row : Journal) std::printf("%s\n", Row.c_str());
    Raster.Destroy();
    return Failures == 0 ? 0 : 1;
}
