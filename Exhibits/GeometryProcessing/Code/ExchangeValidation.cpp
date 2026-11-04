/*====================================================================================================================================
                                                    EXCHANGEVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Exchange (priority 4): X1 STL export read back — closure, Euler characteristic against the B-rep, volume within the
//    chordal bound, binary = ASCII, refusals; X2 STEP AP214 export walked by an independent Part 21 parser — references,
//    counts, closed edge uses, connected loops, carrier types, exact vertices, units, stand-in deviation, refusals; X3 STEP
//    import — every exported body read back (counts, carriers, bit-identical vertices, volume, a byte fixed point after one
//    generation), OpenCascade-authored fixtures against OpenCascade's own measures, refusals that leave the partition alone

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Blend/BlendCatalogue.h"
#include "Engine/GeometryProcessing/Blend/EdgeBlend.h"
#include "Engine/GeometryProcessing/Construction/BodyTransform.h"
#include "Engine/GeometryProcessing/Construction/PrimitiveConstruction.h"
#include "Engine/GeometryProcessing/Exchange/StepExchange.h"
#include "Engine/GeometryProcessing/Exchange/StlExchange.h"
#include "Engine/GeometryProcessing/Geometry/SurfaceGeometry.h"
#include "Engine/GeometryProcessing/Inspection/SpecimenCatalogue.h"
#include "Engine/GeometryProcessing/Measurement/MassProperties.h"
#include "Engine/GeometryProcessing/Tessellation/BodyTessellation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <numbers>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{

struct GateTally
{
    const char* Code     = "";                                                               // [-] - EX1 …
    const char* Subject  = "";                                                               // [-] - What it measures
    const char* Limit    = "";                                                               // [-] - Acceptance rule
    double      Worst    = 0.0;                                                              // [-] - Largest measured value
    long        Checks   = 0;                                                                // [-]
    long        Failures = 0;                                                                // [-]
};

GateTally StlClosureGate  = { "EX1", "STL closure: every specimen (primitives, hole, cavity, NURBS sweep / loft / pipe, rolling-ball and variable fillets, chamfers) written binary and read back welds watertight — no open, non-manifold or misoriented side, no degenerate facet; facets = complex triangles; components = shells; Euler χ of the facets = the B-rep's V − E + 2F − L", "0 defects · χ exact" };
GateTally StlVolumeGate   = { "EX2", "STL geometry: |V(facets) − V(exact)| ≤ sag · A(exact) for every specimen (each facet lies within sag of its face); the sphere's error shrinks at least 2× when sag shrinks 4×; ModelUnitMillimetres 1000 scales every corner by 1000 within float rounding", "≤ sag · A · ratio ≤ ½" };
GateTally StlFormatGate   = { "EX3", "STL formats: binary size 84 + 50 n, header never \"solid…\"; ASCII (9 significant digits) reads back to the same welded corners and facets as binary; stored normals = unit facet normals; writing twice gives the same bytes", "identical" };
GateTally StlRefusalGate  = { "EX4", "STL refusals and defect detection: no bodies, a zero / negative unit, a stale body token, a truncated binary, ASCII without endsolid, a facet with two vertices; one facet removed → 3 open sides, one flipped → 3 misoriented, one repeated → 3 non-manifold", "named · counted" };

GateTally StepStructureGate = { "EX5", "STEP structure, read by an independent Part 21 parser: AP214 schema, every #reference defined once, ADVANCED_FACE / EDGE_CURVE / VERTEX_POINT / CLOSED_SHELL / solid counts = the B-rep's; every EDGE_CURVE used exactly twice per closed shell in opposite effective directions; every EDGE_LOOP connected head to tail; writing twice gives the same bytes", "exact" };
GateTally StepGeometryGate  = { "EX6", "STEP geometry: every face's carrier entity matches its kind (PLANE, CYLINDRICAL / CONICAL / SPHERICAL / TOROIDAL_SURFACE; NURBS → B-spline, rational where weighted; rolling-ball → certified B-spline stand-in); every VERTEX_POINT reads back bit-identical to the model × unit; millimetre unit declared; metre model × 1000 exactly; stand-in deviation within tolerance", "bit-exact · ≤ 10⁻⁶" };
GateTally StepRefusalGate   = { "EX7", "STEP refusals name the cause: no bodies, zero / negative unit, non-positive tolerance, a stale body token, a sheet body", "named" };
GateTally StepTimingGate    = { "EX8", "STEP export time per body (stand-in fitting included)", "≤ 2 s" };

GateTally ImportStructureGate = { "EX9", "STEP import of every exported body (ReadStep → AssembleBody → AuditBody sound): faces, loops, edges, vertices, shells and solid regions = the original's; carrier kinds kept (stand-ins arrive as NURBS); every vertex bit-identical; exported again, read again, exported again → the same bytes (fixed point after one generation); two bodies in one file → two bodies; a metre model read in metres and in millimetres", "exact" };
GateTally ImportGeometryGate  = { "EX10", "STEP import geometry: |ΔV| and |ΔA| against the original's exact mass properties — ≤ 10⁻⁹ relative where every carrier was written exactly, ≤ 2 × stand-in deviation × area otherwise (tolerant edges absorb the stand-in gap, counted)", "≤ 10⁻⁹ rel · ≤ 2δA" };
GateTally ImportFixtureGate   = { "EX11", "OpenCascade-authored STEP (StepFixtures.py: fillet spheres, chamfer cones, torus with two seams, edge-less sphere, apex cone, B-spline loft, surface of revolution / linear extrusion, void, inch units, a boss fillet whose torus loop OpenCascade wrote backwards): read sound; solids, shells, faces, edges, vertices = OpenCascade's (degenerated edges excluded); |ΔV|, |ΔA| against GProp (ε 10⁻¹²; Gauss–Legendre of the exact profile where GProp's quadrature is off); OpenCascade's own triangulation points on the imported carriers", "≤ 10⁻⁶ rel · ≤ 10⁻⁶ mm" };
GateTally ImportRefusalGate   = { "EX12", "STEP import refusals name the cause and add nothing to the partition: empty input, no DATA, unterminated DATA, a missing instance, an unsupported surface, an open shell, no solid, a non-metre length unit, a zero model unit, a missing file, a two-solid file whose second solid is broken", "named · atomic" };
GateTally ImportTimingGate    = { "EX13", "STEP import time per file (parse, conversion, SP-curves, assembly, audit twice)", "≤ 2 s" };

std::vector<GateTally*> AllGates()
{
    return { &StlClosureGate,    &StlVolumeGate,    &StlFormatGate,       &StlRefusalGate,     &StepStructureGate, &StepGeometryGate, &StepRefusalGate,
             &StepTimingGate,    &ImportStructureGate, &ImportGeometryGate, &ImportFixtureGate, &ImportRefusalGate, &ImportTimingGate };
}

struct DetailRow
{
    std::string Gate            = {};
    std::string Name            = {};
    std::string Detail          = {};
    bool        PassedCondition = false;
};

std::vector<DetailRow> Details;
std::string            FileFolder;                                                           // --files: write the exports here
#ifdef FRONTIER_STEP_FIXTURES
std::string FixtureFolder = FRONTIER_STEP_FIXTURES;                                          // --fixtures: OpenCascade-authored STEP files
#else
std::string FixtureFolder;
#endif

void Report(GateTally& Gate, const std::string& Name, const std::string& Detail, double Value, bool PassedCondition)
{
    Gate.Worst = std::max(Gate.Worst, Value);
    ++Gate.Checks;
    if (!PassedCondition) ++Gate.Failures;
    Details.push_back({ Gate.Code, Name, Detail, PassedCondition });
    std::fprintf(stderr, "%s %s %s · %s\n", PassedCondition ? "🟢" : "🔴", Gate.Code, Name.c_str(), Detail.c_str());
}

template <typename... Values> std::string Format(const char* Pattern, Values... Arguments)
{
    char Text[512];
    std::snprintf(Text, sizeof(Text), Pattern, Arguments...);
    return Text;
}

void WriteFile(const std::string& Name, const std::string& Bytes)
{
    if (FileFolder.empty()) return;
    std::filesystem::create_directories(FileFolder);
    std::ofstream Stream(std::filesystem::path(FileFolder) / Name, std::ios::binary);
    Stream.write(Bytes.data(), static_cast<std::streamsize>(Bytes.size()));
}

// 📝 One test body: name, partition, token. Construction specimens carry hand-derived expectations; blend specimens are
//    blended first (BlendEdges) and checked against their own B-rep counts.
struct TestBody
{
    std::string    Name      = {};
    ModelPartition Partition = {};
    BodyToken      Body      = {};
};

std::vector<TestBody> ConstructTestBodies()
{
    std::vector<TestBody> Bodies;
    for (const char* Name : { "Block", "Cylinder", "Cone", "Sphere", "Torus", "PerforatedBlock", "HollowBlock", "PipeBend", "LoftFrustum", "SweptRing", "MirroredCross" })
    {
        TestBody& Entry = Bodies.emplace_back();
        Entry.Name = Name;
        const SpecimenOutcome Outcome = ConstructSpecimen(Entry.Partition, Name, {});
        if (!Outcome.Refusal.empty()) { std::fprintf(stderr, "specimen %s refused: %s\n", Name, Outcome.Refusal.c_str()); Bodies.pop_back(); continue; }
        Entry.Body = Outcome.Body;
    }
    for (const char* Name : { "BoxAllFillet", "BoxAllChamfer", "HoleRimFillet", "BossRootFillet", "VariableFillet", "SmoothFillet" })
    {
        TestBody& Entry = Bodies.emplace_back();
        Entry.Name = Name;
        const BlendSpecimen Specimen = ConstructBlendSpecimen(Entry.Partition, Name);
        const BlendOutcome  Blended  = BlendEdges(Entry.Partition, Specimen.Body, Specimen.Edges, Specimen.Settings);
        if (!Blended.Refusal.empty()) { std::fprintf(stderr, "blend %s refused: %s\n", Name, Blended.Refusal.c_str()); Bodies.pop_back(); continue; }
        Entry.Body = Blended.Body;
    }
    return Bodies;
}

// Euler–Poincaré on the B-rep: V − E + F − (L − F) = 2 (S − G) = χ of the boundary surface.
int BoundaryEulerCharacteristic(const ModelPartition& Partition, BodyToken Body)
{
    const BodyInventory Inventory = Partition.CollectInventory(Body);
    return static_cast<int>(Inventory.Vertices.size()) - static_cast<int>(Inventory.Edges.size()) + 2 * static_cast<int>(Inventory.Faces.size()) - static_cast<int>(Inventory.Loops.size());
}

bool SameReading(const StlReading& First, const StlReading& Second)
{
    if (First.Triangles != Second.Triangles || First.Positions.size() != Second.Positions.size()) return false;
    for (size_t Index = 0; Index < First.Positions.size(); ++Index)
        if (First.Positions[Index].X != Second.Positions[Index].X || First.Positions[Index].Y != Second.Positions[Index].Y || First.Positions[Index].Z != Second.Positions[Index].Z) return false;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    STL
//------------------------------------------------------------------------------------------------------------------------

void ValidateStl()
{
    std::vector<TestBody> Bodies = ConstructTestBodies();
    for (TestBody& Entry : Bodies)
    {
        const BodyInventory Inventory = Entry.Partition.CollectInventory(Entry.Body);
        StlRequest Request;
        Request.Name = Entry.Name;
        const StlExport Binary = WriteStl(Entry.Partition, { Entry.Body }, Request);
        if (!Binary.Refusal.empty()) { Report(StlClosureGate, Entry.Name, Binary.Refusal, 1.0, false); continue; }
        WriteFile(Entry.Name + ".stl", Binary.Bytes);
        const StlReading   Reading   = ReadStl(Binary.Bytes);
        const StlSoundness Soundness = MeasureStlSoundness(Reading);
        const int          Expected  = BoundaryEulerCharacteristic(Entry.Partition, Entry.Body);
        const int          Defects   = Soundness.OpenSides + Soundness.NonManifoldSides + Soundness.MisorientedSides + Soundness.DegenerateTriangles;
        Report(StlClosureGate, Entry.Name,
               Format("%d facets, %zu corners, χ %d = B-rep %d, %d component(s) / %zu shell(s), defects %d (open %d · non-manifold %d · misoriented %d · degenerate %d)",
                      Binary.Triangles, Reading.Positions.size(), Soundness.EulerCharacteristic, Expected, Soundness.Components, Inventory.Shells.size(), Defects,
                      Soundness.OpenSides, Soundness.NonManifoldSides, Soundness.MisorientedSides, Soundness.DegenerateTriangles),
               static_cast<double>(Defects), Reading.Refusal.empty() && Soundness.WatertightCondition && Defects == 0 && Soundness.EulerCharacteristic == Expected
               && static_cast<int>(Reading.Triangles.size()) == Binary.Triangles && Soundness.Components == static_cast<int>(Inventory.Shells.size()));

        const BodyMassProperties Mass  = MeasureBodyMass(Entry.Partition, Entry.Body);
        const double             Bound = Binary.Sag * Mass.SurfaceArea;
        const double             Error = std::fabs(Soundness.Volume - Mass.Volume);
        Report(StlVolumeGate, Entry.Name, Format("V facets %.9g vs exact %.9g: |ΔV| %.3e ≤ sag %.2e · A %.6g = %.3e (ratio %.3f)", Soundness.Volume, Mass.Volume, Error, Binary.Sag, Mass.SurfaceArea, Bound, Error / Bound),
               Error / Bound, Error <= Bound);

        Request.BinaryEnabled = false;
        const StlExport  Ascii        = WriteStl(Entry.Partition, { Entry.Body }, Request);
        const StlReading AsciiReading = ReadStl(Ascii.Bytes);
        double NormalError = 0.0;
        for (size_t Facet = 0; Facet < Reading.Triangles.size(); ++Facet)
        {
            const std::array<int, 3>& Triangle = Reading.Triangles[Facet];
            const Vector3 Normal = Cross(Reading.Positions[Triangle[1]] - Reading.Positions[Triangle[0]], Reading.Positions[Triangle[2]] - Reading.Positions[Triangle[0]]);
            const double  Size   = Length(Normal);
            if (Size > 0.0) NormalError = std::max(NormalError, Length(Normal * (1.0 / Size) - Reading.Normals[Facet]));
        }
        const bool Repeatable = WriteStl(Entry.Partition, { Entry.Body }, StlRequest{ true, 1.0, {}, Entry.Name }).Bytes == Binary.Bytes;
        Report(StlFormatGate, Entry.Name, Format("binary %zu B = 84 + 50 · %d, ASCII %zu B, same welded facets: %s, normals within %.1e, repeatable: %s", Binary.Bytes.size(), Binary.Triangles, Ascii.Bytes.size(),
                                                 SameReading(Reading, AsciiReading) ? "yes" : "no", NormalError, Repeatable ? "yes" : "no"),
               NormalError, Reading.BinaryCondition && !AsciiReading.BinaryCondition && Binary.Bytes.size() == 84 + 50 * static_cast<size_t>(Binary.Triangles)
               && Binary.Bytes.compare(0, 5, "solid") != 0 && SameReading(Reading, AsciiReading) && AsciiReading.Name == Entry.Name && NormalError <= 1.0e-6 && Repeatable);
    }

    // Sphere convergence: sag ÷ 4 → error ÷ ≥ 2.
    {
        TestBody Sphere;
        Sphere.Body = ConstructSpecimen(Sphere.Partition, "Sphere", {}).Body;
        const double Exact = MeasureBodyMass(Sphere.Partition, Sphere.Body).Volume;
        std::vector<double> Errors;
        std::string Trail;
        for (const double Ratio : { 4.0e-3, 1.0e-3, 2.5e-4 })
        {
            StlRequest Request;
            Request.Tessellation.SagRatio = Ratio;
            const StlExport Export = WriteStl(Sphere.Partition, { Sphere.Body }, Request);
            Errors.push_back(std::fabs(MeasureStlSoundness(ReadStl(Export.Bytes)).Volume - Exact));
            Trail += Format("%s%d facets |ΔV| %.3e", Trail.empty() ? "" : " → ", Export.Triangles, Errors.back());
        }
        const double Worse = std::max(Errors[1] / Errors[0], Errors[2] / Errors[1]);
        Report(StlVolumeGate, "Sphere convergence (sag ratio 4e-3 → 1e-3 → 2.5e-4)", Trail + Format(" · worst step ratio %.3f", Worse), Worse, Worse <= 0.5);
    }

    // Model units: millimetre output of a metre model.
    {
        TestBody Block;
        Block.Body = ConstructBlock(Block.Partition, { 0.0, 0.0, 0.0 }, { 4.0, 2.5, 1.0 }).Value;
        StlRequest Metres;
        Metres.ModelUnitMillimetres = 1000.0;
        const StlReading Unit  = ReadStl(WriteStl(Block.Partition, { Block.Body }, StlRequest{}).Bytes);
        const StlReading Scale = ReadStl(WriteStl(Block.Partition, { Block.Body }, Metres).Bytes);
        double Worst = Unit.Positions.size() == Scale.Positions.size() ? 0.0 : 1.0e300;
        for (size_t Index = 0; Worst < 1.0e300 && Index < Unit.Positions.size(); ++Index)
            Worst = std::max(Worst, Length(Scale.Positions[Index] - Unit.Positions[Index] * 1000.0) / 4000.0);
        Report(StlVolumeGate, "Block 4 × 2.5 × 1 m → mm", Format("V %.6g mm³ = 10¹⁰, corners × 1000 within %.1e relative", MeasureStlSoundness(Scale).Volume, Worst), Worst,
               Worst <= 6.0e-8 && std::fabs(MeasureStlSoundness(Scale).Volume - 1.0e10) <= 1.0e10 * 1.0e-6 && Unit.Triangles == Scale.Triangles);
    }
}

void ValidateStlRefusals()
{
    TestBody Block;
    Block.Body = ConstructBlock(Block.Partition, { 0.0, 0.0, 0.0 }, { 1.0, 1.0, 1.0 }).Value;
    const auto Refuses = [&](const std::string& Name, const std::string& Refusal, const char* Wanted)
    {
        Report(StlRefusalGate, Name, Refusal.empty() ? std::string("(accepted)") : Refusal, 0.0, Refusal.find(Wanted) != std::string::npos);
    };
    Refuses("no bodies", WriteStl(Block.Partition, {}, {}).Refusal, "no body");
    StlRequest Zero;
    Zero.ModelUnitMillimetres = 0.0;
    Refuses("zero unit", WriteStl(Block.Partition, { Block.Body }, Zero).Refusal, "positive length");
    Zero.ModelUnitMillimetres = -1.0;
    Refuses("negative unit", WriteStl(Block.Partition, { Block.Body }, Zero).Refusal, "positive length");
    Refuses("stale token", WriteStl(Block.Partition, { BodyToken{ 999, 1 } }, {}).Refusal, "not in the partition");
    const StlExport Binary = WriteStl(Block.Partition, { Block.Body }, {});
    Refuses("truncated binary", ReadStl(Binary.Bytes.substr(0, Binary.Bytes.size() - 7)).Refusal, "neither a binary layout");
    StlRequest AsciiRequest;
    AsciiRequest.BinaryEnabled = false;
    const std::string Ascii = WriteStl(Block.Partition, { Block.Body }, AsciiRequest).Bytes;
    Refuses("ASCII without endsolid", ReadStl(Ascii.substr(0, Ascii.rfind("endsolid"))).Refusal, "missing \"endsolid\"");
    std::string TwoVertices = Ascii;
    const size_t Third = TwoVertices.find("vertex", TwoVertices.find("vertex", TwoVertices.find("vertex") + 1) + 1);
    TwoVertices.erase(Third, TwoVertices.find('\n', Third) - Third + 1);
    Refuses("facet with two vertices", ReadStl(TwoVertices).Refusal, "three vertices");

    const StlReading Sound = ReadStl(Binary.Bytes);
    StlReading Open = Sound, Flipped = Sound, Repeated = Sound;
    Open.Triangles.pop_back();
    std::swap(Flipped.Triangles[0][1], Flipped.Triangles[0][2]);
    Repeated.Triangles.push_back(Repeated.Triangles[0]);
    const StlSoundness OpenMeasure = MeasureStlSoundness(Open), FlipMeasure = MeasureStlSoundness(Flipped), RepeatMeasure = MeasureStlSoundness(Repeated);
    Report(StlRefusalGate, "defects counted", Format("one facet removed → %d open; one flipped → %d misoriented; one repeated → %d non-manifold; none watertight",
                                                     OpenMeasure.OpenSides, FlipMeasure.MisorientedSides, RepeatMeasure.NonManifoldSides),
           0.0, OpenMeasure.OpenSides == 3 && FlipMeasure.MisorientedSides == 3 && RepeatMeasure.NonManifoldSides == 3 && !OpenMeasure.WatertightCondition
           && !FlipMeasure.WatertightCondition && !RepeatMeasure.WatertightCondition && MeasureStlSoundness(Sound).WatertightCondition);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    STEP
//------------------------------------------------------------------------------------------------------------------------

// 📝 Independent Part 21 reading: instances split at ';' outside strings, names, top-level arguments and references —
//    nothing shared with the writer.
struct PartInstance
{
    std::string Name      = {};                                                              // [-] - Leading keyword ("COMPLEX" for ( … ))
    std::string Body      = {};                                                              // [-] - Text after '='
};

struct PartFile
{
    std::string                   Schema    = {};                                            // [-] - FILE_SCHEMA text
    std::map<int, PartInstance>   Instances = {};                                            // [-] - By number
    int                           Repeats   = 0;                                             // [-] - Numbers defined twice
    int                           Dangling  = 0;                                             // [-] - References to undefined numbers
    std::string                   Refusal   = {};
};

PartFile ReadPartFile(const std::string& Text)
{
    PartFile Out;
    const size_t SchemaAt = Text.find("FILE_SCHEMA((");
    if (SchemaAt != std::string::npos) Out.Schema = Text.substr(SchemaAt + 14, Text.find('\'', SchemaAt + 14) - SchemaAt - 14);
    const size_t DataAt = Text.find("DATA;"), EndAt = Text.rfind("ENDSEC;");
    if (Text.rfind("ISO-10303-21;", 0) != 0 || DataAt == std::string::npos || EndAt == std::string::npos || EndAt < DataAt || Text.find("END-ISO-10303-21;") == std::string::npos)
    {
        Out.Refusal = "not an ISO-10303-21 exchange structure";
        return Out;
    }
    std::string Current;
    bool        Quoted = false;
    std::vector<int> References;
    for (size_t At = DataAt + 5; At < EndAt; ++At)
    {
        const char Letter = Text[At];
        if (Letter == '\'') Quoted = !Quoted;
        if (!Quoted && Letter == ';')
        {
            const size_t Hash = Current.find('#'), Equal = Current.find('=');
            if (Hash == std::string::npos || Equal == std::string::npos) { Out.Refusal = "an instance without #n="; return Out; }
            const int Number = std::stoi(Current.substr(Hash + 1, Equal - Hash - 1));
            PartInstance Instance;
            Instance.Body = Current.substr(Equal + 1);
            const size_t First = Instance.Body.find_first_not_of(' ');
            Instance.Name = Instance.Body[First] == '(' ? "COMPLEX" : Instance.Body.substr(First, Instance.Body.find('(') - First);
            for (size_t Scan = 0; Scan < Instance.Body.size(); ++Scan)
                if (Instance.Body[Scan] == '#') References.push_back(std::atoi(Instance.Body.c_str() + Scan + 1));
            if (Out.Instances.count(Number)) ++Out.Repeats;
            Out.Instances[Number] = Instance;
            Current.clear();
            continue;
        }
        if (Letter != '\n' && Letter != '\r') Current += Letter;
    }
    for (const int Reference : References)
        if (!Out.Instances.count(Reference)) ++Out.Dangling;
    return Out;
}

// Top-level arguments of NAME( … ) (commas at depth 1, strings respected).
std::vector<std::string> SplitArguments(const std::string& Body)
{
    std::vector<std::string> Out;
    int         Depth  = 0;
    bool        Quoted = false;
    std::string Current;
    for (const char Letter : Body)
    {
        if (Letter == '\'') Quoted = !Quoted;
        if (!Quoted && Letter == '(' && Depth++ == 0) continue;
        if (!Quoted && Letter == ')' && --Depth == 0) { Out.push_back(Current); break; }
        if (!Quoted && Letter == ',' && Depth == 1) { Out.push_back(Current); Current.clear(); continue; }
        if (Depth >= 1) Current += Letter;
    }
    return Out;
}

int ReferenceOf(const std::string& Argument) { return std::atoi(Argument.c_str() + Argument.find('#') + 1); }

std::vector<int> ReferenceList(const std::string& Argument)
{
    std::vector<int> Out;
    for (size_t At = Argument.find('#'); At != std::string::npos; At = Argument.find('#', At + 1)) Out.push_back(std::atoi(Argument.c_str() + At + 1));
    return Out;
}

int CountNamed(const PartFile& File, const std::string& Name)
{
    int Count = 0;
    for (const auto& [Number, Instance] : File.Instances) Count += Instance.Name == Name;
    return Count;
}

const char* ExpectedCarrier(const SurfaceGeometry& Surface)
{
    switch (Surface.index())
    {
        case 0:  return "PLANE";
        case 1:  return "CYLINDRICAL_SURFACE";
        case 2:  return "CONICAL_SURFACE";
        case 3:  return "SPHERICAL_SURFACE";
        case 4:  return "TOROIDAL_SURFACE";
        default: return "B_SPLINE_SURFACE";
    }
}

std::vector<TestBody> ConstructStepBodies()
{
    std::vector<TestBody> Bodies = ConstructTestBodies();
    // Procedural rolling-ball carriers (stand-ins) and NURBS carriers (exact, rational from promotion; cubic from approximation).
    for (const char* Name : { "TiltedBoreChamfer", "TiltedBossChamfer", "VariableArcFillet", "VariableArcChamfer" })
    {
        TestBody& Entry = Bodies.emplace_back();
        Entry.Name = Name;
        const BlendSpecimen Specimen = ConstructBlendSpecimen(Entry.Partition, Name);
        const BlendOutcome  Blended  = BlendEdges(Entry.Partition, Specimen.Body, Specimen.Edges, Specimen.Settings);
        if (!Blended.Refusal.empty()) { Bodies.pop_back(); continue; }
        Entry.Body = Blended.Body;
    }
    for (const char* Name : { "Torus", "Sphere", "Cylinder" })
    {
        TestBody& Entry = Bodies.emplace_back();
        Entry.Name = std::string("Promoted") + Name;
        const TransformOutcome Promoted = PromoteBodyToNurbs(Entry.Partition, ConstructSpecimen(Entry.Partition, Name, {}).Body);
        if (!Promoted.Refusal.empty()) { Bodies.pop_back(); continue; }
        Entry.Body = Promoted.Body;
    }
    {
        TestBody& Entry = Bodies.emplace_back();
        Entry.Name = "PromotedBoxAllFillet";
        const BlendSpecimen    Specimen = ConstructBlendSpecimen(Entry.Partition, "BoxAllFillet");
        const TransformOutcome Promoted = PromoteBodyToNurbs(Entry.Partition, BlendEdges(Entry.Partition, Specimen.Body, Specimen.Edges, Specimen.Settings).Body);
        if (Promoted.Refusal.empty()) Entry.Body = Promoted.Body; else Bodies.pop_back();
    }
    {
        TestBody& Entry = Bodies.emplace_back();
        Entry.Name = "ApproximatedHoleRimFillet";
        const BlendSpecimen        Specimen     = ConstructBlendSpecimen(Entry.Partition, "HoleRimFillet");
        const ApproximationOutcome Approximated = ApproximateBodyAsNurbs(Entry.Partition, BlendEdges(Entry.Partition, Specimen.Body, Specimen.Edges, Specimen.Settings).Body);
        if (Approximated.Transform.Refusal.empty()) Entry.Body = Approximated.Transform.Body; else Bodies.pop_back();
    }
    return Bodies;
}

// 📝 Points of the exact model (tessellation vertices lie on the exact carriers) for the OCCT distance check.
void WriteSamplePoints(const std::string& Name, const ModelPartition& Partition, BodyToken Body)
{
    if (FileFolder.empty()) return;
    const TessellationOutcome Tessellation = TessellateBody(Partition, Body, {});
    std::string Text;
    const size_t Count = Tessellation.Complex.Positions.size(), Stride = std::max<size_t>(1, Count / 240);
    for (size_t Index = 0; Index < Count; Index += Stride)
        Text += Format("%.17g %.17g %.17g\n", Tessellation.Complex.Positions[Index].X, Tessellation.Complex.Positions[Index].Y, Tessellation.Complex.Positions[Index].Z);
    WriteFile(Name + ".points.txt", Text);
}

void ValidateStep()
{
    std::vector<TestBody> Bodies = ConstructStepBodies();
    std::string Inventory;
    for (TestBody& Entry : Bodies)
    {
        StepRequest Request;
        Request.Name = Entry.Name;
        const StepExport Export = WriteStep(Entry.Partition, { Entry.Body }, Request);
        if (!Export.Refusal.empty()) { Report(StepStructureGate, Entry.Name, Export.Refusal, 1.0, false); continue; }
        WriteFile(Entry.Name + ".step", Export.Bytes);
        WriteSamplePoints(Entry.Name, Entry.Partition, Entry.Body);
        const BodyInventory Brep = Entry.Partition.CollectInventory(Entry.Body);
        PartFile            File = ReadPartFile(Export.Bytes);

        // ---- structure: counts and edge uses
        int SolidRegions = 0;
        for (const RegionToken Region : Brep.Regions) SolidRegions += Entry.Partition.Regions.Resolve(Region).Material == RegionMaterial::Solid;
        std::map<int, std::pair<int, int>> Uses;                                             // EDGE_CURVE → (forward, reverse)
        int Disconnected = 0;
        std::vector<int> FaceCarriers;
        for (const auto& [Number, Instance] : File.Instances)
        {
            if (Instance.Name != "ADVANCED_FACE") continue;
            const std::vector<std::string> Face = SplitArguments(Instance.Body);
            FaceCarriers.push_back(ReferenceOf(Face[2]));
            for (const int Bound : ReferenceList(Face[1]))
            {
                const std::vector<std::string> BoundArguments = SplitArguments(File.Instances[Bound].Body);
                const bool BoundForward = BoundArguments[2].find(".T.") != std::string::npos;
                const std::vector<int> OrientedEdges = ReferenceList(SplitArguments(File.Instances[ReferenceOf(BoundArguments[1])].Body)[1]);
                std::vector<std::pair<int, int>> Ends;
                for (const int OrientedEdge : OrientedEdges)
                {
                    const std::vector<std::string> Use = SplitArguments(File.Instances[OrientedEdge].Body);
                    const int  Curve   = ReferenceOf(Use[3]);
                    const bool Forward = (Use[4].find(".T.") != std::string::npos) == BoundForward;
                    (Forward ? Uses[Curve].first : Uses[Curve].second) += 1;
                    const std::vector<std::string> EdgeCurve = SplitArguments(File.Instances[Curve].Body);
                    const int StartVertex = ReferenceOf(EdgeCurve[1]), EndVertex = ReferenceOf(EdgeCurve[2]);
                    const bool UseForward = Use[4].find(".T.") != std::string::npos;
                    Ends.push_back(UseForward ? std::pair{ StartVertex, EndVertex } : std::pair{ EndVertex, StartVertex });
                }
                for (size_t Index = 0; Index < Ends.size(); ++Index) Disconnected += Ends[Index].second != Ends[(Index + 1) % Ends.size()].first;
            }
        }
        int BadUses = 0;
        for (const auto& [Curve, Count] : Uses) BadUses += Count.first != 1 || Count.second != 1;
        const bool Repeatable = WriteStep(Entry.Partition, { Entry.Body }, Request).Bytes == Export.Bytes;
        const int  Solids     = CountNamed(File, "MANIFOLD_SOLID_BREP") + CountNamed(File, "BREP_WITH_VOIDS");
        Report(StepStructureGate, Entry.Name,
               Format("%d instances, %d dangling, %d repeated; faces %d/%zu, edges %d/%zu, vertices %d/%zu, shells %d/%zu, solids %d/%d; edge uses not (1 forward + 1 reverse): %d; loop breaks %d; repeatable: %s",
                      static_cast<int>(File.Instances.size()), File.Dangling, File.Repeats, CountNamed(File, "ADVANCED_FACE"), Brep.Faces.size(), CountNamed(File, "EDGE_CURVE"), Brep.Edges.size(),
                      CountNamed(File, "VERTEX_POINT"), Brep.Vertices.size(), CountNamed(File, "CLOSED_SHELL"), Brep.Shells.size(), Solids, SolidRegions, BadUses, Disconnected, Repeatable ? "yes" : "no"),
               static_cast<double>(BadUses + Disconnected + File.Dangling + File.Repeats),
               File.Refusal.empty() && File.Schema == "AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }" && File.Dangling == 0 && File.Repeats == 0
               && CountNamed(File, "ADVANCED_FACE") == static_cast<int>(Brep.Faces.size()) && CountNamed(File, "EDGE_CURVE") == static_cast<int>(Brep.Edges.size())
               && CountNamed(File, "VERTEX_POINT") == static_cast<int>(Brep.Vertices.size()) && CountNamed(File, "CLOSED_SHELL") == static_cast<int>(Brep.Shells.size())
               && Solids == SolidRegions && BadUses == 0 && Disconnected == 0 && static_cast<int>(Uses.size()) == static_cast<int>(Brep.Edges.size()) && Repeatable && File.Instances.size() == static_cast<size_t>(Export.Instances));

        // ---- geometry: carrier kinds in writing order (solid regions → shells → faces), vertices bit-exact
        std::vector<const SurfaceGeometry*> Carriers;
        for (const RegionToken Region : Brep.Regions)
            if (Entry.Partition.Regions.Resolve(Region).Material == RegionMaterial::Solid)
                for (const SolidShellToken Shell : Entry.Partition.Regions.Resolve(Region).Shells)
                    for (const FaceToken Face : Entry.Partition.Shells.Resolve(Shell).Faces) Carriers.push_back(&Entry.Partition.Surfaces.Resolve(Entry.Partition.Faces.Resolve(Face).Surface));
        int WrongCarriers = 0, Rational = 0;
        std::map<std::string, int> Kinds;
        for (size_t Index = 0; Index < FaceCarriers.size() && Index < Carriers.size(); ++Index)
        {
            const PartInstance& Carrier = File.Instances[FaceCarriers[Index]];
            const std::string   Kind    = Carrier.Name == "COMPLEX" && Carrier.Body.find("B_SPLINE_SURFACE") != std::string::npos ? "B_SPLINE_SURFACE"
                                          : Carrier.Name == "B_SPLINE_SURFACE_WITH_KNOTS" ? "B_SPLINE_SURFACE" : Carrier.Name;
            Rational += Carrier.Body.find("RATIONAL_B_SPLINE_SURFACE") != std::string::npos;
            ++Kinds[Kind];
            WrongCarriers += Kind != ExpectedCarrier(*Carriers[Index]);
        }
        std::multiset<std::array<double, 3>> Expected, Found;
        for (const VertexToken Vertex : Brep.Vertices)
        {
            const Vector3 Position = Entry.Partition.Vertices.Resolve(Vertex).Position;
            Expected.insert({ Position.X, Position.Y, Position.Z });
        }
        for (const auto& [Number, Instance] : File.Instances)
        {
            if (Instance.Name != "VERTEX_POINT") continue;
            const std::vector<std::string> Point = SplitArguments(SplitArguments(File.Instances[ReferenceOf(SplitArguments(Instance.Body)[1])].Body)[1]);
            Found.insert({ std::strtod(Point[0].c_str(), nullptr), std::strtod(Point[1].c_str(), nullptr), std::strtod(Point[2].c_str(), nullptr) });
        }
        const BodyMassProperties Mass = MeasureBodyMass(Entry.Partition, Entry.Body);
        std::string KindText;
        for (const auto& [Kind, Count] : Kinds) KindText += Format("%s%s %d", KindText.empty() ? "" : ", ", Kind.c_str(), Count);
        Report(StepGeometryGate, Entry.Name,
               Format("%s (%d rational); wrong carrier kinds %d; vertices bit-identical: %s; stand-ins %d surfaces / %d curves, deviation %.2e; exact V %.12g, A %.12g", KindText.c_str(), Rational, WrongCarriers,
                      Expected == Found ? "yes" : "no", Export.ApproximatedSurfaces, Export.ApproximatedCurves, Export.Deviation, Mass.Volume, Mass.SurfaceArea),
               Export.Deviation, WrongCarriers == 0 && FaceCarriers.size() == Carriers.size() && Expected == Found && Export.Deviation <= Request.ApproximationTolerance);
        Report(StepTimingGate, Entry.Name, Format("%.1f ms, %d instances, %zu bytes", Export.Milliseconds, Export.Instances, Export.Bytes.size()), Export.Milliseconds / 1000.0, Export.Milliseconds <= 2000.0);
    }

    // Units: a metre model in millimetres.
    {
        TestBody Block;
        Block.Body = ConstructBlock(Block.Partition, { 0.25, 0.5, 0.125 }, { 4.0, 2.5, 1.0 }).Value;
        StepRequest Metres;
        Metres.ModelUnitMillimetres = 1000.0;
        PartFile File = ReadPartFile(WriteStep(Block.Partition, { Block.Body }, Metres).Bytes);
        std::multiset<std::array<double, 3>> Expected, Found;
        for (const VertexToken Vertex : Block.Partition.CollectInventory(Block.Body).Vertices)
        {
            const Vector3 Position = Block.Partition.Vertices.Resolve(Vertex).Position;
            Expected.insert({ Position.X * 1000.0, Position.Y * 1000.0, Position.Z * 1000.0 });
        }
        bool Millimetre = false;
        for (const auto& [Number, Instance] : File.Instances)
        {
            Millimetre = Millimetre || (Instance.Name == "COMPLEX" && Instance.Body.find("LENGTH_UNIT()") != std::string::npos && Instance.Body.find("SI_UNIT(.MILLI.,.METRE.)") != std::string::npos);
            if (Instance.Name != "VERTEX_POINT") continue;
            const std::vector<std::string> Point = SplitArguments(SplitArguments(File.Instances[ReferenceOf(SplitArguments(Instance.Body)[1])].Body)[1]);
            Found.insert({ std::strtod(Point[0].c_str(), nullptr), std::strtod(Point[1].c_str(), nullptr), std::strtod(Point[2].c_str(), nullptr) });
        }
        Report(StepGeometryGate, "Block 4 × 2.5 × 1 m at (0.25, 0.5, 0.125) → mm", Format("length unit SI_UNIT(.MILLI.,.METRE.): %s; 8 corners = model × 1000 bit-identical: %s", Millimetre ? "yes" : "no", Expected == Found ? "yes" : "no"),
               0.0, Millimetre && Expected == Found);
    }
}

void ValidateStepRefusals()
{
    TestBody Block;
    Block.Body = ConstructBlock(Block.Partition, { 0.0, 0.0, 0.0 }, { 1.0, 1.0, 1.0 }).Value;
    const auto Refuses = [&](const std::string& Name, const std::string& Refusal, const char* Wanted)
    {
        Report(StepRefusalGate, Name, Refusal.empty() ? std::string("(accepted)") : Refusal, 0.0, Refusal.find(Wanted) != std::string::npos);
    };
    Refuses("no bodies", WriteStep(Block.Partition, {}, {}).Refusal, "no body");
    StepRequest Bad;
    Bad.ModelUnitMillimetres = 0.0;
    Refuses("zero unit", WriteStep(Block.Partition, { Block.Body }, Bad).Refusal, "positive length");
    Bad.ModelUnitMillimetres = -25.4;
    Refuses("negative unit", WriteStep(Block.Partition, { Block.Body }, Bad).Refusal, "positive length");
    Bad = {};
    Bad.ApproximationTolerance = 0.0;
    Refuses("zero tolerance", WriteStep(Block.Partition, { Block.Body }, Bad).Refusal, "tolerance must be positive");
    Refuses("stale token", WriteStep(Block.Partition, { BodyToken{ 999, 1 } }, {}).Refusal, "not in the partition");
    Block.Partition.Bodies.Resolve(Block.Body).Category = BodyCategory::Sheet;
    Refuses("sheet body", WriteStep(Block.Partition, { Block.Body }, {}).Refusal, "only solid bodies");
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    STEP IMPORT
//------------------------------------------------------------------------------------------------------------------------

// Carrier kind as it must arrive: analytic and NURBS kept, procedural carriers as their B-spline stand-ins.
size_t ArrivingKind(const SurfaceGeometry& Surface)
{
    const size_t Kind = Surface.index();
    return Kind <= 4 || Kind == 6 ? Kind : 6;
}

std::multiset<std::array<double, 3>> VertexSet(const ModelPartition& Partition, BodyToken Body, double Scale = 1.0)
{
    std::multiset<std::array<double, 3>> Out;
    for (const VertexToken Vertex : Partition.CollectInventory(Body).Vertices)
    {
        const Vector3 Position = Partition.Vertices.Resolve(Vertex).Position;
        Out.insert({ Position.X * Scale, Position.Y * Scale, Position.Z * Scale });
    }
    return Out;
}

std::array<int, 6> TopologyCounts(const ModelPartition& Partition, BodyToken Body)
{
    const BodyInventory Inventory = Partition.CollectInventory(Body);
    int SolidRegions = 0;
    for (const RegionToken Region : Inventory.Regions) SolidRegions += Partition.Regions.Resolve(Region).Material == RegionMaterial::Solid;
    return { static_cast<int>(Inventory.Faces.size()), static_cast<int>(Inventory.Loops.size()), static_cast<int>(Inventory.Edges.size()),
             static_cast<int>(Inventory.Vertices.size()), static_cast<int>(Inventory.Shells.size()), SolidRegions };
}

std::string CountText(const std::array<int, 6>& Counts)
{
    return Format("F %d L %d E %d V %d shells %d solids %d", Counts[0], Counts[1], Counts[2], Counts[3], Counts[4], Counts[5]);
}

void WriteImportedStl(const std::string& Name, const ModelPartition& Partition, BodyToken Body)
{
    if (FileFolder.empty()) return;
    StlRequest Request;
    const StlExport Export = WriteStl(Partition, { Body }, Request);
    if (Export.Refusal.empty()) WriteFile(Name + ".stl", Export.Bytes);
}

void ValidateStepImport()
{
    std::vector<TestBody> Bodies = ConstructStepBodies();
    for (TestBody& Entry : Bodies)
    {
        StepRequest Request;
        Request.Name = Entry.Name;
        const StepExport Export = WriteStep(Entry.Partition, { Entry.Body }, Request);
        if (!Export.Refusal.empty()) { Report(ImportStructureGate, Entry.Name, Export.Refusal, 1.0, false); continue; }
        ModelPartition   Imported;
        const StepImport Reading = ReadStep(Imported, Export.Bytes, {});
        Report(ImportTimingGate, Entry.Name, Format("%.1f ms, %d instances", Reading.Milliseconds, Reading.Instances), Reading.Milliseconds / 1000.0, Reading.Refusal.empty() && Reading.Milliseconds <= 2000.0);
        if (!Reading.Refusal.empty() || Reading.Bodies.size() != 1) { Report(ImportStructureGate, Entry.Name, Reading.Refusal.empty() ? std::string("not one body") : Reading.Refusal, 1.0, false); continue; }
        const BodyToken Body = Reading.Bodies.front();

        // ---- structure
        const std::array<int, 6> Original = TopologyCounts(Entry.Partition, Entry.Body), Arrived = TopologyCounts(Imported, Body);
        std::multiset<size_t> ExpectedKinds, FoundKinds;
        for (const FaceToken Face : Entry.Partition.CollectInventory(Entry.Body).Faces) ExpectedKinds.insert(ArrivingKind(Entry.Partition.Surfaces.Resolve(Entry.Partition.Faces.Resolve(Face).Surface)));
        for (const FaceToken Face : Imported.CollectInventory(Body).Faces) FoundKinds.insert(Imported.Surfaces.Resolve(Imported.Faces.Resolve(Face).Surface).index());
        const bool SameVertices = VertexSet(Entry.Partition, Entry.Body) == VertexSet(Imported, Body);
        const StepExport Second = WriteStep(Imported, { Body }, Request);
        ModelPartition   Again;
        const StepImport SecondReading = ReadStep(Again, Second.Bytes, {});
        const StepExport Third = SecondReading.Refusal.empty() ? WriteStep(Again, SecondReading.Bodies, Request) : StepExport{};
        const bool FixedPoint = Second.Refusal.empty() && Third.Refusal.empty() && Second.Bytes == Third.Bytes;
        Report(ImportStructureGate, Entry.Name,
               Format("%s; kinds kept: %s; vertices bit-identical: %s; tolerant edges %d / vertices %d (largest gap %.1e); generation 2 = 3: %s", CountText(Arrived).c_str(), ExpectedKinds == FoundKinds ? "yes" : "no",
                      SameVertices ? "yes" : "no", Reading.TolerantEdges, Reading.TolerantVertices, Reading.LargestGap, FixedPoint ? "yes" : SecondReading.Refusal.empty() ? "no" : SecondReading.Refusal.c_str()),
               0.0, Original == Arrived && ExpectedKinds == FoundKinds && SameVertices && FixedPoint);

        // ---- geometry
        const BodyMassProperties Exact = MeasureBodyMass(Entry.Partition, Entry.Body), Read = MeasureBodyMass(Imported, Body);
        const bool   ExactCondition = Export.ApproximatedSurfaces == 0 && Export.ApproximatedCurves == 0;
        const double VolumeError = std::fabs(Read.Volume - Exact.Volume), AreaError = std::fabs(Read.SurfaceArea - Exact.SurfaceArea);
        const double VolumeLimit = ExactCondition ? 1.0e-9 * std::fabs(Exact.Volume) : 2.0 * Export.Deviation * Exact.SurfaceArea + 1.0e-9 * std::fabs(Exact.Volume);
        const double AreaLimit   = ExactCondition ? 1.0e-9 * Exact.SurfaceArea : 2.0 * Export.Deviation * Exact.SurfaceArea + 1.0e-9 * Exact.SurfaceArea;
        Report(ImportGeometryGate, Entry.Name,
               Format("%s; ΔV %.2e (limit %.2e), ΔA %.2e (limit %.2e); V %.12g", ExactCondition ? "exact carriers" : "stand-ins", VolumeError, VolumeLimit, AreaError, AreaLimit, Read.Volume),
               VolumeError / std::max(1e-300, std::fabs(Exact.Volume)), VolumeError <= VolumeLimit && AreaError <= AreaLimit);
    }

    // Two bodies in one file.
    {
        ModelPartition Partition;
        const BodyToken First  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 2.0, 1.0, 1.0 }).Value;
        const BodyToken Second = ConstructSpecimen(Partition, "Torus", {}).Body;
        ModelPartition   Imported;
        const StepImport Reading = ReadStep(Imported, WriteStep(Partition, { First, Second }, {}).Bytes, {});
        const bool Same = Reading.Refusal.empty() && Reading.Bodies.size() == 2 && VertexSet(Partition, First) == VertexSet(Imported, Reading.Bodies[0])
                          && VertexSet(Partition, Second) == VertexSet(Imported, Reading.Bodies[1]);
        Report(ImportStructureGate, "Block + Torus in one file", Format("%zu bodies, %d solids; vertices bit-identical in order: %s", Reading.Bodies.size(), Reading.Solids, Same ? "yes" : "no"), 0.0, Same);
    }

    // Units: a metre model, read in metres (round trip within rounding) and in millimetres (× 1000).
    {
        ModelPartition  Partition;
        const BodyToken Block = ConstructBlock(Partition, { 0.25, 0.5, 0.125 }, { 4.0, 2.5, 1.0 }).Value;
        StepRequest Metres;
        Metres.ModelUnitMillimetres = 1000.0;
        const std::string Bytes = WriteStep(Partition, { Block }, Metres).Bytes;
        ModelPartition    InMetres, InMillimetres;
        StepImportRequest MetreRequest;
        MetreRequest.ModelUnitMillimetres = 1000.0;
        const StepImport MetreReading = ReadStep(InMetres, Bytes, MetreRequest);
        const StepImport MillimetreReading = ReadStep(InMillimetres, Bytes, {});
        double Worst = 0.0;
        bool   Readable = MetreReading.Refusal.empty() && MillimetreReading.Refusal.empty();
        if (Readable)
        {
            const auto Expected = VertexSet(Partition, Block), Metric = VertexSet(InMetres, MetreReading.Bodies[0]), Scaled = VertexSet(InMillimetres, MillimetreReading.Bodies[0], 1.0e-3);
            for (auto Want = Expected.begin(), Got = Metric.begin(), Milli = Scaled.begin(); Want != Expected.end() && Got != Metric.end() && Milli != Scaled.end(); ++Want, ++Got, ++Milli)
                for (int Axis = 0; Axis < 3; ++Axis) Worst = std::max({ Worst, std::fabs((*Want)[Axis] - (*Got)[Axis]), std::fabs((*Want)[Axis] - (*Milli)[Axis]) });
            Readable = Expected.size() == Metric.size() && Expected.size() == Scaled.size();
        }
        Report(ImportStructureGate, "Block in metres → mm file → metres and mm", Format("file unit %.0f mm; worst corner error %.1e m (both readings)", MetreReading.FileUnitMillimetres, Worst), Worst, Readable && Worst <= 1.0e-15);
    }
}

void ValidateStepFixtures()
{
    std::ifstream Expectations(std::filesystem::path(FixtureFolder) / "Expectations.txt");
    if (!Expectations) { Report(ImportFixtureGate, "fixtures", "no Expectations.txt in " + FixtureFolder, 1.0, false); return; }
    std::string Line;
    while (std::getline(Expectations, Line))
    {
        if (Line.empty() || Line[0] == '#') continue;
        std::istringstream Fields(Line);
        std::string Name, Unit;
        double Volume = 0.0, Area = 0.0;
        std::array<int, 5> Expected{};
        Fields >> Name >> Unit >> Volume >> Area >> Expected[0] >> Expected[1] >> Expected[2] >> Expected[3] >> Expected[4];
        ModelPartition   Partition;
        const StepImport Reading = ImportStepFile(Partition, (std::filesystem::path(FixtureFolder) / (Name + ".step")).string(), {});
        Report(ImportTimingGate, Name + " (OpenCascade)", Format("%.1f ms, %d instances", Reading.Milliseconds, Reading.Instances), Reading.Milliseconds / 1000.0, Reading.Refusal.empty() && Reading.Milliseconds <= 2000.0);
        if (!Reading.Refusal.empty() || Reading.Bodies.empty()) { Report(ImportFixtureGate, Name, Reading.Refusal.empty() ? std::string("no body") : Reading.Refusal, 1.0, false); continue; }
        std::array<int, 5> Found{};
        double FoundVolume = 0.0, FoundArea = 0.0, PointGap = 0.0;
        int    Points = 0;
        for (const BodyToken Body : Reading.Bodies)
        {
            const std::array<int, 6> Counts = TopologyCounts(Partition, Body);
            Found[0] += Counts[5];
            Found[1] += Counts[4];
            Found[2] += Counts[0];
            Found[3] += Counts[2];
            Found[4] += Counts[3];
            const BodyMassProperties Mass = MeasureBodyMass(Partition, Body);
            FoundVolume += Mass.Volume;
            FoundArea   += Mass.SurfaceArea;
        }
        // OpenCascade triangulation nodes (on its exact surfaces): distance to the nearest imported carrier.
        std::ifstream PointStream(std::filesystem::path(FixtureFolder) / (Name + ".points.txt"));
        double X = 0.0, Y = 0.0, Z = 0.0;
        while (PointStream >> X >> Y >> Z)
        {
            double Nearest = 1e300;
            for (const BodyToken Body : Reading.Bodies)
                for (const FaceToken Face : Partition.CollectInventory(Body).Faces)
                {
                    const SurfaceGeometry& Carrier = Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface);
                    const Vector3 Point{ X, Y, Z };
                    Nearest = std::min(Nearest, Length(EvaluateSurface(Carrier, ProjectOntoSurface(Carrier, Point, {})).Position - Point));
                }
            PointGap = std::max(PointGap, Nearest);
            ++Points;
        }
        const double VolumeError = std::fabs(FoundVolume - Volume) / Volume, AreaError = std::fabs(FoundArea - Area) / Area;
        Report(ImportFixtureGate, Name,
               Format("%s file: solids %d shells %d F %d E %d V %d (OpenCascade %d %d %d %d %d); ΔV/V %.1e, ΔA/A %.1e; %d points, worst %.1e mm; seams added %d, degenerate edges skipped %d, converted surfaces %d, reoriented faces %d, tolerant edges %d (largest gap %.1e mm)",
                      Unit.c_str(), Found[0], Found[1], Found[2], Found[3], Found[4], Expected[0], Expected[1], Expected[2], Expected[3], Expected[4], VolumeError, AreaError, Points, PointGap, Reading.SynthesizedSeams,
                      Reading.DegenerateEdges, Reading.ConvertedSurfaces, Reading.ReorientedFaces, Reading.TolerantEdges, Reading.LargestGap),
               std::max(VolumeError, AreaError), Found == Expected && VolumeError <= 1.0e-6 && AreaError <= 1.0e-6 && Points > 0 && PointGap <= 1.0e-6);
        // Evidence: our own tessellation of what was read (gallery) and our STEP of it (OpenCascade reads it back).
        WriteImportedStl(Name + ".imported", Partition, Reading.Bodies.front());
        StepRequest Request;
        Request.Name = Name;
        const StepExport Export = WriteStep(Partition, Reading.Bodies, Request);
        if (Export.Refusal.empty()) WriteFile(Name + ".reexported.step", Export.Bytes);
    }
}

void ValidateStepImportRefusals()
{
    ModelPartition  Source;
    const BodyToken Block = ConstructBlock(Source, { 0.0, 0.0, 0.0 }, { 1.0, 1.0, 1.0 }).Value;
    const std::string Bytes = WriteStep(Source, { Block }, {}).Bytes;
    const auto Refuses = [&](const std::string& Name, const std::string& Input, const char* Wanted, const StepImportRequest& Request = {})
    {
        ModelPartition   Partition;
        const StepImport Reading = ReadStep(Partition, Input, Request);
        const bool Untouched = Partition.Bodies.OccupiedCount() == 0 && Reading.Bodies.empty();
        Report(ImportRefusalGate, Name, (Reading.Refusal.empty() ? std::string("(accepted)") : Reading.Refusal) + (Untouched ? "; partition untouched" : "; partition CHANGED"), 0.0,
               Reading.Refusal.find(Wanted) != std::string::npos && Untouched);
    };
    const auto Replace = [](std::string Text, const std::string& From, const std::string& To)
    {
        const size_t At = Text.find(From);
        if (At != std::string::npos) Text.replace(At, From.size(), To);
        return Text;
    };
    Refuses("empty input", "", "not an ISO 10303-21");
    Refuses("no DATA section", "ISO-10303-21;\nHEADER;\nENDSEC;\nEND-ISO-10303-21;\n", "no DATA section");
    Refuses("unterminated DATA", Bytes.substr(0, Bytes.find("ENDSEC;\nEND-ISO")), "not closed by ENDSEC");
    {
        // Drop the definition of the first VERTEX_POINT's CARTESIAN_POINT.
        const size_t Vertex = Bytes.find("VERTEX_POINT('',#");
        const std::string Point = "\n#" + std::to_string(std::atoi(Bytes.c_str() + Vertex + 17)) + "=";
        const size_t Line = Bytes.find(Point);
        Refuses("missing instance", Bytes.substr(0, Line + 1) + Bytes.substr(Bytes.find('\n', Line + 1) + 1), "missing instance");
    }
    Refuses("unsupported surface", Replace(Bytes, "=PLANE(", "=OFFSET_SURFACE("), "unsupported face surface OFFSET_SURFACE");
    {
        // One face removed from the closed shell list: the edges around it are used once.
        const size_t Shell = Bytes.find("CLOSED_SHELL('',(#");
        const size_t Comma = Bytes.find(',', Shell + 17);
        Refuses("open shell", Bytes.substr(0, Shell + 17) + Bytes.substr(Comma + 1), "non-manifold or open");
    }
    Refuses("no solid", Replace(Bytes, "MANIFOLD_SOLID_BREP(", "MANIFOLD_SOLID_BREX("), "holds no MANIFOLD_SOLID_BREP");
    Refuses("gram length unit", Replace(Bytes, "SI_UNIT(.MILLI.,.METRE.)", "SI_UNIT(.MILLI.,.GRAM.)"), "not based on the metre");
    StepImportRequest Zero;
    Zero.ModelUnitMillimetres = 0.0;
    Refuses("zero model unit", Bytes, "positive length", Zero);
    {
        ModelPartition   Partition;
        const StepImport Reading = ImportStepFile(Partition, "/nonexistent/Frontier.step", {});
        Report(ImportRefusalGate, "missing file", Reading.Refusal, 0.0, Reading.Refusal.find("cannot open") != std::string::npos && Partition.Bodies.OccupiedCount() == 0);
    }
    {
        // Two solids, the second broken: neither may arrive.
        ModelPartition  Pair;
        const BodyToken First  = ConstructBlock(Pair, { 0.0, 0.0, 0.0 }, { 1.0, 1.0, 1.0 }).Value;
        const BodyToken Second = ConstructBlock(Pair, { 3.0, 0.0, 0.0 }, { 1.0, 1.0, 1.0 }).Value;
        const std::string Both = WriteStep(Pair, { First, Second }, {}).Bytes;
        const size_t Shell = Both.find("CLOSED_SHELL('',(#", Both.find("CLOSED_SHELL('',(#") + 1);
        const size_t Comma = Both.find(',', Shell + 17);
        Refuses("second of two solids open", Both.substr(0, Shell + 17) + Both.substr(Comma + 1), "solid 2");
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int Count, char** Arguments)
{
    std::string Only;
    for (int Index = 1; Index + 1 < Count; ++Index)
    {
        if (std::string(Arguments[Index]) == "--only") Only = Arguments[Index + 1];
        if (std::string(Arguments[Index]) == "--files") FileFolder = Arguments[Index + 1];
        if (std::string(Arguments[Index]) == "--fixtures") FixtureFolder = Arguments[Index + 1];
    }
    const auto Start = std::chrono::steady_clock::now();
    if (Only.empty() || Only == "stl") ValidateStl();
    if (Only.empty() || Only == "stlrefusal") ValidateStlRefusals();
    if (Only.empty() || Only == "step") ValidateStep();
    if (Only.empty() || Only == "steprefusal") ValidateStepRefusals();
    if (Only.empty() || Only == "import") ValidateStepImport();
    if (Only.empty() || Only == "fixtures") ValidateStepFixtures();
    if (Only.empty() || Only == "importrefusal") ValidateStepImportRefusals();
    const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();

    bool PassedCondition = true;
    for (const GateTally* Gate : AllGates()) PassedCondition = PassedCondition && Gate->Failures == 0 && (Gate->Checks > 0 || !Only.empty());
    std::printf("# Exchange validation\n\n%s Priority 4: STL export and read-back; STEP AP214 export and import (%.2f s)\n\n", PassedCondition ? "🟢" : "🔴", Seconds);
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : AllGates())
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    std::printf("\n## Checks\n\n| | Gate | Check | Detail |\n|---|---|---|---|\n");
    const auto Cell = [](std::string Text)
    {
        for (size_t At = Text.find('|'); At != std::string::npos; At = Text.find('|', At + 2)) Text.replace(At, 1, "\\|");
        return Text;
    };
    for (const DetailRow& Row : Details) std::printf("| %s | %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Gate.c_str(), Cell(Row.Name).c_str(), Cell(Row.Detail).c_str());
    return PassedCondition ? 0 : 1;
}
