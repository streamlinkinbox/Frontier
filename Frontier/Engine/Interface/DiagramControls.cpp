/*==========================================================================================================================================
                                                        DIAGRAMCONTROLS.CPP
==========================================================================================================================================*/
// 🧩 Generic, reusable analytical plotting, polar dial, spectrum ramp, disc row, and modal editor frame controls.

#include "Engine/Interface/DiagramControls.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/SceneDirectory.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace Frontier
{

namespace
{

constexpr float PiConstant = 3.14159265358979323846f;

void ConstructDiagramFrame(ImDrawList*             DrawList,
                           const InspectorPalette& Palette,
                           ImVec2                  Min,
                           ImVec2                  Max) noexcept
{
    DrawList->AddRectFilled(Min, Max, Palette.InsetFieldBg, 10.0f);
    DrawList->AddRect(Min, Max, Palette.InsetFieldBorder, 10.0f);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    1. ARC PLOT
//------------------------------------------------------------------------------------------------------------------------

void DiagramControls::ConstructArcPlot(const InspectorPalette& Palette,
                                       const EditorTypefaces&  Faces,
                                       const char*             HeaderLabel,
                                       const char*             BaselineLabel,
                                       float                   HorizontalRatio,
                                       float                   ElevationRatio,
                                       ImU32                   MarkerTint)
{
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Min      = ImGui::GetCursorScreenPos();
    const float  Width    = std::max(150.0f, ImGui::GetContentRegionAvail().x);
    const float  Height   = 104.0f;
    const ImVec2 Max(Min.x + Width, Min.y + Height);
    ConstructDiagramFrame(DrawList, Palette, Min, Max);

    const float BaseY = Max.y - 22.0f;
    DrawList->AddLine(ImVec2(Min.x + 14.0f, BaseY), ImVec2(Max.x - 14.0f, BaseY), Palette.EdgeBorder, 1.0f);
    if (BaselineLabel != nullptr && BaselineLabel[0] != '\0')
    {
        DrawList->AddText(Faces.Regular, 9.0f, ImVec2(Min.x + 16.0f, BaseY + 4.0f), Palette.TextFaint, BaselineLabel);
    }

    const ImVec2 ArcCentre((Min.x + Max.x) * 0.5f, BaseY);
    const float  Rx = (Width - 48.0f) * 0.5f;
    const float  Ry = 54.0f;
    ImVec2       Prev(ArcCentre.x - Rx, ArcCentre.y);
    for (int Step = 1; Step <= 28; ++Step)
    {
        const float Angle = PiConstant * (1.0f - static_cast<float>(Step) / 28.0f);
        const ImVec2 Next(ArcCentre.x + std::cos(Angle) * Rx, ArcCentre.y - std::sin(Angle) * Ry);
        DrawList->AddLine(Prev, Next, ModulateSpectrumAlpha(MarkerTint, 0.42f), 1.3f);
        Prev = Next;
    }

    const float ClampedX = std::clamp(HorizontalRatio, 0.0f, 1.0f);
    const float ClampedY = std::clamp(ElevationRatio, -0.25f, 1.0f);
    const ImVec2 Marker(Min.x + 24.0f + ClampedX * (Width - 48.0f), BaseY - ClampedY * Ry);
    DrawList->AddCircleFilled(Marker, 11.0f, ModulateSpectrumAlpha(MarkerTint, 0.22f), 20);
    DrawList->AddCircleFilled(Marker, 5.2f, MarkerTint, 16);

    if (HeaderLabel != nullptr && HeaderLabel[0] != '\0')
    {
        DrawList->AddText(Faces.Monospace, 9.5f, ImVec2(Min.x + 14.0f, Min.y + 9.0f), Palette.TextSecondary, HeaderLabel);
    }

    ImGui::Dummy(ImVec2(Width, Height + 8.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    2. STRATA PLOT
//------------------------------------------------------------------------------------------------------------------------

void DiagramControls::ConstructStrataPlot(const InspectorPalette& Palette,
                                          const EditorTypefaces&  Faces,
                                          ImU32                   UpperTint,
                                          ImU32                   LowerTint,
                                          const char* const*      TierLabels,
                                          size_t                  TierCount)
{
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Min      = ImGui::GetCursorScreenPos();
    const float  Width    = std::max(150.0f, ImGui::GetContentRegionAvail().x);
    const float  Height   = 96.0f;
    const ImVec2 Max(Min.x + Width, Min.y + Height);
    ConstructDiagramFrame(DrawList, Palette, Min, Max);

    const ImVec2 InnerMin(Min.x + 8.0f, Min.y + 8.0f);
    const ImVec2 InnerMax(Max.x - 8.0f, Max.y - 8.0f);
    DrawList->AddRectFilledMultiColor(InnerMin, InnerMax, UpperTint, UpperTint, LowerTint, LowerTint);

    if (TierLabels != nullptr && TierCount > 0)
    {
        const float BandH = (InnerMax.y - InnerMin.y) / static_cast<float>(TierCount);
        for (size_t Index = 0; Index < TierCount; ++Index)
        {
            const float Y = InnerMin.y + static_cast<float>(Index) * BandH;
            if (Index > 0)
            {
                DrawList->AddLine(ImVec2(InnerMin.x, Y), ImVec2(InnerMax.x, Y),
                                  ModulateSpectrumAlpha(Palette.TextPrimary, 0.16f), 1.0f);
            }
            if (TierLabels[Index] != nullptr)
            {
                DrawList->AddText(Faces.Monospace, 9.0f, ImVec2(InnerMin.x + 8.0f, Y + 5.0f),
                                  IM_COL32(236, 242, 250, 225), TierLabels[Index]);
            }
        }
    }

    ImGui::Dummy(ImVec2(Width, Height + 8.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    3. POLAR DIAL
//------------------------------------------------------------------------------------------------------------------------

bool DiagramControls::ConstructPolarDial(const InspectorPalette& Palette,
                                         const EditorTypefaces&  Faces,
                                         const char*             Id,
                                         const char*             ReadoutLabel,
                                         float&                  InOutAngleDegrees,
                                         float                   MagnitudeRatio,
                                         ImU32                   VectorTint)
{
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Min      = ImGui::GetCursorScreenPos();
    const float  Width    = std::max(150.0f, ImGui::GetContentRegionAvail().x);
    const float  Height   = 112.0f;
    const ImVec2 Max(Min.x + Width, Min.y + Height);
    ConstructDiagramFrame(DrawList, Palette, Min, Max);

    const ImVec2 Centre(Min.x + 62.0f, (Min.y + Max.y) * 0.5f);
    const float  Radius = 38.0f;
    DrawList->AddCircle(Centre, Radius, Palette.EdgeBorder, 32, 1.2f);
    DrawList->AddCircle(Centre, Radius * 0.55f, ModulateSpectrumAlpha(Palette.EdgeBorder, 0.5f), 24, 1.0f);

    bool Changed = false;
    ImGui::SetCursorScreenPos(ImVec2(Centre.x - Radius, Centre.y - Radius));
    ImGui::InvisibleButton(Id, ImVec2(Radius * 2.0f, Radius * 2.0f));
    if (ImGui::IsItemActive())
    {
        const ImVec2 Mouse = ImGui::GetIO().MousePos;
        const float  Dx    = Mouse.x - Centre.x;
        const float  Dy    = Mouse.y - Centre.y;
        float        Deg   = std::atan2(Dy, Dx) * (180.0f / PiConstant) + 90.0f;
        if (Deg < 0.0f)
        {
            Deg += 360.0f;
        }
        InOutAngleDegrees = Deg;
        Changed           = true;
    }

    const float Rad = (InOutAngleDegrees - 90.0f) * (PiConstant / 180.0f);
    const float Len = Radius * std::clamp(MagnitudeRatio, 0.2f, 1.0f);
    const ImVec2 Tip(Centre.x + std::cos(Rad) * Len, Centre.y + std::sin(Rad) * Len);
    DrawList->AddLine(Centre, Tip, VectorTint, 2.2f);
    DrawList->AddCircleFilled(Tip, 4.0f, VectorTint, 12);
    DrawList->AddCircleFilled(Centre, 2.8f, Palette.TextSecondary, 10);

    if (ReadoutLabel != nullptr && ReadoutLabel[0] != '\0')
    {
        DrawList->AddText(Faces.Monospace, 10.0f, ImVec2(Min.x + 122.0f, Centre.y - 6.0f),
                          Palette.TextSecondary, ReadoutLabel);
    }

    ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y + 8.0f));
    ImGui::Dummy(ImVec2(Width, 0.0f));
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    4. CURVE PLOT
//------------------------------------------------------------------------------------------------------------------------

void DiagramControls::ConstructCurvePlot(const InspectorPalette& Palette,
                                         const EditorTypefaces&  Faces,
                                         const char*             HeaderLabel,
                                         const float*            PrimarySamples,
                                         size_t                  PrimaryCount,
                                         ImU32                   PrimaryTint,
                                         const float*            SecondarySamples,
                                         size_t                  SecondaryCount,
                                         ImU32                   SecondaryTint,
                                         float                   ThresholdRatio)
{
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Min      = ImGui::GetCursorScreenPos();
    const float  Width    = std::max(150.0f, ImGui::GetContentRegionAvail().x);
    const float  Height   = 86.0f;
    const ImVec2 Max(Min.x + Width, Min.y + Height);
    ConstructDiagramFrame(DrawList, Palette, Min, Max);

    if (HeaderLabel != nullptr && HeaderLabel[0] != '\0')
    {
        DrawList->AddText(Faces.Monospace, 9.0f, ImVec2(Min.x + 12.0f, Min.y + 8.0f), Palette.TextMuted, HeaderLabel);
    }

    const float PlotLeft   = Min.x + 12.0f;
    const float PlotRight  = Max.x - 12.0f;
    const float PlotTop    = Min.y + 24.0f;
    const float PlotBottom = Max.y - 10.0f;

    const auto DrawSeries = [&](const float* Data, size_t Count, ImU32 Colour) {
        if (Data == nullptr || Count < 2)
        {
            return;
        }
        for (size_t Index = 0; Index + 1 < Count; ++Index)
        {
            const float X0 = PlotLeft + (static_cast<float>(Index) / static_cast<float>(Count - 1)) * (PlotRight - PlotLeft);
            const float X1 = PlotLeft + (static_cast<float>(Index + 1) / static_cast<float>(Count - 1)) * (PlotRight - PlotLeft);
            const float Y0 = PlotBottom - std::clamp(Data[Index], 0.0f, 1.0f) * (PlotBottom - PlotTop);
            const float Y1 = PlotBottom - std::clamp(Data[Index + 1], 0.0f, 1.0f) * (PlotBottom - PlotTop);
            DrawList->AddLine(ImVec2(X0, Y0), ImVec2(X1, Y1), Colour, 1.65f);
        }
    };

    DrawSeries(SecondarySamples, SecondaryCount, SecondaryTint);
    DrawSeries(PrimarySamples, PrimaryCount, PrimaryTint);

    if (ThresholdRatio >= 0.0f && ThresholdRatio <= 1.0f)
    {
        const float ThreshX = PlotLeft + ThresholdRatio * (PlotRight - PlotLeft);
        DrawList->AddLine(ImVec2(ThreshX, PlotTop - 2.0f), ImVec2(ThreshX, PlotBottom), Palette.WarmAmber, 1.3f);
    }

    ImGui::Dummy(ImVec2(Width, Height + 8.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    5. SPECTRUM BAR
//------------------------------------------------------------------------------------------------------------------------

void DiagramControls::ConstructSpectrumBar(const InspectorPalette& Palette,
                                           const EditorTypefaces&  Faces,
                                           const char*             HeaderLabel,
                                           const ImU32*            ColourStops,
                                           size_t                  StopCount,
                                           float                   CursorRatio)
{
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Min      = ImGui::GetCursorScreenPos();
    const float  Width    = std::max(150.0f, ImGui::GetContentRegionAvail().x);
    const float  Height   = 56.0f;
    const ImVec2 Max(Min.x + Width, Min.y + Height);
    ConstructDiagramFrame(DrawList, Palette, Min, Max);

    if (HeaderLabel != nullptr && HeaderLabel[0] != '\0')
    {
        DrawList->AddText(Faces.Monospace, 9.0f, ImVec2(Min.x + 12.0f, Min.y + 8.0f), Palette.TextMuted, HeaderLabel);
    }

    const float BarLeft  = Min.x + 12.0f;
    const float BarRight = Max.x - 12.0f;
    const float BarTop   = Min.y + 26.0f;
    const float BarBot   = Max.y - 11.0f;

    if (ColourStops != nullptr && StopCount >= 2)
    {
        const size_t Segments = StopCount - 1;
        const float  SegW     = (BarRight - BarLeft) / static_cast<float>(Segments);
        for (size_t Seg = 0; Seg < Segments; ++Seg)
        {
            const float X0 = BarLeft + static_cast<float>(Seg) * SegW;
            DrawList->AddRectFilledMultiColor(ImVec2(X0, BarTop), ImVec2(X0 + SegW, BarBot),
                                              ColourStops[Seg], ColourStops[Seg + 1],
                                              ColourStops[Seg + 1], ColourStops[Seg]);
        }
    }

    const float MarkerX = BarLeft + std::clamp(CursorRatio, 0.0f, 1.0f) * (BarRight - BarLeft);
    DrawList->AddLine(ImVec2(MarkerX, BarTop - 3.0f), ImVec2(MarkerX, BarBot + 3.0f), Palette.TextPrimary, 2.0f);

    ImGui::Dummy(ImVec2(Width, Height + 8.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    6. POLYGON DIAGRAM
//------------------------------------------------------------------------------------------------------------------------

void DiagramControls::ConstructPolygonDiagram(const InspectorPalette& Palette,
                                              const EditorTypefaces&  Faces,
                                              const char*             HeaderLabel,
                                              uint32_t                SideCount,
                                              float                   RadiusRatio,
                                              float                   DispersionRatio,
                                              ImU32                   StrokeTint)
{
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Min      = ImGui::GetCursorScreenPos();
    const float  Width    = std::max(150.0f, ImGui::GetContentRegionAvail().x);
    const float  Height   = 96.0f;
    const ImVec2 Max(Min.x + Width, Min.y + Height);
    ConstructDiagramFrame(DrawList, Palette, Min, Max);

    if (HeaderLabel != nullptr && HeaderLabel[0] != '\0')
    {
        DrawList->AddText(Faces.Monospace, 9.0f, ImVec2(Min.x + 12.0f, Min.y + 8.0f), Palette.TextMuted, HeaderLabel);
    }

    const ImVec2   Centre((Min.x + Max.x) * 0.5f, Min.y + 54.0f);
    const uint32_t Sides = std::clamp(SideCount, 3u, 16u);
    const float    R     = 14.0f + std::clamp(RadiusRatio, 0.1f, 1.0f) * 20.0f;

    std::array<ImVec2, 16> Vertices{};
    for (uint32_t Index = 0; Index < Sides; ++Index)
    {
        const float Angle = (2.0f * PiConstant * static_cast<float>(Index)) / static_cast<float>(Sides) - PiConstant * 0.5f;
        Vertices[Index]   = ImVec2(Centre.x + std::cos(Angle) * R, Centre.y + std::sin(Angle) * R);
    }
    DrawList->AddPolyline(Vertices.data(), static_cast<int>(Sides), StrokeTint, ImDrawFlags_Closed, 1.6f);

    if (DispersionRatio > 0.01f)
    {
        DrawList->AddCircle(Centre, R + DispersionRatio * 10.0f,
                            ModulateSpectrumAlpha(Palette.ThemeAccent, 0.45f), 28, 1.2f);
    }

    ImGui::Dummy(ImVec2(Width, Height + 8.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    7. DISC SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

void DiagramControls::ConstructDiscSequence(const InspectorPalette& Palette,
                                            const EditorTypefaces&  Faces,
                                            const char*             HeaderLabel,
                                            const float*            IlluminationRatios,
                                            size_t                  DiscCount,
                                            int                     HighlightedIndex,
                                            ImU32                   LitTint)
{
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Min      = ImGui::GetCursorScreenPos();
    const float  Width    = std::max(150.0f, ImGui::GetContentRegionAvail().x);
    const float  Height   = 68.0f;
    const ImVec2 Max(Min.x + Width, Min.y + Height);
    ConstructDiagramFrame(DrawList, Palette, Min, Max);

    if (HeaderLabel != nullptr && HeaderLabel[0] != '\0')
    {
        DrawList->AddText(Faces.Monospace, 9.0f, ImVec2(Min.x + 12.0f, Min.y + 8.0f), Palette.TextMuted, HeaderLabel);
    }

    if (IlluminationRatios != nullptr && DiscCount > 0)
    {
        const float Span = Width - 40.0f;
        for (size_t Index = 0; Index < DiscCount; ++Index)
        {
            const float X = Min.x + 20.0f + (DiscCount > 1 ? (static_cast<float>(Index) / static_cast<float>(DiscCount - 1)) * Span : Span * 0.5f);
            const ImVec2 Pos(X, Min.y + 44.0f);
            const float  Lit = std::clamp(IlluminationRatios[Index], 0.0f, 1.0f);
            DrawList->AddCircleFilled(Pos, 9.5f, Palette.CountCardBg, 18);
            DrawList->AddCircleFilled(ImVec2(Pos.x + (1.0f - Lit) * 3.5f, Pos.y), 9.5f * (0.35f + 0.65f * Lit),
                                      ModulateSpectrumAlpha(LitTint, 0.35f + 0.65f * Lit), 18);
            if (static_cast<int>(Index) == HighlightedIndex)
            {
                DrawList->AddCircle(Pos, 12.5f, Palette.ThemeAccent, 20, 1.5f);
            }
        }
    }

    ImGui::Dummy(ImVec2(Width, Height + 8.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    8. TIME-SERIES PLOT
//------------------------------------------------------------------------------------------------------------------------

void DiagramControls::ConstructTimeSeriesPlot(const InspectorPalette& Palette,
                                              const EditorTypefaces&  Faces,
                                              const char*             HeaderLabel,
                                              const float*            Samples,
                                              size_t                  SampleCount,
                                              float                   MinimumDomain,
                                              float                   MaximumDomain,
                                              float                   ReferenceValue,
                                              ImU32                   SeriesTint)
{
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Min      = ImGui::GetCursorScreenPos();
    const float  Width    = std::max(150.0f, ImGui::GetContentRegionAvail().x);
    const float  Height   = 84.0f;
    const ImVec2 Max(Min.x + Width, Min.y + Height);
    ConstructDiagramFrame(DrawList, Palette, Min, Max);

    if (HeaderLabel != nullptr && HeaderLabel[0] != '\0')
    {
        DrawList->AddText(Faces.Monospace, 9.0f, ImVec2(Min.x + 12.0f, Min.y + 8.0f), Palette.TextMuted, HeaderLabel);
    }

    const float Left  = Min.x + 12.0f;
    const float Right = Max.x - 12.0f;
    const float Top   = Min.y + 24.0f;
    const float Bot   = Max.y - 10.0f;
    const float Span  = std::max(1.0e-4f, MaximumDomain - MinimumDomain);

    if (ReferenceValue >= MinimumDomain && ReferenceValue <= MaximumDomain)
    {
        const float RefY = Bot - ((ReferenceValue - MinimumDomain) / Span) * (Bot - Top);
        DrawList->AddLine(ImVec2(Left, RefY), ImVec2(Right, RefY),
                          ModulateSpectrumAlpha(Palette.WarmAmber, 0.45f), 1.0f);
    }

    if (Samples != nullptr && SampleCount >= 2)
    {
        for (size_t Index = 0; Index + 1 < SampleCount; ++Index)
        {
            const float X0 = Left + (static_cast<float>(Index) / static_cast<float>(SampleCount - 1)) * (Right - Left);
            const float X1 = Left + (static_cast<float>(Index + 1) / static_cast<float>(SampleCount - 1)) * (Right - Left);
            const float N0 = std::clamp((Samples[Index] - MinimumDomain) / Span, 0.0f, 1.0f);
            const float N1 = std::clamp((Samples[Index + 1] - MinimumDomain) / Span, 0.0f, 1.0f);
            DrawList->AddLine(ImVec2(X0, Bot - N0 * (Bot - Top)),
                              ImVec2(X1, Bot - N1 * (Bot - Top)),
                              SeriesTint, 1.6f);
        }
    }

    ImGui::Dummy(ImVec2(Width, Height + 8.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    9. EDITOR MODAL FRAME
//------------------------------------------------------------------------------------------------------------------------

void DiagramControls::ConstructEditorModalFrame(const char*                                             WindowTitle,
                                                const char*                                             GlyphName,
                                                RecordToken                                             Token,
                                                RecordInspector&                                        Inspector,
                                                const std::function<void(ImDrawList*, ImVec2, ImVec2)>& CanvasCallback)
{
    SceneDirectory&         Directory = SceneDirectory::Instance();
    InspectorRuntime&       Runtime   = InspectorRuntime::Instance();
    const InspectorPalette  Palette   = InspectorPalette::Resolve();
    const EditorTypefaces   Faces     = EditorTypefaces::Resolve();
    const ImGuiViewport*    MainVp    = ImGui::GetMainViewport();
    const ImVec2            Margin(52.0f, 46.0f);
    const ImVec2            ModalPos(MainVp->WorkPos.x + Margin.x, MainVp->WorkPos.y + Margin.y);
    const ImVec2            ModalSize(std::max(640.0f, MainVp->WorkSize.x - Margin.x * 2.0f),
                                      std::max(420.0f, MainVp->WorkSize.y - Margin.y * 2.0f));

    ImGui::SetNextWindowPos(ModalPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ModalSize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(Palette.ColumnBg));
    if (ImGui::Begin("##DomainEditorModal", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking))
    {
        ImDrawList*       DrawList   = ImGui::GetWindowDrawList();
        const std::string RecordName = Directory.QueryName(Token);
        const uint64_t    Revision   = Inspector.QueryRevision();

        DrawList->AddRect(ModalPos, ImVec2(ModalPos.x + ModalSize.x, ModalPos.y + ModalSize.y), Palette.EdgeBorder, 16.0f, 0, 1.5f);
        DrawList->AddRectFilled(ModalPos, ImVec2(ModalPos.x + ModalSize.x, ModalPos.y + 50.0f),
                                Palette.InspectorTopBg, 16.0f, ImDrawFlags_RoundCornersTop);
        DrawList->AddLine(ImVec2(ModalPos.x, ModalPos.y + 50.0f),
                          ImVec2(ModalPos.x + ModalSize.x, ModalPos.y + 50.0f),
                          Palette.EdgeBorder, 1.0f);

        ConstructEditorGlyph(DrawList, ImVec2(ModalPos.x + 24.0f, ModalPos.y + 25.0f), 8.0f,
                             Palette.ThemeAccent, GlyphName, 1.5f);
        DrawList->AddText(Faces.Medium, 14.0f, ImVec2(ModalPos.x + 42.0f, ModalPos.y + 17.5f),
                          Palette.TextPrimary, WindowTitle);

        char SyncReadout[128] = {};
        std::snprintf(SyncReadout, sizeof(SyncReadout), "LIVE CONFIGURATION · %s · REV #%llu",
                      RecordName.c_str(), static_cast<unsigned long long>(Revision));
        DrawList->AddText(Faces.Monospace, 10.5f, ImVec2(ModalPos.x + 360.0f, ModalPos.y + 19.5f),
                          Palette.LiveGreen, SyncReadout);

        const float  CloseW = 92.0f;
        const ImVec2 CloseMin(ModalPos.x + ModalSize.x - CloseW - 14.0f, ModalPos.y + 10.0f);
        ImGui::SetCursorScreenPos(CloseMin);
        if (ImGui::InvisibleButton("##CloseDomainEditorBtn", ImVec2(CloseW, 30.0f)))
        {
            Runtime.CloseActiveEditor();
        }
        const bool CloseHover = ImGui::IsItemHovered();
        DrawList->AddRectFilled(CloseMin, ImVec2(CloseMin.x + CloseW, CloseMin.y + 30.0f),
                                CloseHover ? Palette.SegmentedActiveBg : Palette.CountCardBg, 9.0f);
        DrawList->AddRect(CloseMin, ImVec2(CloseMin.x + CloseW, CloseMin.y + 30.0f), Palette.InsetFieldBorder, 9.0f);
        DrawList->AddText(Faces.Medium, 11.0f, ImVec2(CloseMin.x + 15.0f, CloseMin.y + 9.0f),
                          Palette.TextPrimary, "Close Editor");

        const float  BodyTop    = ModalPos.y + 50.0f;
        const float  BodyH      = ModalSize.y - 50.0f;
        const float  InspectorW = 390.0f;
        const float  CanvasW    = std::max(220.0f, ModalSize.x - InspectorW - 36.0f);
        const ImVec2 CanvasMin(ModalPos.x + 16.0f, BodyTop + 16.0f);
        const ImVec2 CanvasMax(ModalPos.x + 16.0f + CanvasW, BodyTop + BodyH - 16.0f);

        DrawList->AddRectFilled(CanvasMin, CanvasMax, Palette.InsetFieldBg, 14.0f);
        DrawList->AddRect(CanvasMin, CanvasMax, Palette.EdgeBorder, 14.0f);
        const ImU32 GridInk = ModulateSpectrumAlpha(Palette.EdgeBorder, 0.45f);
        for (int Line = 1; Line < 8; ++Line)
        {
            const float X = CanvasMin.x + ((CanvasMax.x - CanvasMin.x) / 8.0f) * static_cast<float>(Line);
            const float Y = CanvasMin.y + ((CanvasMax.y - CanvasMin.y) / 8.0f) * static_cast<float>(Line);
            DrawList->AddLine(ImVec2(X, CanvasMin.y + 38.0f), ImVec2(X, CanvasMax.y - 12.0f), GridInk, 1.0f);
            DrawList->AddLine(ImVec2(CanvasMin.x + 12.0f, Y), ImVec2(CanvasMax.x - 12.0f, Y), GridInk, 1.0f);
        }
        CanvasCallback(DrawList, CanvasMin, CanvasMax);

        ImGui::SetCursorScreenPos(ImVec2(ModalPos.x + ModalSize.x - InspectorW, BodyTop));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
        if (ImGui::BeginChild("##DomainEditorParameters", ImVec2(InspectorW, BodyH), true))
        {
            InspectorControls::ConstructSectionDivider(Palette, Faces, "LIVE PARAMETERS", "InspectorRuntime");
            Inspector.ConstructInspector();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

} // namespace Frontier

#endif // FRONTIER_EDITOR && FRONTIER_WITH_INTERFACE_SDK
