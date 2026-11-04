/*==========================================================================================================================================
                                                           CONSTRUCTPANEL.CPP
==========================================================================================================================================*/
// 🧩 The Construct dialog drawn after Editor.css: .construct-dialog 860 × 760 (#171717, 1 px #373737, radius 12), the 30 px
//    title bar, the CONSTRUCT / FRONTIER path, then either the search, category rail and entity matrix (tiles 155 px wide,
//    116 px tall) or the chosen entity's name and properties, and the footer with Cancel and "Add to scene".

#include "Engine/Interface/ConstructPanel.h"

#include "Engine/Diagnostics/BuildConfiguration.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/CaptionText.h"
#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/ControlTheme.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/Controls/GlyphSymbol.h"
#include "Engine/Interface/Controls/SliderRow.h"
#include "Engine/Interface/Controls/SwitchRow.h"
#include "Engine/Interface/Controls/TintRow.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier
{

namespace
{

constexpr const char* DialogKey = "##ConstructDialog";
constexpr const char* Groups[]  = {"All", "Environment", "Weather", "Forces", "Cameras", "Geometry", "Lighting"};

// 📝 Text on a CSS line box of LineHeight × Size, its top at Top; the advance.
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

std::string LowerText(std::string Text)
{
    std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char Glyph) { return static_cast<char>(std::tolower(Glyph)); });
    return Text;
}

// 📝 .construct-dialog > footer button: 1 px #373737 over #262626, radius 8, 12 px; the add button #333 over #626262.
bool ConstructFooterButton(ImDrawList* DrawList, const EditorTypefaces& Faces, const char* Key, const char* Caption, const char* Glyph, ImVec2 Min,
                           float S, bool Accent)
{
    const float  TextW  = MeasureText(Faces, Faces.Regular, 12.0f * S, Caption);
    const float  Width  = TextW + (Glyph != nullptr ? 22.0f : 0.0f) * S + 28.0f * S;
    const ImVec2 Extent(Width, 34.0f * S);
    ImGui::SetCursorScreenPos(Min);
    const bool   Pressed = ImGui::InvisibleButton(Key, Extent);
    const bool   Hovered = ImGui::IsItemHovered();
    const ImVec2 Max(Min.x + Extent.x, Min.y + Extent.y);
    DrawList->AddRectFilled(Min, Max, ResolveCssColour(Accent ? (Hovered ? 0x3D3D3DFF : 0x333333FF) : (Hovered ? 0x343434FF : 0x262626FF)), 8.0f * S);
    DrawList->AddRect(Min, Max, ResolveCssColour(Accent ? 0x626262FF : 0x373737FF), 8.0f * S, 0, S);
    PlaceText(DrawList, Faces, Faces.Regular, 12.0f * S, Min.x + 14.0f * S, Min.y + 0.5f * (Extent.y - 12.0f * 1.35f * S), 1.35f,
              ResolveCssColour(Accent ? 0xEEEEEEFF : 0xCFCFCFFF), Caption);
    if (Glyph != nullptr)
        (void)GlyphSymbol::Construct(DrawList, Glyph, ImVec2(Min.x + 14.0f * S + TextW + 8.0f * S, Min.y + 10.0f * S), 14.0f * S, 1.8f,
                                     ResolveCssColour(0xEEEEEEFF));
    return Pressed;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    OPENING
//------------------------------------------------------------------------------------------------------------------------

void ConstructPanel::Open()
{
    Open_        = true;
    Raise_       = true;
    Picked_      = -1;
    FocusSearch_ = true;
}

void ConstructPanel::Open(std::string_view Group)
{
    Open();
    Group_ = std::string(Group);
}

void ConstructPanel::Close()
{
    Dismiss_ = Open_ && !Raise_;
    Open_    = false;
    Raise_   = false;
    Picked_  = -1;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DIALOG
//------------------------------------------------------------------------------------------------------------------------

std::optional<ConstructOrder> ConstructPanel::Construct(float Scale)
{
    if (!Open_)
    {
        // 📝 Closed from outside while the modal was up: it is submitted once more to close it.
        if (Dismiss_ && ImGui::BeginPopupModal(DialogKey, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        Dismiss_ = false;
        return std::nullopt;
    }
    const float           S     = std::max(0.5f, Scale);
    const EditorTypefaces Faces = EditorTypefaces::Resolve();
    if (Raise_)
    {
        ImGui::OpenPopup(DialogKey);
        Raise_ = false;
    }

    // 📝 .modal-scrim (#0007) and .construct-dialog: min(860px, 100vw − 24px) × min(760px, 100dvh − 24px), centred.
    const ImGuiViewport* Main = ImGui::GetMainViewport();
    const ImVec2         Extent(std::min(860.0f * S, Main->WorkSize.x - 24.0f * S), std::min(760.0f * S, Main->WorkSize.y - 24.0f * S));
    ImGui::SetNextWindowPos(ImVec2(Main->WorkPos.x + 0.5f * Main->WorkSize.x, Main->WorkPos.y + 0.5f * Main->WorkSize.y), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(Extent, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.4667f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(ResolveCssColour(0x171717FF)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(ResolveCssColour(0x373737FF)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0f * S);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    const bool Shown = ImGui::BeginPopupModal(DialogKey, nullptr,
                                              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(3);
    if (!Shown)
    {
        Open_   = false;
        Picked_ = -1;
        return std::nullopt;
    }

    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Origin   = ImGui::GetWindowPos();
    const float  W        = Extent.x;
    const float  H        = Extent.y;
    bool         Closing  = false;
    std::optional<ConstructOrder> Order;

    // ① .construct-titlebar: 30 px, #121212, 1 px #303030 below; "Construct" and the round close button
    DrawList->AddRectFilled(Origin, ImVec2(Origin.x + W, Origin.y + 30.0f * S), ResolveCssColour(0x121212FF), 12.0f * S, ImDrawFlags_RoundCornersTop);
    DrawList->AddLine(ImVec2(Origin.x, Origin.y + 30.0f * S), ImVec2(Origin.x + W, Origin.y + 30.0f * S), ResolveCssColour(0x303030FF), S);
    PlaceText(DrawList, Faces, Faces.Medium, 12.0f * S, Origin.x + 16.0f * S, Origin.y + 0.5f * (30.0f - 12.0f * 1.35f) * S, 1.35f,
              ResolveCssColour(0xDCDCDCFF), "Construct");
    {
        const ImVec2 Min(Origin.x + W - 10.0f * S - 22.0f * S, Origin.y + 4.0f * S);
        ImGui::SetCursorScreenPos(Min);
        if (ImGui::InvisibleButton("##ConstructClose", ImVec2(22.0f * S, 22.0f * S))) Closing = true;
        const bool Hovered = ImGui::IsItemHovered();
        if (Hovered) DrawList->AddCircleFilled(ImVec2(Min.x + 11.0f * S, Min.y + 11.0f * S), 11.0f * S, ResolveCssColour(0x2A2A2AFF), 24);
        (void)GlyphSymbol::Construct(DrawList, "X", ImVec2(Min.x + 4.0f * S, Min.y + 4.0f * S), 14.0f * S, 1.8f,
                                     ResolveCssColour(Hovered ? 0xF0F0F0FF : 0x9A9A9AFF));
    }

    // ② .construct-path: CONSTRUCT / FRONTIER, 01 Entities › 02 Properties, Ctrl+A
    const float PathTop = Origin.y + 30.0f * S;
    {
        const float LineTop = PathTop + 0.5f * (48.0f - 12.0f * 1.35f) * S;
        float       Pen     = Origin.x + 20.0f * S;
        Pen += PlaceText(DrawList, Faces, Faces.Regular, 12.0f * S, Pen, LineTop, 1.35f, ResolveCssColour(0xD8D8D8FF), "CONSTRUCT / FRONTIER", 0.5f * S);
        Pen += (10.0f + 12.0f) * S;
        const float StepTop = PathTop + 0.5f * (48.0f - 11.0f * 1.35f) * S;
        Pen += PlaceText(DrawList, Faces, Faces.Regular, 11.0f * S, Pen, StepTop, 1.35f, ResolveCssColour(Picked_ < 0 ? 0xC5C5C5FF : 0x707070FF),
                         "01 Entities");
        Pen += 12.0f * S;
        (void)GlyphSymbol::Construct(DrawList, "ChevronRight", ImVec2(Pen, PathTop + 18.0f * S), 12.0f * S, 1.8f, ResolveCssColour(0x707070FF));
        Pen += (12.0f + 12.0f) * S;
        (void)PlaceText(DrawList, Faces, Faces.Regular, 11.0f * S, Pen, StepTop, 1.35f, ResolveCssColour(Picked_ >= 0 ? 0xC5C5C5FF : 0x707070FF),
                        "02 Properties");
        const float KeyW = MeasureText(Faces, Faces.Regular, 10.0f * S, "Ctrl+A");
        (void)PlaceText(DrawList, Faces, Faces.Regular, 10.0f * S, Origin.x + W - 20.0f * S - KeyW, PathTop + 0.5f * (48.0f - 10.0f * 1.35f) * S, 1.35f,
                        ResolveCssColour(0x777777FF), "Ctrl+A");
        DrawList->AddLine(ImVec2(Origin.x + 20.0f * S, PathTop + 48.0f * S), ImVec2(Origin.x + W - 20.0f * S, PathTop + 48.0f * S),
                          ResolveCssColour(0x373737FF), S);
    }

    const float FooterTop = Origin.y + H - 60.0f * S;
    const float BodyTop   = PathTop + 48.0f * S;
    std::vector<int> Candidates;
    if (Picked_ < 0)
    {
        // ③ .construct-search: 38 px, margin 14px 20px 12px, #121212 over 1 px #373737, radius 8
        const ImVec2 SearchMin(Origin.x + 20.0f * S, BodyTop + 14.0f * S);
        const ImVec2 SearchMax(Origin.x + W - 20.0f * S, SearchMin.y + 38.0f * S);
        DrawList->AddRectFilled(SearchMin, SearchMax, ResolveCssColour(0x121212FF), 8.0f * S);
        DrawList->AddRect(SearchMin, SearchMax, ResolveCssColour(0x373737FF), 8.0f * S, 0, S);
        (void)GlyphSymbol::Construct(DrawList, "Search", ImVec2(SearchMin.x + 12.0f * S, SearchMin.y + 11.0f * S), 16.0f * S, 1.7f,
                                     ResolveCssColour(0x777777FF));
        const float FieldX = SearchMin.x + (12.0f + 16.0f + 10.0f) * S;
        const bool  Query  = Query_[0] != '\0';
        ImGui::SetCursorScreenPos(ImVec2(FieldX, SearchMin.y + 0.5f * (38.0f * S - 12.0f * GlyphRun::EmPixelHeight * S)));
        ImGui::PushFont(Faces.Regular, 12.0f * GlyphRun::EmPixelHeight * S / std::max(0.01f, ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ResolveCssColour(0xDDDDDDFF)));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::ColorConvertU32ToFloat4(ResolveCssColour(0x777777FF)));
        ImGui::SetNextItemWidth(std::max(40.0f, SearchMax.x - FieldX - (Query ? 36.0f : 12.0f) * S));
        if (FocusSearch_) ImGui::SetKeyboardFocusHere();
        FocusSearch_ = false;
        (void)ImGui::InputTextWithHint("##ConstructSearch", "Search existing engine entities\xE2\x80\xA6", Query_.data(), Query_.size());
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
        if (Query)
        {
            const ImVec2 Clear(SearchMax.x - 32.0f * S, SearchMin.y + 7.0f * S);
            ImGui::SetCursorScreenPos(Clear);
            if (ImGui::InvisibleButton("##ConstructClear", ImVec2(24.0f * S, 24.0f * S)))
            {
                Query_[0]    = '\0';
                FocusSearch_ = true;
            }
            (void)GlyphSymbol::Construct(DrawList, "X", ImVec2(Clear.x + 5.5f * S, Clear.y + 5.5f * S), 13.0f * S, 1.8f,
                                         ResolveCssColour(ImGui::IsItemHovered() ? 0xDDDDDDFF : 0x777777FF));
        }

        // ④ .construct-catalogue: the 138 px category rail and the results
        const float  CatalogueTop    = SearchMax.y + 12.0f * S;
        const float  CatalogueBottom = FooterTop - 18.0f * S;
        float        RailCursor      = CatalogueTop;
        for (const char* Group : Groups)
        {
            const ImVec2 Min(Origin.x + 20.0f * S, RailCursor);
            ImGui::SetCursorScreenPos(Min);
            ImGui::PushID(Group);
            if (ImGui::InvisibleButton("##ConstructGroup", ImVec2(138.0f * S, 34.0f * S))) Group_ = Group;
            const bool Hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            const bool Active = Group_ == Group;
            if (Active || Hovered)
                DrawList->AddRectFilled(Min, ImVec2(Min.x + 138.0f * S, Min.y + 34.0f * S), ResolveCssColour(Active ? 0x303030FF : 0x292929FF), 5.0f * S);
            PlaceText(DrawList, Faces, Faces.Regular, 12.0f * S, Min.x + 10.0f * S, Min.y + 0.5f * (34.0f - 12.0f * 1.35f) * S, 1.35f,
                      ResolveCssColour(Active ? 0xEEEEEEFF : 0xAAAAAAFF), Group);
            RailCursor += 36.0f * S;
        }
        const std::string Needle = LowerText(Query_.data());
        for (size_t Index = 0; Index < Catalogue_.size(); ++Index)
        {
            const ConstructCatalogueEntry& Entry = Catalogue_[Index];
            if (Group_ != "All" && Entry.Group != Group_) continue;
            if (!Needle.empty() && LowerText(Entry.Caption).find(Needle) == std::string::npos) continue;
            Candidates.push_back(static_cast<int>(Index));
        }
        const ImVec2 ResultsMin(Origin.x + (20.0f + 138.0f + 12.0f) * S, CatalogueTop);
        const ImVec2 ResultsExtent(std::max(60.0f * S, Origin.x + W - 20.0f * S - ResultsMin.x), std::max(60.0f * S, CatalogueBottom - CatalogueTop));
        ImGui::SetCursorScreenPos(ResultsMin);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        if (ImGui::BeginChild("##ConstructResults", ResultsExtent, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar))
        {
            // 📝 .construct-matrix: repeat(auto-fill, minmax(155px, 1fr)), gap 8px; .construct-entity 116 px, padding 8px 9px, gap 9px.
            ImDrawList*  Results = ImGui::GetWindowDrawList();
            const ImVec2 Top     = ImGui::GetCursorScreenPos();
            const float  Avail   = ImGui::GetContentRegionAvail().x - 2.0f * S;
            const int    Columns = std::max(1, static_cast<int>(std::floor((Avail + 8.0f * S) / (163.0f * S))));
            const float  TileW   = (Avail - 8.0f * S * static_cast<float>(Columns - 1)) / static_cast<float>(Columns);
            const float  TileH   = 116.0f * S;
            for (size_t Slot = 0; Slot < Candidates.size(); ++Slot)
            {
                const ConstructCatalogueEntry& Entry = Catalogue_[static_cast<size_t>(Candidates[Slot])];
                const int    Column = static_cast<int>(Slot) % Columns;
                const int    Row    = static_cast<int>(Slot) / Columns;
                const ImVec2 Min(Top.x + static_cast<float>(Column) * (TileW + 8.0f * S), Top.y + static_cast<float>(Row) * (TileH + 8.0f * S));
                const ImVec2 Max(Min.x + TileW, Min.y + TileH);
                ImGui::SetCursorScreenPos(Min);
                ImGui::PushID(Candidates[Slot]);
                if (ImGui::InvisibleButton("##ConstructTile", ImVec2(TileW, TileH)))
                {
                    Picked_ = Candidates[Slot];
                    std::snprintf(Name_.data(), Name_.size(), "%s", Entry.Caption.c_str());
                    Size_    = 1.0f;
                    Albedo_  = Entry.Albedo;
                    Visible_ = true;
                }
                const bool Hovered = ImGui::IsItemHovered();
                if (Hovered) ImGui::SetTooltip("%s", Entry.Summary.c_str());
                ImGui::PopID();
                Results->AddRectFilled(Min, Max, ResolveCssColour(Hovered ? 0x2B2B2BFF : 0x202020FF), 7.0f * S);
                Results->AddRect(Min, Max, ResolveCssColour(Hovered ? 0x666666FF : 0x3A3A3AFF), 7.0f * S, 0, S);
                const char* Glyph = Entry.Glyph.empty() ? "Box" : Entry.Glyph.c_str();
                if (GlyphSymbol::QueryArtwork(Glyph))
                    (void)GlyphSymbol::Construct(Results, Glyph, ImVec2(Min.x + 0.5f * TileW - 26.0f * S, Min.y + 8.0f * S), 52.0f * S, 1.4f, IM_COL32_WHITE);
                else
                    (void)GlyphSymbol::Construct(Results, Glyph, ImVec2(Min.x + 0.5f * TileW - 22.0f * S, Min.y + 12.0f * S), 44.0f * S, 1.3f,
                                                 ResolveCssColour(0xBDBDBDFF));
                PlaceText(Results, Faces, Faces.Regular, 12.0f * S, Min.x + 9.0f * S, Min.y + (8.0f + 52.0f + 9.0f) * S, 1.35f, ResolveCssColour(0xD8D8D8FF),
                          Entry.Caption.c_str());
                PlaceText(Results, Faces, Faces.Regular, 9.0f * S, Min.x + 9.0f * S, Min.y + (8.0f + 52.0f + 9.0f + 17.0f) * S, 1.35f,
                          ResolveCssColour(0x7A7A7AFF), Entry.Shape ? "Engine shape" : Entry.Group.c_str());
            }
            const int   Rows    = (static_cast<int>(Candidates.size()) + Columns - 1) / Columns;
            const float Caption = Top.y + static_cast<float>(Rows) * (TileH + 8.0f * S) + (Rows > 0 ? 8.0f : 16.0f) * S;
            const char* Line    = Candidates.empty() ? "No matching engine entities."
                                                     : "Select an entity to edit its properties, then add it to the scene.";
            PlaceText(Results, Faces, Faces.Regular, 11.0f * S, Top.x, Caption, 1.5f, ResolveCssColour(0x888888FF), Line);
            ImGui::SetCursorScreenPos(ImVec2(Top.x, Caption + 11.0f * 1.5f * S + 16.0f * S));
            ImGui::Dummy(ImVec2(1.0f, 1.0f));
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    else
    {
        // ⑤ .construct-selection: Entities (back), the glyph, the name; below it the properties
        const ConstructCatalogueEntry& Entry = Catalogue_[static_cast<size_t>(Picked_)];
        const float                    RowTop = BodyTop + 14.0f * S;
        {
            const ImVec2 Back(Origin.x + 20.0f * S, RowTop);
            const float  BackW = MeasureText(Faces, Faces.Regular, 12.0f * S, "Entities") + (14.0f + 8.0f + 24.0f) * S;
            ImGui::SetCursorScreenPos(Back);
            if (ImGui::InvisibleButton("##ConstructBack", ImVec2(BackW, 34.0f * S))) Picked_ = -1;
            const bool Hovered = ImGui::IsItemHovered();
            DrawList->AddRectFilled(Back, ImVec2(Back.x + BackW, Back.y + 34.0f * S), ResolveCssColour(Hovered ? 0x343434FF : 0x262626FF), 8.0f * S);
            DrawList->AddRect(Back, ImVec2(Back.x + BackW, Back.y + 34.0f * S), ResolveCssColour(0x373737FF), 8.0f * S, 0, S);
            (void)GlyphSymbol::Construct(DrawList, "ChevronLeft", ImVec2(Back.x + 12.0f * S, Back.y + 10.0f * S), 14.0f * S, 1.8f,
                                         ResolveCssColour(0xCFCFCFFF));
            PlaceText(DrawList, Faces, Faces.Regular, 12.0f * S, Back.x + (12.0f + 14.0f + 8.0f) * S, Back.y + 0.5f * (34.0f - 12.0f * 1.35f) * S, 1.35f,
                      ResolveCssColour(0xCFCFCFFF), "Entities");
            float Pen = Back.x + BackW + 12.0f * S;
            const char* Glyph = Entry.Glyph.empty() ? "Box" : Entry.Glyph.c_str();
            (void)GlyphSymbol::Construct(DrawList, Glyph, ImVec2(Pen, Back.y + 5.0f * S), 24.0f * S, 1.6f, ResolveCssColour(0xD8D8D8FF));
            Pen += (24.0f + 12.0f) * S;
            const ImVec2 FieldMin(Pen, Back.y);
            const ImVec2 FieldMax(Origin.x + W - 20.0f * S, Back.y + 34.0f * S);
            DrawList->AddRectFilled(FieldMin, FieldMax, ResolveCssColour(0x121212FF), 8.0f * S);
            ImGui::SetCursorScreenPos(ImVec2(FieldMin.x + 12.0f * S, FieldMin.y + 0.5f * (34.0f * S - 13.0f * GlyphRun::EmPixelHeight * S)));
            ImGui::PushFont(Faces.Regular, 13.0f * GlyphRun::EmPixelHeight * S / std::max(0.01f, ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ResolveCssColour(0xEEEEEEFF)));
            ImGui::SetNextItemWidth(std::max(40.0f, FieldMax.x - FieldMin.x - 24.0f * S));
            (void)ImGui::InputText("##ConstructName", Name_.data(), Name_.size());
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar(2);
            ImGui::PopFont();
            DrawList->AddLine(ImVec2(Origin.x + 20.0f * S, RowTop + 48.0f * S), ImVec2(Origin.x + W - 20.0f * S, RowTop + 48.0f * S),
                              ResolveCssColour(0x373737FF), S);
        }
        // ⑥ .construct-properties: the geometry inspector's look on the dialog's fill
        const ImVec2 PropertiesMin(Origin.x + 20.0f * S, RowTop + 49.0f * S);
        const ImVec2 PropertiesExtent(W - 40.0f * S, std::max(60.0f * S, FooterTop - PropertiesMin.y - 1.0f * S));
        ImGui::SetCursorScreenPos(PropertiesMin);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        if (ImGui::BeginChild("##ConstructProperties", PropertiesExtent, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar))
        {
            ControlTheme Theme = ControlTheme::ResolveGeometry();
            Theme.PageFill     = ResolveCssColour(0x171717FF);
            Theme.PaddingSide  = 0.0f;
            Theme.PaddingTop   = 14.0f;
            ControlColumn Page(Theme, Faces, S);
            char          Text[320];
            if (Entry.Shape)
            {
                (void)SliderRow::Construct(Page, "ConstructSize", "Scale", Size_, 0.25f, 20.0f, 0.05f, "\xC3\x97", 1.0f, true,
                                           "Uniform scale of the shape's bounding box");
                std::snprintf(Text, sizeof(Text), "Bounding box %.2f \xC3\x97 %.2f \xC3\x97 %.2f m.", Entry.Size[0] * Size_, Entry.Size[1] * Size_,
                              Entry.Size[2] * Size_);
                CaptionText::Construct(Page, Text, 2.0f, 14.0f);
                float Albedo[3] = {Albedo_[0], Albedo_[1], Albedo_[2]};
                if (TintRow::Construct(Page, "ConstructAlbedo", "Surface colour", Albedo, true, "The shape's albedo (linear)"))
                    Albedo_ = {Albedo[0], Albedo[1], Albedo[2]};
            }
            (void)SwitchRow::Construct(Page, "ConstructVisible", "Visible", Visible_);
            if (Entry.Shape)
                std::snprintf(Text, sizeof(Text), "%s Placed on the ground where the view looks (at most 40 m ahead), resting on it; move it with W, turn it with E "
                                                  "and scale it with R. Its distance volume is baked beside its archive "
                                                  "(EngineContent/ShapeArchives/%s.sdfspace), and the viewport lays it out from the reserve.",
                              Entry.Summary.c_str(), Entry.Identifier.c_str());
            else
                std::snprintf(Text, sizeof(Text), "%s Added with its defaults; edit it in the inspector once it is in the scene.", Entry.Summary.c_str());
            CaptionText::Construct(Page, Text, 8.0f, 0.0f);
            Page.Finalize();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ⑦ footer: the count or the note, Cancel and "Add to scene"
    DrawList->AddLine(ImVec2(Origin.x, FooterTop), ImVec2(Origin.x + W, FooterTop), ResolveCssColour(0x373737FF), S);
    {
        char Count[64];
        if (Picked_ < 0) std::snprintf(Count, sizeof(Count), "%zu entities", Candidates.size());
        else std::snprintf(Count, sizeof(Count), "%s", Catalogue_[static_cast<size_t>(Picked_)].Shape ? "Engine shape \xC2\xB7 laid out from the reserve"
                                                                                                    : "Record \xC2\xB7 the level's defaults");
        PlaceText(DrawList, Faces, Faces.Regular, 10.0f * S, Origin.x + 20.0f * S, FooterTop + 0.5f * (60.0f - 10.0f * 1.35f) * S, 1.35f,
                  ResolveCssColour(0x8A8A8AFF), Count);
        float Right = Origin.x + W - 20.0f * S;
        if (Picked_ >= 0)
        {
            const float AddW = MeasureText(Faces, Faces.Regular, 12.0f * S, "Add to scene") + (22.0f + 28.0f) * S;
            Right -= AddW;
            if (ConstructFooterButton(DrawList, Faces, "##ConstructAdd", "Add to scene", "Plus", ImVec2(Right, FooterTop + 13.0f * S), S, true))
            {
                ConstructOrder Made;
                Made.Entry   = Catalogue_[static_cast<size_t>(Picked_)];
                std::string Name = Name_.data();
                while (!Name.empty() && std::isspace(static_cast<unsigned char>(Name.back()))) Name.pop_back();
                while (!Name.empty() && std::isspace(static_cast<unsigned char>(Name.front()))) Name.erase(Name.begin());
                Made.Name     = Name.empty() ? Made.Entry.Caption : Name;
                Made.Size     = Size_;
                Made.Albedo   = Albedo_;
                Made.Surfaced = Made.Entry.Shape && (std::abs(Albedo_[0] - Made.Entry.Albedo[0]) > 1.0e-4f ||
                                                     std::abs(Albedo_[1] - Made.Entry.Albedo[1]) > 1.0e-4f ||
                                                     std::abs(Albedo_[2] - Made.Entry.Albedo[2]) > 1.0e-4f);
                Made.Visible  = Visible_;
                Order         = std::move(Made);
                Closing       = true;
            }
            Right -= 10.0f * S;
        }
        const float CancelW = MeasureText(Faces, Faces.Regular, 12.0f * S, "Cancel") + 28.0f * S;
        Right -= CancelW;
        if (ConstructFooterButton(DrawList, Faces, "##ConstructCancel", "Cancel", nullptr, ImVec2(Right, FooterTop + 13.0f * S), S, false)) Closing = true;
    }

    // 📝 The modal owns Escape (back to the entities, then closed) and a click on the scrim closes it.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        if (Picked_ >= 0) Picked_ = -1;
        else Closing = true;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseHoveringRect(Origin, ImVec2(Origin.x + W, Origin.y + H), false)) Closing = true;
    if (Closing)
    {
        ImGui::CloseCurrentPopup();
        Open_   = false;
        Picked_ = -1;
    }
    ImGui::EndPopup();
    return Order;
}

} // namespace Frontier

#else // !(FRONTIER_EDITOR && FRONTIER_WITH_INTERFACE_SDK)

namespace Frontier
{

void ConstructPanel::Open()
{
    Open_ = true;
}

void ConstructPanel::Close()
{
    Open_ = false;
}

std::optional<ConstructOrder> ConstructPanel::Construct(float)
{
    return std::nullopt;
}

} // namespace Frontier

#endif // FRONTIER_EDITOR && FRONTIER_WITH_INTERFACE_SDK
