//============================================================================================================================================
//                                                    SURFACEOFFSETVERIFICATION.CPP
//============================================================================================================================================
// 📦 Analytic metric checks, live dependency edits, refusal behaviour and native offset persistence.

#include "Kernel/SurfaceOffsetSolver.h"
#include "Console/ConsoleHost.h"
#include "Document/UndoSequence.h"
#include "VerificationPanel.h"
#include "Editor/SolidArcOutlinerAdapter.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <iomanip>
#include <sstream>

using namespace Frontier;

int main(int Count, char** Arguments)
{
    const std::filesystem::path Directory = Count == 3 ? Arguments[2] : SOLIDARC_PROOF_FOLDER "/SurfaceOffsetVerification";
    std::filesystem::create_directories(Directory);
    VerificationPanel Panel("SolidArc surface-distance offsets");
    ConsoleHost Host(Directory.string(), 2000, 1250);
    if (Count == 3)
    {
        Panel.Expect("Liger document opens with live offset", Host.Execute(std::string("open \"") + Arguments[1] + "\""));
        const auto* Source = Host.Document().Find("Guide_B03_UpperCanopy_Across");
        const auto* Skin = Host.Document().Find("Liger_Main_Body");
        const auto* Offset = Host.Document().Find("Guide_O01_RoofSurfaceOffset_Across");
        if (!Source || !Skin || !Offset) return 2;
        SurfaceOffsetOptions Options; Options.Distance = Offset->Recipe.Length; Options.Tolerance = Offset->Recipe.Radius;
        SurfaceOffsetReport Report;
        auto Checked = SurfaceOffsetSolver::Construct(Source->Curve, Skin->Body, Options, &Report);
        Panel.Expect("independent regeneration succeeds", bool(Checked));
        if (!Checked) return 2;
        double ReplayError = 0, Reflection = 0;
        for (int I = 0; I <= 4096; ++I)
        {
            const double T = I / 4096.0;
            Vec3 P = Offset->Curve.Sample(T); P.Y *= -1;
            Reflection = std::max(Reflection, P.Distance(Offset->Curve.Sample(1 - T)));
            ReplayError = std::max(ReplayError, Checked.Payload.Sample(T).Distance(Offset->Curve.Sample(T)));
        }
        Panel.Within("regenerated curve discrepancy [m]", ReplayError, 1e-10);
        Panel.Within("sampled bilateral discrepancy [m]", Reflection, Options.Tolerance * 2);
        Panel.Within("march refinement [m]", Report.MarchRefinement, Options.Tolerance * .2);
        Panel.Within("held-out interpolation [m]", Report.InterpolationError, Options.Tolerance * .5);
        Panel.Within("numerical surface path length [m]", Report.PathLengthError, Options.Tolerance * .2);
        Panel.Within("corner path refinement [m]", Report.CornerRefinement, Options.Tolerance * .2);
        Panel.Within("corner distance-field residual [m]", Report.CornerResidual, Options.Tolerance * .2);
        std::ofstream Measures(Directory / "SurfaceOffsetMeasures.json");
        Measures << std::setprecision(17) << "{\n  \"marchRefinementMetres\": " << Report.MarchRefinement
                 << ",\n  \"sampledInterpolationMetres\": " << Report.InterpolationError
                 << ",\n  \"sourceSupportDiscrepancyMetres\": " << Report.SourceDistance
                 << ",\n  \"normalPathLengthResidualMetres\": " << Report.PathLengthError
                 << ",\n  \"cornerRefinementMetres\": " << Report.CornerRefinement
                 << ",\n  \"cornerDistanceResidualMetres\": " << Report.CornerResidual
                 << ",\n  \"bilateralDiscrepancyMetres\": " << Reflection
                 << ",\n  \"intervals\": " << Report.Intervals << ",\n  \"cornerIntervals\": " << Report.CornerIntervals << "\n}\n";
        Measures.close();
        std::ofstream Samples(Directory / "SurfaceOffsetSamples.csv");
        Samples << std::setprecision(17) << "parameter,sourceX,sourceY,sourceZ,offsetX,offsetY,offsetZ,requestedWidthMetres\n";
        for (size_t I = 0; I < Report.Stations.size(); ++I)
        {
            const auto& A = Report.Sources[I]; const auto& B = Report.Stations[I];
            Samples << Report.Parameters[I] << ',' << A.X << ',' << A.Y << ',' << A.Z << ',' << B.X << ',' << B.Y << ',' << B.Z << ',' << std::fabs(Options.Distance) << '\n';
        }
        Panel.Expect("review display", Host.Execute("show cages off; show iso off; show edges off; show features on; show shading plastic"));
        const char* Views[] = {"Front_Quarter", "Rear_Quarter", "Side", "Top"};
        const char* Cameras[] = {"view front; view orbit 55 23; view persp", "view front; view orbit -55 23; view persp", "view front", "view top"};
        for (int I = 0; I < 4; ++I)
        {
            Panel.Expect("four-view camera", Host.Execute(Cameras[I]) && Host.Execute("view fit; view dolly 1.5"));
            Panel.Expect("four-view native render", Host.Execute(std::string("render Liger_Surface_Offset_") + Views[I] + " --size=2000x1250"));
        }
        Panel.Expect("roof comparison camera", Host.Execute("line (-.35,-.77,1.14) (1.30,.77,1.14) --name=OffsetInspection; select OffsetInspection; view top; view fit selected; delete OffsetInspection"));
        Panel.Expect("roof before", Host.Execute("hide Guide_O01_RoofSurfaceOffset_Across; render Liger_Surface_Offset_Before --size=2000x1250"));
        Panel.Expect("roof after", Host.Execute("unhide Guide_O01_RoofSurfaceOffset_Across; render Liger_Surface_Offset_After --size=2000x1250"));
        const auto Fingerprint = UndoSequence::Fingerprint(Host.Document());
        const auto Saved = (Directory / "SurfaceOffsetRoundtrip.arc").generic_string();
        Panel.Expect("save live offset", Host.Execute("save \"" + Saved + "\""));
        Panel.Expect("reopen live offset", Host.Execute("open \"" + Saved + "\""));
        Panel.Expect("live recipe and geometry survive replay", Fingerprint == UndoSequence::Fingerprint(Host.Document()));
        std::filesystem::remove(Saved);
        return Panel.Conclude();
    }
    if (Count != 1) return 2;
    std::ostringstream Patch;
    Patch << "patch 4 4 " << std::setprecision(17);
    for (int U = 0; U < 4; ++U) for (int V = 0; V < 4; ++V) Patch << '(' << U / 3.0 << ',' << V / 3.0 << ",0) ";
    Patch << "--degree=3 --name=Support";
    Panel.Expect("support patch", Host.Execute(Patch.str()));
    Panel.Expect("source curve", Host.Execute("line (.2,.2,0) (.8,.2,0) --name=Source"));
    Panel.Expect("live surface offset", Host.Execute("surface-offset Source Support .05 --name=Offset"));
    auto Position = [&]() { return Host.Document().Find("Offset")->Curve.Sample(.5); };
    Panel.Within("analytic plane width [m]", std::fabs(Position().Y - .25), 1e-10);
    Panel.Expect("derived geometry is protected", Host.Document().Find("Offset")->Locked);
    Panel.Expect("both curve and support are dependencies", Host.Document().DerivedFrom(Host.Document().Find("Source")->Identity).size() == 1 && Host.Document().DerivedFrom(Host.Document().Find("Support")->Identity).size() == 1);
    Panel.Expect("move source", Host.Execute("move Source (0,.1,0)"));
    Panel.Within("offset follows source [m]", std::fabs(Position().Y - .35), 1e-10);
    Panel.Expect("edit signed width", Host.Execute("surface-offset edit Offset -.025"));
    Panel.Within("opposite-side width [m]", std::fabs(Position().Y - .275), 1e-10);
    const uint64_t Edited = UndoSequence::Fingerprint(Host.Document());
    Panel.Expect("invalid width refused", !Host.Execute("surface-offset edit Offset 0"));
    Panel.Expect("invalid edit leaves document intact", Edited == UndoSequence::Fingerprint(Host.Document()));
    Panel.Expect("unknown option refused", !Host.Execute("surface-offset Source Support .02 --bogus=1"));
    Panel.Expect("undo width edit", Host.Execute("undo"));
    Panel.Within("undo restores width [m]", std::fabs(Position().Y - .35), 1e-10);
    Panel.Expect("redo width edit", Host.Execute("redo"));
    Panel.Within("redo restores width [m]", std::fabs(Position().Y - .275), 1e-10);
    Panel.Expect("move support away", Host.Execute("move Support (0,0,.1)"));
    Panel.Expect("stale offset hidden with complaint", Host.Document().Find("Offset")->Hidden && !Host.Document().Find("Offset")->Recipe.Complaint.empty());
    Panel.Expect("move source onto support", Host.Execute("move Source (0,0,.1)"));
    Panel.Expect("offset recovers automatically", !Host.Document().Find("Offset")->Hidden && Host.Document().Find("Offset")->Recipe.Complaint.empty());
    Panel.Within("support change reflected [m]", std::fabs(Position().Z - .1), 1e-10);
    const auto Fingerprint = UndoSequence::Fingerprint(Host.Document());
    const auto Saved = (Directory / "Offset.arc").generic_string();
    Panel.Expect("save native recipe", Host.Execute("save \"" + Saved + "\""));
    Panel.Expect("open native recipe", Host.Execute("open \"" + Saved + "\""));
    Panel.Expect("recipe fingerprint retained", Fingerprint == UndoSequence::Fingerprint(Host.Document()));
    std::ifstream Journal(Saved); std::string Text((std::istreambuf_iterator<char>(Journal)), {});
    Panel.Expect("capability marker retained", Text.find("require surface-offset") != std::string::npos);
    Panel.Expect("reloaded source remains live", Host.Execute("move Source (0,.1,0)"));
    Panel.Within("reloaded offset follows [m]", std::fabs(Position().Y - .375), 1e-10);
    Panel.Expect("native proof display", Host.Execute("show cages off; show iso off; view top; view fit; render SurfaceOffset_Plane --size=960x640"));
    auto CylinderSurface = NurbsSurface::Cylinder({0,0,0}, Vec3::UnitZ(), 1, 1);
    auto Cylinder = BrepBody::Sew({CylinderSurface.Payload}, ScalarCriteria::MergeTolerance, false);
    auto Generator = NurbsCurve::Line({1,0,.2}, {1,0,.8});
    SurfaceOffsetOptions Options; Options.Distance = .2; Options.Samples = 16;
    SurfaceOffsetReport Report;
    auto Offset = SurfaceOffsetSolver::Construct(Generator.Payload, Cylinder.Payload, Options, &Report);
    if (!Offset) std::cout << "CYLINDER_REFUSAL " << Offset.Denial.Detail << std::endl;
    Panel.Expect("cylinder metric offset", bool(Offset));
    if (Offset)
    {
        double Error = 0;
        for (int I = 0; I <= 32; ++I)
            Error = std::max(Error, Offset.Payload.Sample(I / 32.0).Distance({std::cos(.2), -std::sin(.2), .2 + .6 * I / 32.0}));
        Panel.Within("analytic cylinder geodesic offset [m]", Error, 2e-6);
        Panel.Within("cylinder path-length convergence [m]", Report.PathLengthError, Options.Tolerance * .2);
    }
    const auto Plane = BrepBody::FromSurface(Host.Document().Find("Support")->Surface);
    auto Corner = NurbsCurve::Polyline({{.2,.4,.1},{.5,.4,.1},{.5,.7,.1}}, false);
    Options.Distance = .025;
    auto Inner = SurfaceOffsetSolver::Construct(Corner.Payload, Plane, Options, &Report);
    Panel.Expect("inward C0 corner clips its branches", bool(Inner));
    if (Inner)
    {
        double Closest = 1;
        for (Vec3 P : Report.Stations) Closest = std::min(Closest, P.Distance({.475,.425,.1}));
        Panel.Within("analytic inward corner intersection [m]", Closest, 1e-9);
    }
    Options.Distance = -.025;
    auto Outer = SurfaceOffsetSolver::Construct(Corner.Payload, Plane, Options);
    Panel.Expect("outward corner follows surface-distance contour", bool(Outer));
    if (Outer)
    {
        double Error = 0;
        for (int I = 0; I <= 2048; ++I)
        {
            const Vec3 P = Outer.Payload.Sample(I / 2048.0);
            const Vec3 A{ScalarCriteria::Clamp(P.X, .2, .5), .4, .1};
            const Vec3 B{.5, ScalarCriteria::Clamp(P.Y, .4, .7), .1};
            Error = std::max(Error, std::fabs(std::min(P.Distance(A), P.Distance(B)) - .025));
        }
        Panel.Within("analytic rounded-corner equal distance [m]", Error, Options.Tolerance);
    }
    SolidArcOutlinerBinding Binding;
    Binding.RowRole = SolidArcOutlinerBinding::Role::Figure;
    Binding.FigureIdentity = Host.Document().Find("Offset")->Identity;
    EditorSheet Sheet;
    Panel.Expect("native inspector exposes offset", BuildSolidArcInspectorSheet(Host, Binding, &Sheet));
    bool WidthField = false;
    for (uint32_t G = 0; G < Sheet.GroupCount; ++G) for (uint32_t P = 0; P < Sheet.Groups[G].PropertyCount; ++P)
    {
        auto& Property = Sheet.Groups[G].Properties[P];
        if (std::strcmp(Property.Label, "Signed width") == 0) { Property.Figure = -40; WidthField = true; }
    }
    Panel.Expect("native inspector signed millimetre field", WidthField);
    ApplySolidArcInspectorSheet(Host, Binding, Sheet);
    Panel.Within("inspector drives live offset [m]", std::fabs(Position().Y - .36), 1e-10);
    Panel.Expect("inspector edit supports undo", Host.Execute("undo"));
    Panel.Within("inspector undo restores metric [m]", std::fabs(Position().Y - .375), 1e-10);
    auto LeftPlane = NurbsSurface::Plane({0,0,0}, Vec3::UnitX(), Vec3::UnitY(), .5, 1);
    const double Angle = .35;
    const Vec3 FoldDirection{std::cos(Angle), 0, std::sin(Angle)};
    auto RightPlane = NurbsSurface::Plane({.5,0,0}, FoldDirection, Vec3::UnitY(), .5, 1);
    auto Fold = BrepBody::Sew({LeftPlane.Payload, RightPlane.Payload}, ScalarCriteria::MergeTolerance, false);
    auto FoldSource = NurbsCurve::Polyline({{.2,.3,0}, {.5,.45,0}, Vec3{.5,.6,0} + FoldDirection * .3}, false);
    Options.Distance = .025;
    auto FoldOffset = SurfaceOffsetSolver::Construct(FoldSource.Payload, Fold.Payload, Options);
    if (!FoldOffset) std::cout << "FOLDED_SUPPORT_REFUSAL " << FoldOffset.Denial.Detail << std::endl;
    Panel.Expect("nonconvergent folded support is refused", !FoldOffset);
    auto Circle = NurbsCurve::Circle({.5,.5,.1}, Vec3::UnitZ(), .15);
    auto Ring = SurfaceOffsetSolver::Construct(Circle.Payload, Plane, Options);
    Panel.Expect("smooth closed source remains closed", Ring && Ring.Payload.Closed());
    if (Ring)
    {
        double Error = 0;
        for (int I = 0; I <= 2048; ++I)
            Error = std::max(Error, std::fabs(Ring.Payload.Sample(I / 2048.0).Distance({.5,.5,.1}) - .125));
        Panel.Within("analytic closed circular offset [m]", Error, Options.Tolerance);
    }
    Options.Distance = .2;
    Panel.Expect("offset beyond circle cusp refused", !SurfaceOffsetSolver::Construct(Circle.Payload, Plane, Options));
    auto SeparatePlane = NurbsSurface::Plane({2,0,0}, Vec3::UnitX(), Vec3::UnitY(), .5, 1);
    auto Disconnected = BrepBody::Sew({LeftPlane.Payload, SeparatePlane.Payload}, ScalarCriteria::MergeTolerance, false);
    Panel.Expect("disconnected support cannot be crossed", !SurfaceOffsetSolver::Construct(FoldSource.Payload, Disconnected.Payload, Options));
    Panel.Expect("delete parent", Host.Execute("delete Source"));
    Panel.Expect("deleted parent hides dependent offset", Host.Document().Find("Offset")->Hidden && !Host.Document().Find("Offset")->Recipe.Complaint.empty());
    Panel.Expect("undo parent deletion", Host.Execute("undo"));
    Panel.Expect("undo restores live result", Host.Document().Find("Source") && !Host.Document().Find("Offset")->Hidden);
    Panel.Expect("open support crossing refused", !Host.Execute("surface-offset Source Support .9 --name=Outside"));
    return Panel.Conclude();
}
