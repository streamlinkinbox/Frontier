/*====================================================================================================================================
                                                   NATIVE PROJECT VALIDATION
====================================================================================================================================*/
// 🧩 Validates Project Everest discovery and the record-to-Outliner-to-Inspector lifecycle.

#include "Applications/Editor/ProjectBrowserHost.h"
#include "Engine/Interface/ProjectDescriptor.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Rendering/CelestialModel.h"
#include "Engine/Scene/EnvironmentRecords.h"
#include "Engine/Scene/RecordStore.h"
#include "Engine/Scene/SceneDirectory.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace
{

uint32_t FailureCount = 0;
uint32_t CheckCount   = 0;

void Check(bool Condition, const char* Label)
{
    ++CheckCount;
    if (Condition)
    {
        std::printf("PASS  %s\n", Label);
    }
    else
    {
        ++FailureCount;
        std::printf("FAIL  %s\n", Label);
    }
}

const Frontier::ProjectBrowserEntry* FindEverest(const Frontier::ProjectBrowserEnumeration& Listing)
{
    for (const Frontier::ProjectBrowserEntry& Entry : Listing.Entries)
    {
        if (Entry.Descriptor.Name == "Project Everest")
        {
            return &Entry;
        }
    }
    return nullptr;
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount != 2)
    {
        std::fprintf(stderr, "usage: NativeProjectValidation <repository-root>\n");
        return 2;
    }

    const std::filesystem::path RepositoryRoot = Arguments[1];
    const std::filesystem::path ProjectsRoot   = RepositoryRoot / "Frontier" / "Projects";
    const Frontier::ProjectBrowserEnumeration Listing = Frontier::EnumerateProjects(ProjectsRoot);
    const Frontier::ProjectBrowserEntry* Everest = FindEverest(Listing);
    Check(Everest != nullptr, ".frontier browser enumeration contains Project Everest");

    if (Everest != nullptr)
    {
        Frontier::ProjectDescriptor Descriptor;
        std::string                  Diagnostic;
        const bool                   Decoded = Frontier::DecodeProjectDescriptor(
            Everest->Descriptor.DescriptorPath, Descriptor, Diagnostic);
        Check(Decoded, "Project Everest descriptor decodes through the native TOML codec");
        if (Decoded)
        {
            Check(Descriptor.Name == "Project Everest", "descriptor name is preserved");
            Check(Descriptor.OpeningScene.empty() && Descriptor.CodeImage.empty(),
                  "UI-first project has no scene or code image");
            Check(std::filesystem::is_directory(Descriptor.ProjectDirectory / Descriptor.ContentLocation),
                  "project Content directory exists");
        }
        else
        {
            std::printf("  decoder diagnostic: %s\n", Diagnostic.c_str());
        }
    }

    Frontier::RecordStore&    Store    = Frontier::RecordStore::Instance();
    Frontier::SceneDirectory& Directory = Frontier::SceneDirectory::Instance();
    const size_t              InitialStoreCount = Store.QueryCount();
    const size_t              InitialDirectoryCount = Directory.QueryCount();
    Frontier::RecordToken     SunToken;
    bool                      InspectorDescriptionRemoved = false;

    {
        Frontier::SunRecord         Sun;
        Frontier::AtmosphereRecord  Atmosphere;
        Frontier::CloudRecord       Clouds;
        Frontier::FogRecord         Fog;
        Frontier::WindRecord        Wind;

        (void)Directory.ConfigureName(Sun.Token(), "Sun");
        (void)Directory.ConfigureName(Atmosphere.Token(), "Atmosphere");
        (void)Directory.ConfigureName(Clouds.Token(), "Clouds");
        (void)Directory.ConfigureName(Fog.Token(), "Fog");
        (void)Directory.ConfigureName(Wind.Token(), "Wind");

        Frontier::SceneDirectoryPanel Outliner;
        Outliner.Synchronize(Directory);
        Check(Outliner.QueryTotal() == InitialDirectoryCount + 5,
              "reusable environment records register in the Outliner on construction");
        Check(Outliner.SelectByStableId("sun"), "Outliner selection resolves the Sun record");

        const Frontier::SceneInspectorModel& Inspector = Outliner.QueryInspectorModel();
        Check(Inspector.HasSelection && Inspector.Name == "Sun" &&
                  Inspector.Category == "Directional light",
              "selected record supplies its own Inspector description");
        Check(Inspector.ScalarFields.size() == 3 && Inspector.ScalarFields[0].Target != nullptr,
              "Inspector scalar fields point to live Sun settings");
        if (!Inspector.ScalarFields.empty() && Inspector.ScalarFields[0].Target != nullptr)
        {
            *Inspector.ScalarFields[0].Target = 32.0;
            Check(Sun.Placement.ElevationDegrees == 32.0,
                  "Inspector field pointer edits the owning record setting");
        }
        constexpr double Pi = 3.14159265358979323846;
        const double Elevation = Sun.Placement.ElevationDegrees * Pi / 180.0;
        const double Azimuth = Sun.Placement.AzimuthDegrees * Pi / 180.0;
        const Frontier::SpatialVector ManualDirection = Sun.QuerySunDirection();
        Check(std::abs(ManualDirection.X - std::cos(Elevation) * std::cos(Azimuth)) < 1e-9 &&
                  std::abs(ManualDirection.Y - std::cos(Elevation) * std::sin(Azimuth)) < 1e-9,
              "manual Sun azimuth follows VehicleHMI's east-origin local-vector convention");
        Sun.Placement.AutoPosition = true;
        const double Julian = Frontier::JulianDate(Sun.Placement.SkyYear, static_cast<unsigned>(Sun.Placement.SkyMonth),
                                                    static_cast<unsigned>(Sun.Placement.SkyDay),
                                                    Sun.Placement.LocalSolarTime - Sun.Placement.TimeZoneHours);
        const Frontier::SpatialVector ExpectedDirection = Frontier::SunDirectionLocal(
            Frontier::SunPosition(Julian, Sun.Placement.LatitudeDegrees, Sun.Placement.LongitudeDegrees));
        const Frontier::SpatialVector AutomaticDirection = Sun.QuerySunDirection();
        Check(std::abs(AutomaticDirection.X - ExpectedDirection.X) < 1e-12 &&
                  std::abs(AutomaticDirection.Y - ExpectedDirection.Y) < 1e-12 &&
                  std::abs(AutomaticDirection.Z - ExpectedDirection.Z) < 1e-12,
              "automatic Sun placement delegates to the existing celestial solver");
        Sun.Placement.AutoPosition = false;

        const bool AtmosphereSelected = Outliner.SelectByStableId("atmosphere");
        const Frontier::SceneInspectorModel& AtmosphereInspector = Outliner.QueryInspectorModel();
        const bool AtmosphereBinding = AtmosphereSelected && AtmosphereInspector.ScalarFields.size() == 14 &&
            AtmosphereInspector.ScalarFields.front().Target == &Atmosphere.Parameters.RayleighScattering.R &&
            std::string(AtmosphereInspector.ScalarFields.front().Section) == "Rayleigh";
        Check(AtmosphereBinding, "Atmosphere Inspector groups live Rayleigh, Mie, ozone and surface fields");
        if (AtmosphereBinding)
        {
            *AtmosphereInspector.ScalarFields.front().Target = 0.0065;
            Check(std::abs(Atmosphere.Parameters.RayleighScattering.R - 0.0065) < 1e-12,
                  "Atmosphere Inspector edits its owning scattering record");
        }

        const bool CloudsSelected = Outliner.SelectByStableId("clouds");
        const Frontier::SceneInspectorModel& CloudsInspector = Outliner.QueryInspectorModel();
        const bool CloudsBinding = CloudsSelected && CloudsInspector.ScalarFields.size() == 9 &&
            CloudsInspector.ScalarFields.front().Target == &Clouds.Parameters.Coverage &&
            std::string(CloudsInspector.ScalarFields.front().Section) == "Layer";
        Check(CloudsBinding, "Clouds Inspector groups live layer, advection and optical fields");
        if (CloudsBinding)
        {
            *CloudsInspector.ScalarFields.front().Target = 0.61;
            Check(std::abs(Clouds.Parameters.Coverage - 0.61) < 1e-12,
                  "Clouds Inspector edits its owning slab record");
        }

        const bool FogSelected = Outliner.SelectByStableId("fog");
        const Frontier::SceneInspectorModel& FogInspector = Outliner.QueryInspectorModel();
        const bool FogBinding = FogSelected && FogInspector.ScalarFields.size() == 7 &&
            FogInspector.ScalarFields.front().Target == &Fog.Parameters.DensityAtBase &&
            std::string(FogInspector.ScalarFields.front().Section) == "Medium";
        Check(FogBinding, "Fog Inspector groups live medium, Sun response and tint fields");
        if (FogBinding)
        {
            *FogInspector.ScalarFields.front().Target = 0.01;
            Check(std::abs(Fog.Parameters.DensityAtBase - 0.01) < 1e-12,
                  "Fog Inspector edits its owning medium record");
        }

        const bool WindSelected = Outliner.SelectByStableId("wind");
        const Frontier::SceneInspectorModel& WindInspector = Outliner.QueryInspectorModel();
        const bool WindBinding = WindSelected && WindInspector.ScalarFields.size() == 3 &&
            WindInspector.ScalarFields.front().Target == &Wind.SpeedMps &&
            std::string(WindInspector.ScalarFields.front().Section) == "Wind vector";
        Check(WindBinding, "Wind Inspector groups the live force-field vector and turbulence settings");
        if (WindBinding)
        {
            *WindInspector.ScalarFields.front().Target = 8.5;
            Check(std::abs(Wind.SpeedMps - 8.5) < 1e-12,
                  "Wind Inspector edits its owning force-field record");
        }
        SunToken = Sun.Token();
        InspectorDescriptionRemoved = Directory.QueryEditorDescription(SunToken) != nullptr;
    }

    Check(Store.QueryCount() == InitialStoreCount,
          "record destructors remove their entries from the record store");
    Check(Directory.QueryCount() == InitialDirectoryCount,
          "record destructors unregister their Outliner rows");
    Check(Directory.QueryEditorDescription(SunToken) == nullptr && InspectorDescriptionRemoved,
          "record destruction removes its Inspector description");

    std::printf("%u checks, %u failures\n", CheckCount, FailureCount);
    return FailureCount == 0 ? 0 : 1;
}
