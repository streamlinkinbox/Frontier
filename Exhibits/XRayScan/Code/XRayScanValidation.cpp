/*==========================================================================================================================================
                                                        XRAYSCANVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 The C++ X-ray scan's CPU modules against the HTML prototype's own: the specification, the colour conversions, the clock over a 1240 step
//    script, the layer profile and view records, the occupant masks, the 130 sparkles, the feature edges of 18 shapes and the edges of the
//    hero car (the scene the engine's illumination port loads).
//
//     XRayScanValidation <repository root>
//
// 📝 The reference is Exhibits/XRayScan/Results/ScanReference.toml (ExportScanReference.mjs). Values that pass through pow, exp, sin or cos
//    (V8's fdlibm against the C library) may differ by one unit in the last place; they are compared to 1e-12 and report their exact share.
//    Float payloads, the sparkles and the edges must match bit for bit.

#include "Frontier/Engine/Rendering/GlobalIllumination/SceneStructure.h"
#include "Frontier/Engine/Rendering/XRayScan/XRayScanEdges.h"
#include "Frontier/Engine/Rendering/XRayScan/XRayScanProfile.h"
#include "Frontier/Engine/Rendering/XRayScan/XRayScanSequence.h"
#include "Frontier/Engine/Rendering/XRayScan/XRayScanSparkles.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace Frontier;
using namespace Frontier::XRayScan;

namespace
{

int Failures   = 0;
int Assertions = 0;

void Expect(bool Condition, const std::string& Claim)
{
    ++Assertions;
    if (!Condition)
    {
        ++Failures;
        std::printf("  🔴 %s\n", Claim.c_str());
    }
}

template <typename ReadingView>
std::vector<double> ReadNumbers(const ReadingView& View)
{
    std::vector<double> Values;
    if (const toml::array* Array = View.as_array())
    {
        for (const toml::node& Entry : *Array)
        {
            Values.push_back(Entry.template value<double>().value_or(std::nan("")));
        }
    }
    return Values;
}

template <typename ReadingView>
std::vector<int64_t> ReadIntegers(const ReadingView& View)
{
    std::vector<int64_t> Values;
    if (const toml::array* Array = View.as_array())
    {
        for (const toml::node& Entry : *Array)
        {
            Values.push_back(Entry.template value<int64_t>().value_or(-1));
        }
    }
    return Values;
}

template <typename ReadingView>
std::vector<bool> ReadFlags(const ReadingView& View)
{
    std::vector<bool> Values;
    if (const toml::array* Array = View.as_array())
    {
        for (const toml::node& Entry : *Array)
        {
            Values.push_back(Entry.template value<bool>().value_or(false));
        }
    }
    return Values;
}

double Number(const toml::node_view<const toml::node>& View) { return View.template value<double>().value_or(std::nan("")); }

bool Near(double Left, double Right, double Tolerance = 1.0e-12)
{
    return std::abs(Left - Right) <= Tolerance * std::max(1.0, std::max(std::abs(Left), std::abs(Right)));
}

// 📝 Distance between two floats in units in the last place.
int64_t UlpDistance(float Left, float Right)
{
    auto Order = [](float Value) {
        const int32_t Bits = std::bit_cast<int32_t>(Value);
        return Bits < 0 ? static_cast<int64_t>(INT32_MIN) - Bits : static_cast<int64_t>(Bits);
    };
    return std::abs(Order(Left) - Order(Right));
}

uint64_t Fnv1a64(const unsigned char* Bytes, size_t Count)
{
    uint64_t Hash = 0xcbf29ce484222325ull;
    for (size_t Index = 0; Index < Count; ++Index)
    {
        Hash ^= Bytes[Index];
        Hash *= 0x100000001b3ull;
    }
    return Hash;
}

std::string HexOf(uint64_t Value)
{
    char Text[17];
    std::snprintf(Text, sizeof(Text), "%016llx", static_cast<unsigned long long>(Value));
    return Text;
}

ScanStage ReadStage(const toml::table& Stages, const char* Name)
{
    const toml::table& Entry = *Stages[Name].as_table();
    auto Triple = [&](const char* Key) {
        const std::vector<double> Values = ReadNumbers(Entry[Key]);
        return std::array<double, 3>{Values[0], Values[1], Values[2]};
    };
    ScanStage Stage;
    Stage.Origin = Triple("Origin");
    Stage.Forward = Triple("Forward");
    Stage.Right = Triple("Right");
    Stage.Up = Triple("Up");
    return Stage;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE SECTIONS
//------------------------------------------------------------------------------------------------------------------------

void ValidateSpecification(const toml::table& Reference)
{
    std::printf("[1] the specification\n");
    const toml::table& Values = *Reference["Specification"].as_table();
    const ScanSpecificationValues& Spec = ScanSpecification;
    const std::map<std::string, double> Scalars = {
        {"Cycle", Spec.Cycle}, {"Sweep", Spec.Sweep}, {"Hold", Spec.Hold}, {"FrontStart", Spec.FrontStart}, {"FrontEnd", Spec.FrontEnd},
        {"FrontIdle", Spec.FrontIdle}, {"RampRate", Spec.RampRate}, {"RampSettle", Spec.RampSettle}, {"StepLimit", Spec.StepLimit},
        {"SolidFloor", Spec.SolidFloor}, {"EdgeOpacity", Spec.EdgeOpacity}, {"EdgeMargin", Spec.EdgeMargin}, {"CurtainIdle", Spec.CurtainIdle},
        {"CurtainHeight", Spec.CurtainHeight}, {"ParticleCentre", Spec.ParticleCentre}, {"ParticleSize", Spec.ParticleSize},
        {"ParticleSpeed", Spec.ParticleSpeed}, {"ParticleOpacity", Spec.ParticleOpacity}};
    for (const auto& [Name, Value] : Scalars)
    {
        Expect(Number(Values[Name]) == Value, "Specification." + Name + " equals the prototype's");
    }
    const std::vector<double> Curtain = ReadNumbers(Values["CurtainOpacity"]);
    Expect(Curtain.size() == 2 && Curtain[0] == Spec.CurtainOpacity[0] && Curtain[1] == Spec.CurtainOpacity[1], "Specification.CurtainOpacity");
    const std::vector<double> Box = ReadNumbers(Values["ParticleBox"]);
    Expect(Box.size() == 3 && Box[0] == Spec.ParticleBox[0] && Box[1] == Spec.ParticleBox[1] && Box[2] == Spec.ParticleBox[2], "Specification.ParticleBox");
    Expect(Values["ParticleCount"].value<int64_t>().value_or(-1) == Spec.ParticleCount, "Specification.ParticleCount");
    Expect(Values["GhostColour"].value<std::string>().value_or("") == Spec.GhostColour, "Specification.GhostColour");
    Expect(Values["Accent"].value<std::string>().value_or("") == Spec.Accent, "Specification.Accent");
    Expect(Values["EdgeColour"].value<std::string>().value_or("") == Spec.EdgeColour, "Specification.EdgeColour");
    Expect(Values["ParticleColour"].value<std::string>().value_or("") == Spec.ParticleColour, "Specification.ParticleColour");
}

void ValidateColours(const toml::table& Reference)
{
    std::printf("[2] colour conversions\n");
    double Worst = 0.0;
    size_t Exact = 0, Total = 0;
    for (const toml::node& Entry : *Reference["Colour"].as_array())
    {
        const toml::table& Colour = *Entry.as_table();
        const std::string Hex = Colour["Hex"].value<std::string>().value_or("");
        const std::vector<double> Linear = ReadNumbers(Colour["Linear"]);
        const std::vector<double> Display = ReadNumbers(Colour["Display"]);
        const std::array<double, 3> MyLinear = ConvertHexToLinear(Hex);
        const std::array<double, 3> MyDisplay = ResolveDisplayColour(Hex);
        for (size_t Channel = 0; Channel < 3; ++Channel)
        {
            Worst = std::max({Worst, std::abs(MyLinear[Channel] - Linear[Channel]), std::abs(MyDisplay[Channel] - Display[Channel])});
            Exact += (MyLinear[Channel] == Linear[Channel]) + (MyDisplay[Channel] == Display[Channel]);
            Total += 2;
            Expect(Near(MyLinear[Channel], Linear[Channel]) && Near(MyDisplay[Channel], Display[Channel]), "colour " + Hex + " channel " + std::to_string(Channel));
        }
    }
    const toml::table& Scalars = *Reference["Conversions"].as_table();
    const std::vector<double> Argument = ReadNumbers(Scalars["Argument"]);
    const std::vector<double> Srgb = ReadNumbers(Scalars["Srgb"]);
    const std::vector<double> Ease = ReadNumbers(Scalars["Ease"]);
    const std::vector<double> Damp = ReadNumbers(Scalars["Damp"]);
    for (size_t Index = 0; Index < Argument.size(); ++Index)
    {
        const double Mine[3] = {ConvertLinearToSrgb(Argument[Index]), EaseInOutCubic(std::min(Argument[Index], 1.0)), DampValue(Argument[Index], 1.0, 3.0, 0.05)};
        const double Theirs[3] = {Srgb[Index], Ease[Index], Damp[Index]};
        for (size_t Kind = 0; Kind < 3; ++Kind)
        {
            Worst = std::max(Worst, std::abs(Mine[Kind] - Theirs[Kind]));
            Exact += Mine[Kind] == Theirs[Kind];
            ++Total;
            Expect(Near(Mine[Kind], Theirs[Kind]), "conversion " + std::to_string(Kind) + " at " + std::to_string(Argument[Index]));
        }
    }
    const std::vector<double> AcesInput = ReadNumbers(Scalars["AcesInput"]);
    const std::vector<double> AcesOutput = ReadNumbers(Scalars["AcesOutput"]);
    for (size_t Entry = 0; Entry + 2 < AcesInput.size(); Entry += 3)
    {
        const std::array<double, 3> Mapped = ResolveAcesFilmic({AcesInput[Entry], AcesInput[Entry + 1], AcesInput[Entry + 2]});
        for (size_t Channel = 0; Channel < 3; ++Channel)
        {
            Worst = std::max(Worst, std::abs(Mapped[Channel] - AcesOutput[Entry + Channel]));
            Exact += Mapped[Channel] == AcesOutput[Entry + Channel];
            ++Total;
            Expect(Near(Mapped[Channel], AcesOutput[Entry + Channel]), "ACES filmic sample " + std::to_string(Entry / 3));
        }
    }
    std::printf("    %zu of %zu values bit-identical, worst difference %.3g\n", Exact, Total, Worst);
}

// 📝 Replays the script up to and including StepIndex of the global step counter; returns the sequence after that step.
struct ScriptReplay
{
    const toml::array& Segments;
    XRayScanSequence   Scan{};
    size_t             Segment = 0;
    size_t             InSegment = 0;
    int64_t            StepIndex = -1;

    explicit ScriptReplay(const toml::array& Script) : Segments(Script)
    {
        BeginSegment();
    }
    void BeginSegment()
    {
        if (Segment < Segments.size())
        {
            const int64_t Assign = Segments[Segment].as_table()->at("Assign").value<int64_t>().value_or(-1);
            if (Assign >= 0) Scan.Assign(Assign == 1);
        }
    }
    bool Step()
    {
        while (Segment < Segments.size() && InSegment >= static_cast<size_t>(Segments[Segment].as_table()->at("Steps").value<int64_t>().value_or(0)))
        {
            ++Segment;
            InSegment = 0;
            BeginSegment();
        }
        if (Segment >= Segments.size()) return false;
        Scan.Advance(Segments[Segment].as_table()->at("Raw").value<double>().value_or(0.0));
        ++InSegment;
        ++StepIndex;
        return true;
    }
};

void ValidateClock(const toml::table& Reference)
{
    std::printf("[3] the clock over the script\n");
    ScriptReplay Replay(*Reference["Segment"].as_array());
    const toml::table& Sampled = *Reference["Sequence"].as_table();
    const std::vector<int64_t> Index = ReadIntegers(Sampled["Index"]);
    const std::vector<double> Time = ReadNumbers(Sampled["Time"]), Ramp = ReadNumbers(Sampled["Ramp"]), SweepSeconds = ReadNumbers(Sampled["SweepSeconds"]),
                              Front = ReadNumbers(Sampled["Front"]), Amount = ReadNumbers(Sampled["Amount"]), CycleAmount = ReadNumbers(Sampled["CycleAmount"]),
                              Clock = ReadNumbers(Sampled["Clock"]), Curtain0 = ReadNumbers(Sampled["Curtain0"]), Curtain1 = ReadNumbers(Sampled["Curtain1"]),
                              EdgeOpacity = ReadNumbers(Sampled["EdgeOpacity"]);
    const std::vector<bool> Sweeping = ReadFlags(Sampled["Sweeping"]), Transparent = ReadFlags(Sampled["Transparent"]), Visible = ReadFlags(Sampled["Visible"]);
    size_t Checked = 0, ExactFronts = 0;
    double Worst = 0.0;
    for (size_t Sample = 0; Sample < Index.size(); ++Sample)
    {
        while (Replay.StepIndex < Index[Sample])
        {
            if (!Replay.Step()) break;
        }
        const XRayScanSequence& Scan = Replay.Scan;
        const std::string Where = " at step " + std::to_string(Index[Sample]);
        Expect(Replay.StepIndex == Index[Sample], "the script reaches" + Where);
        Expect(Near(Scan.QueryRamp(), Ramp[Sample]), "Ramp" + Where);
        Expect(Scan.QuerySweepSeconds() == SweepSeconds[Sample], "SweepSeconds" + Where);
        Expect(Near(Scan.QueryFront(), Front[Sample]), "Front" + Where);
        Expect(Near(Scan.QueryAmount(), Amount[Sample]), "Amount" + Where);
        Expect(Near(Scan.QueryCycleAmount(), CycleAmount[Sample]), "CycleAmount" + Where);
        Expect(Near(Scan.QueryClock(), Clock[Sample]), "Clock" + Where);
        Expect(Near(Scan.ResolveCurtain()[0], Curtain0[Sample]) && Near(Scan.ResolveCurtain()[1], Curtain1[Sample]), "Curtain" + Where);
        Expect(Near(Scan.ResolveEdgeOpacity(), EdgeOpacity[Sample]), "EdgeOpacity" + Where);
        Expect(Scan.QuerySweeping() == Sweeping[Sample], "Sweeping" + Where);
        Expect(Scan.QueryTransparent() == Transparent[Sample], "Transparent" + Where);
        Expect(Scan.QueryVisible() == Visible[Sample], "Visible" + Where);
        Worst = std::max({Worst, std::abs(Scan.QueryRamp() - Ramp[Sample]), std::abs(Scan.QueryFront() - Front[Sample]), std::abs(Scan.QueryAmount() - Amount[Sample])});
        ExactFronts += Scan.QueryFront() == Front[Sample];
        ++Checked;
        (void)Time;
    }
    std::printf("    %zu sampled steps, %zu fronts bit-identical, worst difference %.3g\n", Checked, ExactFronts, Worst);
}

void ValidateProfiles(const toml::table& Reference)
{
    std::printf("[4] the layer profile and the view record\n");
    const toml::table& Stages = *Reference["Stage"].as_table();
    const std::vector<float> Cloud = ConstructSparkleCloud();
    size_t Floats = 0, Exact = 0;
    int64_t WorstUlp = 0;
    for (const toml::node& Entry : *Reference["Profile"].as_array())
    {
        const toml::table& Profile = *Entry.as_table();
        const int64_t Step = Profile["Step"].value<int64_t>().value_or(-1);
        ScriptReplay Replay(*Reference["Segment"].as_array());
        while (Replay.StepIndex < Step && Replay.Step())
        {
        }
        const ScanStage Stage = ReadStage(Stages, Profile["Stage"].value<std::string>().value_or("").c_str());
        const std::array<float, 48> Mine = EncodeScanProfile(Replay.Scan, Stage, static_cast<uint32_t>(Cloud.size() / 4), Number(Profile["HudLineTop"]));
        const std::vector<double> Theirs = ReadNumbers(Profile["Values"]);
        Expect(Theirs.size() == 48, "the profile holds 48 floats");
        for (size_t Index = 0; Index < 48 && Index < Theirs.size(); ++Index)
        {
            const float Expected = static_cast<float>(Theirs[Index]);
            const int64_t Distance = UlpDistance(Mine[Index], Expected);
            WorstUlp = std::max(WorstUlp, Distance);
            Exact += Distance == 0;
            ++Floats;
            Expect(Distance <= 1, "profile float " + std::to_string(Index) + " at step " + std::to_string(Step));
        }
        const XRayScanView View = EncodeScanView(Replay.Scan, Stage, std::array<uint32_t, 5>{0, 1, 2, 3, 4}, 0.1);
        const std::vector<double> Plane = ReadNumbers(Profile["ScanPlane"]);
        for (size_t Index = 0; Index < 4; ++Index)
        {
            Expect(UlpDistance(View.ScanPlane[Index], static_cast<float>(Plane[Index])) <= 1, "ScanPlane " + std::to_string(Index) + " at step " + std::to_string(Step));
        }
        Expect(UlpDistance(View.ScanState[0], static_cast<float>(Number(Profile["Amount"]))) <= 1, "ScanState.x at step " + std::to_string(Step));
        Expect(View.ScannedOccupants[0] == 31u && View.ScannedOccupants[1] == 0u, "the five vehicle occupants are scanned");
        Expect(View.SparedFamilies[0] == HeadlightFamily && View.SparedFamilies[1] == TaillightFamily, "the lamps are spared");
    }
    std::printf("    %zu profile floats, %zu bit-identical, worst %lld ulp\n", Floats, Exact, static_cast<long long>(WorstUlp));
    for (const toml::node& Entry : *Reference["Mask"].as_array())
    {
        const toml::table& Case = *Entry.as_table();
        std::vector<uint32_t> Indices;
        for (int64_t Value : ReadIntegers(Case["Indices"])) Indices.push_back(static_cast<uint32_t>(Value));
        const std::vector<int64_t> Words = ReadIntegers(Case["Words"]);
        const std::array<uint32_t, 4> Mine = EncodeOccupantMask(Indices);
        for (size_t Word = 0; Word < 4; ++Word) Expect(static_cast<int64_t>(Mine[Word]) == Words[Word], "occupant mask word " + std::to_string(Word));
    }
}

void ValidateSparkles(const toml::table& Reference)
{
    std::printf("[5] the sparkle cloud\n");
    const toml::table& Sparkles = *Reference["Sparkles"].as_table();
    const std::vector<double> Theirs = ReadNumbers(Sparkles["Values"]);
    const std::vector<float> Mine = ConstructSparkleCloud();
    Expect(Mine.size() == Theirs.size() && Theirs.size() == 130 * 4, "130 sparkles of four floats");
    size_t Exact = 0;
    for (size_t Index = 0; Index < std::min(Mine.size(), Theirs.size()); ++Index)
    {
        Exact += Mine[Index] == static_cast<float>(Theirs[Index]);
        Expect(Mine[Index] == static_cast<float>(Theirs[Index]), "sparkle float " + std::to_string(Index));
    }
    std::printf("    %zu of %zu floats bit-identical\n", Exact, Theirs.size());
}

void ValidateShapes(const toml::table& Reference)
{
    std::printf("[6] feature edges of the shapes\n");
    size_t Segments = 0, Shapes = 0;
    for (const toml::node& Entry : *Reference["Shape"].as_array())
    {
        const toml::table& Shape = *Entry.as_table();
        const std::string Name = Shape["Name"].value<std::string>().value_or("");
        std::vector<ScanFacet> Facets;
        for (const toml::node& Corners : *Shape["Corners"].as_array())
        {
            const std::vector<double> Values = ReadNumbers(toml::node_view<const toml::node>(&Corners));
            ScanFacet Facet;
            for (size_t Corner = 0; Corner < 3; ++Corner) Facet.Corners[Corner] = {Values[Corner * 3], Values[Corner * 3 + 1], Values[Corner * 3 + 2]};
            Facets.push_back(Facet);
        }
        const std::vector<double> Mine = ExtractFeatureEdges(Facets, Number(Shape["Threshold"]));
        const std::vector<double> Theirs = ReadNumbers(Shape["Segments"]);
        Expect(Mine.size() / 6 == static_cast<size_t>(Shape["SegmentCount"].value<int64_t>().value_or(-1)), Name + ": segment count");
        bool Identical = Mine.size() == Theirs.size();
        for (size_t Index = 0; Identical && Index < Mine.size(); ++Index) Identical = Mine[Index] == Theirs[Index];
        Expect(Identical, Name + ": every segment end point, in order, bit for bit");
        Segments += Mine.size() / 6;
        ++Shapes;
    }
    std::printf("    %zu shapes, %zu segments\n", Shapes, Segments);
}

void ValidateCar(const toml::table& Reference, const std::filesystem::path& Root)
{
    std::printf("[7] the hero car's wireframe\n");
    GlobalIllumination::SceneStructure Scene;
    std::string Diagnostic;
    const std::filesystem::path FacetSpace = Root / "EngineContent/EnvironmentArchives/ProvingGround/ProvingGround.facetspace";
    if (!Scene.LoadFacetSpace(FacetSpace, &Diagnostic))
    {
        Expect(false, "the facet space loads: " + Diagnostic);
        return;
    }
    const toml::table& Car = *Reference["Car"].as_table();
    std::vector<ScanOccupantSource> Occupants;
    Occupants.push_back({static_cast<uint32_t>(Scene.VehicleBody), Scene.Occupants[Scene.VehicleBody].FacetSet});
    const std::vector<int64_t> Wheels = ReadIntegers(Car["Wheels"]);
    for (size_t Wheel = 0; Wheel < 4; ++Wheel)
    {
        Occupants.push_back({static_cast<uint32_t>(Scene.VehicleWheels[Wheel]), Scene.Occupants[Scene.VehicleWheels[Wheel]].FacetSet});
        Expect(Scene.VehicleWheels[Wheel] == Wheels[Wheel], "wheel " + std::to_string(Wheel) + " is occupant " + std::to_string(Wheels[Wheel]));
    }
    Expect(Scene.VehicleBody == Car["Body"].value<int64_t>().value_or(-1), "the body is the prototype's occupant");
    // 📝 The scan's input: the facet sets in doubles, as the prototype's scene holds them.
    std::vector<std::vector<ScanFacet>> Sets(Scene.FacetSets.size());
    std::vector<std::span<const ScanFacet>> Views;
    for (size_t Set = 0; Set < Scene.FacetSets.size(); ++Set)
    {
        for (const GlobalIllumination::FacetEntry& Facet : Scene.FacetSets[Set].Facets)
        {
            ScanFacet Converted;
            Converted.Corners = {Facet.Corners[0], Facet.Corners[1], Facet.Corners[2]};
            Converted.Family = Facet.Family;
            Sets[Set].push_back(Converted);
        }
        Views.emplace_back(Sets[Set]);
    }
    Expect(Scene.FacetSets[Occupants[0].FacetSet].Facets.size() == static_cast<size_t>(Car["BodyFacetCount"].value<int64_t>().value_or(-1)), "the body set has the prototype's facet count");
    const ScanEdgeRecords Edges = ConstructScanEdges(Occupants, Views);
    Expect(Edges.Count == static_cast<uint32_t>(Car["SegmentCount"].value<int64_t>().value_or(-1)), "segment count " + std::to_string(Edges.Count));
    std::map<uint32_t, int64_t> Counts;
    for (uint32_t Index = 0; Index < Edges.Count; ++Index)
    {
        uint32_t Tag = 0;
        std::memcpy(&Tag, &Edges.Records[static_cast<size_t>(Index) * 8 + 3], sizeof(Tag));
        ++Counts[Tag];
    }
    const toml::array& Tags = *Car["Tag"].as_array();
    Expect(Counts.size() == Tags.size(), "the same seven tags (occupant | family << 8)");
    for (const toml::node& Entry : Tags)
    {
        const toml::table& Tag = *Entry.as_table();
        const uint32_t Key = static_cast<uint32_t>(Tag["Tag"].value<int64_t>().value_or(-1));
        Expect(Counts[Key] == Tag["Count"].value<int64_t>().value_or(-1), "segments of tag " + std::to_string(Key));
    }
    const std::string Hash = HexOf(Fnv1a64(reinterpret_cast<const unsigned char*>(Edges.Records.data()), Edges.Records.size() * sizeof(float)));
    Expect(Hash == Car["RecordHash"].value<std::string>().value_or(""), "the record bytes hash to " + Hash);
    // 📝 The first and last four records: coordinates in float32 bit for bit, the tag word as raw bits.
    for (const char* Section : {"First", "Last"})
    {
        const toml::table& Part = *Car[Section].as_table();
        const std::vector<double> Records = ReadNumbers(Part["Records"]);
        const std::vector<int64_t> Words = ReadIntegers(Part["Tags"]);
        const size_t First = std::string(Section) == "First" ? 0 : Edges.Count - 4;
        Expect(Records.size() == 32 && Words.size() == 4, std::string(Section) + " records are four of eight floats");
        for (size_t Record = 0; Record < 4 && Record < Words.size(); ++Record)
        {
            uint32_t Tag = 0;
            std::memcpy(&Tag, &Edges.Records[(First + Record) * 8 + 3], sizeof(Tag));
            Expect(static_cast<int64_t>(Tag) == Words[Record], std::string(Section) + " record " + std::to_string(Record) + " tag");
            for (size_t Component = 0; Component < 8; ++Component)
            {
                if (Component == 3) continue;
                Expect(Edges.Records[(First + Record) * 8 + Component] == static_cast<float>(Records[Record * 8 + Component]),
                       std::string(Section) + " record " + std::to_string(Record) + " component " + std::to_string(Component));
            }
        }
    }
    std::printf("    %u segments, record hash %s (prototype %s)\n", Edges.Count, Hash.c_str(), Car["RecordHash"].value<std::string>().value_or("").c_str());
}

} // namespace

int main(int Count, char** Arguments)
{
    if (Count < 2)
    {
        std::printf("usage: XRayScanValidation <repository root>\n");
        return 2;
    }
    const std::filesystem::path Root = Arguments[1];
    toml::table Reference;
    try
    {
        Reference = toml::parse_file((Root / "Exhibits/XRayScan/Results/ScanReference.toml").string());
    }
    catch (const toml::parse_error& Error)
    {
        std::printf("the reference does not parse: %s\n", Error.what());
        return 2;
    }
    ValidateSpecification(Reference);
    ValidateColours(Reference);
    ValidateClock(Reference);
    ValidateProfiles(Reference);
    ValidateSparkles(Reference);
    ValidateShapes(Reference);
    ValidateCar(Reference, Root);
    std::printf("XRayScanValidation: %d assertions, %d failures\n", Assertions, Failures);
    return Failures == 0 ? 0 : 1;
}
