//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Document/FigureRecipe.cpp — Resolving recipe inputs and rebuilding derived figures
//============================================================================================================================================
#include "FigureRecipe.h"
#include "SceneDocument.h"
#include <cmath>
#include <cstring>

namespace Frontier
{

const char* Describe(RecipeOperation Operation) noexcept
{
    switch (Operation)
    {
        case RecipeOperation::Extrude: return "extrude";
        case RecipeOperation::Revolve: return "revolve";
        case RecipeOperation::Loft:    return "loft";
        case RecipeOperation::Sweep:   return "sweep";
        case RecipeOperation::Pipe:    return "pipe";
        case RecipeOperation::Patch:   return "patch";
        case RecipeOperation::FairPatch: return "fairpatch";
        default:                       return "authored";
    }
}

namespace
{
    inline void Mix(uint64_t& H, uint64_t V) noexcept { H ^= V + 0x9e3779b97f4a7c15ull + (H << 6) + (H >> 2); }
    inline uint64_t Bits(double D) noexcept { uint64_t U; std::memcpy(&U, &D, sizeof U); return U; }
    void MixCurve(uint64_t& H, const NurbsCurve& C) noexcept
    {
        Mix(H, C.Degree); for (const Vec4& P : C.Poles) { Mix(H, Bits(P.X)); Mix(H, Bits(P.Y)); Mix(H, Bits(P.Z)); Mix(H, Bits(P.W)); }
        for (double K : C.Knots) Mix(H, Bits(K));
    }
}

std::string RecipeInput::Label(const SceneDocument& Scene) const noexcept
{
    std::string Out;
    auto NameOf = [&](uint32_t Id)
    {
        for (const SceneFigure& F : Scene.Figures()) if (F.Identity == Id) return F.Name;
        std::string Gone = "#"; Gone += std::to_string(Id); Gone += "(gone)"; return Gone;
    };
    if (Figures.empty()) return "?";
    if (Shape == Form::Curve) { for (size_t I = 0; I < Figures.size(); ++I) { if (I) Out += '+'; Out += NameOf(Figures[I]); } return Out; }
    if (Shape == Form::Edge) { Out = NameOf(Figures.front()); Out += " edge "; Out += std::to_string(Edge); return Out; }
    Out = "area{";
    for (size_t I = 0; I < Figures.size(); ++I) { if (I) Out += ' '; Out += NameOf(Figures[I]); }
    Out += '}';
    return Out;
}

Deliver<std::vector<NurbsCurve>> FigureRecipe::ResolveInput(const RecipeInput& In, const SceneDocument& Scene, const Workplane& Work) noexcept
{
    (void)Work;
    auto Find = [&](uint32_t Id) -> const SceneFigure* { for (const SceneFigure& F : Scene.Figures()) if (F.Identity == Id) return &F; return nullptr; };
    if (In.Shape == RecipeInput::Form::Curve)
    {
        std::vector<NurbsCurve> Loops;                                                  // several figures = outer + holes of one station
        for (uint32_t Id : In.Figures)
        {
            const SceneFigure* F = Find(Id);
            if (!F || F->Classification != FigureClassification::Curve) return Deliver<std::vector<NurbsCurve>>::Reject(RefusalReason::DegenerateInput, "a input curve was deleted");
            Loops.push_back(F->Curve);
        }
        if (Loops.empty()) return Deliver<std::vector<NurbsCurve>>::Reject(RefusalReason::DegenerateInput, "no input curve");
        return Deliver<std::vector<NurbsCurve>>::Accept(std::move(Loops));
    }
    if (In.Shape == RecipeInput::Form::Edge)
    {
        const SceneFigure* F = In.Figures.empty() ? nullptr : Find(In.Figures.front());
        if (!F || F->Classification != FigureClassification::Body || In.Edge < 0 || In.Edge >= int(F->Body.Edges.size())) return Deliver<std::vector<NurbsCurve>>::Reject(RefusalReason::DegenerateInput, "an input edge no longer exists");
        return Deliver<std::vector<NurbsCurve>>::Accept({ F->Body.Edges[In.Edge].Curve });
    }
    // Area: the sketch cell with (nearly) the same centroid, else re-derive from the bounding curves alone.
    if (const SketchArea* A = Scene.AreaBySignature(In.Centroid))
    {
        bool SameCurves = true;
        for (uint32_t Id : In.Figures) { bool Found = false; for (uint32_t B : A->BoundingIdentities) Found |= B == Id; SameCurves &= Found; }
        if (SameCurves) return Deliver<std::vector<NurbsCurve>>::Accept(A->Loops());
    }
    std::vector<NurbsCurve> Curves;
    for (uint32_t Id : In.Figures) { const SceneFigure* F = Find(Id); if (!F || F->Classification != FigureClassification::Curve) return Deliver<std::vector<NurbsCurve>>::Reject(RefusalReason::DegenerateInput, "a bounding curve of the area was deleted"); Curves.push_back(F->Curve); }
    if (Curves.empty()) return Deliver<std::vector<NurbsCurve>>::Reject(RefusalReason::DegenerateInput, "area has no bounding curves");
    Vec3 N = Vec3::UnitZ(); Frontier::Plane Flat; if (Curves.front().Planar(Flat)) N = Flat.Normal;
    std::vector<PlanarCell> Cells = ProfileSolver::Cells(Curves, N);
    if (Cells.empty()) return Deliver<std::vector<NurbsCurve>>::Reject(RefusalReason::OpenWire, "the area's curves no longer close");
    // the cell whose centroid is nearest the remembered one
    size_t Best = 0; double BestD = 1e300;
    for (size_t I = 0; I < Cells.size(); ++I)
    {
        std::vector<Vec3> P; Cells[I].Outer.Tessellate(P, nullptr, 1e-3); Vec3 S; for (const Vec3& Q : P) S = S + Q; S = S * (1.0 / double(std::max<size_t>(1, P.size())));
        double D = S.Distance(In.Centroid); if (D < BestD) { BestD = D; Best = I; }
    }
    return Deliver<std::vector<NurbsCurve>>::Accept(Cells[Best].Loops());
}

const NurbsSurface* FigureRecipe::ResolveSupport(const RecipeInput& In, const SceneDocument& Scene, Vec3 Hint) noexcept
{
    auto Find = [&](uint32_t Id) -> const SceneFigure* { for (const SceneFigure& F : Scene.Figures()) if (F.Identity == Id) return &F; return nullptr; };
    if (In.Support == 0)
    {
        // an edge's own body: the face on the other side of the edge is the support (the first face when the edge is a rim of a sheet body)
        if (In.Shape != RecipeInput::Form::Edge || In.Figures.empty()) return nullptr;
        const SceneFigure* F = Find(In.Figures.front());
        if (!F || F->Classification != FigureClassification::Body || In.Edge < 0 || In.Edge >= int(F->Body.Edges.size())) return nullptr;
        const BrepEdge& E = F->Body.Edges[size_t(In.Edge)];
        if (E.Coedges.empty()) return nullptr;
        if (In.Face >= 0 && In.Face < int(F->Body.Faces.size())) return &F->Body.Faces[size_t(In.Face)].Surface;
        if (E.Coedges.size() == 1) return &F->Body.Faces[size_t(F->Body.Coedges[size_t(E.Coedges.front())].Face)].Surface;
        // two faces meet at the edge: continue flush with the one whose normal is most perpendicular to the direction
        //    from the rim into the fill (Hint = the fill's centroid); a window in a skin picks the skin, not the cut wall
        Vec3 M = E.Curve.Sample(0.5 * (E.Curve.DomainStart() + E.Curve.DomainEnd()));
        Vec3 Inward = (Hint - M).Normalised();
        const NurbsSurface* Best = nullptr; double BestDot = 1e300;
        for (int Ce : E.Coedges)
        {
            const NurbsSurface& Su = F->Body.Faces[size_t(F->Body.Coedges[size_t(Ce)].Face)].Surface;
            double U = 0, V = 0; Su.ClosestParameter(M, U, V); double Dot = std::fabs(Su.Normal(U, V).Dot(Inward));
            if (Dot < BestDot) { BestDot = Dot; Best = &Su; }
        }
        return Best;
    }
    const SceneFigure* F = Find(In.Support);
    if (!F) return nullptr;
    if (F->Classification == FigureClassification::Surface) return &F->Surface;
    if (F->Classification == FigureClassification::Body && !F->Body.Faces.empty())
    {
        // the face nearest the rim's midpoint
        Deliver<std::vector<NurbsCurve>> R = ResolveInput(In, Scene, Workplane{});
        if (!R || R.Payload.empty()) return nullptr;
        const NurbsCurve& C = R.Payload.front(); Vec3 M = C.Sample(0.5 * (C.DomainStart() + C.DomainEnd()));
        const NurbsSurface* Best = nullptr; double BestD = 1e300;
        for (const BrepFace& Fa : F->Body.Faces) { double U = 0, V = 0, D = 0; Fa.Surface.ClosestParameter(M, U, V, &D); if (D < BestD) { BestD = D; Best = &Fa.Surface; } }
        return Best;
    }
    return nullptr;
}

Vec3 FigureRecipe::Hint(const SceneDocument& Scene, const Workplane& Work) const noexcept
{
    Vec3 Sum; int N = 0;
    for (const RecipeInput& In : Sections)
    {
        Deliver<std::vector<NurbsCurve>> R = ResolveInput(In, Scene, Work);
        if (!R) continue;
        for (const NurbsCurve& C : R.Payload) for (int K = 0; K < 8; ++K) { Sum = Sum + C.Sample(C.DomainStart() + (C.DomainEnd() - C.DomainStart()) * (K + 0.5) / 8); ++N; }
    }
    return N ? Sum / double(N) : Vec3{};
}

uint64_t FigureRecipe::FingerprintInputs(const SceneDocument& Scene, const Workplane& Work) const noexcept
{
    uint64_t H = 1469598103934665603ull;
    Mix(H, uint64_t(Operation));
    auto MixInput = [&](const RecipeInput& In)
    {
        Mix(H, uint64_t(In.Shape)); for (uint32_t Id : In.Figures) Mix(H, Id); Mix(H, uint64_t(In.Edge + 1));
        Mix(H, uint64_t(In.Continuity)); Mix(H, Bits(In.Tension)); Mix(H, In.Support); Mix(H, uint64_t(In.Face + 1));
        Deliver<std::vector<NurbsCurve>> R = ResolveInput(In, Scene, Work);
        if (!R) { Mix(H, 0xdeadull); return; }
        for (const NurbsCurve& C : R.Payload) MixCurve(H, C);
        if (Operation == RecipeOperation::FairPatch && In.Continuity != RimContinuity::Position)
            if (const NurbsSurface* Sup = ResolveSupport(In, Scene, Hint(Scene, Work))) { Mix(H, Sup->CountU); Mix(H, Sup->CountV); for (const Vec4& P : Sup->Poles) { Mix(H, Bits(P.X)); Mix(H, Bits(P.Y)); Mix(H, Bits(P.Z)); } }
    };
    for (const RecipeInput& In : Sections) MixInput(In);
    if (!Path.Figures.empty()) MixInput(Path);
    for (const RecipeInput& In : Guides) MixInput(In);
    Mix(H, Fair.Spans); Mix(H, Fair.Star ? 1 : 0); Mix(H, Bits(Fair.Fairness)); Mix(H, Fair.Rounds);
    Mix(H, Bits(Direction.X)); Mix(H, Bits(Direction.Y)); Mix(H, Bits(Direction.Z)); Mix(H, Bits(Length));
    Mix(H, Bits(AxisOrigin.X)); Mix(H, Bits(AxisOrigin.Y)); Mix(H, Bits(AxisOrigin.Z)); Mix(H, Bits(Axis.X)); Mix(H, Bits(Axis.Y)); Mix(H, Bits(Axis.Z)); Mix(H, Bits(Angle));
    Mix(H, Bits(Radius)); Mix(H, Sheet ? 1 : 0);
    Mix(H, Loft.DegreeV); Mix(H, (Loft.Loop ? 1 : 0) | (Loft.AlignSeams ? 2 : 0) | (Loft.AlignSense ? 4 : 0) | (Loft.Solid ? 8 : 0));
    Mix(H, uint64_t(Sweep.Bases)); Mix(H, Sweep.Stations); Mix(H, Bits(Sweep.ScaleEnd)); Mix(H, Bits(Sweep.TwistAngle)); Mix(H, Sweep.Solid ? 1 : 0);
    return H;
}

Deliver<FigureRecipe::Product> FigureRecipe::Produce(const SceneDocument& Scene, const Workplane& Work, FairPatchReport* Report) const noexcept
{
    using Out = Deliver<Product>;
    std::vector<std::vector<NurbsCurve>> Stations;
    for (const RecipeInput& In : Sections)
    {
        Deliver<std::vector<NurbsCurve>> R = ResolveInput(In, Scene, Work);
        if (!R) return Out::Reject(R.Denial.Reason, R.Denial.Detail);
        Stations.push_back(std::move(R.Payload));
    }
    auto FromSkin = [](Deliver<SkinSolver::Skin> S) -> Out
    {
        if (!S) return Out::Reject(S.Denial.Reason, S.Denial.Detail);
        Product P; P.IsBody = S.Payload.IsBody; P.Body = std::move(S.Payload.Body); P.Sheet = std::move(S.Payload.Sheet);
        return Out::Accept(std::move(P));
    };
    switch (Operation)
    {
        case RecipeOperation::Extrude:
        {
            if (Stations.empty()) return Out::Reject(RefusalReason::DegenerateInput, "nothing to extrude");
            const std::vector<NurbsCurve>& Loops = Stations.front();
            bool Closed = true; for (const NurbsCurve& C : Loops) Closed &= C.Closed();
            if (Sheet || !Closed)
            {
                Deliver<NurbsSurface> S = NurbsSurface::Extrusion(Loops.front(), Direction, Length);
                if (!S) return Out::Reject(S.Denial.Reason, S.Denial.Detail);
                Product P; P.Sheet = std::move(S.Payload); return Out::Accept(std::move(P));
            }
            Deliver<BrepBody> B = BrepBody::Extrude(Loops, Direction, Length);
            if (!B) return Out::Reject(B.Denial.Reason, B.Denial.Detail);
            Product P; P.IsBody = true; P.Body = std::move(B.Payload); return Out::Accept(std::move(P));
        }
        case RecipeOperation::Revolve:
        {
            if (Stations.empty()) return Out::Reject(RefusalReason::DegenerateInput, "nothing to revolve");
            const std::vector<NurbsCurve>& Loops = Stations.front();
            bool Closed = true; for (const NurbsCurve& C : Loops) Closed &= C.Closed();
            if (Sheet || !Closed)
            {
                Deliver<NurbsSurface> S = NurbsSurface::Revolution(Loops.front(), AxisOrigin, Axis, Angle);
                if (!S) return Out::Reject(S.Denial.Reason, S.Denial.Detail);
                Product P; P.Sheet = std::move(S.Payload); return Out::Accept(std::move(P));
            }
            Deliver<BrepBody> B = BrepBody::Revolve(Loops, AxisOrigin, Axis, Angle);
            if (!B) return Out::Reject(B.Denial.Reason, B.Denial.Detail);
            Product P; P.IsBody = true; P.Body = std::move(B.Payload); return Out::Accept(std::move(P));
        }
        case RecipeOperation::Loft:
        {
            LoftOptions L = Loft; if (Sheet) L.Solid = false;
            // With guides we always take the sheet-only path; re-wrapping a body around a deformed sheet would re-sew
            //    against the original sections, undoing the deformation. We use each station's first loop as the
            //    outer profile (multi-loop loft with guides is rare; the typical case is two to four open sections).
            if (!LoftGuideInputs.empty())
            {
                LoftGuideOptions Opts = LoftGuides;
                for (const RecipeInput& In : LoftGuideInputs)
                {
                    Deliver<std::vector<NurbsCurve>> R = ResolveInput(In, Scene, Work);
                    if (!R) return Out::Reject(R.Denial.Reason, R.Denial.Detail);
                    for (NurbsCurve& C : R.Payload) Opts.Guides.push_back(std::move(C));
                }
                std::vector<NurbsCurve> Outers;
                for (const std::vector<NurbsCurve>& S : Stations) if (!S.empty()) Outers.push_back(S.front());
                if (Outers.size() < 2) return Out::Reject(RefusalReason::DegenerateInput, "loft with guides needs at least two sections");
                Deliver<NurbsSurface> Built = SkinSolver::LoftSheet(Outers, L);
                if (!Built) return Out::Reject(Built.Denial.Reason, Built.Denial.Detail);
                NurbsSurface Projected = SkinSolver::ProjectGuides(Built.Payload, Opts);
                Product P; P.IsBody = false; P.Sheet = std::move(Projected);
                return Out::Accept(std::move(P));
            }
            return FromSkin(SkinSolver::Loft(Stations, L));
        }
        case RecipeOperation::Sweep:
        case RecipeOperation::Pipe:
        {
            Deliver<std::vector<NurbsCurve>> P = ResolveInput(Path, Scene, Work);
            if (!P) return Out::Reject(P.Denial.Reason, P.Denial.Detail);
            SweepOptions S = Sweep; if (Sheet) S.Solid = false;
            if (Operation == RecipeOperation::Pipe) return FromSkin(SkinSolver::Pipe(P.Payload.front(), Radius, S.Solid));
            if (Stations.empty()) return Out::Reject(RefusalReason::DegenerateInput, "sweep has no profile");
            return FromSkin(SkinSolver::Sweep(Stations.front(), P.Payload.front(), S));
        }
        case RecipeOperation::Patch:
        {
            std::vector<NurbsCurve> Boundaries; for (auto& S : Stations) for (NurbsCurve& C : S) Boundaries.push_back(std::move(C));
            return FromSkin(SkinSolver::Patch(std::move(Boundaries)));
        }
        case RecipeOperation::FairPatch:
        {
            std::vector<FairRim> Rims;
            for (size_t I = 0; I < Sections.size(); ++I)
                for (NurbsCurve& C : Stations[I])
                {
                    FairRim R; R.Curve = std::move(C); R.Continuity = Sections[I].Continuity; R.Tension = Sections[I].Tension;
                    R.Support = ResolveSupport(Sections[I], Scene, Hint(Scene, Work));
                    Rims.push_back(std::move(R));
                }
            std::vector<NurbsCurve> GuideCurves;
            for (const RecipeInput& In : Guides)
            {
                Deliver<std::vector<NurbsCurve>> R = ResolveInput(In, Scene, Work);
                if (!R) return Out::Reject(R.Denial.Reason, R.Denial.Detail);
                for (NurbsCurve& C : R.Payload) GuideCurves.push_back(std::move(C));
            }
            return FromSkin(FairPatchSolver::Build(std::move(Rims), GuideCurves, Fair, Report));
        }
        default: return Out::Reject(RefusalReason::DegenerateInput, "figure is authored, not derived");
    }
}

std::string FigureRecipe::Summary(const SceneDocument& Scene) const noexcept
{
    if (!Live()) return "authored";
    std::string S = Describe(Operation);
    if (!Sections.empty()) S += " of";
    for (const RecipeInput& In : Sections) { S += ' '; S += In.Label(Scene); }
    if (!Path.Figures.empty()) { S += " along "; S += Path.Label(Scene); }
    if (Operation == RecipeOperation::FairPatch)
    {
        S.clear(); S = "fairpatch of";
        for (const RecipeInput& In : Sections) { S += ' '; S += In.Label(Scene); S += '['; S += Describe(In.Continuity); if (In.Face >= 0) { S += " f"; S += std::to_string(In.Face); } if (In.Tension != 1.0) { char T[32]; std::snprintf(T, sizeof T, " t%.2g", In.Tension); S += T; } S += ']'; }
        if (!Guides.empty()) { S += " through"; for (const RecipeInput& In : Guides) { S += ' '; S += In.Label(Scene); } }
        if (Fair.Star) S += "  star";
        char T[64]; std::snprintf(T, sizeof T, "  spans %d", Fair.Spans); S += T;
        if (!Complaint.empty()) { S += "  ⚠ "; S += Complaint; }
        return S;
    }
    char Extra[160] = {};
    switch (Operation)
    {
        case RecipeOperation::Extrude: std::snprintf(Extra, sizeof Extra, "  length %.4g  direction (%.2f %.2f %.2f)", Length, Direction.X, Direction.Y, Direction.Z); break;
        case RecipeOperation::Revolve: std::snprintf(Extra, sizeof Extra, "  angle %.4g°  axis (%.2f %.2f %.2f)", ScalarCriteria::Degrees(Angle), Axis.X, Axis.Y, Axis.Z); break;
        case RecipeOperation::Loft:    std::snprintf(Extra, sizeof Extra, "  degree %d%s%s", Loft.DegreeV, Loft.Loop ? "  loop" : "", Loft.Solid ? "" : "  sheet"); break;
        case RecipeOperation::Sweep:   std::snprintf(Extra, sizeof Extra, "  bases %s  scale %.3g  twist %.4g°", Sweep.Bases == SweepBases::Frenet ? "frenet" : Sweep.Bases == SweepBases::Fixed ? "fixed" : "minimal", Sweep.ScaleEnd, ScalarCriteria::Degrees(Sweep.TwistAngle)); break;
        case RecipeOperation::Pipe:    std::snprintf(Extra, sizeof Extra, "  radius %.4g", Radius); break;
        default: break;
    }
    S += Extra;
    if (!Complaint.empty()) { S += "  ⚠ "; S += Complaint; }
    return S;
}

} // namespace Frontier
