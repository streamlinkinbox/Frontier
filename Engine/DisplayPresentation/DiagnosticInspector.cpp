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

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERACTION
//------------------------------------------------------------------------------------------------------------------------

bool DiagnosticInspector::AdvanceInteraction(const InputExchange& Input) noexcept
{
    const bool F3     = Input.IsKeyPressed(VirtualKeyCategory::KeyF3);
    const bool F4     = Input.IsKeyPressed(VirtualKeyCategory::KeyF4);
    const bool F5     = Input.IsKeyPressed(VirtualKeyCategory::KeyF5);
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
    if (Escape && !EscapeHeld_ && Open_) { Open_ = false; }
    F3Held_ = F3; F4Held_ = F4; F5Held_ = F5; EscapeHeld_ = Escape;
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
                                                   uint32_t MaxTextureLevels) const noexcept
{
    if (!Open_ || !Surface.IsRecording()) return;

    const ControlKitPalette& P = ControlKit::Palette();
    constexpr float Inset = 16.0f, Padding = 12.0f, TitleSize = 13.0f, RowSize = 11.0f, RowGap = 4.0f, Width = 360.0f;

    char Title[64];
    std::snprintf(Title, sizeof(Title), "Debug View  \xC2\xB7  %s", DebugViewName(View_));

    char Total[16], Frustum[16], Cone[16], Visible[16], One[16], Two[16], Tris[16];
    Thousands(Total,   sizeof(Total),   T.Valid ? T.ClusterTotal : ClusterTotal);
    Thousands(Frustum, sizeof(Frustum), T.FrustumPassed);
    Thousands(Cone,    sizeof(Cone),    T.ConePassed);
    Thousands(Visible, sizeof(Visible), T.OcclusionPassed);
    Thousands(One,     sizeof(One),     T.PhaseOneDraws);
    Thousands(Two,     sizeof(Two),     T.PhaseTwoDraws);
    Thousands(Tris,    sizeof(Tris),    T.TrianglesDrawn);

    char Rows[7][128];
    std::snprintf(Rows[0], sizeof(Rows[0]), "clusters   %s  \xE2\x86\x92  frustum %s  \xE2\x86\x92  cone %s  \xE2\x86\x92  visible %s", Total, Frustum, Cone, Visible);
    std::snprintf(Rows[1], sizeof(Rows[1]), "drawn      phase 1  %s   +   phase 2  %s   (%s triangles)", One, Two, Tris);
    std::snprintf(Rows[2], sizeof(Rows[2]), "indirect   %s   |   HiZ occlusion %s   |   rays: CWBVH (Tier A)", DrawIndirectCount ? "1 draw/phase" : "fixed-count", Occlusion_ ? "on" : "OFF");
    std::snprintf(Rows[3], sizeof(Rows[3]), "gpu        cull %.2f  \xC2\xB7  raster %.2f  \xC2\xB7  HiZ %.2f  \xC2\xB7  resolve %.2f  \xC2\xB7  kernel %.2f ms",
                  static_cast<double>(T.CullMilliseconds), static_cast<double>(T.RasterMilliseconds), static_cast<double>(T.HiZMilliseconds),
                  static_cast<double>(T.ResolveMilliseconds), static_cast<double>(T.KernelMilliseconds));
    std::snprintf(Rows[4], sizeof(Rows[4]), "restir     temporal %s  \xC2\xB7  spatial %s  \xC2\xB7  alias pick %s  \xC2\xB7  %u cand + %u extra",
                  ReSTIR.TemporalReuse ? "on" : "OFF", ReSTIR.SpatialReuse ? "on" : "OFF", ReSTIR.AliasPick ? "on" : "OFF",
                  ReSTIR.CandidatesPerPixel, ReSTIR.ExtraCandidateCount);
    std::snprintf(Rows[5], sizeof(Rows[5]), "scene      %u mats -> %u slabs (S %u Si %u C %u Sp %u)  \xC2\xB7  %u tex %.1f MB <= %u mips",
                  MaterialStats.DescriptorCount, MaterialStats.SlabCount,
                  MaterialStats.ComplexityCount[0], MaterialStats.ComplexityCount[1],
                  MaterialStats.ComplexityCount[2], MaterialStats.ComplexityCount[3],
                  TextureStats.Count, static_cast<double>(TextureStats.ByteCount) / 1048576.0, MaxTextureLevels);
    std::snprintf(Rows[6], sizeof(Rows[6]), "F3 next  \xC2\xB7  Shift+F3 previous  \xC2\xB7  F4 HiZ on/off  \xC2\xB7  F5 alias pick  \xC2\xB7  Esc close");

    const PlanePoint TitleSizePx = Surface.MeasureText(Title, TitleSize);
    float ContentWidth = std::max(Width - Padding * 2.0f, TitleSizePx.X);
    float RowHeight = 0.0f;
    for (const char* Row : Rows) { const PlanePoint M = Surface.MeasureText(Row, RowSize); ContentWidth = std::max(ContentWidth, M.X); RowHeight = std::max(RowHeight, M.Y); }

    const float CardWidth  = ContentWidth + Padding * 2.0f;
    const float CardHeight = Padding * 2.0f + TitleSizePx.Y + 8.0f + 7.0f * (RowHeight + RowGap) - RowGap;
    const PlaneExtent Card = Spanning(DisplayWidth - Inset - CardWidth, TopInset + Inset, CardWidth, CardHeight);

    // Notch card: CardSub background, 1 px stroke, 12 px radius (matches the FPS pill and toasts).
    Surface.FillRectangle(Card, ColorQuad{ P.CardSub.Red, P.CardSub.Green, P.CardSub.Blue, 0.92f }, 12.0f);
    ControlKit::OutlineRounded(Surface, Card, P.Stroke, 12.0f, 1.0f);

    float Y = Card.MinimumY + Padding;
    Surface.Text(Card.MinimumX + Padding, Y, P.Text, Title, TitleSize);
    // Accent dot next to the title when a view other than Off is active.
    if (View_ != DebugViewCategory::Off)
        ControlKit::FillCircle(Surface, Card.MinimumX + Padding + TitleSizePx.X + 10.0f, Y + TitleSizePx.Y * 0.5f, 3.5f, P.Accent);
    Y += TitleSizePx.Y + 8.0f;

    for (uint32_t I = 0u; I < 7u; ++I)
    {
        const ColorQuad Ink = I == 6u ? P.TextDim : (I == 2u && !Occlusion_ ? ColorQuad{ 0xF5 / 255.0f, 0xA5 / 255.0f, 0x24 / 255.0f, 1.0f } : P.Text);
        Surface.Text(Card.MinimumX + Padding, Y, Ink, Rows[I], RowSize);
        Y += RowHeight + RowGap;
    }
}

} // namespace Frontier
