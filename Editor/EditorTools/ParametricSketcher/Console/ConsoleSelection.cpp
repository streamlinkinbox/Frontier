//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Console/ConsoleSelection.cpp — Phase 4: pick / box selection, select modes, hide / isolate, undo / redo, duplicate
//============================================================================================================================================
// Selection goes through the pick plane of the raster, exactly what a Vulkan id attachment will give later. Whole mode
//    picks whole figure; control mode picks poles (their pick id carries the pole index in the high 16 bits). Box selection
//    walks the pick plane inside the rectangle — no separate frustum code to keep in step with the drawing.
#include "ConsoleHost.h"
#include "Presentation/ScenePresentation.h"
#include <algorithm>
#include <set>

namespace Frontier
{

namespace
{
    void ToggleIndex(std::vector<int>& S, int Index, bool On) noexcept
    {
        auto It = std::find(S.begin(), S.end(), Index);
        if (On && It == S.end()) S.push_back(Index);
        if (!On && It != S.end()) S.erase(It);
    }
    void TogglePole(SceneFigure& Figure, int Pole, bool On) noexcept { ToggleIndex(Figure.SelectedPoles, Pole, On); }
}

void ConsoleHost::HoverAtPixel(double X, double Y) noexcept
{
    if (X < 0 || Y < 0 || X >= Surface->Width() || Y >= Surface->Height()) return;
    Render();
    uint32_t Pick = Surface->Pick(uint32_t(X), uint32_t(Y));
    const auto Part = SceneDocument::PartOf(Pick);
    if (Mode == SelectMode::Whole || Mode == SelectMode::Face) { if (Part == SceneDocument::PickPart::Pole || Part == SceneDocument::PickPart::Edge) Pick = SceneDocument::PickOf(SceneDocument::IdentityOf(Pick)); }
    if (Mode == SelectMode::Control && Part != SceneDocument::PickPart::Pole) Pick = SceneDocument::PickOf(SceneDocument::IdentityOf(Pick));
    if (Mode == SelectMode::Edge && Part != SceneDocument::PickPart::Edge) Pick = SceneDocument::PickOf(SceneDocument::IdentityOf(Pick));
    if (Pick == HoverPick) return;
    HoverPick = Pick;
    if (SketchArea* A = Scene.FindArea(SceneDocument::IdentityOf(Pick))) { Row("hover area a%u (%s, %.4f)", A->Identity - SceneDocument::AreaIdentityBase, A->Filled ? "filled" : "empty", A->Cell.Area); return; }
    if (SceneFigure* I = Scene.Find(SceneDocument::IdentityOf(Pick)))
    {
        int Pole = SceneDocument::PoleOf(Pick), Face = SceneDocument::FaceOf(Pick), Edge = SceneDocument::EdgeOf(Pick);
        if (Pole >= 0) Row("hover #%u %s pole %d", I->Identity, I->Name.c_str(), Pole);
        else if (Face >= 0 && Mode == SelectMode::Face) Row("hover #%u %s face %d", I->Identity, I->Name.c_str(), Face);
        else if (Edge >= 0) Row("hover #%u %s edge %d", I->Identity, I->Name.c_str(), Edge);
        else Row("hover #%u %s", I->Identity, I->Name.c_str());
    }
    else Row("hover nothing");
}

bool ConsoleHost::SelectAtPixel(double X, double Y, bool Toggle) noexcept
{
    Render();
    uint32_t Pick = Surface->Pick(uint32_t(X), uint32_t(Y));
    uint32_t Id = SceneDocument::IdentityOf(Pick); int Pole = SceneDocument::PoleOf(Pick);
    SceneFigure* Figure = Scene.Find(Id);
    if (!Toggle) Scene.ClearSelection();
    if (SketchArea* A = Scene.FindArea(Id))
    {
        A->Selected = Toggle ? !A->Selected : true;
        Row("click (%d,%d): area a%u %s  (%s, area %.4f, %zu hole(s))  ·  %d area(s) selected", int(X), int(Y), A->Identity - SceneDocument::AreaIdentityBase, A->Selected ? "selected" : "deselected", A->Filled ? "filled" : "empty", A->Cell.Area, A->Cell.Holes.size(), Scene.SelectedAreaCount());
        return true;
    }
    if (!Figure) { Row("click (%d,%d): nothing", int(X), int(Y)); return false; }
    if (Mode == SelectMode::Control)
    {
        if (Pole < 0)
        {
            // Clicking a body in control mode selects the figure so its cage appears (Blender: enter edit on it).
            Figure->Selected = true; Row("click (%d,%d): #%u %s — cage shown, click its poles", int(X), int(Y), Figure->Identity, Figure->Name.c_str());
            return true;
        }
        const bool On = Toggle ? !Figure->PoleSelected(Pole) : true;
        TogglePole(*Figure, Pole, On);
        Figure->Selected = true;
        Vec3 P = Figure->PolePosition(Pole);
        Row("pole %d of #%u %s %s  (%.4f %.4f %.4f)  ·  %d pole(s) selected", Pole, Figure->Identity, Figure->Name.c_str(), On ? "selected" : "deselected", P.X, P.Y, P.Z, Scene.SelectedPoleCount());
        return true;
    }
    if (Figure->Classification == FigureClassification::Body && Mode == SelectMode::Face)
    {
        int Face = SceneDocument::FaceOf(Pick);
        if (Face < 0) { Row("click (%d,%d): #%u %s has no face here", int(X), int(Y), Figure->Identity, Figure->Name.c_str()); return false; }
        const bool On = Toggle ? !Figure->FaceSelected(Face) : true;
        ToggleIndex(Figure->SelectedFaces, Face, On);
        const BrepFace& F = Figure->Body.Faces[Face];
        Row("face %d of #%u %s %s  (%s, %zu loop(s))  ·  %d face(s) selected", Face, Figure->Identity, Figure->Name.c_str(), On ? "selected" : "deselected", Describe(F.Surface.Classification), F.Loops.size(), Scene.SelectedFaceCount());
        return true;
    }
    if (Figure->Classification == FigureClassification::Body && Mode == SelectMode::Edge)
    {
        int Edge = SceneDocument::EdgeOf(Pick);
        if (Edge < 0) { Row("click (%d,%d): #%u %s — no edge under the pointer (edges pick within their line width)", int(X), int(Y), Figure->Identity, Figure->Name.c_str()); return false; }
        const bool On = Toggle ? !Figure->EdgeSelected(Edge) : true;
        ToggleIndex(Figure->SelectedEdges, Edge, On);
        const BrepEdge& E = Figure->Body.Edges[Edge];
        Row("edge %d of #%u %s %s  (%s, length %.4f, %zu face(s))  ·  %d edge(s) selected", Edge, Figure->Identity, Figure->Name.c_str(), On ? "selected" : "deselected", Describe(E.Curve.Classification), E.Curve.Length(), E.Coedges.size(), Scene.SelectedEdgeCount());
        return true;
    }
    Figure->Selected = Toggle ? !Figure->Selected : true;
    DescribeFigure(*Figure);
    Row("%d selected", Scene.SelectedCount());
    return true;
}

int ConsoleHost::SelectInRectangle(double X0, double Y0, double X1, double Y1, bool Toggle, bool Subtract) noexcept
{
    Render();
    if (!Toggle && !Subtract) Scene.ClearSelection();
    const uint32_t Ax = uint32_t(std::clamp(std::min(X0, X1), 0.0, double(Surface->Width() - 1)));
    const uint32_t Bx = uint32_t(std::clamp(std::max(X0, X1), 0.0, double(Surface->Width() - 1)));
    const uint32_t Ay = uint32_t(std::clamp(std::min(Y0, Y1), 0.0, double(Surface->Height() - 1)));
    const uint32_t By = uint32_t(std::clamp(std::max(Y0, Y1), 0.0, double(Surface->Height() - 1)));
    std::set<uint32_t> Picks;
    for (uint32_t Y = Ay; Y <= By; ++Y) for (uint32_t X = Ax; X <= Bx; ++X) { uint32_t P = Surface->Pick(X, Y); if (P) Picks.insert(P); }
    int Changed = 0;
    for (uint32_t P : Picks)
    {
        SceneFigure* Figure = Scene.Find(SceneDocument::IdentityOf(P)); if (!Figure) continue;
        int Pole = SceneDocument::PoleOf(P);
        if (Mode == SelectMode::Control)
        {
            if (Pole < 0) continue;                                                     // box in control mode only takes poles
            TogglePole(*Figure, Pole, !Subtract); Figure->Selected = true; ++Changed;
        }
        else if (Mode == SelectMode::Face && Figure->Classification == FigureClassification::Body)
        {
            int Face = SceneDocument::FaceOf(P); if (Face < 0) continue;
            if (Figure->FaceSelected(Face) != Subtract) continue;
            ToggleIndex(Figure->SelectedFaces, Face, !Subtract); ++Changed;
        }
        else if (Mode == SelectMode::Edge && Figure->Classification == FigureClassification::Body)
        {
            int Edge = SceneDocument::EdgeOf(P); if (Edge < 0) continue;
            if (Figure->EdgeSelected(Edge) != Subtract) continue;
            ToggleIndex(Figure->SelectedEdges, Edge, !Subtract); ++Changed;
        }
        else if (Figure->Selected == Subtract)
        {
            Figure->Selected = !Subtract; ++Changed;
        }
    }
    return Changed;
}

void ConsoleHost::RegisterSelection() noexcept
{
    auto Add = [&](const char* Verb, const char* Help, Command Fn) { Commands[Verb] = std::move(Fn); Usage[Verb] = Help; };
    auto Number = [&](const CommandLine& C, size_t I, double& Out) -> bool { auto N = C.Number(I); if (!N) return false; Out = *N; return true; };

    Add("select", "select <figure...> | all | none | invert  ·  select box x0 y0 x1 y1 [--add|--subtract]  ·  select poles|faces|edges <figure> <i...>|all|none [--add]", [=, this](const CommandLine& C)
    {
        if (C.Count() == 1 && C.Arguments[0] == "none") { Scene.ClearSelection(); Row("selection cleared"); return true; }
        // sketch areas: `select a0 a3 [--add]`
        {
            bool AllAreas = C.Count() > 0; std::vector<SketchArea*> Picked;
            for (const std::string& T : C.Arguments)
            {
                SketchArea* A = (T.size() > 1 && (T[0] == 'a' || T[0] == 'A') && std::isdigit(static_cast<unsigned char>(T[1]))) ? Scene.FindArea(SceneDocument::AreaIdentityBase + uint32_t(std::atoi(T.c_str() + 1))) : nullptr;
                if (!A) { AllAreas = false; break; }
                Picked.push_back(A);
            }
            if (AllAreas)
            {
                if (!C.Switch("add")) Scene.ClearSelection();
                for (SketchArea* A : Picked) { A->Selected = true; Row("  area a%u selected (%s, %.4f)", A->Identity - SceneDocument::AreaIdentityBase, A->Filled ? "filled" : "empty", A->Cell.Area); }
                return true;
            }
        }
        if (C.Count() == 1 && C.Arguments[0] == "invert")
        {
            if (Mode == SelectMode::Control)
            {
                for (SceneFigure& I : Scene.Figures())
                {
                    if (!I.Selected) continue;
                    std::vector<int> Inv; for (int P = 0; P < I.PoleCount(); ++P) if (!I.PoleSelected(P)) Inv.push_back(P);
                    I.SelectedPoles = Inv;
                }
            }
            else
            {
                for (SceneFigure& I : Scene.Figures()) if (!I.Hidden) I.Selected = !I.Selected;
            }
        }
        else if (C.Count() >= 1 && C.Arguments[0] == "box")
        {
            double V[4]; for (int I = 0; I < 4; ++I) if (!Number(C, 1 + I, V[I])) return Refuse("select box: x0 y0 x1 y1 required");
            int N = SelectInRectangle(V[0], V[1], V[2], V[3], C.Switch("add"), C.Switch("subtract"));
            Row("box (%d,%d)-(%d,%d): %d change(s)", int(V[0]), int(V[1]), int(V[2]), int(V[3]), N);
        }
        else if (C.Count() >= 3 && (C.Arguments[0] == "faces" || C.Arguments[0] == "edges"))
        {
            const bool Faces = C.Arguments[0] == "faces";
            SceneFigure* I = Resolve(C.Arguments[1]); if (!I) return Refuse("no figure '%s'", C.Arguments[1].c_str());
            if (I->Classification != FigureClassification::Body) return Refuse("select %s: '%s' is not a body", C.Arguments[0].c_str(), I->Name.c_str());
            std::vector<int>& Target = Faces ? I->SelectedFaces : I->SelectedEdges;
            const int Count = Faces ? int(I->Body.Faces.size()) : int(I->Body.Edges.size());
            if (!C.Switch("add")) for (SceneFigure& J : Scene.Figures()) { J.SelectedFaces.clear(); J.SelectedEdges.clear(); }
            if (C.Arguments[2] == "all") { Target.clear(); for (int K = 0; K < Count; ++K) Target.push_back(K); }
            else if (C.Arguments[2] == "none") Target.clear();
            else for (size_t K = 2; K < C.Count(); ++K)
            {
                double Ix; if (!Number(C, K, Ix) || Ix < 0 || Ix >= Count) return Refuse("select %s: index %s out of range 0..%d", C.Arguments[0].c_str(), C.Arguments[K].c_str(), Count - 1);
                ToggleIndex(Target, int(Ix), true);
            }
            SelectMode Want = Faces ? SelectMode::Face : SelectMode::Edge;
            if (Mode != Want) { Mode = Want; Row("select mode %s", SelectModeName(Mode)); }
        }
        else if (C.Count() >= 2 && C.Arguments[0] == "poles")
        {
            SceneFigure* I = Resolve(C.Arguments[1]); if (!I) return Refuse("no figure '%s'", C.Arguments[1].c_str());
            if (!C.Switch("add")) for (SceneFigure& J : Scene.Figures()) J.SelectedPoles.clear();
            I->Selected = true;
            if (C.Count() == 3 && C.Arguments[2] == "all") { I->SelectedPoles.clear(); for (int P = 0; P < I->PoleCount(); ++P) I->SelectedPoles.push_back(P); }
            else if (C.Count() == 3 && C.Arguments[2] == "none") I->SelectedPoles.clear();
            else for (size_t K = 2; K < C.Count(); ++K)
            {
                double P; if (!Number(C, K, P) || P < 0 || P >= I->PoleCount()) return Refuse("select poles: index %s out of range 0..%d", C.Arguments[K].c_str(), I->PoleCount() - 1);
                TogglePole(*I, int(P), true);
            }
            if (Mode != SelectMode::Control) { Mode = SelectMode::Control; Row("select mode control"); }
        }
        else
        {
            std::vector<SceneFigure*> Items = ResolveMany(C, 0); if (Items.empty()) return Refuse("select: nothing matched");
            if (!C.Switch("add")) Scene.ClearSelection();
            for (SceneFigure* I : Items) I->Selected = true;
        }
        int N = 0; for (const SceneFigure& I : Scene.Figures()) if (I.Selected) { ++N; DescribeFigure(I); }
        std::string Sub;
        if (Scene.SelectedPoleCount()) Sub += ", " + std::to_string(Scene.SelectedPoleCount()) + " pole(s)";
        if (Scene.SelectedFaceCount()) Sub += ", " + std::to_string(Scene.SelectedFaceCount()) + " face(s)";
        if (Scene.SelectedEdgeCount()) Sub += ", " + std::to_string(Scene.SelectedEdgeCount()) + " edge(s)";
        Row("%d selected%s", N, Sub.c_str());
        return true;
    });
    Add("delete", "delete <figure...> | selected | empty <name> | empty all  — remove figures from the scene. Phase 19 added `delete empty <name>|all` for transform handles.", [=, this](const CommandLine& C)
    {
        // Phase 19: handle `delete empty <name>|all` separately so Empties can be removed without
        //    touching real geometry. This runs before ResolveMany so `empty` isn't a figure name.
        if (C.Count() >= 1 && C.Arguments[0] == "empty")
        {
            if (C.Count() < 2) return Refuse("delete empty: a name or `all` required");
            const std::string& Target = C.Arguments[1];
            size_t Before = Scene.Figures().size();
            if (Target == "all")
            {
                for (size_t I = Scene.Figures().size(); I > 0; --I)
                {
                    auto& F = Scene.Figures()[I - 1];
                    if (F.Classification == FigureClassification::Empty) Scene.Remove(F.Identity);
                }
            }
            else
            {
                const SceneFigure* F = Resolve(Target);
                if (!F) return Refuse("delete empty: no figure '%s'", Target.c_str());
                if (F->Classification != FigureClassification::Empty) return Refuse("delete empty: '%s' is not an empty", Target.c_str());
                Scene.Remove(F->Identity);
            }
            Row("delete empty: removed %zu figure(s)", Before - Scene.Figures().size());
            return true;
        }
        std::vector<uint32_t> Ids; for (SceneFigure* I : ResolveMany(C, 0)) Ids.push_back(I->Identity);
        if (Ids.empty()) return Refuse("delete: nothing selected");
        for (uint32_t Id : Ids) Scene.Remove(Id);
        Row("deleted %zu figure(s)", Ids.size());
        return true;
    });
    Add("hide", "hide <figure...> | selected | unselected", [=, this](const CommandLine& C)
    {
        int N = 0;
        if (C.Count() == 1 && C.Arguments[0] == "unselected") { for (SceneFigure& I : Scene.Figures()) if (!I.Selected && !I.Hidden) { I.Hidden = true; ++N; } }
        else for (SceneFigure* I : ResolveMany(C, 0)) { I->Hidden = true; I->Selected = false; I->SelectedPoles.clear(); ++N; }
        Row("hidden %d figure(s)", N);
        return true;
    });
    Add("unhide", "unhide <figure...> | all", [=, this](const CommandLine& C)
    {
        int N = 0; for (SceneFigure* I : ResolveMany(C, 0)) if (I->Hidden) { I->Hidden = false; ++N; }
        Row("unhidden %d figure(s)", N);
        return true;
    });
    Add("isolate", "isolate [figure...] — hide everything else (Plasticity: Shift+H)  ·  isolate off", [=, this](const CommandLine& C)
    {
        if (C.Count() == 1 && C.Arguments[0] == "off") { for (SceneFigure& I : Scene.Figures()) I.Hidden = false; Row("isolate off"); return true; }
        std::vector<SceneFigure*> Keep = ResolveMany(C, 0); if (Keep.empty()) return Refuse("isolate: nothing selected");
        for (SceneFigure& I : Scene.Figures()) I.Hidden = std::find(Keep.begin(), Keep.end(), &I) == Keep.end();
        Row("isolated %zu figure(s)", Keep.size());
        return true;
    });
    Add("duplicate", "duplicate [figure...] [(dx,dy,dz)] — copy, select the copies (Shift+D)", [=, this](const CommandLine& C)
    {
        Vec3 Offset; bool WithOffset = false;
        CommandLine Sub = C;
        if (C.Count() && C.Arguments.back().front() == '(') { auto P = CommandCodec::ParsePoint(C.Arguments.back()); if (!P) return Refuse("duplicate: bad offset"); Offset = *P; WithOffset = true; Sub.Arguments.pop_back(); }
        std::vector<SceneFigure*> Src = ResolveMany(Sub, 0); if (Src.empty()) return Refuse("duplicate: nothing selected");
        std::vector<uint32_t> Ids; for (SceneFigure* I : Src) Ids.push_back(I->Identity);
        Scene.ClearSelection();
        for (uint32_t Id : Ids)
        {
            SceneFigure Copy = *Scene.Find(Id);
            SceneFigure& D = Scene.Duplicate(Copy);
            if (WithOffset) D.Transform(Mat4::Translation(Offset));
            D.Selected = true; DescribeFigure(D);
        }
        Row("duplicated %zu figure(s)%s", Ids.size(), WithOffset ? "" : " in place — G to move");
        return true;
    });
    Add("undo", "undo [n] — step back (Ctrl+Z)", [=, this](const CommandLine& C)
    {
        double N = 1; (void)Number(C, 0, N);
        int Done = 0; std::string Last;
        for (int I = 0; I < int(N) && Undo.CanUndo(); ++I) { Last = Undo.Undo(Scene); ++Done; }
        if (!Done) return Refuse("undo: nothing to undo");
        // Phase 15: dims live in the host (not the scene), so `undo` doesn't restore them by itself.
        //    Re-emit the auto dim set so the dim tree matches the rolled-back figures.
        ReemitAllDimensions();
        Row("undo %d → '%s' reverted  ·  %zu undo / %zu redo", Done, Last.c_str(), Undo.UndoEntries().size(), Undo.RedoEntries().size());
        return true;
    });
    Add("redo", "redo [n] — step forward (Ctrl+Shift+Z / Ctrl+Y)", [=, this](const CommandLine& C)
    {
        double N = 1; (void)Number(C, 0, N);
        int Done = 0; std::string Last;
        for (int I = 0; I < int(N) && Undo.CanRedo(); ++I) { Last = Undo.Redo(Scene); ++Done; }
        if (!Done) return Refuse("redo: nothing to redo");
        // Phase 15: same as undo — re-emit dims so the dim tree tracks the re-applied figures.
        ReemitAllDimensions();
        Row("redo %d → '%s' reapplied  ·  %zu undo / %zu redo", Done, Last.c_str(), Undo.UndoEntries().size(), Undo.RedoEntries().size());
        return true;
    });
    Add("timeline", "timeline — list undo / redo entries", [=, this](const CommandLine&)
    {
        size_t K = 0;
        for (const auto& E : Undo.UndoEntries()) Row("%3zu  %s", ++K, E.Label.c_str());
        Row("── now ── (%zu undo, %zu redo)", Undo.UndoEntries().size(), Undo.RedoEntries().size());
        for (auto It = Undo.RedoEntries().rbegin(); It != Undo.RedoEntries().rend(); ++It) Row("  ↷  %s", It->Label.c_str());
        return true;
    });
    Add("clear", "clear — empty the scene (undoable)", [=, this](const CommandLine&) { Scene.Clear(); Row("scene cleared"); return true; });
}

} // namespace Frontier
