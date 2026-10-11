//============================================================================================================================================
//                                                    DIAGNOSTICINSPECTOR.CPP
//============================================================================================================================================
// 🧩 F3 debug-view popup: key edge detection and the top-right telemetry card (ReSTIR flags + scene census rows, R6 row 3).

#include "DiagnosticInspector.h"
#include "ControlKit.h"
#include "ReSTIRIntegrator.h"
#include "../ContentInterchange/MaterialIndex.h"
#include "../ContentInterchange/TextureIndex.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <imgui.h>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERACTION
//------------------------------------------------------------------------------------------------------------------------

bool DiagnosticInspector::AdvanceInteraction(const InputExchange& Input) noexcept
{
    const bool F3     = Input.IsKeyPressed(VirtualKeyCategory::KeyF3);
    const bool F4     = Input.IsKeyPressed(VirtualKeyCategory::KeyF4);
    const bool F5     = Input.IsKeyPressed(VirtualKeyCategory::KeyF5);
    const bool F6     = Input.IsKeyPressed(VirtualKeyCategory::KeyF6);
    const bool Escape = Input.IsKeyPressed(VirtualKeyCategory::KeyEscape);
    const bool Shift  = Input.IsKeyPressed(VirtualKeyCategory::KeyLeftShift) || Input.IsKeyPressed(VirtualKeyCategory::KeyRightShift);
    bool Changed = false;

    if (F3 && !F3Held_)
    {
        constexpr uint32_t N = static_cast<uint32_t>(DebugViewCategory::Count);
        if (!Open_) { Open_ = true; }                                   // first press: open the popup (view unchanged)
        else
        {
            const uint32_t Current = static_cast<uint32_t>(View_);
            View_ = static_cast<DebugViewCategory>(Shift ? (Current + N - 1u) % N : (Current + 1u) % N);
            Changed = true;
        }
    }
    if (F4 && !F4Held_) { Occlusion_ = !Occlusion_; Changed = true; }
    if (F5 && !F5Held_) { AliasPick_ = !AliasPick_; Changed = true; }   // R6 row 3: alias pick vs uniform identity
    // The patch preview's screen-error tolerance. Changing it changes which alternative every opaque patch
    //    selects THIS frame, so it is reported as a change like every other key here: the caller persists it and
    //    restarts the accumulation (a debug view does not accumulate, but returning to shaded rendering must not
    //    inherit a history shaded through a different mesh).
    if (F6 && !F6Held_)
    {
        PatchErrorPixels_ = PatchErrorPixels_ >= 8.0f ? 1.0f : PatchErrorPixels_ * 2.0f;
        Changed = true;
    }
    if (Escape && !EscapeHeld_ && Open_) { Open_ = false; }
    F3Held_ = F3; F4Held_ = F4; F5Held_ = F5; F6Held_ = F6; EscapeHeld_ = Escape;
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        LAYOUT
//------------------------------------------------------------------------------------------------------------------------

namespace {

void Thousands(char* Out, size_t Capacity, uint32_t Value)
{
    char Raw[16];
    const int Length = std::snprintf(Raw, sizeof(Raw), "%u", Value);
    size_t W = 0u;
    for (int I = 0; I < Length && W + 1u < Capacity; ++I)
    {
        if (I > 0 && (Length - I) % 3 == 0) Out[W++] = ' ';
        Out[W++] = Raw[I];
    }
    Out[W] = '\0';
}

} // namespace

void DiagnosticInspector::ConstructInspectorLayout(PixelSpace& Surface, float TopInset, float DisplayWidth, const VisibilityTelemetry& T,
                                                   uint32_t ClusterTotal, bool DrawIndirectCount, const ReSTIRIntegratorConfiguration& ReSTIR,
                                                   const MaterialIndexMetrics& MaterialStats, const TextureIndexMetrics& TextureStats,
                                                   uint32_t MaxTextureLevels, const char* MaterialSummary) noexcept
{
    PointerCaptured_ = false;
    if (!Open_ || !Surface.IsRecording()) return;

    (void)TopInset;
    (void)DisplayWidth;
    char Total[16], Frustum[16], Cone[16], Visible[16], One[16], Two[16], Tris[16];
    Thousands(Total,   sizeof(Total),   T.Valid ? T.ClusterTotal : ClusterTotal);
    Thousands(Frustum, sizeof(Frustum), T.FrustumPassed);
    Thousands(Cone,    sizeof(Cone),    T.ConePassed);
    Thousands(Visible, sizeof(Visible), T.OcclusionPassed);
    Thousands(One,     sizeof(One),     T.PhaseOneDraws);
    Thousands(Two,     sizeof(Two),     T.PhaseTwoDraws);
    Thousands(Tris,    sizeof(Tris),    T.TrianglesDrawn);

    // 8 rows since the Celestial port split the gpu line in two, plus the M7a material row when a selection exists.
    //    Everything below derives the row COUNT rather than repeating the literal — the hint row's index and the
    //    card height were both hardcoded 7s, so adding a row silently dropped the last line and mis-sized the card
    //    until they were derived.
    char Rows[9][160];
    std::snprintf(Rows[0], sizeof(Rows[0]), "clusters   %s  \xE2\x86\x92  frustum %s  \xE2\x86\x92  cone %s  \xE2\x86\x92  visible %s", Total, Frustum, Cone, Visible);
    // The patch line: how many of the drawn clusters took their coarse alternative, and what that saved. A
    //    reviewer dollying in and out watches these two figures move; if they do not move, the preview really
    //    is not selecting anything and the number says so instead of the tiles keeping the secret.
    char Fine[16];
    Thousands(Fine, sizeof(Fine), T.TrianglesFine);
    const uint32_t DrawnClusters = T.PhaseOneDraws + T.PhaseTwoDraws;
    const double Saved = T.TrianglesFine > 0u
                       ? 100.0 * (1.0 - static_cast<double>(T.TrianglesDrawn) / static_cast<double>(T.TrianglesFine))
                       : 0.0;
    std::snprintf(Rows[1], sizeof(Rows[1]),
                  "drawn      phase 1  %s   +   phase 2  %s   (%s triangles)   |   patch coarse %u/%u  \xE2\x86\x92  %s fine, -%.1f%%",
                  One, Two, Tris, T.CoarsePatches, DrawnClusters, Fine, Saved);
    std::snprintf(Rows[2], sizeof(Rows[2]), "indirect   %s   |   HiZ occlusion %s   |   patch error %g px%s   |   rays: CWBVH (Tier A)",
                  DrawIndirectCount ? "1 draw/phase" : "fixed-count", Occlusion_ ? "on" : "OFF",
                  static_cast<double>(PatchErrorPixels_),
                  (View_ == DebugViewCategory::PatchTiles || View_ == DebugViewCategory::PatchWire) ? "" : " (preview only)");
    // The shadow figure is only meaningful when the GI-off stage ran, so it is shown as a dash rather than 0.00
    //    otherwise — a zero would read as "shadows are free" instead of "shadows did not run this frame".
    // A stage that did not run this frame prints as an en dash, not 0.00: a zero would read as "free" rather
    //    than "absent", and exactly one of shadow/ReSTIR runs in any given mode.
    const auto Cell = [](char* Out, size_t N, float Ms) {
        if (Ms > 0.0f) std::snprintf(Out, N, "%.2f", static_cast<double>(Ms));
        else           std::snprintf(Out, N, "%s", "\xE2\x80\x93");
    };
    char ShadowCell[16], RestirCell[16], SkyCell[16], VolCell[16];
    Cell(ShadowCell, sizeof(ShadowCell), T.ShadowMilliseconds);
    Cell(RestirCell, sizeof(RestirCell), T.RestirMilliseconds);
    Cell(SkyCell,    sizeof(SkyCell),    T.SkyMilliseconds);
    Cell(VolCell,    sizeof(VolCell),    T.VolumeMilliseconds);
    // Two rows rather than one. With sky and volumetrics added the single row reached 158 of 160 bytes, which is
    //    not a margin — a four-digit frame time on a stalled GPU would silently truncate exactly when the reader
    //    most needs the number. Geometry on one line, shading stages on the next.
    std::snprintf(Rows[3], sizeof(Rows[3]), "gpu        cull %.2f  \xC2\xB7  raster %.2f  \xC2\xB7  HiZ %.2f  \xC2\xB7  resolve %.2f ms",
                  static_cast<double>(T.CullMilliseconds), static_cast<double>(T.RasterMilliseconds),
                  static_cast<double>(T.HiZMilliseconds), static_cast<double>(T.ResolveMilliseconds));
    std::snprintf(Rows[4], sizeof(Rows[4]), "shading    shadow %s  \xC2\xB7  restir %s  \xC2\xB7  sky %s  \xC2\xB7  volume %s  \xC2\xB7  post %.2f ms",
                  ShadowCell, RestirCell, SkyCell, VolCell, static_cast<double>(T.PostMilliseconds));
    std::snprintf(Rows[5], sizeof(Rows[5]), "restir     temporal %s  \xC2\xB7  spatial %s (%u taps)  \xC2\xB7  indirect pool %s  \xC2\xB7  alias pick %s  \xC2\xB7  %u cand + %u extra",
                  ReSTIR.TemporalReuse ? "on" : "OFF", ReSTIR.SpatialReuse ? "on" : "OFF", ReSTIR.SpatialTapCount,
                  ReSTIR.GlobalIlluminationReuse ? "on" : "OFF", ReSTIR.AliasPick ? "on" : "OFF",
                  ReSTIR.CandidatesPerPixel, ReSTIR.ExtraCandidateCount);
    std::snprintf(Rows[6], sizeof(Rows[6]), "scene      %u mats -> %u slabs (S %u Si %u C %u Sp %u)  \xC2\xB7  %u tex %.1f MB <= %u mips",
                  MaterialStats.DescriptorCount, MaterialStats.SlabCount,
                  MaterialStats.ComplexityCount[0], MaterialStats.ComplexityCount[1],
                  MaterialStats.ComplexityCount[2], MaterialStats.ComplexityCount[3],
                  TextureStats.Count, static_cast<double>(TextureStats.ByteCount) / 1048576.0, MaxTextureLevels);
    // The material row appears only while a material is selected (empty summary = no scene = omitted, not dashed).
    uint32_t RowCount = 8u;
    if (MaterialSummary && *MaterialSummary)
    {
        std::snprintf(Rows[7], sizeof(Rows[7]), "material   %s", MaterialSummary);
        RowCount = 9u;
    }
    std::snprintf(Rows[RowCount - 1u], sizeof(Rows[RowCount - 1u]), "F3 next  \xC2\xB7  Shift+F3 previous  \xC2\xB7  F4 HiZ on/off  \xC2\xB7  F5 alias pick  \xC2\xB7  F6 patch error  \xC2\xB7  Esc close");

    ImGui::SetNextWindowSize(ImVec2(390, 530), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(28, 90), ImGuiCond_FirstUseEver);
    const ImVec2 Display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(300.0f, Display.x), std::min(260.0f, Display.y)), Display);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 18));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(29, 29, 29, 248));
    if (ImGui::Begin("Statistics / Debug##native-diagnostics", &Open_, ImGuiWindowFlags_NoDocking))
    {
        PointerCaptured_ = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const ImVec2 Position = ImGui::GetWindowPos();
        const ImVec2 Extent = ImGui::GetWindowSize();
        ImGui::SetWindowPos(ImVec2(std::clamp(Position.x, 0.0f, std::max(0.0f, Display.x - Extent.x)),
                                   std::clamp(Position.y, 0.0f, std::max(0.0f, Display.y - Extent.y))));
        ImGui::TextDisabled("NATIVE / COMPLETED GPU TELEMETRY");
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##metric", &Metric_, "FPS\0Frame interval\0GPU span\0");
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##presentation", &Presentation_, "Stats and graph\0Stats only\0Graph only\0");
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##period", &Period_, "15 seconds\0 30 seconds\0 60 seconds\0");
        ImGui::Checkbox("Pause recording", &Paused_);
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) DurationCount_ = DurationCursor_ = 0;
        const unsigned Wanted = Period_ == 0 ? 30u : Period_ == 1 ? 60u : 120u;
        const unsigned Count = std::min(DurationCount_, Wanted);
        const auto Reading = [&](unsigned Position)
        {
            const auto& Sample = Durations_[(DurationCursor_ + 120 - Count + Position) % 120];
            return Metric_ == 0 ? Sample.Fps : Metric_ == 1 ? Sample.Milliseconds : Sample.Gpu;
        };
        float Minimum = 0, Maximum = 0, Sum = 0;
        unsigned Valid = 0;
        for (unsigned Position = 0; Position < Count; ++Position)
        {
            const float Measurement = Reading(Position);
            if (Measurement < 0) continue;
            Minimum = Valid ? std::min(Minimum, Measurement) : Measurement;
            Maximum = std::max(Maximum, Measurement);
            Sum += Measurement;
            ++Valid;
        }
        const char* Unit = Metric_ == 0 ? "FPS" : "ms";
        if (Presentation_ != 2)
        {
            if (Count && Reading(Count - 1) >= 0)
                ImGui::Text("%.2f %s", double(Reading(Count - 1)), Unit);
            else ImGui::TextUnformatted(Metric_ == 2 ? "GPU timing unavailable" : "Waiting for measurements");
            if (Valid) ImGui::TextWrapped("Mean %.2f   Min %.2f   Max %.2f %s", double(Sum / Valid), double(Minimum), double(Maximum), Unit);
        }
        if (Presentation_ != 1)
        {
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            const ImVec2 Size(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(70.0f, std::min(210.0f, ImGui::GetContentRegionAvail().y - 135.0f)));
            ImGui::InvisibleButton("##duration-graph", Size);
            auto* Commands = ImGui::GetWindowDrawList();
            Commands->AddRectFilled(Origin, ImVec2(Origin.x + Size.x, Origin.y + Size.y), IM_COL32(20, 20, 20, 255), 12);
            const float Ceiling = std::max(1.0f, Maximum * 1.15f);
            const auto Coordinate = [&](unsigned Position)
            {
                return ImVec2(Origin.x + 8 + (Size.x - 16) * Position / std::max(1u, Count - 1),
                              Origin.y + Size.y - 8 - (Size.y - 16) * Reading(Position) / Ceiling);
            };
            for (unsigned Line = 1; Line < 4; ++Line)
                Commands->AddLine(ImVec2(Origin.x + 8, Origin.y + Size.y * Line / 4),
                                  ImVec2(Origin.x + Size.x - 8, Origin.y + Size.y * Line / 4), IM_COL32(65, 65, 65, 130));
            for (unsigned Position = 1; Position < Count; ++Position)
                if (Reading(Position - 1) >= 0 && Reading(Position) >= 0)
                    Commands->AddLine(Coordinate(Position - 1), Coordinate(Position), IM_COL32(183, 196, 155, 255), 2);
            if (Count && ImGui::IsItemHovered())
            {
                const unsigned Position = unsigned(std::clamp((ImGui::GetIO().MousePos.x - Origin.x - 8) / std::max(1.0f, Size.x - 16), 0.0f, 1.0f) * (Count - 1));
                if (Reading(Position) >= 0) ImGui::SetTooltip("%.1f seconds ago\n%.2f %s", double(Count - 1 - Position) * 0.5, double(Reading(Position)), Unit);
                else ImGui::SetTooltip("Completed GPU timestamp unavailable");
            }
            ImGui::TextDisabled("0 - %.1f %s / %u samples", double(Ceiling), Unit, Count);
        }
        ImGui::TextWrapped(Metric_ == 2 ? "Latest completed GPU span at each sample. Excludes UI and presentation; delayed readback, never a CPU estimate." : "Half-second means of actual engine frame intervals. Includes pacing; this is not CPU work time.");
        if (ImGui::CollapsingHeader("Renderer details"))
        {
            ImGui::Text("View: %s", DebugViewName(View_));
            for (unsigned Row = 0; Row < RowCount; ++Row) ImGui::TextWrapped("%s", Rows[Row]);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void DiagnosticInspector::RecordDurations(float Seconds, const VisibilityTelemetry& Telemetry) noexcept
{
    if (Paused_ || !std::isfinite(Seconds) || Seconds <= 0 || Seconds > 1)
    {
        IntervalSeconds_ = 0;
        IntervalCount_ = 0;
        return;
    }
    IntervalSeconds_ += Seconds;
    ++IntervalCount_;
    if (IntervalSeconds_ < 0.5f) return;
    if (Durations_.empty()) Durations_.resize(120);
    const float Gpu = Telemetry.Valid && std::isfinite(Telemetry.FrameMilliseconds) && Telemetry.FrameMilliseconds > 0
                    ? Telemetry.FrameMilliseconds : -1.0f;
    Durations_[DurationCursor_] = {IntervalCount_ / IntervalSeconds_, IntervalSeconds_ * 1000 / IntervalCount_, Gpu};
    DurationCursor_ = (DurationCursor_ + 1) % 120;
    DurationCount_ = std::min(120u, DurationCount_ + 1);
    IntervalSeconds_ = 0;
    IntervalCount_ = 0;
}

} // namespace Frontier
