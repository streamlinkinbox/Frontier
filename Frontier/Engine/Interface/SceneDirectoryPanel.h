/*==========================================================================================================================================
                                                        SCENE DIRECTORY PANEL
                                     ------------------------------------------------------------------
        Portable Outliner view-model: flattened rows, real category tabs with counts, selection for the inspector.
==========================================================================================================================================*/

// 🧩 Portable Outliner view-model: flattened rows, real category tabs with counts, selection for the inspector.

#pragma once

#include "Engine/Scene/RecordClassification.h"
#if defined(FRONTIER_EDITOR_PROFILE)
    #include "Engine/Scene/RecordEditorDescription.h"
#endif
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
    RecordToken          ParentToken;                                      // [-] - Tree parent (for nested rows/search)
    std::string          Name;                                             // [text] - Display name (configured or default)
    RecordClassification Classification = RecordClassification::Unclassified;
    uint32_t             DepthTier      = 0;                               // [tiers] - Nesting depth; 0 is a root row
    uint32_t             ChildCount     = 0;                               // [records] - Direct children in the directory
    bool                 Visible        = true;                            // [-] - Directory eye toggle (dimmed when off)
    bool                 Enabled        = true;                            // [-] - Component enabled state
    bool                 Collapsed      = false;                           // [-] - Panel-side: subtree folded away
    std::string          Group;                                            // [text] - VehicleHMI Outliner group
    std::string          Category;                                         // [text] - Entity type used by Outliner search
    std::string          Glyph;                                            // [text] - VehicleHMI / Lucide glyph name
    std::string          Colour;                                           // [text] - Row accent "#rrggbb"
    std::string          StableId;                                         // [text] - Pinned entity id
    std::string          Meta;                                             // [text] - Live right-hand readout badge
    std::string          State;                                            // [text] - "live", "warm", or empty
    bool                *OutlinerToggle = nullptr;                         // [-] - Optional row-level eye toggle
    bool                 HiddenInOutliner = false;                         // [-] - Editor-authored tree visibility
};

struct SceneClassificationTab
{
    RecordClassification Classification = RecordClassification::Unclassified;
    uint32_t             Count          = 0;   // [records] - Live records in this classification
};

struct SceneDirectoryGroupSection
{
    std::string                    Group;      // [text] - Render, Environment, Forcefield, Vehicle, ...
    uint32_t                       Count = 0;  // [records] - Members before the text filter
    std::vector<SceneDirectoryRow> Rows;       // [-] - Visible members in registration order
};

struct SceneDirectoryEnabledCounts
{
    uint32_t Enabled  = 0;   // [records]
    uint32_t Disabled = 0;   // [records]
};

struct SceneInspectorModel
{
    bool        HasSelection = false;
    bool        HasEnabled   = false;          // [-] - The record exposes an enabled setting
    bool        Enabled      = true;           // [-] - Current enabled setting
    bool       *EnabledPtr   = nullptr;        // [-] - Direct live enabled bool pointer
    uint64_t   *RevisionPtr  = nullptr;        // [-] - Optional dirty revision counter
    std::string Group;                         // [text] - Crumb group
    std::string Category;                      // [text] - Eyebrow type
    std::string Name;                          // [text] - Object header
    std::string Colour;                        // [text] - Accent "#rrggbb"
    std::string Description;                   // [text] - Header description
    std::string StableId;                      // [text] - Pinned entity id
    std::string DedicatedEditorTitle;          // [text] - Dedicated popup editor window title

#if defined(FRONTIER_EDITOR_PROFILE)
    std::vector<RecordEditorScalarField> ScalarFields;
    std::vector<RecordEditorSwitch>      Switches;
    std::vector<RecordEditorChoiceField> ChoiceFields;
    std::vector<RecordEditorAction>      Actions;
    std::function<void()>                ConstructInfographic;
    std::function<void(const std::function<void()>&)> ConstructInspector;
    std::function<void()>                OpenDedicatedEditor;
#endif
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
    void ConfigureWorkspaceContext(std::string ProjectName, std::string ProjectStatus)
    {
        WorkspaceName   = std::move(ProjectName);
        WorkspaceStatus = std::move(ProjectStatus);
    }
    [[nodiscard]] const std::string& QueryWorkspaceName() const noexcept { return WorkspaceName; }
    [[nodiscard]] const std::string& QueryWorkspaceStatus() const noexcept { return WorkspaceStatus; }

    [[nodiscard]] const std::vector<SceneDirectoryRow>&          QueryRows() const { return Rows; }
    [[nodiscard]] const std::vector<SceneDirectoryRow>&          QueryVisible() const { return Visible; }
    [[nodiscard]] const std::vector<SceneClassificationTab>&     QueryTabs() const { return Tabs; }
    [[nodiscard]] uint32_t                                       QueryTotal() const { return Total; }
    [[nodiscard]] const SceneInspectorSummary&                   QueryInspector() const { return Inspector; }
    [[nodiscard]] const std::vector<SceneDirectoryGroupSection>& QueryGroups() const { return Groups; }
    [[nodiscard]] const SceneInspectorModel&                     QueryInspectorModel() const { return InspectorModel; }
    [[nodiscard]] SceneDirectoryEnabledCounts                    QueryEnabledCounts() const { return EnabledCounts; }

    void ConfigureClassificationFilter(std::optional<RecordClassification> Filter);
    [[nodiscard]] std::optional<RecordClassification> QueryClassificationFilter() const { return ClassificationFilter; }

    void ConfigureTextFilter(std::string Text);
    [[nodiscard]] const std::string& QueryTextFilter() const { return TextFilter; }

    void ConfigureCollapsed(RecordToken Token, bool Collapsed);
    [[nodiscard]] bool QueryCollapsed(RecordToken Token) const;

    void ConfigureGroupCollapsed(const std::string& Group, bool Collapsed);
    [[nodiscard]] bool QueryGroupCollapsed(const std::string& Group) const;

    void RequestSpawn(RecordClassification Classification);
    [[nodiscard]] std::optional<RecordClassification> TakeSpawnRequest();

    void RequestOpenDedicatedEditor(const std::string& StableId);
    [[nodiscard]] std::optional<std::string> TakeDedicatedEditorRequest();

    void ConstructInterface();

    void Select(RecordToken Token);
    bool SelectByStableId(const std::string& StableId);
    void ClearSelection();
    [[nodiscard]] RecordToken QuerySelected() const { return Selected; }

  private:
    void RebuildVisible();

    std::vector<SceneDirectoryRow>          Rows;
    std::vector<SceneDirectoryGroupSection> Groups;
    SceneDirectoryEnabledCounts             EnabledCounts;
    SceneInspectorModel                     InspectorModel;
    std::vector<SceneDirectoryRow>          Visible;
    std::vector<SceneClassificationTab>     Tabs;
    uint32_t                                Total = 0;
    std::string                             WorkspaceName = "Project workspace";
    std::string                             WorkspaceStatus = "No opening scene";
    std::optional<RecordClassification>     ClassificationFilter;
    std::string                             TextFilter;
    std::unordered_set<uint64_t>            CollapsedTokens;
    std::unordered_set<std::string>         CollapsedGroups{"Objects"};
    std::optional<RecordClassification>     PendingSpawn;
    std::optional<std::string>              PendingDedicatedEditorId;
    RecordToken                             Selected;
    SceneInspectorSummary                   Inspector;
};

} // namespace Frontier
