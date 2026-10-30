/*==========================================================================================================================================
                                                    EVERESTSCENEDIRECTORY.H
==========================================================================================================================================*/
// Own the UI milestone's editable environment records; records publish their own inspector descriptions.
#pragma once
#include "Frontier/Engine/Interface/SceneDirectoryPanel.h"
#include "Frontier/Engine/Scene/RecordEntry.h"
#include <memory>
#include <vector>

namespace Frontier
{
class EverestSceneDirectory
{
  public:
    EverestSceneDirectory();
    ~EverestSceneDirectory();
    EverestSceneDirectory(const EverestSceneDirectory &) = delete;
    EverestSceneDirectory &operator=(const EverestSceneDirectory &) = delete;
    void Synchronize();
    RecordToken ConstructSun();
    bool EraseSelected();
    SceneDirectoryPanel &QueryPanel() noexcept { return Projection; }

  private:
    std::vector<std::unique_ptr<RecordEntry>> Records;
    SceneDirectoryPanel Projection;
};
} // namespace Frontier
