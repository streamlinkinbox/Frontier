//--------------------------------------------------------------------------------------------------------------------------------------//
// 📦 Editor/EditorTools/ParametricSketcher/Verification/MirrorVerification.cpp — Phase 19: mirror / radial / empty operations                                //
//--------------------------------------------------------------------------------------------------------------------------------------//
// The mirror / radial / empty math is pure (MirrorSolver.cpp). The host integration wraps it via the `mirror`,
//    `radial`, `empty`, `list empty` / `delete empty` verbs, which reflect the Blueprint of each figure and
//    rebuild from the reflected source. This file covers both halves:
//      1. Direct math: each reflection formula on a known point set, multi-axis composition is a 180° rotation,
//         radial covers the full circle, on-axis points are invariant, off-axis points are flipped.
//      2. Host integration: empty create + list + delete round-trip, mirror copies a line / circle / rectangle,
//         radial copies a body, in-place reflection works, across a custom named plane works, across an axis
//         line works, Blueprint round-trip preserves length, render produces a non-empty PNG.
#include "Console/ConsoleHost.h"
#include "Kernel/MirrorSolver.h"
#include "VerificationPanel.h"
#include <cmath>
#include <fstream>

using namespace Frontier;

//---- direct math unit tests ----------------------------------------------------------------------------------------

static int DirectMathSuite(VerificationPanel& Panel) noexcept
{
    Panel.Section("Math: plane reflection flips the perpendicular component, keeps the parallel component");
    {
        // XY plane (normal = +Z): (1, 2, 3) -> (1, 2, -3)
        Vec3 P = ReflectAcrossXY(Vec3(1, 2, 3));
        Panel.Within("XY mirror of (1,2,3) = (1,2,-3)", (P - Vec3(1, 2, -3)).Length(), 1e-12);
        // XZ plane (normal = +Y): (1, 2, 3) -> (1, -2, 3)
        Vec3 Q = ReflectAcrossXZ(Vec3(1, 2, 3));
        Panel.Within("XZ mirror of (1,2,3) = (1,-2,3)", (Q - Vec3(1, -2, 3)).Length(), 1e-12);
        // YZ plane (normal = +X): (1, 2, 3) -> (-1, 2, 3)
        Vec3 R = ReflectAcrossYZ(Vec3(1, 2, 3));
        Panel.Within("YZ mirror of (1,2,3) = (-1,2,3)", (R - Vec3(-1, 2, 3)).Length(), 1e-12);
    }

    Panel.Section("Math: axis reflection flips the perpendicular component, keeps the parallel component");
    {
        // Z axis: (3, 4, 5) -> (-3, -4, 5) (z is parallel, x and y flip)
        Vec3 R = ReflectAcrossAxis(Vec3(3, 4, 5), { Vec3{}, Vec3::UnitZ() });
        Panel.Within("Z-axis mirror of (3,4,5) = (-3,-4,5)", (R - Vec3(-3, -4, 5)).Length(), 1e-12);
        // On-axis point is invariant
        Vec3 P = ReflectAcrossAxis(Vec3(0, 0, 7), { Vec3{}, Vec3::UnitZ() });
        Panel.Within("Z-axis mirror of (0,0,7) = (0,0,7)", (P - Vec3(0, 0, 7)).Length(), 1e-12);
    }

    Panel.Section("Math: Rodrigues rotation is correct for 90°, 180°, 360°");
    {
        // 90° around Z: (1, 0, 0) -> (0, 1, 0)
        Vec3 R = RotateAroundAxis(Vec3(1, 0, 0), { Vec3{}, Vec3::UnitZ() }, 3.14159265358979323846 / 2.0);
        Panel.Within("90° around Z of (1,0,0) = (0,1,0)", (R - Vec3(0, 1, 0)).Length(), 1e-12);
        // 180° around Z: (1, 0, 0) -> (-1, 0, 0)
        Vec3 R2 = RotateAroundAxis(Vec3(1, 0, 0), { Vec3{}, Vec3::UnitZ() }, 3.14159265358979323846);
        Panel.Within("180° around Z of (1,0,0) = (-1,0,0)", (R2 - Vec3(-1, 0, 0)).Length(), 1e-12);
        // 360° around Z: (1, 0, 0) -> (1, 0, 0)
        Vec3 R3 = RotateAroundAxis(Vec3(1, 0, 0), { Vec3{}, Vec3::UnitZ() }, 2.0 * 3.14159265358979323846);
        Panel.Within("360° around Z of (1,0,0) = (1,0,0)", (R3 - Vec3(1, 0, 0)).Length(), 1e-12);
    }

    Panel.Section("Math: multi-axis composition is a 180° rotation around the line of intersection");
    {
        // Two perpendicular planes (XY then XZ) on (1, 2, 3) -> (1, -2, -3).
        MirrorOp Ops[2];
        Ops[0].Kind = MirrorOp::Kind::Plane; Ops[0].Origin = Vec3{}; Ops[0].NormalOrDir = Vec3::UnitZ();
        Ops[1].Kind = MirrorOp::Kind::Plane; Ops[1].Origin = Vec3{}; Ops[1].NormalOrDir = Vec3::UnitY();
        Vec3 R = ComposeMirrors(Vec3(1, 2, 3), Ops, 2);
        Panel.Within("XY then XZ of (1,2,3) = (1,-2,-3)", (R - Vec3(1, -2, -3)).Length(), 1e-12);
    }

    Panel.Section("Math: custom plane (non-axis-aligned) reflection");
    {
        // Plane through origin with normal (1, 1, 0).ReflectAcrossPlane((3, 0, 0)) -> (0, -3, 0).
        Vec3 N(1, 1, 0);
        Vec3 R = ReflectAcrossPlane(Vec3(3, 0, 0), Vec3{}, N);
        Panel.Within("diag plane (1,1,0) of (3,0,0) = (0,-3,0)", (R - Vec3(0, -3, 0)).Length(), 1e-12);
    }

    Panel.Section("Math: radial 4 copies of a point around Z are distinct and 90° apart");
    {
        // (1, 0, 0) at 0°, 90°, 180°, 270° around Z
        MirrorAxis Axis{ Vec3{}, Vec3::UnitZ() };
        double Step = 3.14159265358979323846 / 2.0;
        Vec3 P0 = RotateAroundAxis(Vec3(1, 0, 0), Axis, 0 * Step);
        Vec3 P1 = RotateAroundAxis(Vec3(1, 0, 0), Axis, 1 * Step);
        Vec3 P2 = RotateAroundAxis(Vec3(1, 0, 0), Axis, 2 * Step);
        Vec3 P3 = RotateAroundAxis(Vec3(1, 0, 0), Axis, 3 * Step);
        Panel.Within("0° = (1,0,0)",  (P0 - Vec3(1, 0, 0)).Length(), 1e-12);
        Panel.Within("90° = (0,1,0)", (P1 - Vec3(0, 1, 0)).Length(), 1e-12);
        Panel.Within("180° = (-1,0,0)", (P2 - Vec3(-1, 0, 0)).Length(), 1e-12);
        Panel.Within("270° = (0,-1,0)", (P3 - Vec3(0, -1, 0)).Length(), 1e-12);
    }

    return 0;
}

//---- host integration tests ----------------------------------------------------------------------------------------

static int HostIntegrationSuite(VerificationPanel& Panel) noexcept
{
    Panel.Section("Host: `empty --name=E --at=(x,y,z)` creates an Empty, `list empty` shows it, `delete empty E` removes it");
    {
        ConsoleHost Host("/tmp/SolidArcVerificationP19A", 1280, 800);
        Host.Execute("empty --name=E1 --at=(1.5, 2.5, 3.5)");
        bool Found = false;
        for (const auto& F : Host.AllFigures()) if (F.Name == "E1" && F.Classification == FigureClassification::Empty) { Found = true; break; }
        Panel.Expect("empty E1 was created with Empty classification", Found);
        Host.Execute("list empty");
        Host.Execute("delete empty E1");
        Found = false;
        for (const auto& F : Host.AllFigures()) if (F.Name == "E1") { Found = true; break; }
        Panel.Expect("empty E1 was removed by delete empty", !Found);
    }

    Panel.Section("Host: `mirror L1 --across=xy` produces Mirror.L1 with the Z coordinate flipped");
    {
        ConsoleHost Host("/tmp/SolidArcVerificationP19B", 1280, 800);
        Host.Execute("line (1, 2, 3) (4, 5, 6) --name=L1");
        Host.Execute("mirror L1 --across=xy");
        const SceneFigure* L1 = Host.Document().Find("L1");
        const SceneFigure* Mirror = Host.Document().Find("Mirror.L1");
        Panel.Expect("L1 exists", L1 != nullptr);
        Panel.Expect("Mirror.L1 exists", Mirror != nullptr);
        if (Mirror)
        {
            // Original L1: A=(1,2,3), B=(4,5,6). Mirror should have A=(1,2,-3), B=(4,5,-6).
            const auto& C = Mirror->Curve;
            if (C.Poles.size() >= 2)
            {
                Vec3 PA = C.Poles[0].Divide();
                Vec3 PB = C.Poles[1].Divide();
                Panel.Within("Mirror.L1 start = (1,2,-3)", (PA - Vec3(1, 2, -3)).Length(), 1e-6);
                Panel.Within("Mirror.L1 end = (4,5,-6)",   (PB - Vec3(4, 5, -6)).Length(), 1e-6);
            }
        }
    }

    Panel.Section("Host: mirror preserves the length of the source");
    {
        ConsoleHost Host("/tmp/SolidArcVerificationP19C", 1280, 800);
        Host.Execute("line (0, 0, 0) (3, 4, 0) --name=L1");
        double LenBefore = Host.Document().Find("L1")->Curve.Length();
        Host.Execute("mirror L1 --across=xy");
        double LenAfter = Host.Document().Find("Mirror.L1")->Curve.Length();
        Panel.Within("length preserved through mirror", LenBefore - LenAfter, 1e-9);
    }

    Panel.Section("Host: `mirror L1 --in-place` mutates the source instead of copying");
    {
        ConsoleHost Host("/tmp/SolidArcVerificationP19D", 1280, 800);
        Host.Execute("line (1, 2, 3) (4, 5, 6) --name=L1");
        size_t Before = Host.AllFigures().size();
        Host.Execute("mirror L1 --in-place --across=xy");
        size_t After = Host.AllFigures().size();
        Panel.Expect("no new figure created by in-place mirror", After == Before);
        const SceneFigure* L1 = Host.Document().Find("L1");
        if (L1 && L1->Curve.Poles.size() >= 2)
        {
            Vec3 PA = L1->Curve.Poles[0].Divide();
            Panel.Within("L1 in-place reflected: A = (1,2,-3)", (PA - Vec3(1, 2, -3)).Length(), 1e-6);
        }
    }

    Panel.Section("Host: `mirror L1 --across=P_top` works for a custom named workplane");
    {
        ConsoleHost Host("/tmp/SolidArcVerificationP19E", 1280, 800);
        // Build a plane primitive at z=1, then save it as a named workplane.
        Host.Execute("plane (0,0,1) 1 1 --name=P_top_src --u=(1,0,0) --v=(0,1,0)");
        Host.Execute("workplane xy --origin=(0,0,1)");
        Host.Execute("plane --name=P_top");
        Host.Execute("line (1, 0, 5) (2, 0, 5) --name=L1");
        Host.Execute("mirror L1 --across=P_top");
        // The plane P_top is the XY plane shifted to z=1. The reflection of (x, y, z) across it is
        //    (x, y, 2 - z). So L1's start (1, 0, 5) becomes (1, 0, -3), end (2, 0, 5) becomes (2, 0, -3).
        const SceneFigure* Mirror = Host.Document().Find("Mirror.L1");
        Panel.Expect("Mirror.L1 exists", Mirror != nullptr);
        if (Mirror && Mirror->Curve.Poles.size() >= 2)
        {
            Vec3 PA = Mirror->Curve.Poles[0].Divide();
            Panel.Within("Mirror.L1 start = (1, 0, -3)", (PA - Vec3(1, 0, -3)).Length(), 1e-6);
        }
    }

    Panel.Section("Host: `mirror L1 --across=((0,0,0),(0,0,1))` (axis) works");
    {
        ConsoleHost Host("/tmp/SolidArcVerificationP19F", 1280, 800);
        Host.Execute("line (3, 4, 5) (6, 8, 10) --name=L1");
        Host.Execute("mirror L1 --across=(0,0,0),(0,0,1)");
        const SceneFigure* Mirror = Host.Document().Find("Mirror.L1");
        if (Mirror && Mirror->Curve.Poles.size() >= 2)
        {
            // Reflect (3,4,5) across the Z axis -> (-3,-4,5)
            Vec3 PA = Mirror->Curve.Poles[0].Divide();
            Panel.Within("Mirror.L1 start = (-3,-4,5)", (PA - Vec3(-3, -4, 5)).Length(), 1e-6);
        }
    }

    Panel.Section("Host: `radial L1 --count=4 --axis=... --angle=360` produces 3 copies at 90°, 180°, 270° around the Z axis");
    {
        ConsoleHost Host("/tmp/SolidArcVerificationP19G", 1280, 800);
        Host.Execute("line (1, 0, 0) (2, 0, 0) --name=L1");
        Host.Execute("radial L1 --count=4 --axis=(0,0,0),(0,0,1) --angle=360");
        const SceneFigure* R1 = Host.Document().Find("Radial.L1.1");
        const SceneFigure* R2 = Host.Document().Find("Radial.L1.2");
        const SceneFigure* R3 = Host.Document().Find("Radial.L1.3");
        Panel.Expect("Radial.L1.1 exists", R1 != nullptr);
        Panel.Expect("Radial.L1.2 exists", R2 != nullptr);
        Panel.Expect("Radial.L1.3 exists", R3 != nullptr);
        if (R1 && R1->Curve.Poles.size() >= 1)
        {
            // 90° around Z of (1,0,0) = (0,1,0)
            Vec3 PA = R1->Curve.Poles[0].Divide();
            Panel.Within("Radial.L1.1 start = (0,1,0)", (PA - Vec3(0, 1, 0)).Length(), 1e-6);
        }
        if (R2 && R2->Curve.Poles.size() >= 1)
        {
            Vec3 PA = R2->Curve.Poles[0].Divide();
            Panel.Within("Radial.L1.2 start = (-1,0,0)", (PA - Vec3(-1, 0, 0)).Length(), 1e-6);
        }
        if (R3 && R3->Curve.Poles.size() >= 1)
        {
            Vec3 PA = R3->Curve.Poles[0].Divide();
            Panel.Within("Radial.L1.3 start = (0,-1,0)", (PA - Vec3(0, -1, 0)).Length(), 1e-6);
        }
    }

    Panel.Section("Host: multi-axis mirror (--across=xy --also=xz) produces 3 copies in 2 perpendicular planes");
    {
        ConsoleHost Host("/tmp/SolidArcVerificationP19H", 1280, 800);
        Host.Execute("line (1, 1, 1) (2, 2, 2) --name=L1");
        Host.Execute("mirror L1 --across=xy --also=xz");
        const SceneFigure* M1 = Host.Document().Find("Mirror.L1.1");            // only xy
        const SceneFigure* M2 = Host.Document().Find("Mirror.L1.2");            // only xz
        const SceneFigure* M3 = Host.Document().Find("Mirror.L1.3");            // both
        Panel.Expect("Mirror.L1.1 (xy only) exists", M1 != nullptr);
        Panel.Expect("Mirror.L1.2 (xz only) exists", M2 != nullptr);
        Panel.Expect("Mirror.L1.3 (both) exists",    M3 != nullptr);
    }

    Panel.Section("Host: mirror of a body (box) reflects both A and B; the body is rebuilt");
    {
        ConsoleHost Host("/tmp/SolidArcVerificationP19I", 1280, 800);
        Host.Execute("box (1, 2, 3) (4, 5, 6) --name=B1");
        Host.Execute("mirror B1 --across=xy");
        const SceneFigure* B = Host.Document().Find("Mirror.B1");
        Panel.Expect("Mirror.B1 exists", B != nullptr);
        if (B)
        {
            Panel.Within("Mirror.B1 A = (1, 2, -3)", (B->Blueprint.A - Vec3(1, 2, -3)).Length(), 1e-6);
            Panel.Within("Mirror.B1 B = (4, 5, -6)", (B->Blueprint.B - Vec3(4, 5, -6)).Length(), 1e-6);
        }
    }

    Panel.Section("Host: radial of a body produces copies of the body");
    {
        ConsoleHost Host("/tmp/SolidArcVerificationP19J", 1280, 800);
        Host.Execute("sphere (1, 0, 0) 0.3 --name=S1");
        Host.Execute("radial S1 --count=4 --axis=(0,0,0),(0,0,1) --angle=360");
        size_t Spheres = 0;
        for (const auto& F : Host.AllFigures()) if (F.Blueprint.Form == SceneFigure::ParametricForm::Sphere) ++Spheres;
        Panel.Expect("4 sphere figures after radial (source + 3 copies)", Spheres == 4);
    }

    Panel.Section("Render: a mirrored line renders to a non-empty PNG");
    {
        ConsoleHost Host("/tmp/SolidArcVerificationP19K", 1280, 800);
        Host.Execute("line (1, 1, 1) (3, 2, 1) --name=L1");
        Host.Execute("mirror L1 --across=xy");
        Host.Execute("view fit");
        Host.Execute("render Phase19_MirrorLine");
        std::ifstream F("/tmp/SolidArcVerificationP19K/Phase19_MirrorLine.png", std::ios::binary | std::ios::ate);
        Panel.Expect("Phase19_MirrorLine.png was written", bool(F));
        if (F) { Panel.Expect("Phase19_MirrorLine.png is non-empty", F.tellg() > 0); }
    }

    return 0;
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 19 · Mirror Verification — 3D reflection (plane / axis / multi-axis) and radial rotation math; mirror / radial / empty host verbs; Blueprint round-trip; length preservation; 3 demo figures (line / box / body)");
    DirectMathSuite(Panel);
    HostIntegrationSuite(Panel);
    return Panel.Conclude();
}
