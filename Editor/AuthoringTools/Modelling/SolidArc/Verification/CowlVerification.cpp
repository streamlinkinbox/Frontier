//============================================================================================================================================
//                                                           COWLVERIFICATION.CPP
//============================================================================================================================================
// 📦 Native Liger cowl replay, open-skin topology, orientation, sampling, persistence and presentation checks.

#include "Console/ConsoleHost.h"
#include "Document/UndoSequence.h"
#include "VerificationPanel.h"
#include <bit>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace Frontier;

int main(int Count, char** Arguments)
{
    if (Count < 2 || Count > 4)
    {
        std::cerr << "CowlVerification <cowl.arc> [samples.f64 [charts.queries]]\n";
        return 2;
    }
    const bool Consolidated = Count == 4;
    VerificationPanel Panel("SolidArc · Liger cowl · native exterior geometry");
    const std::filesystem::path Directory = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / (Consolidated ? "ConsolidatedCowlVerification" : "CowlVerification");
    std::filesystem::create_directories(Directory);
    ConsoleHost Host(Directory.string(), 1280, 800);
    Panel.Expect("native cowl opens", Host.Execute(std::string("open \"") + Arguments[1] + "\""));
    Panel.Expect("one sewn exterior body", Host.AllFigures().size() == 1);
    if (Host.AllFigures().size() != 1) return Panel.Conclude();
    const SceneFigure& Cowl = Host.AllFigures().front();
    Panel.Expect("cowl is editable B-rep, not display triangles", Cowl.Classification == FigureClassification::Body);
    const BodyReport Report = Cowl.Body.Validate();
    Panel.Expect("all source charts retained without caps", Report.Faces == (Consolidated ? 30 : 308));
    Panel.Expect("source edge count retained", Report.Edges == (Consolidated ? 244 : 656));
    Panel.Expect("intentional boundary count retained", Report.OpenEdges == 80);
    Panel.Expect("one connected exterior skin", Report.Hulls == 1);
    Panel.Expect("no non-manifold edges", Report.NonManifoldEdges == 0 && Report.Manifold);
    Panel.Expect("no misoriented edges", Report.MisorientedEdges == 0 && Report.Oriented);
    Panel.Expect("not a fabricated closed solid", !Report.Closed && !Report.Solid());
    Panel.Expect("measured area retained in square metres", Report.Area > 0.793 && Report.Area < 0.794);
    bool SourceOrientation = true;
    for (const BrepFace& Face : Cowl.Body.Faces) SourceOrientation = SourceOrientation && !Face.Reversed;
    Panel.Expect("source face orientations preserved", SourceOrientation);

    // 📝 Optional binary output is compared against separately extracted Blender samples by SurfaceMetrics.py.
    if (Count >= 3)
    {
        static_assert(sizeof(double) == 8 && std::endian::native == std::endian::little);
        std::ofstream Samples(Arguments[2], std::ios::binary);
        std::ifstream Queries;
        if (Consolidated) Queries.open(Arguments[3]);
        bool ValidQueries = true;
        for (int Chart = 0; Chart < 308; ++Chart)
        {
            int Identity = Chart;
            std::array<double, 6> Transform{ 1, 0, 0, 0, 1, 0 };
            if (Consolidated)
            {
                if (!(Queries >> Identity)) { ValidQueries = false; break; }
                for (double& Coefficient : Transform)
                    if (!(Queries >> Coefficient) || !std::isfinite(Coefficient)) ValidQueries = false;
            }
            if (!ValidQueries || Identity < 0 || static_cast<size_t>(Identity) >= Cowl.Body.Faces.size())
            { ValidQueries = false; break; }
            if (Transform[0] * Transform[4] - Transform[1] * Transform[3] <= 0) { ValidQueries = false; break; }
            const BrepFace& Face = Cowl.Body.Faces[Identity];
            for (int U = 0; U <= 32; ++U)
            {
                for (int V = 0; V <= 32; ++V)
                {
                    const double A = Transform[0] * U / 32.0 + Transform[1] * V / 32.0 + Transform[2];
                    const double B = Transform[3] * U / 32.0 + Transform[4] * V / 32.0 + Transform[5];
                    if (A < -1e-12 || A > 1 + 1e-12 || B < -1e-12 || B > 1 + 1e-12) ValidQueries = false;
                    const Vec3 Position = Face.Surface.Sample(A, B);
                    const double Coordinates[3] = { Position.X, Position.Y, Position.Z };
                    Samples.write(reinterpret_cast<const char*>(Coordinates), sizeof(Coordinates));
                }
            }
        }
        if (Consolidated)
        {
            std::string Extra;
            if (Queries >> Extra) ValidQueries = false;
        }
        Panel.Expect("reference chart transforms are complete and oriented", ValidQueries);
        Samples.close();
        Panel.Expect("native samples written successfully", !Samples.fail());
    }

    const uint64_t Before = UndoSequence::Fingerprint(Host.Document());
    const std::string Saved = (Directory / "Cowl_RoundTrip.arc").generic_string();
    Panel.Expect("native cowl saves", Host.Execute("save \"" + Saved + "\""));
    Panel.Expect("clear saved cowl", Host.Execute("reset"));
    Panel.Expect("native cowl reopens", Host.Execute("open \"" + Saved + "\""));
    Panel.Expect("exact geometric fingerprint survives save/reopen", UndoSequence::Fingerprint(Host.Document()) == Before);
    for (const char* Command : { "show cages off", "show iso off", "show shading plastic", "view orbit 60 30", "view fit",
                                "render Liger_Cowl_Native --size=1600x1000" })
        Panel.Expect(Command, Host.Execute(Command));
    return Panel.Conclude();
}
