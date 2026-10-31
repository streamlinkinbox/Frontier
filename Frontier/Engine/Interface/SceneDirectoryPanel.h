/*==========================================================================================================================================
                                                        SCENE DIRECTORY PANEL
                                     ------------------------------------------------------------------
        Portable Outliner view-model: flattened rows, folder enclosures, category tabs with counts, selection for the inspector.
==========================================================================================================================================*/

// 🧩 Portable Outliner view-model: flattened rows, folder enclosures, category tabs with counts, selection for the inspector.

#pragma once

#include "Engine/Scene/RecordClassification.h"
#include "Engine/Scene/RecordToken.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace Frontier
{

class SceneDirectory;

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct SceneDirectoryRow
{
    RecordToken          Token;                                            // [-] - Record identity
    std::string          Name;                                             // [text] - Display name (configured or default)
    std::string          LiveBadge;                                        // [text] - Live telemetry or folder tag
    RecordClassification Classification = RecordClassification::Unclassified;
    uint32_t             AccentColour   = 0;                               // [RGBA8] - 0 = classification default
    uint32_t             DepthTier      = 0;                               // [tiers] - Nesting depth; 0 is a root row
    uint32_t             ChildCount     = 0;                               // [records] - Direct enclosed count
    bool                 Visible        = true;                            // [-] - Directory eye toggle (dimmed when off)
    bool                 Collapsed      = false;                           // [-] - Panel-side: subtree folded away
};

struct SceneClassificationTab
{
    RecordClassification Classification = RecordClassification::Unclassified;
    uint32_t             Count          = 0;   // [records] - Live records in this classification
};

struct SceneInspectorSummary
{
    bool                 HasSelection   = false;
    RecordToken          Token;
    std::string          Name;                                                // [text]
    RecordClassification Classification = RecordClassification::Unclassified;
    std::string          ParentName;                                          // [text] - Empty for a root record
    uint32_t             ChildCount     = 0;                                  // [records]
    uint32_t             DepthTier      = 0;                                  // [tiers]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  SCENE DIRECTORY PANEL
//------------------------------------------------------------------------------------------------------------------------

class SceneDirectoryPanel
{
  public:
    void Synchronize(const SceneDirectory& Directory);

    [[nodiscard]] const std::vector<SceneDirectoryRow>&      QueryRows() const { return Rows; }
    [[nodiscard]] const std::vector<SceneDirectoryRow>&      QueryVisible() const { return Visible; }
    [[nodiscard]] const std::vector<SceneClassificationTab>& QueryTabs() const { return Tabs; }
    [[nodiscard]] uint32_t                                   QueryTotal() const { return Total; }
    [[nodiscard]] uint32_t                                   QueryEnabledCount() const { return EnabledCount; }
    [[nodiscard]] uint32_t                                   QueryDisabledCount() const { return DisabledCount; }
    [[nodiscard]] const SceneInspectorSummary&               QueryInspector() const { return Inspector; }

    void ConfigureClassificationFilter(std::optional<RecordClassification> Filter);
    [[nodiscard]] std::optional<RecordClassification> QueryClassificationFilter() const { return ClassificationFilter; }

    void ConfigureTextFilter(std::string Text);
    [[nodiscard]] const std::string& QueryTextFilter() const { return TextFilter; }

    void ConfigureCollapsed(RecordToken Token, bool Collapsed);
    [[nodiscard]] bool QueryCollapsed(RecordToken Token) const;

    void RequestSpawn(RecordClassification Classification);
    [[nodiscard]] std::optional<RecordClassification> TakeSpawnRequest();

    void ConstructInterface();

    void Select(RecordToken Token);
    void ClearSelection();
    [[nodiscard]] RecordToken QuerySelected() const { return Selected; }

  private:
    void RebuildVisible();

    std::vector<SceneDirectoryRow>      Rows;
    std::vector<SceneDirectoryRow>      Visible;
    std::vector<SceneClassificationTab> Tabs;
    uint32_t                            Total         = 0;
    uint32_t                            EnabledCount  = 0;
    uint32_t                            DisabledCount = 0;
    std::optional<RecordClassification> ClassificationFilter;
    std::string                         TextFilter;
    std::unordered_set<uint64_t>        CollapsedTokens;
    std::optional<RecordClassification> PendingSpawn;
    RecordToken                         Selected;
    SceneInspectorSummary               Inspector;
};

} // namespace Frontier
