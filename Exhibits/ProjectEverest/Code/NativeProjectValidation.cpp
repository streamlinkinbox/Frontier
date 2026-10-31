/*====================================================================================================================================
                                                   NATIVE PROJECT VALIDATION
====================================================================================================================================*/
// 🧩 Validates Project Everest discovery and the record-to-Outliner-to-Inspector lifecycle.

#include "Applications/Editor/ProjectBrowserHost.h"
#include "Engine/Interface/ProjectDescriptor.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Scene/EnvironmentRecords.h"
#include "Engine/Scene/RecordStore.h"
#include "Engine/Scene/SceneDirectory.h"

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
