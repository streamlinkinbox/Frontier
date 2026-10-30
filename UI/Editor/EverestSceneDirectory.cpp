/*==========================================================================================================================================
                                                    EVERESTSCENEDIRECTORY.CPP
==========================================================================================================================================*/
// Scene construction and destruction drive registration; the editor never maintains a parallel scene list.
#include "EverestSceneDirectory.h"
#include "Frontier/Engine/Scene/EnvironmentRecords.h"
#include "Frontier/Engine/Scene/SceneDirectory.h"
#include "Frontier/Engine/Diagnostics/LogStream.h"
#include <algorithm>

namespace Frontier
{
EverestSceneDirectory::EverestSceneDirectory()
{
    Projection.ConfigureWorkspaceCaption("Project Everest");
    Records.push_back(std::make_unique<SunRecord>());
    Records.push_back(std::make_unique<AtmosphereRecord>());
    Records.push_back(std::make_unique<CloudRecord>());
    Records.push_back(std::make_unique<FogRecord>());
    Records.push_back(std::make_unique<WindRecord>());
    Synchronize();
    Projection.Select(Records.front()->Token());
    Synchronize();
}

EverestSceneDirectory::~EverestSceneDirectory()
{
    Projection.ClearSelection();
    Records.clear();
}

RecordToken EverestSceneDirectory::ConstructSun()
{
    auto Entry = std::make_unique<SunRecord>();
    const auto Token = Entry->Token();
    Records.push_back(std::move(Entry));
    Synchronize();
    Projection.Select(Token);
    Synchronize();
    return Token;
}

bool EverestSceneDirectory::EraseSelected()
{
    const auto Token = Projection.QuerySelected();
    const auto Position = std::find_if(Records.begin(), Records.end(),
        [&](const auto &Entry) { return Entry->Token() == Token; });
    if (Position == Records.end()) return false;
    Projection.ClearSelection();
    Records.erase(Position);
    Synchronize();
    return true;
}

void EverestSceneDirectory::Synchronize()
{
    Projection.Synchronize(SceneDirectory::Instance());
    if (const auto RequestedEditor = Projection.TakeDedicatedEditorRequest())
    {
        // The reused panels already invoke registered callbacks. Do not invoke them twice here.
        const auto &Inspector = Projection.QueryInspectorModel();
        if (!Inspector.OpenDedicatedEditor)
        {
            const auto Category = RegisterLogCategory("Everest", LogColour::Blue);
            WriteLog(Category, LogSeverity::Warning,
                "No dedicated editor is registered for this record; use its Inspector controls.");
        }
    }
    if (const auto Requested = Projection.TakeSpawnRequest())
    {
        if (*Requested == RecordClassification::Environment) ConstructSun();
        else
        {
            const auto Category = RegisterLogCategory("Everest", LogColour::Blue);
            WriteLog(Category, LogSeverity::Warning,
                "This record category needs an application-authored constructor and inspector; no placeholder was created.");
        }
    }
}
} // namespace Frontier
