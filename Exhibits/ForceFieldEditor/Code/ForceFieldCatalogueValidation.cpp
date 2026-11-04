/*==========================================================================================================================================
                                                     FORCEFIELDCATALOGUEVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 The C++ force field against the approved prototype's numbers: every contribution, activation, centre, profile and the budget
//    of the demo field, then the level's ForceSource records, their round trips and the renderer's path through them.
//
//     ForceFieldCatalogueValidation <repository root>
//
// 📝 The reference (Exhibits/ForceFieldEditor/Results/CatalogueReference.toml) is written by ExportCatalogueReference.mjs from
//    ForceFieldCatalogue.js. The double path (a PlanetaryWindProfile set from the reference's doubles) must agree to 1e-9; the
//    production path reads WindConfiguration's floats, so it agrees to float precision.

#include "Engine/PhysicalDynamics/AtmosphericFlow.h"
#include "Engine/PhysicalDynamics/ForceField.h"
#include "Engine/PhysicalDynamics/ForceSourceCatalogue.h"
#include "Engine/Scene/SceneWorld.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

int Assertions = 0;
int Failures   = 0;

void Verify(bool Condition, const std::string& Caption)
{
    ++Assertions;
    std::cout << (Condition ? "  PASS  " : "  FAIL  ") << Caption << '\n';
    if (!Condition) ++Failures;
}

constexpr double π = 3.14159265358979323846;

double Read(const toml::table& Table, std::string_view Key, double Fallback = 0.0)
{
    if (const auto Value = Table[Key].value<double>()) return *Value;
    if (const auto Whole = Table[Key].value<int64_t>()) return static_cast<double>(*Whole);
    return Fallback;
}

SpatialVector ReadVector(const toml::table& Table, std::string_view Key)
{
    SpatialVector Out{};
    if (const toml::array* Array = Table[Key].as_array(); Array && Array->size() >= 3)
        Out = {(*Array)[0].value<double>().value_or(0.0), (*Array)[1].value<double>().value_or(0.0), (*Array)[2].value<double>().value_or(0.0)};
    return Out;
}

ForceSourceCategory ResolveCategory(const std::string& Type)
{
    static const std::map<std::string, ForceSourceCategory> Types = {
        {"Tornado", ForceSourceCategory::TornadoColumn}, {"GustFront", ForceSourceCategory::GustFront}, {"Microburst", ForceSourceCategory::Microburst},
        {"Thermal", ForceSourceCategory::ThermalUpdraft}, {"Jet", ForceSourceCategory::JetCorridor},    {"Puff", ForceSourceCategory::GustPuff},
        {"Blast", ForceSourceCategory::BlastWave},       {"Wake", ForceSourceCategory::VehicleWake},     {"Current", ForceSourceCategory::Current},
        {"Attractor", ForceSourceCategory::Attractor},   {"Repulsor", ForceSourceCategory::Repulsor},    {"Orbit", ForceSourceCategory::Orbit},
        {"Lift", ForceSourceCategory::Lift},             {"Magnetic", ForceSourceCategory::MagneticDipole}, {"Gravity", ForceSourceCategory::Gravity},
        {"Drag", ForceSourceCategory::DragVolume}};
    const auto Found = Types.find(Type);
    return Found != Types.end() ? Found->second : ForceSourceCategory::None;
}

// 📝 The prototype's source (field axes, its key names) as a ForceSource record's configuration (scene axes).
ForceSourceConfiguration ConstructFromReference(const toml::table& Entry)
{
    const ForceSourceCategory Category = ResolveCategory(Entry["type"].value_or(std::string{}));
    ForceSourceConfiguration  Source   = ConstructForceSource(Category);
    Source.Enabled = Entry["enabled"].value_or(true);
    const SpatialVector Centre = ReadVector(Entry, "centre");
    const std::array<double, 3> Scene = ConvertFieldToSceneAxes(Centre);
    for (size_t Axis = 0; Axis < 3; ++Axis) Source.CentreMetres[Axis] = Scene[Axis];
    const std::string Reach = Entry["reach"].value_or(std::string{"Sphere"});
    Source.Reach = Reach == "Everywhere" ? ForceReach::Everywhere : Reach == "Column" ? ForceReach::Column : Reach == "Box" ? ForceReach::Box : ForceReach::Sphere;
    const std::string Falloff = Entry["falloff"].value_or(std::string{"Smooth"});
    Source.Falloff = Falloff == "None" ? ForceFalloff::None : Falloff == "Linear" ? ForceFalloff::Linear
                   : Falloff == "InverseSquare" ? ForceFalloff::InverseSquare : ForceFalloff::Smooth;
    const std::string Activation = Entry["activation"].value_or(std::string{"Always"});
    Source.Activation = Activation == "Window" ? ForceActivation::Window : Activation == "Trigger" ? ForceActivation::Trigger : ForceActivation::Always;
    Source.TriggeredAtSeconds = Read(Entry, "triggered_at", -1.0);
    const std::pair<const char*, double ForceSourceConfiguration::*> Keys[] = {
        {"strength", &ForceSourceConfiguration::Strength},
        {"inner_radius", &ForceSourceConfiguration::InnerRadiusMetres},
        {"collar_radius", &ForceSourceConfiguration::CollarRadiusMetres},
        {"inflow", &ForceSourceConfiguration::InflowMetresPerSecond},
        {"updraft", &ForceSourceConfiguration::UpdraftMetresPerSecond},
        {"height", &ForceSourceConfiguration::HeightMetres},
        {"spin", &ForceSourceConfiguration::Spin},
        {"track_bearing", &ForceSourceConfiguration::TrackBearingDegrees},
        {"track_speed", &ForceSourceConfiguration::TrackSpeedMetresPerSecond},
        {"front_speed", &ForceSourceConfiguration::FrontSpeedMetresPerSecond},
        {"crest_depth", &ForceSourceConfiguration::CrestDepthMetres},
        {"half_width", &ForceSourceConfiguration::HalfWidthMetres},
        {"bearing", &ForceSourceConfiguration::BearingDegrees},
        {"downdraft", &ForceSourceConfiguration::DowndraftMetresPerSecond},
        {"ring_radius", &ForceSourceConfiguration::RingRadiusMetres},
        {"length", &ForceSourceConfiguration::LengthMetres},
        {"lift", &ForceSourceConfiguration::LiftMetresPerSecond},
        {"radius_y", &ForceSourceConfiguration::VerticalRadiusMetres},
        {"drift", &ForceSourceConfiguration::Drift},
        {"expansion", &ForceSourceConfiguration::ExpansionMetresPerSecond},
        {"thickness", &ForceSourceConfiguration::ThicknessMetres},
        {"initial_radius", &ForceSourceConfiguration::InitialRadiusMetres},
        {"circulation", &ForceSourceConfiguration::CirculationMetresPerSecond},
        {"speed", &ForceSourceConfiguration::BodySpeedMetresPerSecond},
        {"swirl", &ForceSourceConfiguration::Swirl},
        {"swallow", &ForceSourceConfiguration::SwallowRadiusMetres},
        {"radius", &ForceSourceConfiguration::ReachMetres},
        {"begins", &ForceSourceConfiguration::BeginsSeconds},
        {"lasts", &ForceSourceConfiguration::LastsSeconds},
        {"repeats", &ForceSourceConfiguration::RepeatsSeconds},
        {"ramp_in", &ForceSourceConfiguration::RampInSeconds},
        {"ramp_out", &ForceSourceConfiguration::RampOutSeconds},
    };
    for (const auto& [Key, Member] : Keys)
        if (Entry.contains(Key)) Source.*Member = Read(Entry, Key);
    if (Entry.contains("half_x")) Source.HalfExtentsMetres[0] = Read(Entry, "half_x");
    if (Entry.contains("half_z")) Source.HalfExtentsMetres[1] = Read(Entry, "half_z");
    return Source;
}

ForceContribution ResolveContribution(const std::string& Name)
{
    return Name == "Accelerate" ? ForceContribution::Accelerate : Name == "Damp" ? ForceContribution::Damp : ForceContribution::Flow;
}

struct WindReference
{
    double   Bearing = 45, Speed = 6.5, GustStrength = 0.25, GustFrequency = 0.085, Shear = 0.15, Veer = 8, Turbulence = 0.45, TurbulenceScale = 95;
    uint32_t Seed = 7331;
    double   GroundHeight = 0, MesoscaleScale = 18000, MesoscaleStrength = 0.22;
    bool     Enabled = true, IncludeTurbulence = true;
};

// 📝 The double path: the profile from the reference's doubles, exactly as ConfigureAtmosphericFlow would set it.
void ConfigureExactField(ForceFieldSpace& Field, const WindReference& Wind, const std::vector<ForceSourceConfiguration>& Sources, double Time)
{
    PlanetaryWindProfile Profile;
    Profile.BearingRadians          = (Wind.Bearing - 90.0) * π / 180.0;
    Profile.SpeedMetersPerSecond    = Wind.Enabled ? Wind.Speed : 0.0;
    Profile.GustAmplitude           = Wind.GustStrength;
    Profile.GustFrequencyHz         = Wind.GustFrequency;
    Profile.ShearPerKilometer       = Wind.Shear;
    Profile.VeerRadiansPerKilometer = Wind.Veer * π / 180.0;
    Profile.SwirlStrength           = Wind.IncludeTurbulence ? Wind.Turbulence : 0.0;
    Profile.SwirlScaleMeters        = Wind.TurbulenceScale;
    Profile.Seed                    = Wind.Seed;
    Profile.MesoscaleScaleMeters    = Wind.MesoscaleScale;
    Profile.MesoscaleStrength       = Wind.MesoscaleStrength;
    Field.ConfigureEnabled(true);
    Field.SetWindProfile(Profile);
    Field.SetGroundPlaneYMeters(Wind.GroundHeight);
    Field.SetElapsedSeconds(Time);
    Field.ConfigureAuthoredSources(Sources, Time);
}

struct Sample
{
    ForceContribution Contribution = ForceContribution::Flow;
    int32_t           Solo         = -1;
    double            Time = 0, Cell = 0;
    SpatialVector     Point{}, Value{};
};

double Separation(SpatialVector A, SpatialVector B)
{
    return std::max({std::abs(A.X - B.X), std::abs(A.Y - B.Y), std::abs(A.Z - B.Z)});
}

double Magnitude(SpatialVector A)
{
    return std::max({std::abs(A.X), std::abs(A.Y), std::abs(A.Z)});
}

std::string Scientific(double Value)
{
    char Text[32];
    std::snprintf(Text, sizeof(Text), "%.2e", Value);
    return Text;
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    const std::filesystem::path Root = ArgumentCount > 1 ? std::filesystem::path(Arguments[1]) : std::filesystem::current_path();
    toml::table Reference;
    try
    {
        Reference = toml::parse_file((Root / "Exhibits/ForceFieldEditor/Results/CatalogueReference.toml").string());
    }
    catch (const toml::parse_error& Error)
    {
        std::cerr << "Reference unreadable: " << Error.description() << '\n';
        return 2;
    }

    WindReference Wind;
    if (const toml::table* Table = Reference["wind"].as_table())
    {
        Wind.Enabled           = (*Table)["enabled"].value_or(true);
        Wind.Bearing           = Read(*Table, "bearing");
        Wind.Speed             = Read(*Table, "speed");
        Wind.GustStrength      = Read(*Table, "gust_strength");
        Wind.GustFrequency     = Read(*Table, "gust_frequency");
        Wind.Shear             = Read(*Table, "shear");
        Wind.Veer              = Read(*Table, "veer");
        Wind.Turbulence        = Read(*Table, "turbulence");
        Wind.TurbulenceScale   = Read(*Table, "turbulence_scale");
        Wind.Seed              = static_cast<uint32_t>(Read(*Table, "seed"));
        Wind.GroundHeight      = Read(*Table, "ground_height");
        Wind.MesoscaleScale    = Read(*Table, "mesoscale_scale");
        Wind.MesoscaleStrength = Read(*Table, "mesoscale_strength");
    }
    if (const toml::table* Table = Reference["field"].as_table()) Wind.IncludeTurbulence = (*Table)["include_turbulence"].value_or(true);

    std::vector<ForceSourceConfiguration> Sources;
    std::vector<std::string>              Names;
    if (const toml::array* Array = Reference["sources"].as_array())
        for (const toml::node& Node : *Array)
            if (const toml::table* Entry = Node.as_table())
            {
                Sources.push_back(ConstructFromReference(*Entry));
                Names.push_back((*Entry)["name"].value_or(std::string{}));
            }
    Verify(Sources.size() == 14, "The reference holds the prototype's 14 demo sources");

    std::vector<Sample> Samples;
    if (const toml::array* Array = Reference["samples"].as_array())
        for (const toml::node& Node : *Array)
            if (const toml::table* Entry = Node.as_table())
                Samples.push_back({ResolveContribution((*Entry)["contribution"].value_or(std::string{})), static_cast<int32_t>(Read(*Entry, "solo", -1)),
                                   Read(*Entry, "time"), Read(*Entry, "cell"), ReadVector(*Entry, "point"), ReadVector(*Entry, "value")});
    Verify(Samples.size() > 1500, "The reference holds " + std::to_string(Samples.size()) + " samples");

    //------------------------------------------------------------------------------------------------------------------------
    std::cout << "\nContributions (double path, 1e-9)\n";
    {
        std::map<ForceContribution, double> Worst;
        std::map<ForceContribution, size_t> Count;
        double SoloWorst = 0.0, Largest = 0.0;
        ForceFieldSpace Field;
        double          Prepared = -1.0;
        for (const Sample& Entry : Samples)
        {
            if (Entry.Time != Prepared)
            {
                ConfigureExactField(Field, Wind, Sources, Entry.Time);
                Prepared = Entry.Time;
            }
            const SpatialVector Value = Field.EvaluateContribution(Entry.Contribution, Entry.Point, Entry.Cell, true, Entry.Solo);
            const double        Error = Separation(Value, Entry.Value) / std::max(1.0, Magnitude(Entry.Value));
            if (Entry.Solo == -1) Worst[Entry.Contribution] = std::max(Worst[Entry.Contribution], Error);
            else SoloWorst = std::max(SoloWorst, Error);
            Largest = std::max(Largest, Magnitude(Entry.Value));
            ++Count[Entry.Contribution];
            if (Error > 1e-9)
                std::cout << "    worst so far at (" << Entry.Point.X << ", " << Entry.Point.Y << ", " << Entry.Point.Z << ") t=" << Entry.Time
                          << " cell=" << Entry.Cell << " solo=" << Entry.Solo << ": " << Value.X << ' ' << Value.Y << ' ' << Value.Z << " vs "
                          << Entry.Value.X << ' ' << Entry.Value.Y << ' ' << Entry.Value.Z << '\n';
        }
        const char* Labels[] = {"Flow", "Accelerate", "Damp"};
        for (const auto& [Contribution, Error] : Worst)
            Verify(Error <= 1e-9, std::string(Labels[static_cast<int>(Contribution)]) + ": " + std::to_string(Count[Contribution]) +
                                      " samples, worst relative error " + Scientific(Error));
        Verify(SoloWorst <= 1e-9, "Solo (each source alone, the wind alone): worst relative error " + Scientific(SoloWorst));
        Verify(Largest > 30.0, "The samples reach the strong parts of the field (largest " + std::to_string(Largest) + ")");
    }

    //------------------------------------------------------------------------------------------------------------------------
    std::cout << "\nActivation, centre and profile\n";
    {
        ForceWindReading Reading;
        Reading.Enabled                 = Wind.Enabled;
        Reading.BearingDegrees          = Wind.Bearing;
        Reading.SpeedMetresPerSecond    = Wind.Speed;
        Reading.VeerDegreesPerKilometre = Wind.Veer;
        Reading.ShearPerKilometre       = Wind.Shear;
        Reading.GroundHeightMetres      = Wind.GroundHeight;
        double EnvelopeWorst = 0.0, CentreWorst = 0.0;
        bool   StatusEqual   = true;
        if (const toml::array* Array = Reference["activations"].as_array())
            for (const toml::node& Node : *Array)
                if (const toml::table* Entry = Node.as_table())
                {
                    const ForceSourceConfiguration& Source  = Sources.at(static_cast<size_t>(Read(*Entry, "source")));
                    const double                    Time    = Read(*Entry, "time");
                    const ForceActivationReading    Reading2 = ResolveForceActivation(Source, Time);
                    EnvelopeWorst = std::max({EnvelopeWorst, std::abs(Reading2.Envelope - Read(*Entry, "envelope")),
                                              std::abs(Reading2.AgeSeconds - Read(*Entry, "age"))});
                    StatusEqual &= (*Entry)["status"].value_or(std::string{}) == ResolveForceSourceStatusLabel(Reading2.Status);
                    CentreWorst = std::max(CentreWorst, Separation(ResolveForceSourceCentre(Source, Reading, Time, Reading2.AgeSeconds),
                                                                   ReadVector(*Entry, "centre")));
                }
        Verify(EnvelopeWorst <= 1e-12, "Envelope and age over windows, triggers and ramps: worst " + Scientific(EnvelopeWorst));
        Verify(StatusEqual, "Status labels (Active, Waiting, Armed, Spent) match");
        Verify(CentreWorst <= 1e-9, "Moving centres (track, drift, front travel): worst " + Scientific(CentreWorst) + " m");
        double ProfileWorst = 0.0;
        if (const toml::array* Array = Reference["profiles"].as_array())
            for (const toml::node& Node : *Array)
                if (const toml::table* Entry = Node.as_table())
                {
                    const ForceSourceConfiguration& Source  = Sources.at(static_cast<size_t>(Read(*Entry, "source")));
                    const ForceSourceProfile        Profile = ResolveForceSourceProfile(Source, Reading, 10.0, 0.0);
                    ProfileWorst = std::max(ProfileWorst, std::abs(Profile.ReachMetres - Read(*Entry, "reach")));
                    const toml::array* Distances  = (*Entry)["distance"].as_array();
                    const toml::array* Magnitudes = (*Entry)["magnitude"].as_array();
                    for (uint32_t Step = 0; Distances && Magnitudes && Step < kForceProfileSamples && Step < Magnitudes->size(); ++Step)
                    {
                        ProfileWorst = std::max(ProfileWorst, std::abs(Profile.DistanceMetres[Step] - (*Distances)[Step].value_or(0.0)));
                        ProfileWorst = std::max(ProfileWorst, std::abs(Profile.Magnitude[Step] - (*Magnitudes)[Step].value_or(0.0)) /
                                                                  std::max(1.0, std::abs((*Magnitudes)[Step].value_or(0.0))));
                    }
                }
        Verify(ProfileWorst <= 1e-9, "Radial profiles of all 14 types: worst " + Scientific(ProfileWorst));
    }

    //------------------------------------------------------------------------------------------------------------------------
    std::cout << "\nBudget\n";
    {
        ForceFieldConfiguration Field;
        const ForceFieldBudget  Budget = ResolveForceFieldBudget(Field, Sources);
        const toml::table*      Table  = Reference["budget"].as_table();
        Verify(Table && Budget.Baked == Read(*Table, "baked") && Budget.Live == Read(*Table, "live") && Budget.Damping == Read(*Table, "damp"),
               "Baked, live and damping counts: " + std::to_string(Budget.Baked) + " / " + std::to_string(Budget.Live) + " / " +
                   std::to_string(Budget.Damping));
        Verify(Table && Budget.CascadeBytes == static_cast<uint64_t>(Read(*Table, "cascade_bytes")) &&
                   Budget.TotalBytes == static_cast<uint64_t>(Read(*Table, "total_bytes")),
               "Cascade memory " + std::to_string(Budget.TotalBytes / (1024 * 1024)) + " MB over " + std::to_string(Budget.LevelCount) + " levels");
        bool Levels = Table != nullptr && Budget.LevelCount == 4;
        if (const toml::array* Cells = Table ? (*Table)["cells"].as_array() : nullptr)
            for (uint32_t Level = 0; Level < Budget.LevelCount && Level < Cells->size(); ++Level)
                Levels &= std::abs(Budget.Levels[Level].CellMetres - (*Cells)[Level].value_or(0.0)) < 1e-9;
        Verify(Levels, "Cascade cells 2 m, 16 m, 128 m, 1 km");
    }

    //------------------------------------------------------------------------------------------------------------------------
    std::cout << "\nThe level's ForceSource records (Default.levelspace)\n";
    {
        SceneWorld  World;
        std::string Diagnostic;
        const bool  Loaded = World.LoadLevelSpace(Root / "Projects/Everest/Content/Levels/Default.levelspace", &Diagnostic);
        Verify(Loaded, "Default.levelspace loads" + (Diagnostic.empty() ? std::string{} : ": " + Diagnostic));
        const std::vector<const SceneRecordBase*> Records = World.FindAllByType("ForceSource");
        Verify(Records.size() == Sources.size(), "The level holds " + std::to_string(Records.size()) + " ForceSource records");
        const SceneRecordBase* FieldRecord = World.FindAllByType("ForceField").empty() ? nullptr : World.FindAllByType("ForceField").front();
        bool                   Parented    = FieldRecord != nullptr;
        bool                   Equal       = Records.size() == Sources.size();
        double                 Worst       = 0.0;
        std::vector<ForceSourceConfiguration> Level;
        for (size_t Index = 0; Index < Records.size() && Index < Sources.size(); ++Index)
        {
            const auto* Record = dynamic_cast<const ForceSourceRecord*>(Records[Index]);
            if (!Record) { Equal = false; continue; }
            Parented &= FieldRecord && Record->QueryParentToken().Value == FieldRecord->QueryToken().Value;
            const ForceSourceConfiguration& A = Record->QueryConfiguration();
            ForceSourceConfiguration        B = Sources[Index];
            B.TriggeredAtSeconds = -1.0;   // 📝 runtime only: the level does not keep a firing time
            Equal &= Record->QueryRecordName() == Names[Index] && A.Category == B.Category && A.Reach == B.Reach && A.Falloff == B.Falloff &&
                     A.Activation == B.Activation && A.Enabled == B.Enabled && A.ActsOn == B.ActsOn;
            const ForceSourceDescriptor& Descriptor = ResolveForceSourceDescriptor(A.Category);
            for (const ForceParameterDescriptor& Parameter : Descriptor.Parameters) Worst = std::max(Worst, std::abs(A.*(Parameter.Member) - B.*(Parameter.Member)));
            for (const ForceBearingDescriptor& Bearing : Descriptor.Bearings) Worst = std::max(Worst, std::abs(A.*(Bearing.Member) - B.*(Bearing.Member)));
            for (size_t Axis = 0; Axis < 3; ++Axis) Worst = std::max(Worst, std::abs(A.CentreMetres[Axis] - B.CentreMetres[Axis]));
            Worst = std::max({Worst, std::abs(A.ReachMetres - B.ReachMetres), std::abs(A.Spin - B.Spin), std::abs(A.BeginsSeconds - B.BeginsSeconds),
                              std::abs(A.LastsSeconds - B.LastsSeconds), std::abs(A.RepeatsSeconds - B.RepeatsSeconds),
                              std::abs(A.RampInSeconds - B.RampInSeconds), std::abs(A.RampOutSeconds - B.RampOutSeconds)});
            if (A.Reach == ForceReach::Column || A.Reach == ForceReach::Box) Worst = std::max(Worst, std::abs(A.HeightMetres - B.HeightMetres));
            Level.push_back(A);
        }
        Verify(Parented, "Every ForceSource record sits under the Force Field");
        Verify(Equal && Worst <= 1e-9, "Names, categories, reach, timing and every parameter equal the prototype's (worst " + Scientific(Worst) + ")");

        // 📝 Round trip: each record serialized and read back into a fresh record of its type.
        bool RoundTrip = true;
        for (const SceneRecordBase* Entry : Records)
        {
            toml::table First, Second;
            Entry->SerializeConfiguration(First);
            const RecordToken Copy = World.ConstructRecord("ForceSource", "Copy");
            SceneRecordBase*  Fresh = World.FindRecord(Copy);
            if (!Fresh) { RoundTrip = false; continue; }
            Fresh->DeserializeConfiguration(First);
            Fresh->SerializeConfiguration(Second);
            RoundTrip &= First == Second;
            (void)World.DestroyRecord(Copy);
        }
        Verify(RoundTrip, "Every ForceSource record survives serialize → deserialize unchanged");

        // 📝 The production path: the level's records and Wind through ConfigureAtmosphericFlow (WindConfiguration's floats).
        WindConfiguration WindRecordConfiguration;
        if (const auto Winds = World.FindAllByType("Wind"); !Winds.empty())
            if (const auto* Record = dynamic_cast<const WindRecord*>(Winds.front())) WindRecordConfiguration = Record->QueryConfiguration();
        ForceFieldConfiguration Controls;
        if (FieldRecord)
            if (const auto* Record = dynamic_cast<const ForceFieldRecord*>(FieldRecord)) Controls = Record->QueryConfiguration();
        Verify(std::abs(WindRecordConfiguration.DirectionDegrees - Wind.Bearing) < 1e-6 && std::abs(WindRecordConfiguration.SpeedMetresPerSecond - Wind.Speed) < 1e-6 &&
                   std::abs(WindRecordConfiguration.CurlNoiseTurbulence - Wind.Turbulence) < 1e-6 &&
                   std::abs(WindRecordConfiguration.MesoscaleScaleMetres - Wind.MesoscaleScale) < 1e-3,
               "The level's Wind is the prototype's (45°, 6.5 m/s, turbulence 0.45, mesoscale 18 km)");
        std::vector<ForceSourceConfiguration> Fired = Level;
        for (size_t Index = 0; Index < Fired.size(); ++Index) Fired[Index].TriggeredAtSeconds = Sources[Index].TriggeredAtSeconds;
        ForceFieldSpace Field;
        double          WorstFlow = 0.0, Prepared = -1.0;
        size_t          Count     = 0;
        for (const Sample& Entry : Samples)
        {
            if (Entry.Solo != -1) continue;
            if (Entry.Time != Prepared)
            {
                Controls.TimelineSeconds = Entry.Time;
                ConfigureAtmosphericFlow(Field, Controls, WindRecordConfiguration, Entry.Time, Fired);
                Prepared = Entry.Time;
            }
            const SpatialVector Value = Field.EvaluateContribution(Entry.Contribution, Entry.Point, Entry.Cell, true);
            WorstFlow = std::max(WorstFlow, Separation(Value, Entry.Value) / std::max(1.0, Magnitude(Entry.Value)));
            ++Count;
        }
        Verify(WorstFlow <= 2e-4, "The renderer's path (level records, float Wind) over " + std::to_string(Count) + " samples: worst relative " +
                                      Scientific(WorstFlow));
    }

    //------------------------------------------------------------------------------------------------------------------------
    std::cout << "\nTaxonomy\n";
    {
        bool Ordered = EnumerateForceSourceDescriptors().size() == 16;
        int  Flows = 0, Accelerations = 0, Dampings = 0;
        for (const ForceSourceDescriptor& Descriptor : EnumerateForceSourceDescriptors())
        {
            Flows += Descriptor.Contribution == ForceContribution::Flow;
            Accelerations += Descriptor.Contribution == ForceContribution::Accelerate;
            Dampings += Descriptor.Contribution == ForceContribution::Damp;
            Ordered &= ResolveForceSourceDescriptor(Descriptor.Identifier) == &Descriptor;
        }
        Verify(Ordered && Flows == 9 && Accelerations == 6 && Dampings == 1, "16 types: 9 flows, 6 accelerations, 1 damping, each found by its identifier");
        bool Native = true;
        for (uint8_t Value = 1; Value <= 8; ++Value)
            Native &= static_cast<uint8_t>(ResolveForceSourceDescriptor(static_cast<ForceSourceCategory>(Value)).Category) == Value;
        Verify(Native, "Categories 1 – 8 keep the GPU force record numbers");
    }

    std::cout << "\n[ForceFieldCatalogueValidation] " << (Assertions - Failures) << " / " << Assertions << " passed\n";
    return Failures == 0 ? 0 : 1;
}
