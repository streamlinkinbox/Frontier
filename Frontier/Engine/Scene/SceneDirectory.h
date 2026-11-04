/*==========================================================================================================================================
                                                             SCENE DIRECTORY
                                     ------------------------------------------------------------------
          Editor-machinery Outliner model: names and nesting for every registered record, Editor/Development only.
==========================================================================================================================================*/

// 🧩 Editor-machinery Outliner model: names, nesting, and presentation metadata for registered records.

#pragma once

#include "RecordClassification.h"
#include "RecordToken.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frontier
{

class RecordEntry;

//------------------------------------------------------------------------------------------------------------------------
//                                                         TYPES
//------------------------------------------------------------------------------------------------------------------------

enum class RecordOperationalCondition : uint8_t
{
    Live,
    Warm,
    Standby
};

struct SceneDirectoryMetadata
{
    std::string                GroupName;
    std::string                SubtitleCategory;
    std::string                GlyphIdentifier;
    std::string                AccentHex;
    std::string                LiveMetadata;
    RecordOperationalCondition OperationalCondition = RecordOperationalCondition::Live;
    bool                       RemovableInEditor    = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENE DIRECTORY
//------------------------------------------------------------------------------------------------------------------------

class SceneDirectory
{
  public:
    [[nodiscard]] static SceneDirectory& Instance();

    SceneDirectory()                                 = default;
    SceneDirectory(const SceneDirectory&)            = delete;
    SceneDirectory& operator=(const SceneDirectory&) = delete;

    void Register(const RecordEntry& Entry);
    void Unregister(RecordToken Token);

    void                      ConfigureActiveSceneHeader(std::string Title, std::string Subtitle);
    [[nodiscard]] std::string QueryActiveSceneTitle() const;
    [[nodiscard]] std::string QueryActiveSceneSubtitle() const;

    bool ConfigureName(RecordToken Token, std::string Name);
    bool ConfigureParent(RecordToken Child, RecordToken Parent);
    bool ConfigureVisible(RecordToken Token, bool Visible);
    bool ConfigureMetadata(RecordToken Token, SceneDirectoryMetadata Metadata);

    [[nodiscard]] bool                   QueryVisible(RecordToken Token) const;
    [[nodiscard]] bool                   QueryRegistered(RecordToken Token) const;
    [[nodiscard]] std::string            QueryName(RecordToken Token) const;
    [[nodiscard]] RecordToken            QueryParent(RecordToken Token) const;
    [[nodiscard]] RecordClassification   QueryClassification(RecordToken Token) const;
    [[nodiscard]] SceneDirectoryMetadata QueryMetadata(RecordToken Token) const;
    [[nodiscard]] uint32_t               QueryChildCount(RecordToken Token) const;
    [[nodiscard]] size_t                 QueryCount() const;

    void CopyRegistrationOrder(std::vector<RecordToken>& Out) const;

  private:
    struct DirectoryEntry
    {
        std::string            Name;
        RecordToken            Parent;
        RecordClassification   Classification = RecordClassification::Unclassified;
        bool                   Visible        = true;
        SceneDirectoryMetadata Metadata{};
    };

    mutable std::mutex                           Mutex;
    std::vector<RecordToken>                     RegistrationOrder;
    std::unordered_map<uint64_t, DirectoryEntry> Entries;
    std::string                                  ActiveSceneTitle    = "Untitled";
    std::string                                  ActiveSceneSubtitle = "Frontier LevelSpace";
};

} // namespace Frontier
