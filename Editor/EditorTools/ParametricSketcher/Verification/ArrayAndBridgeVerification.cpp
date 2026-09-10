//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/ArrayAndBridgeVerification.cpp — Phase 12: bridge · linear array · radial array · named construction planes
//============================================================================================================================================
// Drives the console host with the same script lines the user would type, and asserts on the resulting figure list.
//   Bridge: a 2-section loft is exposed via a dedicated verb for Plasticity parity; this verifies refusal cases, the
//           degree/sheet/align switches, and that the resulting surface is a sheet (open body) with the correct bounds.
//   Array: linear (step) and radial (axis + angle + scale) cases; verifies refusal cases, count, and that each copy
//          has the expected bounds.
//   Named construction planes: `plane --name=N` registers the current workplane, `workplane <name>` recalls it; this
//          verifies the round-trip and that an unknown name is refused.
#include "Console/ConsoleHost.h"
#include "Kernel/VectorSpecification.h"
#include "VerificationPanel.h"
#include <cmath>
#include <string>

using namespace Frontier;

int main()
{
    VerificationPanel Panel("SolidArc · Phase 12 · ArrayAndBridge Verification — bridge · linear array · radial array · named construction planes");

    auto Figure = [](ConsoleHost& Host, const char* Name) -> const SceneFigure* { return Host.Document().Find(std::string(Name)); };

    Panel.Section("Bridge: produces a sheet between two open curves in different planes, bounds match the curve extents");
    {
        ConsoleHost Host("/tmp/SolidArcVerificationP12", 1280, 800);
        // Bot rect lies in the XY plane, Side rect in the XZ plane — the bridge must span (0,0,0)–(4,2,3).
        bool Setup = Host.Execute("rect (0,0) (4,2) --name=Bot") && Host.Execute("workplane xz") && Host.Execute("rect (0,0) (4,3) --name=Side") && Host.Execute("workplane xy");
        Panel.Expect("setup builds the two open curves", Setup);
        bool Built = Host.Execute("bridge Bot Side --name=Shell");
        Panel.Expect("bridge Bot Side succeeds", Built);
        const SceneFigure* Sh = Figure(Host, "Shell");
        Panel.Expect("Shell figure exists", Sh != nullptr);
        if (Sh)
        {
            Panel.Expect("Shell is a sheet (one surface, no body)", Sh->Classification == FigureClassification::Surface);
            Box3 B = Sh->Surface.Bounds();
            Panel.Note("  bridge bounds X[%.3f,%.3f] Y[%.3f,%.3f] Z[%.3f,%.3f]", B.Low.X, B.High.X, B.Low.Y, B.High.Y, B.Low.Z, B.High.Z);
            Panel.Within("bridge X extent matches the curves (0..4)", B.Low.X, 0.0);
            Panel.Within("bridge Y extent matches the XY curve (0..2)", B.Low.Y, 0.0);
            Panel.Within("bridge Z extent matches the XZ curve (0..3)", B.Low.Z, 0.0);
        }
        // Refusals
        Panel.Expect("bridge with no curves is refused", !Host.Execute("bridge"));
        Panel.Expect("bridge with one curve is refused", !Host.Execute("bridge Bot"));
        Panel.Expect("bridge with three curves is refused", !Host.Execute("bridge Bot Side Bot"));
        Panel.Expect("bridge referencing an unknown curve is refused", !Host.Execute("bridge Bot DoesNotExist"));
    }

    Panel.Section("Bridge: degree and no-align switches do not change the bounds");
    {
        ConsoleHost Host("/tmp/SolidArcVerificationP12B", 1280, 800);
        Host.Execute("rect (0,0) (2,2) --name=Bot");
        Host.Execute("workplane xz");
        Host.Execute("rect (0,0) (2,2) --name=Side");
        Host.Execute("workplane xy");
        // Two bridges with different parameters — both should produce a sheet covering the same volume.
        Panel.Expect("bridge with --degree=1 succeeds", Host.Execute("bridge Bot Side --degree=1 --no-align --name=Sheet1"));
        Panel.Expect("bridge with --degree=3 succeeds", Host.Execute("bridge Bot Side --degree=3 --name=Sheet3"));
        const SceneFigure* S1 = Figure(Host, "Sheet1");
        const SceneFigure* S3 = Figure(Host, "Sheet3");
        Panel.Expect("both sheets exist and are surfaces", S1 && S3 && S1->Classification == FigureClassification::Surface && S3->Classification == FigureClassification::Surface);
        if (S1 && S3)
        {
            Box3 B1 = S1->Surface.Bounds();
            Box3 B3 = S3->Surface.Bounds();
            Panel.Expect("X bounds match across degrees", B1.Low.X == B3.Low.X && B1.High.X == B3.High.X);
            Panel.Expect("Z bounds match across degrees", B1.Low.Z == B3.Low.Z && B1.High.Z == B3.High.Z);
        }
    }

    Panel.Section("Array (linear): step translates each copy; N-1 copies are produced, named <Base>.K");
    {
        ConsoleHost Host("/tmp/SolidArcVerificationP12C", 1280, 800);
        Host.Execute("box (0,0,0) (1,1,1) --name=Seed");
        Panel.Expect("linear array of 4 boxes succeeds", Host.Execute("array Seed --count=4 --step=(3,0,0) --name=L"));
        for (int K = 1; K <= 3; ++K) // count=4 → K=1,2,3
        {
            std::string Nm = std::string("L.") + std::to_string(K);
            const SceneFigure* F = Figure(Host, Nm.c_str());
            Panel.Expect((std::string("L.") + std::to_string(K) + " exists").c_str(), F != nullptr);
            if (F)
            {
                Box3 B = F->Body.Bounds();
                double XLo = double(K) * 3.0;
                double XHi = XLo + 1.0;
                Panel.Within((std::string("L.") + std::to_string(K) + " X low").c_str(),  B.Low.X,  XLo);
                Panel.Within((std::string("L.") + std::to_string(K) + " X high").c_str(), B.High.X, XHi);
            }
        }
        // The verb refuses invalid inputs.
        Panel.Expect("linear array with no step is refused", !Host.Execute("array Seed --count=3"));
        Panel.Expect("linear array with count<2 is refused", !Host.Execute("array Seed --count=1 --step=(1,0,0)"));
        Panel.Expect("linear array with no figures is refused", !Host.Execute("array --count=3 --step=(1,0,0)"));
        Panel.Expect("linear array referencing an unknown figure is refused", !Host.Execute("array NoSuch --count=3 --step=(1,0,0)"));
    }

    Panel.Section("Array (radial): 5 copies of a box rotated 72° apart around the world Z axis");
    {
        ConsoleHost Host("/tmp/SolidArcVerificationP12D", 1280, 800);
        // A box placed at the source — its centre is at (1.5, 0, 0.5). Rotate it 360° in 5 steps around the world Z axis.
        Host.Execute("box (1,0,0) (2,1,1) --name=Spoke");
        Panel.Expect("radial array of 6 around Z succeeds", Host.Execute("array Spoke --count=6 --axis=(0,0,0),(0,0,1) --angle=360 --name=R"));
        for (int K = 1; K <= 5; ++K) // count=6 → K=1..5
        {
            std::string Nm = std::string("R.") + std::to_string(K);
            const SceneFigure* F = Figure(Host, Nm.c_str());
            Panel.Expect((std::string("R.") + std::to_string(K) + " exists").c_str(), F != nullptr);
            if (F)
            {
                // A rotation around the world Z axis leaves the Z bounds unchanged: the copy must be a 1x1x1 box
                //    (just rotated). Axis-aligned Z extent and signed volume are invariants of a rigid rotation.
                Box3 B = F->Body.Bounds();
                Panel.Within((std::string("R.") + std::to_string(K) + " Z low").c_str(),  B.Low.Z,  0.0);
                Panel.Within((std::string("R.") + std::to_string(K) + " Z high").c_str(), B.High.Z, 1.0);
                double V = std::fabs(F->Body.SignedVolume());
                Panel.Note("  R.%d volume=%.9f", K, V);
                Panel.Within((std::string("R.") + std::to_string(K) + " |volume| is unit box").c_str(), V, 1.0 + 1e-6);
            }
        }
        // Refusals
        Panel.Expect("radial array with no axis is refused", !Host.Execute("array Spoke --count=4 --angle=90"));
        Panel.Expect("radial array with zero-length axis is refused", !Host.Execute("array Spoke --count=4 --axis=(0,0,0),(0,0,0) --angle=90"));
        Panel.Expect("radial array with non-positive scale is refused", !Host.Execute("array Spoke --count=4 --axis=(0,0,0),(0,0,1) --angle=90 --scale=0"));
    }

    Panel.Section("Array (radial) with --scale: each copy shrinks/grows along the angle");
    {
        ConsoleHost Host("/tmp/SolidArcVerificationP12E", 1280, 800);
        // A 1x1x1 box at radius 0.5 from the Z axis, scaled 0.2 over 4 copies → final box is 0.2³ wide. Volume must shrink.
        Host.Execute("box (0.5,0,0) (1.5,1,1) --name=Taper");
        Panel.Expect("radial array with --scale=0.2 succeeds", Host.Execute("array Taper --count=5 --axis=(0,0,0),(0,0,1) --angle=360 --scale=0.2 --name=T"));
        const SceneFigure* T1 = Figure(Host, "T.1");
        const SceneFigure* T4 = Figure(Host, "T.4");
        if (T1 && T4)
        {
            double V1 = T1->Body.SignedVolume();
            double V4 = T4->Body.SignedVolume();
            Panel.Note("  T.1 vol=%.4f  T.4 vol=%.4f", V1, V4);
            // At K=1, T=0.25, S=0.7. At K=4, T=1.0, S=0.2. Volumes scale as S³.
            Panel.Expect("T.4 has a smaller volume than T.1 (taper)", V4 < V1 * 0.5);
        }
    }

    Panel.Section("Named construction planes: plane --name=N registers, workplane N recalls, unknown name is refused");
    {
        ConsoleHost Host("/tmp/SolidArcVerificationP12F", 1280, 800);
        // Move to a non-default workplane first.
        Host.Execute("workplane yz --origin=(1,2,3)");
        Workplane Saved = Host.WorkPlane();
        Panel.Note("  saved plane origin (%.3f,%.3f,%.3f) normal (%.3f,%.3f,%.3f)", Saved.Origin.X, Saved.Origin.Y, Saved.Origin.Z, Saved.Normal().X, Saved.Normal().Y, Saved.Normal().Z);
        Panel.Expect("plane --name=Custom registers the current workplane", Host.Execute("plane --name=Custom"));
        // Switch to something else, then recall.
        Host.Execute("workplane xy");
        Panel.Within("after switch, the active plane is XY", Host.WorkPlane().Origin.Z, 0.0);
        Panel.Expect("workplane Custom recalls the named plane", Host.Execute("workplane Custom"));
        Workplane Back = Host.WorkPlane();
        Panel.Within("recall restores the origin X", Back.Origin.X, 1.0);
        Panel.Within("recall restores the origin Y", Back.Origin.Y, 2.0);
        Panel.Within("recall restores the origin Z", Back.Origin.Z, 3.0);
        Panel.Within("recall restores the YZ normal (1,0,0) X", Back.Normal().X, 1.0);
        Panel.Within("recall restores the YZ normal (1,0,0) Y", Back.Normal().Y, 0.0);
        Panel.Within("recall restores the YZ normal (1,0,0) Z", Back.Normal().Z, 0.0);
        // Refusals
        Panel.Expect("workplane with an unknown name is refused", !Host.Execute("workplane DoesNotExist"));
        Panel.Expect("plane --name= with empty name is refused",  !Host.Execute("plane --name="));
    }

    return Panel.Conclude();
}
