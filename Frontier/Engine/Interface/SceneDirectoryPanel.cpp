// 🧩 Portable Outliner view-model: flattened rows, real category tabs with counts, selection for the inspector.

#include "Engine/Interface/SceneDirectoryPanel.h"

#include "Engine/Scene/SceneDirectory.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace Frontier
{

namespace
{

constexpr uint32_t MaximumDepthTier = 128;   // [tiers]

void AppendSubtree(const SceneDirectory& Directory, RecordToken Token, uint32_t DepthTier,
                   const std::unordered_map<uint64_t, std::vector<RecordToken>>& Children,
                   const std::unordered_set<uint64_t>& CollapsedTokens,
                   std::vector<SceneDirectoryRow>& Out)
{
    SceneDirectoryRow Row;
    Row.Token          = Token;
    Row.ParentToken    = Directory.QueryParent(Token);
    Row.Name           = Directory.QueryName(Token);
    Row.Classification = Directory.QueryClassification(Token);
    Row.Visible        = Directory.QueryVisible(Token);
    Row.Enabled        = Row.Visible;
    Row.Category       = RecordClassificationName(Row.Classification);
#if defined(FRONTIER_EDITOR_PROFILE)
    if (const RecordEditorDescription* Description = Directory.QueryEditorDescription(Token))
    {
        Row.Group            = Description->Group != nullptr ? Description->Group : "";
        Row.Category         = Description->Category != nullptr ? Description->Category : Row.Category;
        Row.Glyph            = Description->Glyph != nullptr ? Description->Glyph : "";
        Row.Colour           = Description->Colour != nullptr ? Description->Colour : "";
        Row.StableId         = Description->StableId != nullptr ? Description->StableId : "";
        Row.OutlinerToggle   = Description->OutlinerToggle;
        Row.HiddenInOutliner = Description->HiddenInOutliner;
        if (Description->QueryLiveMeta)
        {
            Row.Meta = Description->QueryLiveMeta();
        }
        if (Description->QueryLiveState)
        {
            Row.State = Description->QueryLiveState();
        }
    }
#endif
    Row.DepthTier  = DepthTier;
    Row.ChildCount = Directory.QueryChildCount(Token);
    Row.Collapsed  = CollapsedTokens.find(Token.Value) != CollapsedTokens.end();
    Out.push_back(Row);

    if (DepthTier >= MaximumDepthTier)
    {
        return;
    }
    const auto It = Children.find(Token.Value);
    if (It != Children.end())
    {
        for (const RecordToken Child : It->second)
        {
            AppendSubtree(Directory, Child, DepthTier + 1, Children, CollapsedTokens, Out);
        }
    }
}

} // namespace

void SceneDirectoryPanel::Synchronize(const SceneDirectory& Directory)
{
    std::vector<RecordToken> Order;
    Directory.CopyRegistrationOrder(Order);
    Total = 0;

    std::unordered_map<uint64_t, std::vector<RecordToken>> Children;
    std::vector<RecordToken>                               Roots;
    for (const RecordToken Token : Order)
    {
        const RecordToken Parent = Directory.QueryParent(Token);
        if (Parent.IsValid() && Directory.QueryRegistered(Parent))
        {
            Children[Parent.Value].push_back(Token);
        }
        else
        {
            Roots.push_back(Token);
        }
    }

    for (auto It = CollapsedTokens.begin(); It != CollapsedTokens.end();)
    {
        It = Directory.QueryRegistered(RecordToken{*It}) ? std::next(It) : CollapsedTokens.erase(It);
    }

    Rows.clear();
    for (const RecordToken Root : Roots)
    {
        AppendSubtree(Directory, Root, 0, Children, CollapsedTokens, Rows);
    }
    Rows.erase(std::remove_if(Rows.begin(), Rows.end(), [](const SceneDirectoryRow& Row) {
                   return Row.HiddenInOutliner;
               }),
               Rows.end());
    Total = static_cast<uint32_t>(Rows.size());

    Tabs.clear();
    for (uint32_t Index = 0; Index < static_cast<uint32_t>(RecordClassification::Count); ++Index)
    {
        Tabs.push_back(SceneClassificationTab{static_cast<RecordClassification>(Index), 0});
    }
    for (const SceneDirectoryRow& Row : Rows)
    {
        Tabs[static_cast<size_t>(Row.Classification)].Count += 1;
    }

    Inspector = SceneInspectorSummary{};
    if (Selected.IsValid() && Directory.QueryRegistered(Selected))
    {
        Inspector.HasSelection   = true;
        Inspector.Token          = Selected;
        Inspector.Name           = Directory.QueryName(Selected);
        Inspector.Classification = Directory.QueryClassification(Selected);
        Inspector.ChildCount     = Directory.QueryChildCount(Selected);
        const RecordToken Parent = Directory.QueryParent(Selected);
        Inspector.ParentName     = Parent.IsValid() ? Directory.QueryName(Parent) : std::string{};
        for (const SceneDirectoryRow& Row : Rows)
        {
            if (Row.Token == Selected)
            {
                Inspector.DepthTier = Row.DepthTier;
                break;
            }
        }
    }
    else
    {
        Selected = RecordToken{};
    }

    static const char* const GroupOrder[] = {
        "Render", "Environment", "Forcefield", "Objects", "Vehicle", "Effects", "Destruction", "Level"};
    const auto FallbackGroup = [](RecordClassification Classification) {
        switch (Classification)
        {
            case RecordClassification::Illumination:
            case RecordClassification::Camera:
            case RecordClassification::Interface:    return "Render";
            case RecordClassification::Environment:  return "Environment";
            case RecordClassification::Physics:      return "Forcefield";
            case RecordClassification::Vehicle:
            case RecordClassification::Tyre:         return "Vehicle";
            case RecordClassification::Content:      return "Level";
            case RecordClassification::Geometry:
            case RecordClassification::Unclassified:
            case RecordClassification::Count:        break;
        }
        return "Objects";
    };

    // Preserve the VehicleHMI environment entity order independently of record-construction order.
    static const char* const EnvironmentEntityOrder[] = {
        "sun", "clouds", "local-cloud", "local-fog", "atmosphere", "stars", "moon", "flare", "fog", "aerial", "lightning"};
    std::vector<size_t> EnvironmentIndices;
    std::vector<SceneDirectoryRow> EnvironmentRows;
    for (size_t Index = 0; Index < Rows.size(); ++Index)
    {
        SceneDirectoryRow& Row = Rows[Index];
        if (Row.Group.empty())
        {
            Row.Group = FallbackGroup(Row.Classification);
        }
        if (Row.Group == "Environment")
        {
            EnvironmentIndices.push_back(Index);
            EnvironmentRows.push_back(Row);
        }
    }
    const auto EnvironmentRank = [](const SceneDirectoryRow& Row) {
        for (size_t Index = 0; Index < std::size(EnvironmentEntityOrder); ++Index)
        {
            if (Row.StableId == EnvironmentEntityOrder[Index])
            {
                return Index;
            }
        }
        return std::size(EnvironmentEntityOrder);
    };
    std::stable_sort(EnvironmentRows.begin(), EnvironmentRows.end(), [&](const SceneDirectoryRow& A,
                                                                         const SceneDirectoryRow& B) {
        return EnvironmentRank(A) < EnvironmentRank(B);
    });
    for (size_t Index = 0; Index < EnvironmentIndices.size(); ++Index)
    {
        Rows[EnvironmentIndices[Index]] = std::move(EnvironmentRows[Index]);
    }

    // The VehicleHMI search matches entity names and types, and keeps ancestors visible when a child matches.
    std::unordered_set<uint64_t> SearchMatches;
    if (!TextFilter.empty())
    {
        for (const SceneDirectoryRow& Row : Rows)
        {
            std::string Lower = Row.Name + " " + Row.Category;
            std::transform(Lower.begin(), Lower.end(), Lower.begin(),
                           [](unsigned char Glyph) { return static_cast<char>(std::tolower(Glyph)); });
            if (Lower.find(TextFilter) != std::string::npos)
            {
                for (RecordToken Token = Row.Token; Token.IsValid(); Token = Directory.QueryParent(Token))
                {
                    SearchMatches.insert(Token.Value);
                }
            }
        }
    }

    Groups.clear();
    EnabledCounts = SceneDirectoryEnabledCounts{};
    for (SceneDirectoryRow& Row : Rows)
    {
        if (Row.Group.empty())
        {
            Row.Group = FallbackGroup(Row.Classification);
        }
        Row.Enabled = Row.Visible && (Row.OutlinerToggle == nullptr || *Row.OutlinerToggle);
        if (Row.Enabled)
        {
            EnabledCounts.Enabled += 1;
        }
        else
        {
            EnabledCounts.Disabled += 1;
        }

        SceneDirectoryGroupSection* Section = nullptr;
        for (SceneDirectoryGroupSection& Candidate : Groups)
        {
            if (Candidate.Group == Row.Group)
            {
                Section = &Candidate;
                break;
            }
        }
        if (Section == nullptr)
        {
            Groups.push_back(SceneDirectoryGroupSection{Row.Group, 0, {}});
            Section = &Groups.back();
        }
        Section->Count += 1;
        const bool TextMatches = TextFilter.empty() || SearchMatches.find(Row.Token.Value) != SearchMatches.end();
        if (TextMatches && (!ClassificationFilter.has_value() || Row.Classification == *ClassificationFilter))
        {
            Section->Rows.push_back(Row);
        }
    }

    std::stable_sort(Groups.begin(), Groups.end(), [&](const SceneDirectoryGroupSection& A,
                                                       const SceneDirectoryGroupSection& B) {
        const auto Rank = [&](const std::string& Group) {
            for (size_t Index = 0; Index < std::size(GroupOrder); ++Index)
            {
                if (Group == GroupOrder[Index])
                {
                    return Index;
                }
            }
            return std::size(GroupOrder);
        };
        return Rank(A.Group) < Rank(B.Group);
    });

    InspectorModel = SceneInspectorModel{};
#if defined(FRONTIER_EDITOR_PROFILE)
    if (Selected.IsValid())
    {
        if (const RecordEditorDescription* Description = Directory.QueryEditorDescription(Selected))
        {
            InspectorModel.HasSelection = true;
            InspectorModel.Group        = Description->Group != nullptr ? Description->Group : "";
            InspectorModel.Category     = Description->Category != nullptr ? Description->Category : "";
            InspectorModel.Colour       = Description->Colour != nullptr ? Description->Colour : "";
            InspectorModel.Name         = Directory.QueryName(Selected);
            const std::string DefaultName = std::string(RecordClassificationName(Inspector.Classification)) +
                                            " " + std::to_string(Selected.Value);
            if (InspectorModel.Name == DefaultName && Description->DisplayName != nullptr &&
                Description->DisplayName[0] != '\0')
            {
                InspectorModel.Name = Description->DisplayName;
            }
            InspectorModel.Description          = Description->Description != nullptr ? Description->Description : "";
            InspectorModel.StableId             = Description->StableId != nullptr ? Description->StableId : "";
            InspectorModel.DedicatedEditorTitle = Description->DedicatedEditorTitle != nullptr ? Description->DedicatedEditorTitle : "";
            InspectorModel.HasEnabled           = Description->Enabled != nullptr;
            InspectorModel.Enabled              = Description->Enabled == nullptr || *Description->Enabled;
            InspectorModel.EnabledPtr           = Description->Enabled;
            InspectorModel.RevisionPtr          = Description->Revision;
            InspectorModel.ScalarFields         = Description->ScalarFields;
            InspectorModel.Switches             = Description->Switches;
            InspectorModel.ChoiceFields         = Description->ChoiceFields;
            InspectorModel.Actions              = Description->Actions;
            InspectorModel.ConstructInfographic = Description->ConstructInfographic;
            InspectorModel.OpenDedicatedEditor  = Description->OpenDedicatedEditor;
        }
    }
#endif

    RebuildVisible();
}

void SceneDirectoryPanel::ConfigureClassificationFilter(std::optional<RecordClassification> Filter)
{
    ClassificationFilter = Filter;
    RebuildVisible();
}

void SceneDirectoryPanel::ConfigureTextFilter(std::string Text)
{
    TextFilter = std::move(Text);
    std::transform(TextFilter.begin(), TextFilter.end(), TextFilter.begin(),
                   [](unsigned char Glyph) { return static_cast<char>(std::tolower(Glyph)); });
    Synchronize(SceneDirectory::Instance());
}

void SceneDirectoryPanel::ConfigureCollapsed(RecordToken Token, bool Collapsed)
{
    if (Collapsed)
    {
        CollapsedTokens.insert(Token.Value);
    }
    else
    {
        CollapsedTokens.erase(Token.Value);
    }
    for (SceneDirectoryRow& Row : Rows)
    {
        if (Row.Token == Token)
        {
            Row.Collapsed = Collapsed;
        }
    }
    RebuildVisible();
}

bool SceneDirectoryPanel::QueryCollapsed(RecordToken Token) const
{
    return CollapsedTokens.find(Token.Value) != CollapsedTokens.end();
}

void SceneDirectoryPanel::ConfigureGroupCollapsed(const std::string& Group, bool Collapsed)
{
    if (Collapsed)
    {
        CollapsedGroups.insert(Group);
    }
    else
    {
        CollapsedGroups.erase(Group);
    }
}

bool SceneDirectoryPanel::QueryGroupCollapsed(const std::string& Group) const
{
    return CollapsedGroups.find(Group) != CollapsedGroups.end();
}

void SceneDirectoryPanel::RequestSpawn(RecordClassification Classification)
{
    PendingSpawn = Classification;
}

std::optional<RecordClassification> SceneDirectoryPanel::TakeSpawnRequest()
{
    std::optional<RecordClassification> Request = PendingSpawn;
    PendingSpawn.reset();
    return Request;
}

void SceneDirectoryPanel::RequestOpenDedicatedEditor(const std::string& StableId)
{
    if (!StableId.empty())
    {
        PendingDedicatedEditorId = StableId;
    }
}

std::optional<std::string> SceneDirectoryPanel::TakeDedicatedEditorRequest()
{
    std::optional<std::string> Req = PendingDedicatedEditorId;
    PendingDedicatedEditorId.reset();
    return Req;
}

void SceneDirectoryPanel::Select(RecordToken Token)
{
    Selected = Token;
}

bool SceneDirectoryPanel::SelectByStableId(const std::string& StableId)
{
    for (const SceneDirectoryRow& Row : Rows)
    {
        if (Row.StableId == StableId)
        {
            Selected = Row.Token;
            Synchronize(SceneDirectory::Instance());
            return true;
        }
    }
    return false;
}

void SceneDirectoryPanel::ClearSelection()
{
    Selected  = RecordToken{};
    Inspector = SceneInspectorSummary{};
}

void SceneDirectoryPanel::RebuildVisible()
{
    Visible.clear();
    const bool Searching = !TextFilter.empty();
    if (Searching)
    {
        std::unordered_set<uint64_t> Matches;
        std::unordered_map<uint64_t, RecordToken> Parents;
        for (const SceneDirectoryRow& Row : Rows)
        {
            Parents.emplace(Row.Token.Value, Row.ParentToken);
        }
        for (const SceneDirectoryRow& Row : Rows)
        {
            std::string Lower = Row.Name + " " + Row.Category;
            std::transform(Lower.begin(), Lower.end(), Lower.begin(),
                           [](unsigned char Glyph) { return static_cast<char>(std::tolower(Glyph)); });
            if (Lower.find(TextFilter) == std::string::npos)
            {
                continue;
            }
            for (RecordToken Token = Row.Token; Token.IsValid();)
            {
                Matches.insert(Token.Value);
                const auto Parent = Parents.find(Token.Value);
                if (Parent == Parents.end())
                {
                    break;
                }
                Token = Parent->second;
            }
        }
        for (const SceneDirectoryRow& Row : Rows)
        {
            if ((!ClassificationFilter.has_value() || Row.Classification == *ClassificationFilter) &&
                Matches.find(Row.Token.Value) != Matches.end())
            {
                Visible.push_back(Row);
            }
        }
        return;
    }

    bool     Folding   = false;
    uint32_t FoldDepth = 0;
    for (const SceneDirectoryRow& Row : Rows)
    {
        const bool ClassOk = !ClassificationFilter.has_value() || Row.Classification == *ClassificationFilter;
        if (Folding && Row.DepthTier > FoldDepth)
        {
            continue;
        }
        Folding = false;
        if (Row.Collapsed)
        {
            Folding   = true;
            FoldDepth = Row.DepthTier;
        }
        if (ClassOk)
        {
            Visible.push_back(Row);
        }
    }
}

} // namespace Frontier
