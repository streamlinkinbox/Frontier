/*==========================================================================================================================================
                                                 SCENEDIRECTORYPANELPRESENTATION.CPP
==========================================================================================================================================*/
// 🧩 The Outliner drawn 1:1 with the demo's EditorShell.ConstructOutliner + FrontierEditor.css: brand, workspace title,
//    Enabled / Disabled tiles, the Outliner heading with search, groups with their folder artwork and chevrons, rows with the
//    demo's glyphs, readouts, state dots and eye toggles, and the footer; plus Frontier's add, delete and *Editor buttons.

#include "Engine/Interface/SceneDirectoryPanel.h"

#include "Engine/Diagnostics/BuildConfiguration.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/ControlTheme.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/Controls/GlyphSymbol.h"
#include "Engine/Interface/DropdownControls.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/SceneDirectory.h"
#include "SceneDirectoryPanelRegistration.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL UTILITIES
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 FrontierEditor.css :root and the outliner rules, in CSS pixels.
constexpr uint32_t ColumnFill    = 0x161616FF;   // --column
constexpr uint32_t EdgeRule      = 0x2A2A2AFF;   // --edge
constexpr uint32_t LiveGreen     = 0x3EC46DFF;   // --on
constexpr uint32_t WarmAmber     = 0xE0A24AFF;   // --warn
constexpr uint32_t BodyInk       = 0xDCDCDCFF;   // body colour
constexpr float    BodyPadTop    = 22.0f;        // .outliner-body padding: 22px 14px 14px
constexpr float    BodyPadSide   = 14.0f;
constexpr float    BodyPadBottom = 14.0f;

// 📝 InspectorPages.js Accent, for records whose type gives no colour.
ImU32 FallbackClassificationTint(RecordClassification Classification) noexcept
{
    switch (Classification)
    {
        case RecordClassification::Environment:  return ResolveCssColour(0xE8C27AFF);
        case RecordClassification::Geometry:     return ResolveCssColour(0x8FB3A4FF);
        case RecordClassification::Illumination: return ResolveCssColour(0x9FB4D6FF);
        case RecordClassification::Camera:       return ResolveCssColour(0x9FB4D6FF);
        case RecordClassification::Physics:      return ResolveCssColour(0xE8833AFF);
        case RecordClassification::Interface:    return ResolveCssColour(0xA8BBEBFF);
        case RecordClassification::Content:      return ResolveCssColour(0xA8BBEBFF);
        default:                                 break;
    }
    return ResolveCssColour(0x848484FF);
}

std::string ResolveGroupLabel(const SceneDirectoryMetadata& Metadata, RecordClassification Classification)
{
    if (!Metadata.GroupName.empty())
    {
        return Metadata.GroupName;
    }
    return std::string(RecordClassificationName(Classification));
}

// 📝 A row is On when its feature switch is on (the demo's Toggle.Read); records without a switch follow the eye.
struct RowSwitch
{
    RecordInspector* Inspector = nullptr;
    bool             Switched  = false;   // the inspector carries the record's Enabled switch
    bool             On        = true;
};

RowSwitch ResolveRowSwitch(InspectorRuntime& Runtime, const SceneDirectoryRow& Row)
{
    RowSwitch Switch;
    Switch.Inspector = Runtime.FindInspector(Row.Token);
    Switch.Switched  = Switch.Inspector != nullptr && Switch.Inspector->QueryToggleSupported();
    Switch.On        = Switch.Switched ? Switch.Inspector->QueryToggleEnabled() : Row.Visible;
    return Switch;
}

// 📝 Text on a CSS line box of LineHeight × Size, its top at Top.
float PlaceText(ImDrawList* DrawList, const EditorTypefaces& Faces, ImFont* Face, float Size, float X, float Top, float LineHeight, ImU32 Colour,
                const char* Text, float Tracking = 0.0f)
{
    return GlyphRun::Rasterize(DrawList, &Faces, Face, Size, ImVec2(X, GlyphRun::ResolveBaseline(Top, Size, LineHeight)), Colour, Text, Tracking,
                               false);
}

float MeasureText(const EditorTypefaces& Faces, ImFont* Face, float Size, const char* Text, float Tracking = 0.0f)
{
    return GlyphRun::Rasterize(nullptr, &Faces, Face, Size, ImVec2(0.0f, 0.0f), 0, Text, Tracking, true);
}

// 📝 The text clipped with an ellipsis to Width (text-overflow: ellipsis).
std::string EllipsizeText(const EditorTypefaces& Faces, ImFont* Face, float Size, const std::string& Text, float Width)
{
    if (MeasureText(Faces, Face, Size, Text.c_str()) <= Width) return Text;
    std::string Clipped = Text;
    while (!Clipped.empty() && MeasureText(Faces, Face, Size, (Clipped + "\xE2\x80\xA6").c_str()) > Width) Clipped.pop_back();
    return Clipped + "\xE2\x80\xA6";
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PANEL REGISTRATION
//------------------------------------------------------------------------------------------------------------------------

DockPanelSpecification MakeSceneDirectoryPanelSpecification(SceneDirectoryPanel& Panel)
{
    DockPanelSpecification Specification;
    Specification.Key              = "Outliner";
    Specification.Caption          = "Outliner";
    Specification.ConstructContent = [&Panel]() { Panel.ConstructInterface(); };
    Specification.Open             = true;
    Specification.WheelScrollOnly  = true;
    return Specification;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    OUTLINER PRESENTATION
//------------------------------------------------------------------------------------------------------------------------

void SceneDirectoryPanel::ConstructInterface()
{
    SceneDirectory&                        Directory = SceneDirectory::Instance();
    InspectorRuntime&                      Runtime   = InspectorRuntime::Instance();
    const InspectorPalette                 Palette   = InspectorPalette::Resolve();
    const EditorTypefaces                  Faces     = EditorTypefaces::Resolve();
    const float                            S         = Runtime.ResolveInterfaceScale();
    static std::array<char, 128>           SearchBuffer{};
    static std::string                     SynchronizedSearch;
    static std::unordered_set<std::string> FoldedGroups;

    const ImVec2 WindowPos  = ImGui::GetWindowPos();
    const ImVec2 CursorTop  = ImGui::GetCursorScreenPos();
    const ImVec2 WindowSize = ImGui::GetWindowSize();
    const float  PanelTop   = CursorTop.y;
    const float  PanelBot   = WindowPos.y + WindowSize.y;
    const float  LeftX      = WindowPos.x + BodyPadSide * S;
    const float  RightX     = WindowPos.x + WindowSize.x - BodyPadSide * S;
    const float  InnerW     = std::max(140.0f * S, RightX - LeftX);
    ImDrawList*  DrawList   = ImGui::GetWindowDrawList();

    DrawList->AddRectFilled(ImVec2(WindowPos.x, PanelTop), ImVec2(WindowPos.x + WindowSize.x, PanelBot), ResolveCssColour(ColumnFill));
    DrawList->AddLine(ImVec2(WindowPos.x + WindowSize.x - 1.0f, PanelTop), ImVec2(WindowPos.x + WindowSize.x - 1.0f, PanelBot),
                      ResolveCssColour(EdgeRule), 1.0f);

    float CursorY = PanelTop + BodyPadTop * S;

    // -- .outliner-heading: "Outliner" + count, and Frontier's '+' add record menu ------------------------------------
    {
        const float LineTall = 12.0f * 1.35f * S;
        const float TitleW   = PlaceText(DrawList, Faces, Faces.Medium, 12.0f * S, LeftX + 10.0f * S, CursorY, 1.35f, ResolveCssColour(BodyInk),
                                         "Outliner");
        char Count[24] = {};
        std::snprintf(Count, sizeof(Count), "%02u", QueryTotal());
        const float CountW  = MeasureText(Faces, Faces.Regular, 10.0f * S, Count);
        const float BadgeX  = LeftX + 10.0f * S + TitleW + 6.0f * S;
        const float BadgeH  = (10.0f * 1.35f + 4.0f) * S + 2.0f * S;
        const float BadgeY  = CursorY + 0.5f * (LineTall - BadgeH);
        DrawList->AddRect(ImVec2(BadgeX, BadgeY), ImVec2(BadgeX + CountW + 12.0f * S, BadgeY + BadgeH), ResolveCssColour(0x313131FF), 5.0f * S, 0, S);
        PlaceText(DrawList, Faces, Faces.Regular, 10.0f * S, BadgeX + 6.0f * S, BadgeY + 3.0f * S, 1.35f, ResolveCssColour(0x7E7E7EFF), Count);

        const float  AddSide = 22.0f * S;
        const ImVec2 AddMin(RightX - 10.0f * S - AddSide, CursorY + 0.5f * (LineTall - AddSide));
        ImGui::SetCursorScreenPos(AddMin);
        if (ImGui::InvisibleButton("##AddSceneRecordBtn", ImVec2(AddSide, AddSide)))
        {
            if (ConstructDialogOffered) ConstructDialogRequested = true;
            else                        ImGui::OpenPopup("##OutlinerAddRecordPopup");
        }
        const bool AddHovered = ImGui::IsItemHovered();
        DrawList->AddRectFilled(AddMin, ImVec2(AddMin.x + AddSide, AddMin.y + AddSide),
                                AddHovered ? ResolveCssColour(0x2A2A2AFF) : ResolveCssColour(0x1D1D1DFF), 6.0f * S);
        DrawList->AddRect(AddMin, ImVec2(AddMin.x + AddSide, AddMin.y + AddSide), ResolveCssColour(0x2C2C2CFF), 6.0f * S, 0, S);
        (void)GlyphSymbol::Construct(DrawList, "Plus", ImVec2(AddMin.x + 4.0f * S, AddMin.y + 4.0f * S), 14.0f * S, 1.7f,
                                     AddHovered ? ResolveCssColour(0xF0F0F0FF) : ResolveCssColour(0xA9A9A9FF));
        if (AddHovered) ImGui::SetTooltip(ConstructDialogOffered ? "Construct an object or add a record (Ctrl+A)" : "Add a record to the level");

        ImGui::SetNextWindowPos(ImVec2(RightX - 210.0f * S, AddMin.y + AddSide + 2.0f * S), ImGuiCond_Appearing);
        if (DropdownControlSurface::BeginMenu("##OutlinerAddRecordPopup"))
        {
            if (!ConstructibleTypes.empty())
            {
                for (const auto& [TypeId, DisplayLabel] : ConstructibleTypes)
                {
                    if (DropdownControlSurface::Option(DisplayLabel.c_str()))
                    {
                        RequestConstructRecord(TypeId);
                    }
                }
            }
            else
            {
                for (uint32_t Index = 0; Index < static_cast<uint32_t>(RecordClassification::Count); ++Index)
                {
                    const auto Classification = static_cast<RecordClassification>(Index);
                    char OptionLabel[64] = {};
                    std::snprintf(OptionLabel, sizeof(OptionLabel), "Add %s", RecordClassificationName(Classification));
                    if (DropdownControlSurface::Option(OptionLabel))
                    {
                        RequestSpawn(Classification);
                    }
                }
            }
            DropdownControlSurface::EndMenu();
        }
        CursorY += LineTall + 10.0f * S;
    }

    // -- .search: "Find an object…" -------------------------------------------------------------------------------------
    {
        // 📝 .outliner-body is a flex column: when its content overflows, the search (height 34px) is the one item that can
        //    shrink, down to its content (the 11px input in a bordered box, 18px); the tree never shrinks.
        float TreeContent = 0.0f;
        {
            std::vector<std::pair<std::string, uint32_t>> Groups;
            for (const SceneDirectoryRow& Row : QueryVisible())
            {
                const std::string Group = ResolveGroupLabel(Directory.QueryMetadata(Row.Token), Row.Classification);
                auto Found = std::find_if(Groups.begin(), Groups.end(), [&Group](const auto& Entry) { return Entry.first == Group; });
                if (Found == Groups.end()) Groups.emplace_back(Group, 1u);
                else                       ++Found->second;
            }
            for (const auto& [Group, Members] : Groups)
            {
                const bool Artwork = Group == "Environment" || Group == "Forcefield";
                const bool Folded  = FoldedGroups.find(Group) != FoldedGroups.end() && QueryTextFilter().empty();
                TreeContent += (std::max(Artwork ? 18.0f : 12.0f, 10.0f * 1.35f) + 8.0f + (Folded ? 0.0f : 40.0f * static_cast<float>(Members)) + 16.0f) * S;
            }
        }
        const float FooterSpan = 0.0f;
        const float Overflow   = TreeContent - ((PanelBot - FooterSpan) - (CursorY + (34.0f + 18.0f) * S));
        const float SearchH    = std::max(18.0f * S, 34.0f * S - std::max(0.0f, Overflow));
        const ImVec2 SearchMin(LeftX + 4.0f * S, CursorY);
        const ImVec2 SearchMax(RightX - 4.0f * S, CursorY + SearchH);
        DrawList->AddRectFilled(SearchMin, SearchMax, ResolveCssColour(0x1D1D1DFF), 8.0f * S);
        DrawList->AddRect(SearchMin, SearchMax, ResolveCssColour(0x282828FF), 8.0f * S, 0, S);
        (void)GlyphSymbol::Construct(DrawList, "Search", ImVec2(SearchMin.x + 12.0f * S, CursorY + 0.5f * SearchH - 7.0f * S), 14.0f * S, 1.7f,
                                     ResolveCssColour(0x707070FF));

        if (QueryTextFilter() != SynchronizedSearch)
        {
            std::snprintf(SearchBuffer.data(), SearchBuffer.size(), "%s", QueryTextFilter().c_str());
            SynchronizedSearch = QueryTextFilter();
        }

        const float FieldX = SearchMin.x + (12.0f + 14.0f + 9.0f) * S;
        ImGui::SetCursorScreenPos(ImVec2(FieldX, CursorY + 0.5f * (SearchH - 11.0f * GlyphRun::EmPixelHeight * S)));
        ImGui::PushFont(Faces.Regular, 11.0f * GlyphRun::EmPixelHeight * S / std::max(0.01f, ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ResolveCssColour(0xCCCCCCFF)));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::ColorConvertU32ToFloat4(ResolveCssColour(0x767676FF)));
        ImGui::SetNextItemWidth(std::max(40.0f, SearchMax.x - 11.0f * S - FieldX));
        if (ImGui::InputTextWithHint("##OutlinerSearchField", "Find an object\xE2\x80\xA6", SearchBuffer.data(), SearchBuffer.size()))
        {
            ConfigureTextFilter(SearchBuffer.data());
            SynchronizedSearch = QueryTextFilter();
        }
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
        CursorY += SearchH + 18.0f * S;
    }

    // -- .tree: groups and rows (scrolls between the search and the footer) --------------------------------------------
    const float FooterHeight = 0.0f;
    const float TreeHeight   = std::max(60.0f * S, (PanelBot - FooterHeight) - CursorY);

    ImGui::SetCursorScreenPos(ImVec2(LeftX, CursorY));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    // 📝 No scrollbar (owner's request): the wheel scrolls the tree, eased, with Patch H's overscroll spring.
    if (ImGui::BeginChild("##SceneDirectoryGroupedTree", ImVec2(InnerW, TreeHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar))
    {
        ImDrawList* NestedDraw  = ImGui::GetWindowDrawList();
        const auto& VisibleRows = QueryVisible();
        bool        NeedsSync   = false;
        const float TreeW       = ImGui::GetContentRegionAvail().x;

        std::vector<std::string> OrderedGroups;
        for (const SceneDirectoryRow& Row : VisibleRows)
        {
            const std::string Group = ResolveGroupLabel(Directory.QueryMetadata(Row.Token), Row.Classification);
            if (std::find(OrderedGroups.begin(), OrderedGroups.end(), Group) == OrderedGroups.end())
            {
                OrderedGroups.push_back(Group);
            }
        }

        for (const std::string& GroupName : OrderedGroups)
        {
            uint32_t GroupMemberCount = 0;
            for (const SceneDirectoryRow& Row : VisibleRows)
            {
                if (ResolveGroupLabel(Directory.QueryMetadata(Row.Token), Row.Classification) == GroupName)
                {
                    ++GroupMemberCount;
                }
            }

            // 📝 .group-label: [folder artwork 18 px for Environment / Forcefield] chevron 12 px, the name, the count.
            const bool   GroupFolded = (FoldedGroups.find(GroupName) != FoldedGroups.end()) && QueryTextFilter().empty();
            const bool   Artwork     = GroupName == "Environment" || GroupName == "Forcefield";
            const float  LabelTall   = std::max(Artwork ? 18.0f : 12.0f, 10.0f * 1.35f) * S;
            const ImVec2 GroupOrigin = ImGui::GetCursorScreenPos();
            ImGui::PushID(GroupName.c_str());
            if (ImGui::InvisibleButton("##GroupFoldBtn", ImVec2(TreeW, LabelTall + 8.0f * S)))
            {
                if (FoldedGroups.find(GroupName) != FoldedGroups.end()) FoldedGroups.erase(GroupName);
                else                                                     FoldedGroups.insert(GroupName);
            }
            float      LabelX       = GroupOrigin.x + 10.0f * S;
            if (Artwork)
            {
                (void)GlyphSymbol::Construct(NestedDraw, "NativeEnvironment", ImVec2(LabelX, GroupOrigin.y), 18.0f * S, 1.6f, IM_COL32_WHITE);
                LabelX += (18.0f + 7.0f) * S;
            }
            const ImU32 LabelInk = ResolveCssColour(0x818181FF);
            (void)GlyphSymbol::Construct(NestedDraw, GroupFolded ? "ChevronRight" : "ChevronDown",
                                         ImVec2(LabelX, GroupOrigin.y + 0.5f * (LabelTall - 12.0f * S)), 12.0f * S, 1.8f, LabelInk);
            LabelX += (12.0f + 7.0f) * S;
            const float TextTop = GroupOrigin.y + 0.5f * (LabelTall - 10.0f * 1.35f * S);
            PlaceText(NestedDraw, Faces, Faces.Regular, 10.0f * S, LabelX, TextTop, 1.35f, LabelInk, GroupName.c_str(), 0.3f * S);
            char GroupCountText[16] = {};
            std::snprintf(GroupCountText, sizeof(GroupCountText), "%u", GroupMemberCount);
            const float CountW = MeasureText(Faces, Faces.Regular, 9.0f * S, GroupCountText);
            PlaceText(NestedDraw, Faces, Faces.Regular, 9.0f * S, GroupOrigin.x + TreeW - 10.0f * S - CountW,
                      GroupOrigin.y + 0.5f * (LabelTall - 9.0f * 1.35f * S), 1.35f, ResolveCssColour(0x575757FF), GroupCountText);
            ImGui::PopID();
            ImGui::SetCursorScreenPos(ImVec2(GroupOrigin.x, GroupOrigin.y + LabelTall + 8.0f * S));

            if (!GroupFolded)
            {
                for (const SceneDirectoryRow& Row : VisibleRows)
                {
                    const SceneDirectoryMetadata Meta = Directory.QueryMetadata(Row.Token);
                    if (ResolveGroupLabel(Meta, Row.Classification) != GroupName)
                    {
                        continue;
                    }

                    // 📝 .tree-row: margin 2px 2px, height 38px, radius 8px, a 1 px border.
                    const ImVec2 Slot        = ImGui::GetCursorScreenPos();
                    const ImVec2 RowMin(Slot.x + 2.0f * S, Slot.y + 2.0f * S);
                    const float  RowH        = 38.0f * S;
                    const ImVec2 RowMax(Slot.x + TreeW - 2.0f * S, RowMin.y + RowH);
                    const bool   SelectedRow = (QuerySelected() == Row.Token);
                    const ImU32  EntityTint  = ParseHexSpectrum(Meta.AccentHex, FallbackClassificationTint(Row.Classification));
                    const RowSwitch Switch   = ResolveRowSwitch(Runtime, Row);
                    const bool   On          = Switch.On;

                    ImGui::PushID(static_cast<int>(Row.Token.Value));

                    const bool  HasEditor  = Runtime.QueryHasEditor(Row.Token);
                    const bool  Removable  = Meta.RemovableInEditor;
                    const float EyeW       = 30.0f * S;
                    const float TrashW     = (Removable && SelectedRow) ? 22.0f * S : 0.0f;
                    const float EditorBtnW = HasEditor ? 22.0f * S : 0.0f;
                    const float StateW     = (2.0f + 7.0f + 4.0f) * S;
                    const float ButtonW    = std::max(40.0f * S, (RowMax.x - RowMin.x) - EyeW - StateW - TrashW - EditorBtnW);

                    // 📝 While the row is renamed its name is a text field, so the row button stays out of its way.
                    const bool Renamed    = Renaming == Row.Token;
                    bool       RowClicked = false, RowDouble = false, RowMenu = false;
                    if (!Renamed)
                    {
                        ImGui::SetCursorScreenPos(RowMin);
                        RowClicked = ImGui::InvisibleButton("##SelectRow", ImVec2(ButtonW, RowH), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
                        RowMenu    = RowClicked && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
                        RowClicked = RowClicked && !RowMenu;
                        RowDouble  = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
                    }
                    const bool RowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseHoveringRect(RowMin, RowMax);
                    if (RowMenu)
                    {
                        Select(Row.Token);
                        NeedsSync = true;
                        ImGui::OpenPopup("##RowMenu");
                    }
                    // 📝 The Project Zero editor's row menu: Rename, Hide, Fracture (not yet: no action), Duplicate, Remove.
                    if (DropdownControlSurface::BeginMenu("##RowMenu"))
                    {
                        if (DropdownControlSurface::Option("Rename        F2")) BeginRename(Row.Token);
                        if (DropdownControlSurface::Option(On ? "Hide" : "Show"))
                        {
                            if (Switch.Switched) Switch.Inspector->ConfigureToggle(!On);
                            else                 (void)Directory.ConfigureVisible(Row.Token, !Row.Visible);
                            NeedsSync = true;
                        }
                        (void)DropdownControlSurface::Option("Fracture");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fracture is not available yet: the physics (Jolt) comes first");
                        if (Meta.RemovableInEditor)
                        {
                            if (DropdownControlSurface::Option("Duplicate     Ctrl+D")) RequestDuplicateRecord(Row.Token);
                            if (DropdownControlSurface::Option("Remove        Del")) RequestDeleteRecord(Row.Token);
                        }
                        DropdownControlSurface::EndMenu();
                    }

                    if (SelectedRow)
                    {
                        // 📝 linear-gradient(110deg, #333, #292929), border #434343, inset 0 1px #ffffff06.
                        NestedDraw->AddRectFilledMultiColor(ImVec2(RowMin.x + 8.0f * S, RowMin.y), ImVec2(RowMax.x - 8.0f * S, RowMax.y),
                                                            ResolveCssColour(0x333333FF), ResolveCssColour(0x292929FF),
                                                            ResolveCssColour(0x292929FF), ResolveCssColour(0x313131FF));
                        NestedDraw->AddRectFilled(RowMin, ImVec2(RowMin.x + 8.0f * S, RowMax.y), ResolveCssColour(0x333333FF), 8.0f * S,
                                                  ImDrawFlags_RoundCornersLeft);
                        NestedDraw->AddRectFilled(ImVec2(RowMax.x - 8.0f * S, RowMin.y), RowMax, ResolveCssColour(0x292929FF), 8.0f * S,
                                                  ImDrawFlags_RoundCornersRight);
                        NestedDraw->AddRect(RowMin, RowMax, ResolveCssColour(0x434343FF), 8.0f * S, 0, S);
                        NestedDraw->AddLine(ImVec2(RowMin.x + 8.0f * S, RowMin.y + 1.5f * S), ImVec2(RowMax.x - 8.0f * S, RowMin.y + 1.5f * S),
                                            ResolveCssColour(0xFFFFFF06), S);
                        // 📝 ::before: 2 × 16 px at left −4px, top 10px from the padding box, the entity colour (outside the row,
                        //    so outside the tree's clip rectangle).
                        NestedDraw->PushClipRect(ImVec2(RowMin.x - 6.0f * S, RowMin.y), RowMax, false);
                        NestedDraw->AddRectFilled(ImVec2(RowMin.x - 3.0f * S, RowMin.y + 11.0f * S), ImVec2(RowMin.x - 1.0f * S, RowMin.y + 27.0f * S),
                                                  EntityTint, 2.0f * S);
                        NestedDraw->PopClipRect();
                    }
                    else if (RowHovered)
                    {
                        NestedDraw->AddRectFilled(RowMin, RowMax, ResolveCssColour(0x202020FF), 8.0f * S);
                    }

                    // 📝 .object-button: padding-left 16px (+22px per nesting tier: .nested 38px), gap 11px, 12px text.
                    const float Fade   = On ? 1.0f : 0.45f;
                    float       ItemX  = RowMin.x + (16.0f + 22.0f * static_cast<float>(Row.DepthTier)) * S;
                    const float Middle = RowMin.y + 0.5f * RowH;

                    if (Row.ChildCount > 0 && (RowHovered || Row.Collapsed))
                    {
                        // 📝 Frontier's subtree fold, shown on hover (the demo has no subtrees to fold).
                        ImGui::SetCursorScreenPos(ImVec2(ItemX - 14.0f * S, Middle - 8.0f * S));
                        if (ImGui::InvisibleButton("##SubtreeFold", ImVec2(12.0f * S, 16.0f * S)))
                        {
                            ConfigureCollapsed(Row.Token, !Row.Collapsed);
                        }
                        (void)GlyphSymbol::Construct(NestedDraw, Row.Collapsed ? "ChevronRight" : "ChevronDown",
                                                     ImVec2(ItemX - 14.0f * S, Middle - 6.0f * S), 12.0f * S, 1.8f, ResolveCssColour(0x7D7D7DFF));
                    }

                    const char* Glyph = Switch.Inspector != nullptr ? Switch.Inspector->QueryDirectoryGlyph() : nullptr;
                    if (Glyph == nullptr) Glyph = Meta.GlyphIdentifier.empty() ? "Box" : Meta.GlyphIdentifier.c_str();
                    const ImU32 GlyphInk = FadeColour(ResolveAlpha(EntityTint, 0.85f), Fade);
                    if (GlyphSymbol::QueryArtwork(Glyph))
                    {
                        // 📝 .tree-row .native-glyph { transform: scale(1.5) } about the 16 px box's centre.
                        (void)GlyphSymbol::Construct(NestedDraw, Glyph, ImVec2(ItemX - 4.0f * S, Middle - 12.0f * S), 24.0f * S, 1.6f,
                                                     FadeColour(IM_COL32(255, 255, 255, 217), Fade));
                    }
                    else if (!GlyphSymbol::Construct(NestedDraw, Glyph, ImVec2(ItemX, Middle - 8.0f * S), 16.0f * S, 1.6f, GlyphInk))
                    {
                        ConstructEditorGlyph(NestedDraw, ImVec2(ItemX + 8.0f * S, Middle), 6.8f * S, GlyphInk, Glyph, 1.4f);
                    }
                    ItemX += (16.0f + 11.0f) * S;

                    // 📝 .row-meta (9px #666) at the button's right padding; the name takes what is left.
                    std::string Readout = Switch.Inspector != nullptr ? Switch.Inspector->QueryDirectoryReadout() : std::string();
                    if (Readout.empty()) Readout = Meta.LiveMetadata;
                    const float ButtonRight = RowMin.x + ButtonW - 6.0f * S;
                    float       NameRight   = ButtonRight;
                    if (!Readout.empty())
                    {
                        const float ReadoutW = MeasureText(Faces, Faces.Regular, 9.0f * S, Readout.c_str());
                        PlaceText(NestedDraw, Faces, Faces.Regular, 9.0f * S, ButtonRight - ReadoutW, Middle - 0.5f * 9.0f * 1.35f * S, 1.35f,
                                  FadeColour(ResolveCssColour(0x666666FF), Fade), Readout.c_str());
                        NameRight = ButtonRight - ReadoutW - 11.0f * S;
                    }
                    const ImU32 NameInk = FadeColour(SelectedRow ? ResolveCssColour(0xF1F1F1FF) : ResolveCssColour(0xA3A3A3FF), Fade);
                    if (Renamed)
                    {
                        // 📝 Inline rename: Enter or leaving the field keeps the name, Escape drops it.
                        static std::array<char, 128> RenameBuffer{};
                        if (RenameFocus) std::snprintf(RenameBuffer.data(), RenameBuffer.size(), "%s", Row.Name.c_str());
                        const float FieldH = 12.0f * 1.35f * S + 6.0f * S;
                        NestedDraw->AddRectFilled(ImVec2(ItemX - 4.0f * S, Middle - 0.5f * FieldH), ImVec2(NameRight, Middle + 0.5f * FieldH),
                                                  ResolveCssColour(0x151515FF), 6.0f * S);
                        NestedDraw->AddRect(ImVec2(ItemX - 4.0f * S, Middle - 0.5f * FieldH), ImVec2(NameRight, Middle + 0.5f * FieldH),
                                            ResolveCssColour(0x4A4A4AFF), 6.0f * S, 0, S);
                        ImGui::SetCursorScreenPos(ImVec2(ItemX, Middle - 0.5f * 12.0f * GlyphRun::EmPixelHeight * S));
                        ImGui::PushFont(Faces.Regular, 12.0f * GlyphRun::EmPixelHeight * S /
                                                           std::max(0.01f, ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi));
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ResolveCssColour(0xF1F1F1FF)));
                        ImGui::SetNextItemWidth(std::max(40.0f * S, NameRight - ItemX - 6.0f * S));
                        if (RenameFocus) ImGui::SetKeyboardFocusHere();
                        const bool Entered = ImGui::InputText("##RenameRow", RenameBuffer.data(), RenameBuffer.size(),
                                                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                        const bool Escaped = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
                        const bool Left    = !RenameFocus && !ImGui::IsItemActive();
                        ImGui::PopStyleColor(2);
                        ImGui::PopStyleVar(2);
                        ImGui::PopFont();
                        RenameFocus = false;
                        if (Escaped) Renaming = RecordToken{};
                        else if (Entered || Left)
                        {
                            if (std::string(RenameBuffer.data()) != Row.Name) RequestRenameRecord(Row.Token, RenameBuffer.data());
                            Renaming = RecordToken{};
                        }
                    }
                    else
                    {
                        const std::string Name = EllipsizeText(Faces, Faces.Regular, 12.0f * S, Row.Name, std::max(0.0f, NameRight - ItemX));
                        PlaceText(NestedDraw, Faces, Faces.Regular, 12.0f * S, ItemX, Middle - 0.5f * 12.0f * 1.35f * S, 1.35f, NameInk, Name.c_str());
                    }

                    float RightCursor = RowMin.x + ButtonW;

                    // Frontier: the *Editor launch button [↗] and the delete button on a selected removable row.
                    if (HasEditor)
                    {
                        ImGui::SetCursorScreenPos(ImVec2(RightCursor, RowMin.y + 6.0f * S));
                        if (ImGui::InvisibleButton("##RowEditorBtn", ImVec2(EditorBtnW, RowH - 12.0f * S)) || RowDouble)
                        {
                            Select(Row.Token);
                            (void)Runtime.RequestEditorOpen(Row.Token);
                            NeedsSync = true;
                        }
                        const bool EditorHover = ImGui::IsItemHovered();
                        ConstructEditorGlyph(NestedDraw, ImVec2(RightCursor + EditorBtnW * 0.5f, Middle), 5.0f * S,
                                             EditorHover ? Palette.ThemeAccent : ResolveCssColour(0x6A6A6AFF), "EditorLaunch", 1.25f);
                        if (EditorHover) ImGui::SetTooltip("Open the editor");
                        RightCursor += EditorBtnW;
                    }
                    if (TrashW > 0.0f)
                    {
                        ImGui::SetCursorScreenPos(ImVec2(RightCursor, RowMin.y + 6.0f * S));
                        if (ImGui::InvisibleButton("##RowDeleteBtn", ImVec2(TrashW, RowH - 12.0f * S)))
                        {
                            RequestDeleteRecord(Row.Token);
                        }
                        const bool TrashHover = ImGui::IsItemHovered();
                        (void)GlyphSymbol::Construct(NestedDraw, "Trash2", ImVec2(RightCursor + 0.5f * TrashW - 6.5f * S, Middle - 6.5f * S), 13.0f * S,
                                                     1.7f, TrashHover ? Palette.DangerRed : ResolveCssColour(0x6A6A6AFF));
                        if (TrashHover) ImGui::SetTooltip("Delete this record");
                        RightCursor += TrashW;
                    }

                    // 📝 .row-state: 7 px, margin 0 4px 0 2px; live --on, warm --warn, otherwise #3a3a3a (always when off).
                    ImU32 DotColour = ResolveCssColour(0x3A3A3AFF);
                    if (On)
                    {
                        const RecordOperationalCondition Condition =
                            Switch.Inspector != nullptr ? Switch.Inspector->QueryDirectoryCondition() : Meta.OperationalCondition;
                        if (Condition == RecordOperationalCondition::Live) DotColour = ResolveCssColour(LiveGreen);
                        if (Condition == RecordOperationalCondition::Warm) DotColour = ResolveCssColour(WarmAmber);
                    }
                    NestedDraw->AddCircleFilled(ImVec2(RightCursor + (2.0f + 3.5f) * S, Middle), 3.5f * S, DotColour, 16);
                    RightCursor += StateW;

                    // 📝 .visibility: 30 px, the eye (13 px, stroke 1.7) shown on hover, selection, or when off. It flips the
                    //    record's feature switch (the demo's Toggle) — or, for records without one, the Outliner eye.
                    ImGui::SetCursorScreenPos(ImVec2(RightCursor, RowMin.y));
                    if (ImGui::InvisibleButton("##EyeToggle", ImVec2(EyeW, RowH)))
                    {
                        if (Switch.Switched) Switch.Inspector->ConfigureToggle(!On);
                        else                 (void)Directory.ConfigureVisible(Row.Token, !Row.Visible);
                        NeedsSync = true;
                    }
                    const bool EyeHovered = ImGui::IsItemHovered();
                    if (RowHovered || SelectedRow || !On || EyeHovered)
                    {
                        (void)GlyphSymbol::Construct(NestedDraw, On ? "Eye" : "EyeOff", ImVec2(RightCursor + 0.5f * EyeW - 6.5f * S, Middle - 6.5f * S),
                                                     13.0f * S, 1.7f, ResolveCssColour(0x6A6A6AFF));
                    }
                    if (EyeHovered) ImGui::SetTooltip("%s %s", On ? "Disable" : "Enable", Row.Name.c_str());

                    if (RowClicked)
                    {
                        Select(Row.Token);
                        NeedsSync = true;
                    }

                    ImGui::PopID();
                    // 📝 Sibling block margins collapse: the next row's 2 px top margin is this row's bottom margin.
                    ImGui::SetCursorScreenPos(ImVec2(Slot.x, RowMax.y));
                    ImGui::Dummy(ImVec2(TreeW, 0.0f));
                }
            }
            // 📝 .group { margin-bottom: 16px } (the last row's 2 px margin collapses into it)
            ImGui::Dummy(ImVec2(TreeW, (GroupFolded ? 16.0f : 14.0f) * S));
        }

        if (NeedsSync)
        {
            Synchronize(Directory);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    ImGui::SetCursorScreenPos(ImVec2(LeftX, PanelTop));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
}

} // namespace Frontier

#else // !(FRONTIER_EDITOR && FRONTIER_WITH_INTERFACE_SDK)

namespace Frontier
{

void SceneDirectoryPanel::ConstructInterface()
{
}

} // namespace Frontier

#endif // FRONTIER_EDITOR && FRONTIER_WITH_INTERFACE_SDK
