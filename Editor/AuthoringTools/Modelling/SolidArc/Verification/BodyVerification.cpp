//============================================================================================================================================
//                                                           BODYVERIFICATION.CPP
//============================================================================================================================================
// 📦 Native main-body surface replay, source-chart sampling, persistence and rendered geometry verification.

#include "Console/ConsoleHost.h"
#include "Document/UndoSequence.h"
#include "VerificationPanel.h"
#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>

using namespace Frontier;

int main(int Count, char** Arguments)
{
    if (Count != 4 && Count != 5)
    {
        std::cerr << "BodyVerification <body.arc> <body.queries> <samples.f64> [assembly.arc]\n";
        return 2;
    }
    std::map<int, int> SourceSurfaces;
    std::ifstream Declarations(Arguments[2]);
    int SurfaceIndex = -1, JunctionDeclaration = -1;
    std::string Remainder;
    while (Declarations >> SurfaceIndex >> JunctionDeclaration)
    {
        SourceSurfaces[SurfaceIndex] = JunctionDeclaration;
        std::getline(Declarations, Remainder);
    }
    if (SourceSurfaces.empty() || SourceSurfaces.begin()->first != 0 ||
        SourceSurfaces.rbegin()->first + 1 != static_cast<int>(SourceSurfaces.size())) return 2;
    VerificationPanel Panel("SolidArc · Liger main-body starting skin");
    const std::filesystem::path Destination = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "BodyVerification";
    std::filesystem::create_directories(Destination);
    ConsoleHost Host(Destination.string(), 1600, 1000);
    Panel.Expect("main body document opens", Host.Execute(std::string("open \"") + Arguments[1] + "\""));
    const SceneFigure* BodyFigure = nullptr;
    std::map<int, const NurbsSurface*> Junctions;
    const std::string Prefix = "Main_Junction_";
    for (const SceneFigure& Figure : Host.AllFigures())
    {
        if (Figure.Name == "Liger_Main_Body" && Figure.Classification == FigureClassification::Body) BodyFigure = &Figure;
        else if (Figure.Name.starts_with(Prefix) && Figure.Classification == FigureClassification::Surface)
            Junctions.emplace(std::stoi(Figure.Name.substr(Prefix.size())), &Figure.Surface);
    }
    Panel.Expect("one main body and explicit source junction sheets", BodyFigure && Junctions.size() == 24 && Host.AllFigures().size() == 25);
    if (!BodyFigure) return Panel.Conclude();
    const BodyReport Report = BodyFigure->Body.Validate();
    Panel.Expect("all fitted surfaces retained", Report.Faces + static_cast<int>(Junctions.size()) == static_cast<int>(SourceSurfaces.size()));
    Panel.Expect("no fabricated closed solid", !Report.Closed);
    Panel.Expect("sewn main body has no three-face edges", Report.NonManifoldEdges == 0);
    Panel.Expect("sewn main body is consistently oriented", Report.MisorientedEdges == 0);
    std::ofstream Metrics(Destination / "BodyMetrics.json");
    Metrics << std::setprecision(17) << "{\"faces\":" << Report.Faces << ",\"vertices\":" << Report.Vertices
            << ",\"edges\":" << Report.Edges << ",\"openEdges\":" << Report.OpenEdges
            << ",\"nonManifoldEdges\":" << Report.NonManifoldEdges << ",\"misorientedEdges\":" << Report.MisorientedEdges
            << ",\"hulls\":" << Report.Hulls << ",\"areaM2\":" << Report.Area << ",\"independentJunctionSheets\":" << Junctions.size() << "}\n";
    static_assert(sizeof(double) == 8 && std::endian::native == std::endian::little);
    std::ifstream Queries(Arguments[2]);
    std::ofstream Samples(Arguments[3], std::ios::binary);
    bool Valid = Queries.good() && Samples.good();
    int ChartCount = 0;
    int Identity = -1, Junction = -1;
    while (Queries >> Identity >> Junction)
    {
        std::array<double, 6> Transform{};
        for (double& Coefficient : Transform) Valid = static_cast<bool>(Queries >> Coefficient) && std::isfinite(Coefficient) && Valid;
        if (!Valid || Identity < 0 || Identity >= static_cast<int>(SourceSurfaces.size()) || (Junction != 0 && Junction != 1)) { Valid = false; break; }
        const NurbsSurface* PatchSurface = nullptr;
        if (Junction)
        {
            const auto Found = Junctions.find(Identity);
            if (Found != Junctions.end()) PatchSurface = Found->second;
        }
        else
        {
            int Offset = 0;
            for (const auto& [SheetIndex, SheetSurface] : Junctions)
            {
                (void)SheetSurface;
                if (SheetIndex < Identity) ++Offset;
            }
            const int FaceIndex = Identity - Offset;
            if (FaceIndex >= 0 && FaceIndex < Report.Faces) PatchSurface = &BodyFigure->Body.Faces[FaceIndex].Surface;
        }
        if (!PatchSurface || Transform[0] * Transform[4] - Transform[1] * Transform[3] <= 0) { Valid = false; break; }
        for (int Along = 0; Along <= 16; ++Along)
        {
            for (int Across = 0; Across <= 16; ++Across)
            {
                const double ParameterU = Transform[0] * Along / 16.0 + Transform[1] * Across / 16.0 + Transform[2];
                const double ParameterV = Transform[3] * Along / 16.0 + Transform[4] * Across / 16.0 + Transform[5];
                if (ParameterU < -1e-12 || ParameterU > 1 + 1e-12 || ParameterV < -1e-12 || ParameterV > 1 + 1e-12) Valid = false;
                const Vec3 Position = PatchSurface->Sample(ParameterU, ParameterV);
                const double Coordinates[3] = { Position.X, Position.Y, Position.Z };
                if (!std::isfinite(Position.X) || !std::isfinite(Position.Y) || !std::isfinite(Position.Z)) Valid = false;
                Samples.write(reinterpret_cast<const char*>(Coordinates), sizeof(Coordinates));
            }
        }
        ++ChartCount;
    }
    Panel.Expect("every original chart sampled from native geometry", Valid && Queries.eof() && ChartCount == 17474 && Samples.good());
    Samples.close();
    auto SurfaceSignature = [&Host]()
    {
        std::vector<double> Signature;
        auto Append = [&Signature](const NurbsSurface& PatchSurface)
        {
            Signature.insert(Signature.end(), { double(PatchSurface.CountU), double(PatchSurface.CountV),
                double(PatchSurface.DegreeU), double(PatchSurface.DegreeV),
                double(PatchSurface.KnotsU.size()), double(PatchSurface.KnotsV.size()) });
            Signature.insert(Signature.end(), PatchSurface.KnotsU.begin(), PatchSurface.KnotsU.end());
            Signature.insert(Signature.end(), PatchSurface.KnotsV.begin(), PatchSurface.KnotsV.end());
            for (const Vec4& Pole : PatchSurface.Poles) Signature.push_back(Pole.W);
        };
        for (const SceneFigure& Figure : Host.AllFigures())
        {
            if (Figure.Classification == FigureClassification::Surface) Append(Figure.Surface);
            else for (const BrepFace& Face : Figure.Body.Faces) Append(Face.Surface);
        }
        return Signature;
    };
    const auto BeforeSurfaces = SurfaceSignature();
    const uint64_t Before = UndoSequence::Fingerprint(Host.Document());
    const std::string Saved = (Destination / "Body_RoundTrip.arc").generic_string();
    Panel.Expect("main body saves", Host.Execute("save \"" + Saved + "\""));
    Panel.Expect("main body reopens", Host.Execute("open \"" + Saved + "\""));
    Panel.Expect("geometry survives save/reopen", Before == UndoSequence::Fingerprint(Host.Document()));
    Panel.Expect("all spline degrees, dimensions, weights and knots survive replay", BeforeSurfaces == SurfaceSignature());
    for (const char* Command : { "show cages off", "show iso off", "show shading plastic", "view iso", "view fit",
                                "render Liger_Main_Body --size=1600x1000", "view front", "view fit",
                                "render Liger_Main_Body_Side --size=1600x1000", "show edges off", "view orbit 35 18", "view persp", "view fit",
                                "render Liger_Main_Body_Shaded --size=1600x1000" })
        Panel.Expect(Command, Host.Execute(Command));
    Panel.Expect("edge display does not alter geometry", Before == UndoSequence::Fingerprint(Host.Document()));
    if (Count == 5)
    {
        Panel.Expect("combined reconstruction opens", Host.Execute(std::string("open \"") + Arguments[4] + "\""));
        Panel.Expect("all components and junction sheets retained", Host.AllFigures().size() == 27);
        size_t BodyCount = 0;
        size_t FaceCount = 0;
        for (const SceneFigure& Figure : Host.AllFigures())
        {
            if (Figure.Classification == FigureClassification::Body)
            {
                ++BodyCount;
                FaceCount += Figure.Body.Faces.size();
            }
        }
        Panel.Expect("three supplied components retain every sewn face", BodyCount == 3 && FaceCount == SourceSurfaces.size() - Junctions.size() + 200);
        for (const char* Command : { "show cages off", "show iso off", "show edges off", "show shading plastic",
                                    "view front", "view orbit 35 18", "view persp", "view fit",
                                    "render Liger_Reconstruction --size=1600x1000" })
            Panel.Expect(Command, Host.Execute(Command));
    }
    return Panel.Conclude();
}
