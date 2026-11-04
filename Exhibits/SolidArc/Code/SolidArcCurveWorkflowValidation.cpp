// 🧩 Exact pointer splits, target-only live distance edits, atomic refusals and reusable dock panels.
#include "UI/SolidArc/Interface/SolidArcInterface.h"
#include "UI/SolidArc/Interface/SolidArcScript.h"
#include "UI/SolidArc/Document/SolidArcDocumentCodec.h"
#include "UI/SolidArc/Viewport/SolidArcSurfaceTracing.h"
#include <imgui_internal.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace Frontier;
using namespace Frontier::SolidArc;
using namespace Frontier::GeometryProcessing;

namespace
{
int Checks = 0;
void Require(bool Condition, const char* Description)
{
    if (!Condition) throw std::runtime_error(Description);
    ++Checks;
    std::cout << "PASS " << Description << std::endl;
}
std::string Curve(SolidArcDocument& Document, const std::vector<Vector3>& Poles)
{
    const auto Trace = MakeSolidArcControlTrace(Poles, SolidArcDrawingKind::Curve, SketchSplineForm::Nurbs, false);
    Require(DrawSolidArcProjectedCurve(Document, Trace.Curve, false, false, .001).AcceptedCondition, "draw exact control trace");
    return Document.Selection.Curves.front();
}
struct CurveInteraction
{
    ImGuiContext* Context = nullptr;
    SolidArcWorkspaceState Workspace;
    CurveInteraction()
    {
        Context = ImGui::CreateContext();
        ImGui::GetIO().DisplaySize = ImVec2(1366, 900);
        ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        SolidArcOptions Options;
        Options.RecoveryPath.clear();
        ConfigureSolidArcInterface(Options);
        InitialiseSolidArcWorkspace(Workspace, Options);
        Workspace.WelcomeOpen = false;
    }
    ~CurveInteraction() { ImGui::DestroyContext(Context); }
    void Tick()
    {
        ImGui::GetIO().DeltaTime = 1.f / 60;
        ImGui::NewFrame();
        DrawSolidArcInterface(Workspace, ImTextureID(1), 1.0 / 60);
        ImGui::Render();
        if (const auto* Draw = ImGui::GetDrawData(); Draw && Draw->Textures)
            for (auto* Texture : *Draw->Textures)
                if (Texture->Status == ImTextureStatus_WantCreate || Texture->Status == ImTextureStatus_WantUpdates)
                {
                    Texture->SetTexID(ImTextureID(100));
                    Texture->SetStatus(ImTextureStatus_OK);
                }
    }
    void Script(const std::string& Text)
    {
        auto Playback = ParseSolidArcScript(Text);
        Require(Playback.Refusal.empty(), "interaction script parses");
        while (!FinishedSolidArcScript(Playback)) { (void)AdvanceSolidArcScript(Playback, Workspace); Tick(); }
        for (const auto& Expectation : Playback.Expectations)
        {
            if (!Expectation.Passed) std::cerr << Expectation.Text << ": " << Expectation.Observed << "; " << Workspace.Status << std::endl;
            Require(Expectation.Passed, Expectation.Text.c_str());
        }
    }
};
}

int main()
{
    try
    {
        SolidArcDocument Document;
        const auto Label = Curve(Document, {{-1, 0, .1}, {-.4, .8, .4}, {.3, -.5, -.2}, {1, .1, .3}});
        const auto Original = *Document.Regeneration.Curves.at(Label).Curve.Definition;
        const auto Before = EncodeSolidArcDocument(Document);
        const auto UndoCount = Document.UndoEntries.size();
        constexpr double Parameter = .371;
        Require(SplitSolidArcSpaceCurveAt(Document, Label, Parameter).AcceptedCondition, "split inside a curved 3D NURBS");
        Require(Document.Regeneration.Curves.size() == 2 && Document.Selection.Curves.size() == 2, "both exact pieces retained and selected");
        const auto& First = *Document.Regeneration.Curves.at(Label).Curve.Definition;
        const auto& Second = *Document.Regeneration.Curves.at(Document.Selection.Curves.back()).Curve.Definition;
        double Worst = 0;
        for (int Index = 0; Index <= 100; ++Index)
        {
            const double Share = Index / 100.0;
            Worst = std::max(Worst, Distance(EvaluateNurbsCurve(First, Share).Position, EvaluateNurbsCurve(Original, Share * Parameter).Position));
            Worst = std::max(Worst, Distance(EvaluateNurbsCurve(Second, Share).Position, EvaluateNurbsCurve(Original, Parameter + Share * (1 - Parameter)).Position));
        }
        Require(Worst < 1.e-10, "202-sample split comparison preserves the NURBS shape below 1e-10 metres");
        Require(Distance(First.Poles.back(), Second.Poles.front()) < 1.e-12, "pieces share the exact split endpoint");
        Require(Document.UndoEntries.size() == UndoCount + 1, "split is one undo operation");
        Require(UndoSolidArcCommand(Document) && EncodeSolidArcDocument(Document) == Before, "undo restores byte-identical original document");
        Require(RedoSolidArcCommand(Document) && Document.Regeneration.Curves.size() == 2, "redo restores both pieces");
        const auto SplitText = EncodeSolidArcDocument(Document);
        const auto Reloaded = DecodeSolidArcDocument(SplitText);
        Require(Reloaded.Refusal.empty() && Reloaded.Tree.Steps.size() == 2, "both split definitions survive document serialization");
        const auto Intact = EncodeSolidArcDocument(Document);
        const auto UndoIntact = Document.UndoEntries.size();
        Require(!SplitSolidArcSpaceCurveAt(Document, Label, 0).AcceptedCondition, "endpoint split refuses");
        Require(!SplitSolidArcSpaceCurveAt(Document, Label, std::numeric_limits<double>::quiet_NaN()).AcceptedCondition, "nonfinite split refuses");
        Require(EncodeSolidArcDocument(Document) == Intact && Document.UndoEntries.size() == UndoIntact, "refusals leave geometry and undo history intact");
        Document.Appearance.LockedFigures.insert(Label);
        Require(!SplitSolidArcSpaceCurveAt(Document, Label, .5).AcceptedCondition, "locked curve split refuses");

        {
            CurveInteraction Interaction;
            auto& Workspace = Interaction.Workspace;
            const auto Picked = Curve(Workspace.Document, {{-1, 0, 0}, {1, 0, 0}});
            RefreshSolidArcScene(Workspace, false);
            FitSolidArcCamera(Workspace.Camera, Workspace.Scene);
            Workspace.PoleDrag.PickedLabel = Picked;
            Workspace.PoleDrag.PickedPoles = {1};
            Interaction.Script("frames 4\nkey Keypad7\nframes 3\nclick @SketchCut\nmove world 0.23 0 0\nframes 3\nexpect trim-hover > 0\nexpect trim-breaks == 1\nclick world 0.23 0 0\nframes 3\nexpect curves == 2\nexpect trim-commits == 1\nkey Escape\n");
            Require(Workspace.PoleDrag.PickedPoles.empty() && Workspace.PoleDrag.PickedLabel.empty(), "split clears stale component indices instead of leaving a misleading pole selection");
            const auto& Piece = *Workspace.Document.Regeneration.Curves.at(Picked).Curve.Definition;
            Require(std::abs(Piece.Poles.back().X - .23) < .004, "viewport click splits at the mouse, not an existing pole or midpoint");
            Require(Workspace.WorkspacePanels.QueryOpen("solidarc-outliner") && Workspace.WorkspacePanels.QueryOpen("solidarc-viewport") && Workspace.WorkspacePanels.QueryOpen("solidarc-inspector"), "three primary panels registered in Frontier docking");
            const auto Layout = Workspace.WorkspacePanels.EncodeLayout();
            std::string Diagnostic;
            Require(PanelDockspace::ValidateLayout(Layout, Diagnostic), "live SolidArc dock layout uses the existing validated archive format");
            Interaction.Script("drag left @PanelTabOutliner by 320 180 8\nframes 4\n");
            const auto* Floating = ImGui::FindWindowByName("Outliner###FrontierPanel-solidarc-outliner");
            Require(Floating && Floating->DockId == 0, "dragging a real tab floats its panel through Frontier docking");
            Interaction.Script("click @CurveGuide\nframes 2\n");
            Require(Workspace.CurveGuideVisible, "workflow guide opens from the always-visible status bar");
            Workspace.CurveGuideVisible = false;
            Workspace.WorkspacePanels.ConfigureOpen("solidarc-outliner", false);
            Interaction.Script("click @ResetPanels\nframes 3\n");
            Require(Workspace.WorkspacePanels.QuerySeated() && Workspace.WorkspacePanels.QueryOpen("solidarc-outliner"), "reset reopens hidden panels and reconstructs the reusable dockspace");
            const auto* Implicit = ImGui::FindWindowByName("Debug##Default");
            Require(!Implicit || !Implicit->Active, "panel extent registration never exposes ImGui implicit Debug window");
        }
        {
            CurveInteraction Interaction;
            auto& Workspace = Interaction.Workspace;
            SolidArcConstructRequest Block;
            Block.Width = Block.Depth = 2; Block.Height = 1;
            Require(ConstructSolidArcPrimitive(Workspace.Document, Block).AcceptedCondition, "reference fixture constructs");
            const auto Reference = Workspace.Document.Regeneration.Bodies.front().Key;
            Workspace.Document.Appearance.LockedFigures.insert(Reference);
            const auto Guide = Curve(Workspace.Document, {{.3, .2, 1}, {.3, 1.8, 1}});
            const auto Target = Curve(Workspace.Document, {{.7, .3, 1}, {.7, 1.7, 1}});
            Require(LinkSolidArcSurfaceOffset(Workspace.Document, Guide, Target, Reference, .2, .01).AcceptedCondition, "live target fixture links");
            Workspace.Document.Selection.Curves = {Target};
            RefreshSolidArcScene(Workspace, false);
            Interaction.Script("frames 4\nclick @OffsetDistance\nkey A Ctrl\ntype 0.35\nkey Enter\nframes 4\nexpect offset-linked-count == 1\nexpect last-curve-mid-x > 0.63\nexpect last-curve-mid-x < 0.67\nexpect refused == 0\n");
            Require(!Workspace.SurfaceOffsetFeedback.empty() && !Workspace.SurfaceOffsetFailed, "target-only distance edit shows persistent success feedback");
            const auto BeforeInvalid = EncodeSolidArcDocument(Workspace.Document);
            Interaction.Script("click @OffsetDistance\nkey A Ctrl\ntype -0.1\nkey Enter\nframes 3\n");
            Require(Workspace.SurfaceOffsetFailed && Workspace.SurfaceOffsetFeedback.find("Not applied") != std::string::npos, "invalid target-only distance reports visible refusal");
            Require(EncodeSolidArcDocument(Workspace.Document) == BeforeInvalid, "invalid distance keeps the previously applied curve");
            Require(!SplitSolidArcSpaceCurveAt(Workspace.Document, Target, .5).AcceptedCondition, "live target cannot be destructively split");
            Require(!SplitSolidArcSpaceCurveAt(Workspace.Document, Guide, .5).AcceptedCondition, "guide with a live dependent cannot be destructively split");
        }
        std::cout << "PASS " << Checks << " curve workflow checks\n";
        return 0;
    }
    catch (const std::exception& Failure) { std::cerr << "FAIL " << Failure.what() << '\n'; return 1; }
}
