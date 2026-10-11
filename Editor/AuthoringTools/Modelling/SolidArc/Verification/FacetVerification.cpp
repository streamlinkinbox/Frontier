//============================================================================================================================================
//                                                          FACETVERIFICATION.CPP
//============================================================================================================================================
// 📦 Source-faithful roof/glass-frame planar CAD: topology, full-face displacement bounds, orientation and persistence.

#include "Console/ConsoleHost.h"
#include "Document/UndoSequence.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>

using namespace Frontier;

int main(int Count, char** Arguments)
{
    if (Count < 3 || Count > 4)
    {
        std::cerr << "FacetVerification <document.arc> <reference.triangles> [assembly.arc]\n";
        return 2;
    }
    VerificationPanel Panel("SolidArc · Roof/glass-frame · source facets retained as CAD");
    const auto Directory = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "FacetVerification";
    std::filesystem::create_directories(Directory);
    ConsoleHost Host(Directory.string(), 1280, 800);
    Panel.Expect("native document opens", Host.Execute(std::string("open \"") + Arguments[1] + "\""));
    Panel.Expect("one sewn body", Host.AllFigures().size() == 1);
    if (Host.AllFigures().size() != 1) return Panel.Conclude();
    const auto& Figure = Host.AllFigures().front();
    const auto& Body = Figure.Body;
    Panel.Expect("editable surface body rather than display triangles", Figure.Classification == FigureClassification::Body);
    const BodyReport Report = Body.Validate();
    Panel.Expect("all source facets retained", Report.Faces == 170);
    Panel.Expect("only coincident source vertices sewn", Report.Vertices == 147);
    Panel.Expect("source geometric edge count retained", Report.Edges == 316);
    Panel.Expect("all geometric open boundaries retained", Report.OpenEdges == 122 && !Report.Closed);
    Panel.Expect("connected oriented manifold", Report.Hulls == 1 && Report.Manifold && Report.Oriented);
    Panel.Expect("no extra topology or caps", Report.EulerCharacteristic == 1 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
    Panel.Within("source area retained", std::fabs(Report.Area - 0.25725924039627546), 1e-8);
    std::ifstream Reference(Arguments[2]);
    bool Valid = true, Planar = true, Oriented = true;
    double Maximum = 0;
    int Reoriented = 0;
    for (const BrepFace& Face : Body.Faces)
    {
        Vec3 A, B, C;
        for (Vec3* P : { &A, &B, &C })
        {
            if (!(Reference >> P->X >> P->Y >> P->Z)) Valid = false;
            if (!std::isfinite(P->X) || !std::isfinite(P->Y) || !std::isfinite(P->Z)) Valid = false;
        }
        if (!Valid) break;
        const NurbsSurface& S = Face.Surface;
        Planar = Planar && S.DegreeU == 1 && S.DegreeV == 1 && S.CountU == 2 && S.CountV == 2 && !S.Rational();
        if (!Planar) break;
        // 📝 Bilinear non-negative basis: the largest coefficient displacement bounds the entire surface.
        Maximum = std::max({ Maximum, S.Pole(0, 0).Divide().Distance(A), S.Pole(1, 0).Divide().Distance(B),
                            S.Pole(0, 1).Divide().Distance(C), S.Pole(1, 1).Divide().Distance(C) });
        Reoriented += Face.Reversed ? 1 : 0;
        Oriented = Oriented && S.Normal(.4, .4).Dot((B - A).Cross(C - A).Normalised()) > .999999;
    }
    std::string Extra;
    if (Reference >> Extra) Valid = false;
    Panel.Expect("complete finite source fixture", Valid);
    Panel.Expect("facets remain planar triangular surfaces", Planar);
    Panel.Expect("underlying source parameter orientations retained", Oriented);
    Panel.Expect("inconsistent source winding repaired", Reoriented == 72);
    std::cout << "Reoriented source facets: " << Reoriented << "\n";
    Panel.Within("continuous source deviation below one nanometre", Maximum, 1e-9);
    std::ofstream(Directory / "FacetMetrics.json") << std::setprecision(17)
        << "{\"continuousMaximumBoundMm\":" << Maximum * 1000 << ",\"faces\":" << Report.Faces
        << ",\"reorientedSourceFacets\":" << Reoriented << ",\"openEdges\":" << Report.OpenEdges << ",\"areaM2\":" << Report.Area << "}\n";
    const uint64_t Before = UndoSequence::Fingerprint(Host.Document());
    const std::string Saved = (Directory / "Facet_RoundTrip.arc").generic_string();
    Panel.Expect("save native facets", Host.Execute("save \"" + Saved + "\""));
    Panel.Expect("reopen native facets", Host.Execute("open \"" + Saved + "\""));
    Panel.Expect("geometry survives replay exactly", UndoSequence::Fingerprint(Host.Document()) == Before);
    for (const char* Command : { "show cages off", "show iso off", "show shading plastic", "view iso", "view fit",
                                "render Liger_Roof_Glass_Frame --size=1600x1000" })
        Panel.Expect(Command, Host.Execute(Command));
    if (Count == 4)
    {
        Panel.Expect("open partial exterior assembly", Host.Execute(std::string("open \"" ) + Arguments[3] + "\""));
        Panel.Expect("two independent exterior components", Host.AllFigures().size() == 2);
        bool CowlFound = false, RoofFound = false;
        for (const auto& Component : Host.AllFigures())
        {
            const BodyReport Topology = Component.Body.Validate();
            if (Component.Name == "Liger_Front_Cowl") CowlFound = Topology.Faces == 30 && Topology.OpenEdges == 80;
            if (Component.Name == "Liger_Roof_Glass_Frame") RoofFound = Topology.Faces == 170 && Topology.OpenEdges == 122;
        }
        Panel.Expect("assembly preserves both component topologies", CowlFound && RoofFound);
        const uint64_t Assembly = UndoSequence::Fingerprint(Host.Document());
        const std::string Combined = (Directory / "Partial_Exterior_RoundTrip.arc").generic_string();
        Panel.Expect("save assembly", Host.Execute("save \"" + Combined + "\""));
        Panel.Expect("reopen assembly", Host.Execute("open \"" + Combined + "\""));
        Panel.Expect("assembly geometry survives replay", UndoSequence::Fingerprint(Host.Document()) == Assembly);
        for (const char* Command : { "show cages off", "show iso off", "show shading plastic", "view iso", "view fit",
                                    "render Liger_Exterior_Partial --size=1600x1000" })
            Panel.Expect(Command, Host.Execute(Command));
    }
    return Panel.Conclude();
}
