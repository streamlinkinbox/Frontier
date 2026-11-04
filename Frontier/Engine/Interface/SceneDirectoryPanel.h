/*==========================================================================================================================================
                                                        SCENE DIRECTORY PANEL
                                     ------------------------------------------------------------------
        Portable Outliner view-model: flattened rows, real category tabs with counts, selection for the inspector.
==========================================================================================================================================*/

// 🧩 Portable Outliner view-model: flattened rows, real category tabs with counts, selection for the inspector.

#pragma once

#include "Engine/Scene/RecordClassification.h"
#include "Engine/Scene/RecordToken.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
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
    RecordClassification Classification = RecordClassification::Unclassified;
    uint32_t             DepthTier      = 0;                               // [tiers] - Nesting depth; 0 is a root row
    uint32_t             ChildCount     = 0;                               // [records] - Direct children in the directory
    bool                 Visible        = true;                            // [-] - Directory eye toggle (dimmed when off)
    bool                 Collapsed      = false;                           // [-] - Panel-side: subtree folded away
};

struct SceneClassificationTab
{
    RecordClassification Classification = RecordClassification::Unclassified;
    uint32_t             Count          = 0;   // [records] - Live records in this classification
};

// What the inspector shows for the current selection ("empty inspector" = HasSelection false).
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

// 📝 Same split as the Log Stream: this portable view-model owns projection, filtering and selection; the native
//    drawing (dock panel + inspector) joins it at the SDK phase against the upstream kit. Synchronize() snapshots
//    the directory into a flattened tree: roots in registration order, children directly beneath their parent in
//    registration order. In Shipping nothing ever registers, so every projection stays empty by construction.
class SceneDirectoryPanel
{
  public:
    void Synchronize(const SceneDirectory& Directory);

    [[nodiscard]] const std::vector<SceneDirectoryRow>&      QueryRows() const { return Rows; }       // Flattened tree, unfiltered
    [[nodiscard]] const std::vector<SceneDirectoryRow>&      QueryVisible() const { return Visible; } // After the classification filter
    [[nodiscard]] const std::vector<SceneClassificationTab>& QueryTabs() const { return Tabs; }       // Every classification, with live counts
    [[nodiscard]] uint32_t                                   QueryTotal() const { return Total; }     // [records]
    [[nodiscard]] const SceneInspectorSummary&               QueryInspector() const { return Inspector; }

    void ConfigureClassificationFilter(std::optional<RecordClassification> Filter);   // nullopt = All
    [[nodiscard]] std::optional<RecordClassification> QueryClassificationFilter() const { return ClassificationFilter; }

    void ConfigureTextFilter(std::string Text);   // Case-insensitive substring over row names; empty clears.
    [[nodiscard]] const std::string& QueryTextFilter() const { return TextFilter; }

    void ConfigureCollapsed(RecordToken Token, bool Collapsed);
    [[nodiscard]] bool QueryCollapsed(RecordToken Token) const;

    // Spawn flow: the presentation's add menu records a request; the host services it (records are engine
    // objects — the panel never invents them) and the new record appears by construction.
    void RequestSpawn(RecordClassification Classification);
    [[nodiscard]] std::optional<RecordClassification> TakeSpawnRequest();

    // Dynamic Game Engine Editor construction / deletion catalogue (populated by SceneWorld).
    void ConfigureVisibleTypes(std::vector<std::string> Types) { VisibleTypes = {Types.begin(),Types.end()}; }
    void ConfigureConstructibleTypes(std::vector<std::pair<std::string, std::string>> TypeEntries);
    [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& QueryConstructibleTypes() const noexcept
    {
        return ConstructibleTypes;
    }
    void RequestConstructRecord(std::string TypeIdentifier);
    [[nodiscard]] std::optional<std::string> ConsumeConstructRequest();
    void RequestDeleteRecord(RecordToken Token);
    [[nodiscard]] std::optional<RecordToken> ConsumeDeleteRequest();

    // 📝 The row menu's and the keys' requests (the Project Zero editor's row menu: Rename, Hide, Fracture, Duplicate, Remove);
    //    the host services them against the level's records, as it does construction and deletion.
    void RequestDuplicateRecord(RecordToken Token);
    [[nodiscard]] std::optional<RecordToken> ConsumeDuplicateRequest();
    void RequestRenameRecord(RecordToken Token, std::string Name);
    [[nodiscard]] std::optional<std::pair<RecordToken, std::string>> ConsumeRenameRequest();
    void BeginRename(RecordToken Token);   // the row's name turns into a text field (F2, the row menu's Rename)
    [[nodiscard]] RecordToken QueryRenaming() const { return Renaming; }

    // 📝 With the Construct dialog offered, the '+' button asks the host to open it (the host draws it) instead of the add menu.
    void ConfigureConstructDialog(bool Offered) { ConstructDialogOffered = Offered; }
    [[nodiscard]] bool ConsumeConstructDialogRequest();

    // Native drawing (interface-SDK builds; an empty stub without the SDK, so portable builds keep linking).
    // The host calls Synchronize() each frame first, then services TakeSpawnRequest().
    void ConstructInterface();

    void Select(RecordToken Token);   // Unknown/stale tokens clear the selection on the next Synchronize
    void ClearSelection();
    [[nodiscard]] RecordToken QuerySelected() const { return Selected; }

  private:
    void RebuildVisible();

    std::unordered_set<std::string> VisibleTypes;
    std::vector<SceneDirectoryRow>      Rows;
    std::vector<SceneDirectoryRow>      Visible;
    std::vector<SceneClassificationTab> Tabs;
    uint32_t                            Total = 0;   // [records]
    std::optional<RecordClassification> ClassificationFilter;
    std::string                         TextFilter;            // [text] - Lower-cased on configure
    std::unordered_set<uint64_t>        CollapsedTokens;       // [-] - Panel-side fold state
    std::optional<RecordClassification>              PendingSpawn;
    std::vector<std::pair<std::string, std::string>> ConstructibleTypes;
    std::vector<std::string>                         PendingConstructTypes;
    std::vector<RecordToken>                         PendingDeleteTokens;
    std::vector<RecordToken>                         PendingDuplicateTokens;
    std::vector<std::pair<RecordToken, std::string>> PendingRenames;
    RecordToken                                      Renaming;
    bool                                             RenameFocus              = false;
    bool                                             ConstructDialogOffered   = false;
    bool                                             ConstructDialogRequested = false;
    RecordToken                                      Selected;
    SceneInspectorSummary               Inspector;
};

} // namespace Frontier
