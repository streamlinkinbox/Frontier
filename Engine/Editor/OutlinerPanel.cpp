//============================================================================================================================================
//                                                     OUTLINERPANEL.CPP
//============================================================================================================================================
// 🧩 Development editor outliner — the celestial page's outliner, spoken in ImGui. Every figure below is the
//    page's own stylesheet, read as pixels: the 22 px head padding, the 20 px light title with its 12 px
//    "Scene · N nodes" sub, the 28 px round compact button, the 18 px census tiles with their 30 px thin
//    numerals, the 40 px search pill, the 26 px narrowing pills with their 6 px tint dots, the 36 px rows
//    indented 8 + 16 × depth with an 11 px radius, and the four-column foot strip. Icons are the page's own
//    SVG set, stroked live from VectorCodec's outliner glyph records through GlyphSpace's flattener, so the
//    headless proof rasterises the same paths the Vulkan build does.
//
//    Behaviour, also the page's: click picks (Ctrl toggles, Shift spans), the chevron opens, double-click on a
//    row toggles it, the eye hides, a drag reparents (top 28 % of a row = sibling before, the rest = child,
//    empty space = root, cycles refused), the search shows ancestors and force-opens, Tab compacts, Ctrl+Shift+F
//    lands in the search, and an empty outline says "Nothing here.".

#include "OutlinerPanel.h"

#include "ControlPanel.h"
#include "../DisplayPresentation/IconPresentation.h"
#include "../DisplayPresentation/GlyphSpace.h"
#include "../DisplayPresentation/VectorCodec.h"

#include <imgui.h>
#include <imgui_internal.h>   // ImGuiWindow: the SkipItems early-out; the dock tab bar hides behind the head

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace Frontier {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                          TOKENS
//------------------------------------------------------------------------------------------------------------------------
// The page's :root, as bytes over white — the panel never tints against anything but its own glass.

constexpr ImU32 kText    = IM_COL32(255, 255, 255, 240);   // --text .94
constexpr ImU32 kT2      = IM_COL32(255, 255, 255, 143);   // --t2 .56
constexpr ImU32 kT3      = IM_COL32(255, 255, 255, 82);    // --t3 .32
constexpr ImU32 kG2      = IM_COL32(255, 255, 255, 11);    // --g2 .045
constexpr ImU32 kG3      = IM_COL32(255, 255, 255, 20);    // --g3 .08
constexpr ImU32 kStroke  = IM_COL32(255, 255, 255, 18);    // --stroke .07
constexpr ImU32 kStroke2 = IM_COL32(255, 255, 255, 33);    // --stroke2 .13
constexpr ImU32 kWash    = IM_COL32(255, 255, 255, 5);     // the foot band, shared with its siblings
constexpr ImU32 kSelBg   = IM_COL32(255, 255, 255, 23);    // .node.sel .09
constexpr ImU32 kTileBg  = IM_COL32(255, 255, 255, 9);     // .ss .035
constexpr ImU32 kTileIco = IM_COL32(255, 255, 255, 15);    // .ss-ico .06
constexpr ImU32 kGlass   = IM_COL32(15, 16, 18, 189);      // --glass rgba(15,16,18,.74)
constexpr ImU32 kRed     = IM_COL32(0xFF, 0x3B, 0x30, 255);
constexpr ImU32 kGreen   = IM_COL32(0x34, 0xC7, 0x59, 255);
constexpr ImU32 kOrange  = IM_COL32(0xFF, 0xB4, 0x54, 255);
constexpr ImU32 kInkOnOk = IM_COL32(0x0B, 0x1A, 0x12, 255); // .nstat.ok color
constexpr ImU32 kWarnBg  = IM_COL32(255, 180, 84, 41);     // rgba(255,180,84,.16)
constexpr ImU32 kErrBg   = IM_COL32(255, 80, 80, 41);      // rgba(255,80,80,.16)
constexpr ImU32 kErrTile = IM_COL32(255, 80, 80, 38);      // .ss.warn .ss-ico .15
constexpr ImU32 kInfoBg  = IM_COL32(255, 255, 255, 20);    // rgba(255,255,255,.08)
constexpr ImU32 kWhite   = IM_COL32(255, 255, 255, 255);

// Pill tints: the page's FILTERS colours, and the folder / auto glyph accents.
constexpr ImU32 kPillLights   = IM_COL32(0xFF, 0xB4, 0x54, 255);
constexpr ImU32 kPillSky      = IM_COL32(0x5A, 0xA9, 0xFF, 255);
constexpr ImU32 kPillBodies   = IM_COL32(0xDF, 0xE6, 0xF5, 255);
constexpr ImU32 kPillGeometry = IM_COL32(0xE2, 0xE8, 0xF0, 255);
constexpr ImU32 kPillCamera   = IM_COL32(0x34, 0xC7, 0x59, 255);

// Metrics. The page at its resting 316 px; compact is 236.
constexpr float kHeadPadX     = 22.0f;
constexpr float kHeadPadTop   = 22.0f;
constexpr float kHeadPadBot   = 12.0f;
constexpr float kHeadPadCompX = 16.0f;
constexpr float kHeadPadCompT = 16.0f;
constexpr float kHeadPadCompB = 8.0f;
constexpr float kSidePad      = 14.0f;    // .scene-stat / .search / .filters
constexpr float kTreePad      = 10.0f;    // .tree padding 2px 10px 10px
constexpr float kRowH         = 39.0f;
constexpr float kRowHCompact  = 34.0f;
constexpr float kCadRowH      = 44.0f;   // document style: a two-line row
constexpr float kCadFolderH   = 34.0f;   // document style: a group head
constexpr float kCadStep      = 16.0f;   // document style: indent per depth
constexpr float kCadRadius    = 16.0f;   // document style: row ground
constexpr float kCadTile      = 15.0f;   // document style: half of the 30 px symbol tile
constexpr float kRowRadius    = 7.0f;    // rows are rectangles: selection, hover and drop ground all square
constexpr float kRowGap       = 7.0f;
constexpr float kChevBox      = 14.0f;
constexpr float kIcoBox       = 30.0f;
constexpr float kStatBox      = 16.0f;
constexpr float kEyeBox       = 24.0f;

ImU32 WithAlpha(ImU32 Tint, uint8_t Alpha) noexcept
{
    return (Tint & ~IM_COL32_A_MASK) | (static_cast<ImU32>(Alpha) << IM_COL32_A_SHIFT);
}

ImU32 ScaleAlpha(ImU32 Tint, float Scale) noexcept
{
    const float A = static_cast<float>((Tint >> IM_COL32_A_SHIFT) & 0xFFu) * Scale;
    return WithAlpha(Tint, static_cast<uint8_t>(A < 0.0f ? 0.0f : (A > 255.0f ? 255.0f : A)));
}

ImU32 RowTint(const EditorInstance& Row) noexcept
{
    return IM_COL32(static_cast<int>(Row.Tint[0] * 255.0f + 0.5f), static_cast<int>(Row.Tint[1] * 255.0f + 0.5f),
        static_cast<int>(Row.Tint[2] * 255.0f + 0.5f), 255);
}

bool ContainsFolded(const char* Hay, const char* Needle) noexcept
{
    if (Needle[0] == '\0')
    {
        return true;
    }
    for (; *Hay != '\0'; ++Hay)
    {
        const char* H = Hay;
        const char* N = Needle;
        while (*N != '\0' && *H != '\0'
            && std::tolower(static_cast<unsigned char>(*H)) == std::tolower(static_cast<unsigned char>(*N)))
        {
            ++H;
            ++N;
        }
        if (*N == '\0')
        {
            return true;
        }
    }
    return false;
}

void UpperCopy(char* Dst, uint32_t Cap, const char* Src) noexcept
{
    uint32_t i = 0u;
    for (; Src[i] != '\0' && i + 1u < Cap; ++i)
    {
        Dst[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(Src[i])));
    }
    Dst[i] = '\0';
}

// Letter-spaced text — ImGui has no tracking, so the spaced labels walk codepoint by codepoint (UTF-8 aware).
//    Returns the advance, so callers right-align off it.
float MeasureSpaced(ImFont* Font, float Size, const char* Text, float Spacing) noexcept
{
    float W = 0.0f;
    uint32_t N = 0u;
    for (const char* P = Text; *P != '\0';)
    {
        unsigned int C = 0u;
        const int Len = ImTextCharFromUtf8(&C, P, nullptr);
        if (Len <= 0)
        {
            break;
        }
        W += Font->CalcTextSizeA(Size, FLT_MAX, 0.0f, P, P + Len).x;
        P += Len;
        ++N;
    }
    return N > 0u ? W + Spacing * static_cast<float>(N - 1u) : 0.0f;
}

void DrawSpaced(ImDrawList* Draw, ImFont* Font, float Size, const ImVec2& Pos, ImU32 Tint, const char* Text,
                float Spacing) noexcept
{
    float X = Pos.x;
    for (const char* P = Text; *P != '\0';)
    {
        unsigned int C = 0u;
        const int Len = ImTextCharFromUtf8(&C, P, nullptr);
        if (Len <= 0)
        {
            break;
        }
        const ImVec2 G = Font->CalcTextSizeA(Size, FLT_MAX, 0.0f, P, P + Len);
        Draw->AddText(Font, Size, ImVec2(X, Pos.y), Tint, P, P + Len);
        X += G.x + Spacing;
        P += Len;
    }
}

// Text at a size, vertically centred on a line. The faces are the ones ControlPanel seated; the size is the
//    page's, so a 13 px name is 13 px whatever face size the host chose.
void DrawSized(ImDrawList* Draw, ImFont* Font, float Size, float X, float CentreY, ImU32 Tint, const char* Text,
               const char* End = nullptr) noexcept
{
    const ImVec2 G = Font->CalcTextSizeA(Size, FLT_MAX, 0.0f, Text, End);
    Draw->AddText(Font, Size, ImVec2(X, CentreY - G.y * 0.5f), Tint, Text, End);
}

float MeasureSized(ImFont* Font, float Size, const char* Text) noexcept
{
    return Font->CalcTextSizeA(Size, FLT_MAX, 0.0f, Text).x;
}

// Name with an ellipsis when it will not fit — the page's text-overflow.
void DrawClipped(ImDrawList* Draw, ImFont* Font, float Size, float X, float CentreY, float MaxW, ImU32 Tint,
                 const char* Text) noexcept
{
    if (MaxW <= 0.0f)
    {
        return;
    }
    if (MeasureSized(Font, Size, Text) <= MaxW)
    {
        DrawSized(Draw, Font, Size, X, CentreY, Tint, Text);
        return;
    }
    const char* Dots = "\xe2\x80\xa6";
    const float DotsW = MeasureSized(Font, Size, Dots);
    const char* End = Text;
    for (const char* P = Text; *P != '\0';)
    {
        unsigned int C = 0u;
        const int Len = ImTextCharFromUtf8(&C, P, nullptr);
        if (Len <= 0)
        {
            break;
        }
        if (Font->CalcTextSizeA(Size, FLT_MAX, 0.0f, Text, P + Len).x + DotsW > MaxW)
        {
            break;
        }
        P += Len;
        End = P;
    }
    DrawSized(Draw, Font, Size, X, CentreY, Tint, Text, End);
    const float Used = Font->CalcTextSizeA(Size, FLT_MAX, 0.0f, Text, End).x;
    DrawSized(Draw, Font, Size, X + Used, CentreY, Tint, Dots);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        SVG GLYPHS
//------------------------------------------------------------------------------------------------------------------------
// The page's icon function: 24-unit viewBox scaled into a Size box, stroke 1.6 (scaled), round caps and joins,
//    each child at its own opacity, dashed children walked segment by segment. GlyphSpace flattens the path;
//    the polylines land on the window's draw list, so both the headless proof and the Vulkan build see the
//    same triangles. The flattener returns its contours in a vector; that is the codec's own scratch, built
//    once per glyph draw, and the panel keeps none of it.

// Flattened once, kept for good: each glyph child's polylines in viewBox units, in fixed buffers. The
//    flattener allocates while it works; that happens the first time a glyph is asked for, never per tick.
struct FlatContour
{
    ImVec2   Points[96];
    uint32_t Count  = 0u;
    bool     Closed = false;
};

struct FlatStroke
{
    FlatContour Contours[6];
    uint32_t    Count = 0u;
};

struct FlatGlyph
{
    FlatStroke Strokes[kOutlinerGlyphStrokes];
    bool       Seated = false;
};

FlatGlyph gFlatGlyphs[static_cast<uint32_t>(OutlinerIconCategory::Count)];

void SeatFlat(FlatStroke& Out, std::string_view Path) noexcept
{
    const auto Contours = GlyphSpace::Flatten(Path);
    Out.Count = 0u;
    for (const GlyphSpace::Contour& Contour : Contours)
    {
        if (Out.Count >= 6u)
        {
            break;
        }
        FlatContour& F = Out.Contours[Out.Count++];
        F.Closed = Contour.Closed;
        F.Count  = 0u;
        for (const PlanePoint& P : Contour.Points)
        {
            if (F.Count >= 96u)
            {
                break;
            }
            F.Points[F.Count++] = ImVec2(P.X, P.Y);
        }
    }
}

// The warning triangle the foot strips hang off a Poor realtime band: three strokes, the upright bar,
//    and its dot. One painter, copied to each strip's file, so the three feet warn alike.
void FootWarn(ImDrawList* Draw, const ImVec2& At, float Size, ImU32 Tint) noexcept
{
    Draw->AddTriangle(ImVec2(At.x + Size * 0.5f, At.y), ImVec2(At.x + Size, At.y + Size),
        ImVec2(At.x, At.y + Size), Tint, 1.5f);
    Draw->AddLine(ImVec2(At.x + Size * 0.5f, At.y + Size * 0.34f),
        ImVec2(At.x + Size * 0.5f, At.y + Size * 0.62f), Tint, 1.5f);
    Draw->AddCircleFilled(ImVec2(At.x + Size * 0.5f, At.y + Size * 0.78f), 1.2f, Tint);
}

const FlatGlyph& FlatOf(OutlinerIconCategory Icon) noexcept
{
    FlatGlyph& G = gFlatGlyphs[static_cast<uint32_t>(Icon)];
    if (!G.Seated)
    {
        const OutlinerGlyphRecord& Glyph = VectorCodec::QueryOutlinerIcon(Icon);
        for (uint32_t s = 0u; s < Glyph.StrokeCount; ++s)
        {
            SeatFlat(G.Strokes[s], Glyph.Strokes[s].SvgPathString);
        }
        G.Seated = true;
    }
    return G;
}

void StrokeContour(ImDrawList* Draw, const FlatContour& Contour, const ImVec2& Origin, float Scale,
                   ImU32 Tint, float Thick, float DashOn, float DashOff) noexcept
{
    ImVec2 Points[96];
    const uint32_t N = Contour.Count;
    if (N < 2u)
    {
        if (N == 1u)
        {
            // A zero-length child ("h.01"): the page shows a round dot the width of the stroke.
            Draw->AddCircleFilled(ImVec2(Origin.x + Contour.Points[0].x * Scale, Origin.y + Contour.Points[0].y * Scale),
                Thick * 0.5f, Tint);
        }
        return;
    }
    for (uint32_t i = 0u; i < N; ++i)
    {
        Points[i] = ImVec2(Origin.x + Contour.Points[i].x * Scale, Origin.y + Contour.Points[i].y * Scale);
    }
    if (DashOn <= 0.0f)
    {
        Draw->AddPolyline(Points, static_cast<int>(N), Tint, Contour.Closed ? ImDrawFlags_Closed : ImDrawFlags_None, Thick);
        // Round caps: the page's stroke-linecap. ImGui cuts butt; a disc at each open end restores it.
        if (!Contour.Closed)
        {
            Draw->AddCircleFilled(Points[0], Thick * 0.5f, Tint);
            Draw->AddCircleFilled(Points[N - 1u], Thick * 0.5f, Tint);
        }
        return;
    }
    // Dashed: walk the polyline, emitting On-length runs separated by Off-length gaps.
    const float On  = DashOn * Scale;
    const float Off = DashOff * Scale;
    float Phase = 0.0f;    // distance into the current on/off cycle
    bool  Lit   = true;
    ImVec2 Run[64];
    int    RunN = 0;
    const uint32_t Segments = Contour.Closed ? N : N - 1u;
    for (uint32_t s = 0u; s < Segments; ++s)
    {
        ImVec2 A = Points[s];
        const ImVec2 B = Points[(s + 1u) % N];
        float Len = std::sqrt((B.x - A.x) * (B.x - A.x) + (B.y - A.y) * (B.y - A.y));
        if (Len <= 0.0001f)
        {
            continue;
        }
        const ImVec2 Dir((B.x - A.x) / Len, (B.y - A.y) / Len);
        if (Lit && RunN == 0)
        {
            Run[RunN++] = A;
        }
        while (Len > 0.0f)
        {
            const float Want = (Lit ? On : Off) - Phase;
            if (Want >= Len)
            {
                Phase += Len;
                A = B;
                if (Lit && RunN < 64)
                {
                    Run[RunN++] = A;
                }
                Len = 0.0f;
            }
            else
            {
                A = ImVec2(A.x + Dir.x * Want, A.y + Dir.y * Want);
                Len -= Want;
                Phase = 0.0f;
                if (Lit)
                {
                    if (RunN < 64)
                    {
                        Run[RunN++] = A;
                    }
                    if (RunN >= 2)
                    {
                        Draw->AddPolyline(Run, RunN, Tint, ImDrawFlags_None, Thick);
                        Draw->AddCircleFilled(Run[0], Thick * 0.5f, Tint);
                        Draw->AddCircleFilled(Run[RunN - 1], Thick * 0.5f, Tint);
                    }
                    RunN = 0;
                    Lit  = false;
                }
                else
                {
                    Lit = true;
                    Run[0] = A;
                    RunN   = 1;
                }
            }
        }
    }
    if (Lit && RunN >= 2)
    {
        Draw->AddPolyline(Run, RunN, Tint, ImDrawFlags_None, Thick);
        Draw->AddCircleFilled(Run[0], Thick * 0.5f, Tint);
        Draw->AddCircleFilled(Run[RunN - 1], Thick * 0.5f, Tint);
    }
}

void DrawIcon(ImDrawList* Draw, OutlinerIconCategory Icon, const ImVec2& Origin, float Size, ImU32 Tint) noexcept
{
    const OutlinerGlyphRecord& Glyph = VectorCodec::QueryOutlinerIcon(Icon);
    const FlatGlyph& Flat = FlatOf(Icon);
    const float Scale = Size / 24.0f;
    const float Thick = Glyph.StrokeWidth * Scale;
    for (uint32_t s = 0u; s < Glyph.StrokeCount; ++s)
    {
        const OutlinerGlyphStroke& Stroke = Glyph.Strokes[s];
        const ImU32 Ink = ScaleAlpha(Tint, Stroke.Opacity);
        const FlatStroke& FS = Flat.Strokes[s];
        for (uint32_t c = 0u; c < FS.Count; ++c)
        {
            const FlatContour& Contour = FS.Contours[c];
            if (Stroke.Filled)
            {
                ImVec2 Points[96];
                for (uint32_t i = 0u; i < Contour.Count; ++i)
                {
                    Points[i] = ImVec2(Origin.x + Contour.Points[i].x * Scale, Origin.y + Contour.Points[i].y * Scale);
                }
                if (Contour.Count >= 3u)
                {
                    Draw->AddConvexPolyFilled(Points, static_cast<int>(Contour.Count), Ink);
                }
                continue;
            }
            StrokeContour(Draw, Contour, Origin, Scale, Ink, Thick, Stroke.DashOn, Stroke.DashOff);
        }
    }
}

// Rotated chevron: the page turns the same glyph 90° when a row stands open. `Turn` carries the row's open
//    phase, so the glyph SWEEPS through the quarter turn with the fold instead of snapping at the click; 0 and 1
//    reproduce the two poses the discrete version drew, to the float.
void DrawChevron(ImDrawList* Draw, const ImVec2& Centre, float Size, ImU32 Tint, float Turn) noexcept
{
    const float Sweep = Turn < 0.0f ? 0.0f : (Turn > 1.0f ? 1.0f : Turn);
    const float Angle = Sweep * 1.57079633f;
    const float Cos = std::cos(Angle), Sin = std::sin(Angle);
    const FlatGlyph& Flat = FlatOf(OutlinerIconCategory::Chevron);
    const float Scale = Size / 24.0f;
    const float Thick = 1.6f * Scale;
    const FlatStroke& FS = Flat.Strokes[0];
    for (uint32_t c = 0u; c < FS.Count; ++c)
    {
        const FlatContour& Contour = FS.Contours[c];
        ImVec2 Points[96];
        for (uint32_t i = 0u; i < Contour.Count; ++i)
        {
            const float Lx = (Contour.Points[i].x - 12.0f) * Scale;
            const float Ly = (Contour.Points[i].y - 12.0f) * Scale;
            Points[i] = ImVec2(Centre.x + Lx * Cos - Ly * Sin, Centre.y + Lx * Sin + Ly * Cos);
        }
        if (Contour.Count >= 2u)
        {
            Draw->AddPolyline(Points, static_cast<int>(Contour.Count), Tint, ImDrawFlags_None, Thick);
            Draw->AddCircleFilled(Points[0], Thick * 0.5f, Tint);
            Draw->AddCircleFilled(Points[Contour.Count - 1u], Thick * 0.5f, Tint);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SYMBOLS
//------------------------------------------------------------------------------------------------------------------------
// A tool's own row art, drawn live in the row's tint: the symbol inside a tinted tile, for a folder row and for a figure
//    row alike, so a folder shows the very symbol its figures wear. A row with no symbol keeps the stock folder shell.
//    Each symbol is the web outliner's own strokes, scaled to the row size.

void DrawSymbol(ImDrawList* Draw, EditorSymbol Mark, const ImVec2& C, float Half, ImU32 Ink, float Thick) noexcept
{
    // The web outliner's own strokes, in its 24-unit viewBox, scaled so the whole box is Half * 2.4 px across.
    const float U = Half * 0.1f;
    auto P = [&](float X, float Y) noexcept { return ImVec2(C.x + (X - 12.0f) * U, C.y + (Y - 12.0f) * U); };
    auto Run = [&](const float (*Points)[2], uint32_t Count, bool Shut) noexcept
    {
        for (uint32_t i = 0u; i < Count; ++i)
            Draw->PathLineTo(P(Points[i][0], Points[i][1]));
        Draw->PathStroke(Ink, Shut ? ImDrawFlags_Closed : ImDrawFlags_None, Thick);
    };
    auto Stroke = [&](float X0, float Y0, float X1, float Y1) noexcept { Draw->AddLine(P(X0, Y0), P(X1, Y1), Ink, Thick); };
    switch (Mark)
    {
    case EditorSymbol::Line:
        // The curve: M3 17 c4-10 10 4 18-8.
        Draw->PathClear();
        Draw->PathLineTo(P(3.0f, 17.0f));
        Draw->PathBezierCubicCurveTo(P(7.0f, 7.0f), P(13.0f, 21.0f), P(21.0f, 9.0f), 14);
        Draw->PathStroke(Ink, ImDrawFlags_None, Thick);
        break;
    case EditorSymbol::Profile:
    {
        // The sketch zig-zag: M4 20 8 6l4 10 3-6 5 10.
        static const float Zig[5][2] = { { 4.0f, 20.0f }, { 8.0f, 6.0f }, { 12.0f, 16.0f }, { 15.0f, 10.0f }, { 20.0f, 20.0f } };
        Draw->PathClear();
        Run(Zig, 5u, false);
        break;
    }
    case EditorSymbol::Body:
    {
        static const float Hex[6][2] = { { 12.0f, 3.0f }, { 20.0f, 7.5f }, { 20.0f, 16.5f }, { 12.0f, 21.0f }, { 4.0f, 16.5f }, { 4.0f, 7.5f } };
        Draw->PathClear();
        Run(Hex, 6u, true);
        Stroke(12.0f, 12.0f, 20.0f, 7.5f);
        Stroke(12.0f, 12.0f, 12.0f, 21.0f);
        Stroke(12.0f, 12.0f, 4.0f, 7.5f);
        break;
    }
    case EditorSymbol::Surface:
    {
        // M3 8l7-4 11 4-7 4z  M3 8v8l11 4v-8  M21 8v8l-7 4
        static const float Top[4][2]  = { { 3.0f, 8.0f }, { 10.0f, 4.0f }, { 21.0f, 8.0f }, { 14.0f, 12.0f } };
        static const float Left[4][2] = { { 3.0f, 8.0f }, { 3.0f, 16.0f }, { 14.0f, 20.0f }, { 14.0f, 12.0f } };
        static const float Right[3][2] = { { 21.0f, 8.0f }, { 21.0f, 16.0f }, { 14.0f, 20.0f } };
        Draw->PathClear();
        Run(Top, 4u, true);
        Draw->PathClear();
        Run(Left, 4u, false);
        Draw->PathClear();
        Run(Right, 3u, false);
        break;
    }
    case EditorSymbol::Construction:
    {
        // The work plane: M3 15l6-8h12l-6 8z  M9 7v10M15 7v10
        static const float Plane[4][2] = { { 3.0f, 15.0f }, { 9.0f, 7.0f }, { 21.0f, 7.0f }, { 15.0f, 15.0f } };
        Draw->PathClear();
        Run(Plane, 4u, true);
        Stroke(9.0f, 7.0f, 9.0f, 17.0f);
        Stroke(15.0f, 7.0f, 15.0f, 17.0f);
        break;
    }
    case EditorSymbol::Dimension:
    {
        // M4 18V6M20 18V6M4 12h16M8 9l-4 3 4 3M16 9l4 3-4 3
        static const float Left[3][2]  = { { 8.0f, 9.0f }, { 4.0f, 12.0f }, { 8.0f, 15.0f } };
        static const float Right[3][2] = { { 16.0f, 9.0f }, { 20.0f, 12.0f }, { 16.0f, 15.0f } };
        Stroke(4.0f, 18.0f, 4.0f, 6.0f);
        Stroke(20.0f, 18.0f, 20.0f, 6.0f);
        Stroke(4.0f, 12.0f, 20.0f, 12.0f);
        Draw->PathClear();
        Run(Left, 3u, false);
        Draw->PathClear();
        Run(Right, 3u, false);
        break;
    }
    case EditorSymbol::Constraint:
        // The link glyph's two joined loops.
        Draw->AddCircle(P(8.5f, 12.0f), 5.2f * U, Ink, 16, Thick);
        Draw->AddCircle(P(15.5f, 12.0f), 5.2f * U, Ink, 16, Thick);
        break;
    default:
        break;
    }
}

// Seats the tile and the symbol inside it, in the 30 px icon box whose top-left is Box. Only a folder row with no symbol
//    of its own falls back to the flat two-tone folder: a darker tab and back panel behind a lighter front body, with a
//    white paper strip between the two while it is open.
void DrawSymbolArt(ImDrawList* Draw, const EditorInstance& Row, const ImVec2& Box, float Cy, ImU32 Tint, float Fade, bool Open, bool Document) noexcept
{
    const bool  Folder = Row.Category == EditorInstanceCategory::Folder && Row.Symbol == EditorSymbol::None;
    const float Cx     = Box.x + kIcoBox * 0.5f;
    const ImU32 Ink    = ScaleAlpha(Tint, Fade);
    auto Wash = [&](uint8_t Alpha) { return ScaleAlpha(WithAlpha(Tint, Alpha), Fade); };
    if (Folder)
    {
        const float R = static_cast<float>( Tint        & 0xFFu);
        const float G = static_cast<float>((Tint >> 8u)  & 0xFFu);
        const float B = static_cast<float>((Tint >> 16u) & 0xFFu);
        const ImU32 Back  = ScaleAlpha(IM_COL32(static_cast<int>(R * 0.62f), static_cast<int>(G * 0.62f), static_cast<int>(B * 0.62f), 255), Fade);
        const ImU32 Front = ScaleAlpha(IM_COL32(static_cast<int>(R * 0.96f + 8.0f), static_cast<int>(G * 0.96f + 8.0f), static_cast<int>(B * 0.96f + 8.0f), 255), Fade);
        const ImU32 Paper = ScaleAlpha(IM_COL32(244, 246, 250, 255), Fade);

        const float X0 = Cx - 13.0f, X1 = Cx + 13.0f;
        const float Y0 = Cy - 10.0f, Y1 = Cy + 10.0f;
        // Back panel with its tab: the tab rises on the left, the panel runs the full width beneath it.
        Draw->AddRectFilled(ImVec2(X0, Y0), ImVec2(X0 + 11.0f, Y0 + 7.0f), Back, 3.0f, ImDrawFlags_RoundCornersTop);
        Draw->AddRectFilled(ImVec2(X0, Y0 + 3.0f), ImVec2(X1, Y1), Back, 3.0f);
        // Paper, only while open.
        if (Open)
            Draw->AddRectFilled(ImVec2(X0 + 2.5f, Y0 + 4.5f), ImVec2(X1 - 2.5f, Y0 + 11.0f), Paper, 1.5f);
        // Front body: lighter, starts lower when the paper shows.
        Draw->AddRectFilled(ImVec2(X0, Y0 + (Open ? 7.5f : 6.0f)), ImVec2(X1, Y1), Front, 3.0f);
    }
    else
    {
        // Document style: the HTML's 30 px tile, its own colour at 14 % inside a 35 % hairline, the mark a little larger.
        const float Half = Document ? kCadTile : 11.0f;
        const ImVec2 A(Cx - Half, Cy - Half), B(Cx + Half, Cy + Half);
        // A folder row wears the same tile as its figures, a little stronger, so the group reads before its rows do.
        const bool Group = Row.Category == EditorInstanceCategory::Folder;
        Draw->AddRectFilled(A, B, Wash(Group ? 64 : (Document ? 36 : 38)), Document ? 10.0f : 7.0f);
        Draw->AddRect(A, B, Wash(Group ? 150 : (Document ? 90 : 150)), Document ? 10.0f : 7.0f, 0, 1.0f);
        DrawSymbol(Draw, Row.Symbol, ImVec2(Cx, Cy), Document ? 6.5f : 5.0f, Ink, Document ? 1.5f : 1.3f);
    }
}

// One capsule outline, turned to Angle: the link glyph is two of them overlapped.
void StrokeCapsule(ImDrawList* Draw, const ImVec2& C, float Half, float Radius, float Angle, ImU32 Ink, float Thick) noexcept
{
    const float Dx = std::cos(Angle), Dy = std::sin(Angle);
    const ImVec2 Near(C.x - Dx * Half, C.y - Dy * Half), Far(C.x + Dx * Half, C.y + Dy * Half);
    Draw->PathClear();
    Draw->PathArcTo(Far, Radius, Angle - 1.57079633f, Angle + 1.57079633f, 8);
    Draw->PathArcTo(Near, Radius, Angle + 1.57079633f, Angle + 4.71238898f, 8);
    Draw->PathStroke(Ink, ImDrawFlags_Closed, Thick);
}

// The document row's status: a bare glyph in its own colour, no disc. Seated reads as a green check, a derived body as a
//    cyan link, a reference as the violet plane, a warning as an orange triangle on a faint wash.
enum class RowStatus : uint32_t { Seated, Linked, Reference, Warning, None };

RowStatus StatusOf(const EditorInstance& Row, EditorStanding Standing) noexcept
{
    if (Standing == EditorStanding::Warn || Standing == EditorStanding::Err)
    {
        return Row.Visible ? RowStatus::Warning : RowStatus::None;
    }
    if (!Row.Visible)
    {
        return RowStatus::None;
    }
    if (std::strcmp(Row.Tag, "Live") == 0)
    {
        return RowStatus::Linked;
    }
    if (std::strcmp(Row.Tag, "Ref") == 0)
    {
        return RowStatus::Reference;
    }
    return RowStatus::Seated;
}

void DrawRowStatus(ImDrawList* Draw, RowStatus Status, const ImVec2& C, float Fade) noexcept
{
    switch (Status)
    {
    case RowStatus::Seated:
        DrawIcon(Draw, OutlinerIconCategory::Check, ImVec2(C.x - 6.5f, C.y - 6.5f), 13.0f, ScaleAlpha(kGreen, Fade));
        break;
    case RowStatus::Linked:
    {
        const ImU32 Ink = ScaleAlpha(IM_COL32(79, 216, 224, 255), Fade);
        StrokeCapsule(Draw, ImVec2(C.x - 2.4f, C.y + 2.4f), 2.4f, 2.7f, -0.78539816f, Ink, 1.4f);
        StrokeCapsule(Draw, ImVec2(C.x + 2.4f, C.y - 2.4f), 2.4f, 2.7f, -0.78539816f, Ink, 1.4f);
        break;
    }
    case RowStatus::Reference:
    {
        const ImU32 Ink = ScaleAlpha(IM_COL32(180, 140, 255, 255), Fade);
        const ImVec2 Quad[4] = { ImVec2(C.x - 6.0f, C.y + 3.5f), ImVec2(C.x - 2.5f, C.y - 3.5f), ImVec2(C.x + 6.0f, C.y - 3.5f), ImVec2(C.x + 2.5f, C.y + 3.5f) };
        Draw->AddPolyline(Quad, 4, Ink, ImDrawFlags_Closed, 1.4f);
        break;
    }
    case RowStatus::Warning:
        Draw->AddRectFilled(ImVec2(C.x - 11.0f, C.y - 11.0f), ImVec2(C.x + 11.0f, C.y + 11.0f), ScaleAlpha(kWarnBg, Fade), 7.0f);
        DrawIcon(Draw, OutlinerIconCategory::Warn, ImVec2(C.x - 6.0f, C.y - 6.0f), 12.0f, ScaleAlpha(kOrange, Fade));
        break;
    default:
        break;
    }
}

IconSymbol ArtworkFor(const EditorInstance& Row) noexcept
{
    // A base mesh names itself through its artwork — editor-cube, editor-sphere and the rest — exactly as
    //    Editor.jsx seats them. Everything downstream (the fracture card's primitive, the viewport marker)
    //    reads it back off this, so the row must carry it rather than fall through to the generic mesh.
    if (Row.Artwork != IconSymbol::Count) return Row.Artwork;
    switch (Row.Glyph)
    {
    case EditorGlyph::Fog: case EditorGlyph::AerialFog: return IconSymbol::Fog;
    case EditorGlyph::Effects: return IconSymbol::EnvironmentExposure;
    case EditorGlyph::Globe: return IconSymbol::FolderWorld;
    case EditorGlyph::Folder: return IconSymbol::FolderGeneric;
    case EditorGlyph::Camera: return IconSymbol::Camera;
    case EditorGlyph::Sun: return IconSymbol::Sun;
    case EditorGlyph::Stars: return IconSymbol::OutlinerStars;
    case EditorGlyph::Moon: return IconSymbol::Moon;
    case EditorGlyph::Sky: return IconSymbol::SkyScattering;
    case EditorGlyph::Cloud: return IconSymbol::Clouds;
    case EditorGlyph::VolumeClouds: return IconSymbol::Clouds;
    case EditorGlyph::LocalCloud: return IconSymbol::LocalCloud;
    case EditorGlyph::VolumeFog: return IconSymbol::LocalFog;
    case EditorGlyph::Wind: return IconSymbol::Wind;
    case EditorGlyph::Rain: return IconSymbol::OutlinerPrecipitation;
    case EditorGlyph::Rainbow: return IconSymbol::Rainbow;
    case EditorGlyph::Flare: return IconSymbol::LensFlare;
    case EditorGlyph::Plane: return IconSymbol::EditorMesh;
    case EditorGlyph::Bulb: return IconSymbol::EditorPointLight;
    default: break;
    }
    if (Row.Glyph != EditorGlyph::Auto) return IconSymbol::Count;
    switch (Row.Category)
    {
    case EditorInstanceCategory::Camera: return IconSymbol::Camera;
    case EditorInstanceCategory::Folder: return IconSymbol::FolderGeneric;
    case EditorInstanceCategory::Light: return IconSymbol::EditorPointLight;
    case EditorInstanceCategory::Geometry: return IconSymbol::EditorMesh;
    default: return IconSymbol::Count;
    }
}

OutlinerIconCategory IconFor(const EditorInstance& Row) noexcept
{
    if (Row.Glyph != EditorGlyph::Auto)
    {
        // EditorGlyph mirrors OutlinerIconCategory one for one, offset by Auto.
        return static_cast<OutlinerIconCategory>(static_cast<uint32_t>(Row.Glyph) - 1u);
    }
    switch (Row.Category)
    {
    case EditorInstanceCategory::Light:  return OutlinerIconCategory::Bulb;
    case EditorInstanceCategory::Camera: return OutlinerIconCategory::Camera;
    case EditorInstanceCategory::Folder: return OutlinerIconCategory::Folder;
    case EditorInstanceCategory::Geometry:
    default:                             return OutlinerIconCategory::Plane;
    }
}

EditorNarrowing NarrowingFor(const EditorInstance& Row) noexcept
{
    if (Row.Narrowing != EditorNarrowing::Auto)
    {
        return Row.Narrowing;
    }
    switch (Row.Category)
    {
    case EditorInstanceCategory::Light:    return EditorNarrowing::Lights;
    case EditorInstanceCategory::Camera:   return EditorNarrowing::Camera;
    case EditorInstanceCategory::Geometry: return EditorNarrowing::Geometry;
    case EditorInstanceCategory::Folder:
    default:                               return EditorNarrowing::Auto;   // folders pass through their rows
    }
}

ImU32 DefaultPillTint(EditorNarrowing Narrowing) noexcept
{
    switch (Narrowing)
    {
    case EditorNarrowing::Lights:   return kPillLights;
    case EditorNarrowing::Sky:      return kPillSky;
    case EditorNarrowing::Bodies:   return kPillBodies;
    case EditorNarrowing::Geometry: return kPillGeometry;
    case EditorNarrowing::Camera:   return kPillCamera;
    default:                        return kT2;
    }
}

const char* DefaultPillLabel(EditorNarrowing Narrowing) noexcept
{
    switch (Narrowing)
    {
    case EditorNarrowing::Lights:   return "Lights";
    case EditorNarrowing::Sky:      return "Sky";
    case EditorNarrowing::Bodies:   return "Bodies";
    case EditorNarrowing::Geometry: return "Geometry";
    case EditorNarrowing::Camera:   return "Camera";
    default:                        return "";
    }
}

constexpr uint32_t kDefaultFilterCount = static_cast<uint32_t>(EditorNarrowing::Count) - 1u;

uint32_t NarrowingMask(EditorNarrowing Narrowing) noexcept
{
    const uint32_t Slot = static_cast<uint32_t>(Narrowing);
    if (Slot == 0u || Slot >= 32u)
        return 0u;
    return 1u << Slot;
}

EditorNarrowing DefaultFilterNarrowing(uint32_t Slot) noexcept
{
    return Slot < kDefaultFilterCount ? static_cast<EditorNarrowing>(Slot + 1u) : EditorNarrowing::Auto;
}

uint32_t DefaultFilterMask(uint32_t Slot) noexcept
{
    return NarrowingMask(DefaultFilterNarrowing(Slot));
}

uint32_t RowFilterMask(const EditorInstance& Row) noexcept
{
    if (Row.FilterMask != 0u)
        return Row.FilterMask;
    return NarrowingMask(NarrowingFor(Row));
}

// The row after Index's run: the next row at Index's depth or shallower.
uint32_t RunEnd(const EditorInstance* Instances, uint32_t InstanceCount, uint32_t Index) noexcept
{
    uint32_t End = Index + 1u;
    while (End < InstanceCount && Instances[End].Depth > Instances[Index].Depth)
    {
        ++End;
    }
    return End;
}

uint32_t OwnerOf(const EditorInstance* Instances, uint32_t Index) noexcept
{
    if (Instances[Index].Depth == 0u)
    {
        return kNoEditorInstance;
    }
    for (uint32_t k = Index; k > 0u; --k)
    {
        if (Instances[k - 1u].Depth < Instances[Index].Depth)
        {
            return k - 1u;
        }
    }
    return kNoEditorInstance;
}

// The standing the row shows: the feed's word when it gave one, else hidden → err, else ok. Folders total
//    their run: any warn or err below reads as "N issues".
void StandingOf(const EditorInstance* Instances, uint32_t InstanceCount, uint32_t Index, bool HasKids,
                EditorStanding& Standing, char* Note, uint32_t NoteCap) noexcept
{
    const EditorInstance& Row = Instances[Index];
    if (HasKids && Row.Category == EditorInstanceCategory::Folder)
    {
        uint32_t Bad = 0u;
        const uint32_t End = RunEnd(Instances, InstanceCount, Index);
        for (uint32_t k = Index + 1u; k < End; ++k)
        {
            if (Instances[k].Depth != Row.Depth + 1u)
            {
                continue;
            }
            EditorStanding S = Instances[k].Standing;
            if (S == EditorStanding::Auto)
            {
                S = Instances[k].Visible ? EditorStanding::Ok : EditorStanding::Err;
            }
            if (S == EditorStanding::Warn || S == EditorStanding::Err)
            {
                ++Bad;
            }
        }
        Standing = Bad > 0u ? EditorStanding::Warn : EditorStanding::Ok;
        if (Bad > 0u)
        {
            std::snprintf(Note, NoteCap, "%u issue%s", Bad, Bad > 1u ? "s" : "");
        }
        else
        {
            std::snprintf(Note, NoteCap, "All good");
        }
        return;
    }
    if (!Row.Visible)
    {
        Standing = EditorStanding::Err;
        std::snprintf(Note, NoteCap, "Hidden");
        return;
    }
    if (Row.Standing != EditorStanding::Auto)
    {
        Standing = Row.Standing;
        std::snprintf(Note, NoteCap, "%s", Row.StandingNote[0] != '\0' ? Row.StandingNote : "Seated");
        return;
    }
    Standing = EditorStanding::Ok;
    std::snprintf(Note, NoteCap, "Seated");
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                           WIRING
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::AssignControls(ControlPanel* Controls) noexcept
{
    Controls_ = Controls;
    IconPresentation::Attach();
}

void OutlinerPanel::AssignTabOpen(bool* Open) noexcept
{
    TabOpen_ = Open;
}

void OutlinerPanel::AssignWindowTitle(const char* Title) noexcept
{
    WindowTitle_ = (Title != nullptr && Title[0] != '\0') ? Title : "Outliner";
}

void OutlinerPanel::AssignReadout(const EditorReadout* Readout) noexcept
{
    Readout_ = Readout;
}

void OutlinerPanel::ClearFilterCatalog() noexcept
{
    FilterCount_ = 0u;
    for (uint32_t Slot = 0u; Slot < kMaxOutlinerFilters; ++Slot)
    {
        FilterOn_[Slot] = false;
        FilterLabels_[Slot] = nullptr;
        FilterTints_[Slot] = 0u;
        FilterMasks_[Slot] = 0u;
    }
}

void OutlinerPanel::AssignFilter(uint32_t Slot, const char* Label, uint32_t Tint, uint32_t Mask) noexcept
{
    if (Slot >= kMaxOutlinerFilters || Label == nullptr || Label[0] == '\0' || Mask == 0u)
        return;
    FilterLabels_[Slot] = Label;
    FilterTints_[Slot]  = Tint;
    FilterMasks_[Slot]  = Mask;
    if (FilterCount_ <= Slot)
        FilterCount_ = Slot + 1u;
}

void OutlinerPanel::AssignFilterCatalog(const OutlinerFilterEntry* Entries, uint32_t Count) noexcept
{
    ClearFilterCatalog();
    if (Entries == nullptr)
        return;
    const uint32_t Clamped = Count < kMaxOutlinerFilters ? Count : kMaxOutlinerFilters;
    for (uint32_t Slot = 0u; Slot < Clamped; ++Slot)
        AssignFilter(Slot, Entries[Slot].Label, Entries[Slot].Tint, Entries[Slot].Mask);
}

void OutlinerPanel::AssignNarrowingSlot(EditorNarrowing Slot, const char* Label, uint32_t Tint) noexcept
{
    const uint32_t Index = static_cast<uint32_t>(Slot);
    if (Index == 0u || Index >= static_cast<uint32_t>(EditorNarrowing::Count))
        return;
    if (FilterCount_ == 0u)
    {
        for (uint32_t DefaultSlot = 0u; DefaultSlot < kDefaultFilterCount; ++DefaultSlot)
        {
            const EditorNarrowing Narrowing = DefaultFilterNarrowing(DefaultSlot);
            AssignFilter(DefaultSlot, DefaultPillLabel(Narrowing), DefaultPillTint(Narrowing), NarrowingMask(Narrowing));
        }
    }
    AssignFilter(Index - 1u, Label, Tint, NarrowingMask(Slot));
}

uint32_t OutlinerPanel::QueryFilterCount() const noexcept
{
    return FilterCount_ != 0u ? FilterCount_ : kDefaultFilterCount;
}

const char* OutlinerPanel::QueryFilterLabel(uint32_t Slot) const noexcept
{
    if (FilterCount_ != 0u)
        return (Slot < FilterCount_ && FilterLabels_[Slot] != nullptr) ? FilterLabels_[Slot] : "";
    return DefaultPillLabel(DefaultFilterNarrowing(Slot));
}

uint32_t OutlinerPanel::QueryFilterTint(uint32_t Slot) const noexcept
{
    if (FilterCount_ != 0u)
        return (Slot < FilterCount_ && FilterTints_[Slot] != 0u) ? FilterTints_[Slot] : kT2;
    return DefaultPillTint(DefaultFilterNarrowing(Slot));
}

uint32_t OutlinerPanel::QueryFilterMask(uint32_t Slot) const noexcept
{
    if (FilterCount_ != 0u)
        return (Slot < FilterCount_ && FilterMasks_[Slot] != 0u) ? FilterMasks_[Slot] : 0u;
    return DefaultFilterMask(Slot);
}

uint32_t OutlinerPanel::QuerySelectedFilterMask() const noexcept
{
    uint32_t Mask = 0u;
    const uint32_t Count = QueryFilterCount();
    for (uint32_t Slot = 0u; Slot < Count && Slot < kMaxOutlinerFilters; ++Slot)
        if (FilterOn_[Slot])
            Mask |= QueryFilterMask(Slot);
    return Mask;
}

uint32_t OutlinerPanel::QueryPicked() const noexcept
{
    return PickedCount_ > 0u ? Picked_[0] : kNoEditorInstance;
}

uint32_t OutlinerPanel::QueryPickedCount() const noexcept
{
    return PickedCount_;
}

uint32_t OutlinerPanel::QueryPickedAt(uint32_t Slot) const noexcept
{
    return Slot < PickedCount_ ? Picked_[Slot] : kNoEditorInstance;
}

void OutlinerPanel::RevealInstance(uint32_t Index, const EditorInstance* Rows, uint32_t Count) noexcept
{
    if (!Rows || Index >= Count || Index >= kMaxEditorInstances) return;
    QueryText_[0] = 0;
    for (bool& Enabled : FilterOn_) Enabled = false;
    uint32_t Depth = Rows[Index].Depth;
    for (uint32_t Earlier = Index; Earlier > 0 && Depth > 0;)
    {
        --Earlier;
        if (Rows[Earlier].Depth < Depth)
        {
            Shut_[Earlier] = false;
            Depth = Rows[Earlier].Depth;
        }
    }
    PickInstance(Index);
    Revealed_ = kNoEditorInstance;
}

void OutlinerPanel::PickInstance(uint32_t Index) noexcept
{
    ExplicitPick_=true;
    Picked_[0]   = Index;
    PickedCount_ = 1u;
    Anchor_      = Index;
}

void OutlinerPanel::AssignPicks(const uint32_t* Rows, uint32_t Count) noexcept
{
    ExplicitPick_ = true;
    PickedCount_  = 0u;
    for (uint32_t Slot = 0u; Rows != nullptr && Slot < Count && PickedCount_ < kMaxEditorPicked; ++Slot)
        AddPick(Rows[Slot]);
    Anchor_ = PickedCount_ > 0u ? Picked_[0] : kNoEditorInstance;
}

bool OutlinerPanel::IsPicked(uint32_t Index) const noexcept
{
    for (uint32_t i = 0u; i < PickedCount_; ++i)
    {
        if (Picked_[i] == Index)
        {
            return true;
        }
    }
    return false;
}

void OutlinerPanel::AddPick(uint32_t Index) noexcept
{
    if (Index == kNoEditorInstance) return;
    if (!IsPicked(Index) && PickedCount_ < kMaxEditorPicked)
    {
        Picked_[PickedCount_++] = Index;
    }
}

void OutlinerPanel::TogglePick(uint32_t Index) noexcept
{
    if (Index == kNoEditorInstance) return;
    if (IsPicked(Index))
        RemovePick(Index);
    else
        AddPick(Index);
    Anchor_ = Index;
}

void OutlinerPanel::RemovePick(uint32_t Index) noexcept
{
    for (uint32_t i = 0u; i < PickedCount_; ++i)
    {
        if (Picked_[i] == Index)
        {
            for (uint32_t j = i; j + 1u < PickedCount_; ++j)
            {
                Picked_[j] = Picked_[j + 1u];
            }
            --PickedCount_;
            return;
        }
    }
}

void OutlinerPanel::HandleRowClick(uint32_t Index, uint32_t InstanceCount) noexcept
{
    const bool Ctrl  = ImGui::GetIO().KeyCtrl;
    const bool Shift = ImGui::GetIO().KeyShift;
    if (Shift && Anchor_ != kNoEditorInstance && Anchor_ < InstanceCount)
    {
        const uint32_t Lo = Anchor_ < Index ? Anchor_ : Index;
        const uint32_t Hi = Anchor_ < Index ? Index : Anchor_;
        for (uint32_t k = Lo; k <= Hi; ++k)
        {
            AddPick(k);
        }
    }
    else if (Ctrl)
    {
        if (IsPicked(Index))
        {
            RemovePick(Index);
        }
        else
        {
            AddPick(Index);
            Anchor_ = Index;
        }
    }
    else
    {
        Picked_[0]   = Index;
        PickedCount_ = 1u;
        Anchor_      = Index;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          REPARENT
//------------------------------------------------------------------------------------------------------------------------

bool OutlinerPanel::MoveRun(EditorInstance* Instances, uint32_t InstanceCount, uint32_t Lifted, uint32_t Target,
                            bool Before) noexcept
{
    if (Lifted >= InstanceCount || Instances[Lifted].Pinned || Instances[Lifted].Component)
    {
        return false;
    }
    const uint32_t LiftedEnd = RunEnd(Instances, InstanceCount, Lifted);
    const uint32_t RunLen    = LiftedEnd - Lifted;

    // The seat: where the run lands and its depth there. A cycle is a target inside the run.
    uint32_t NewDepth = 0u;
    uint32_t Seat     = InstanceCount;
    if (Target != kNoEditorInstance)
    {
        if (Target >= InstanceCount || Instances[Target].Component || (Target >= Lifted && Target < LiftedEnd))
        {
            return false;
        }
        if (Before)
        {
            NewDepth = Instances[Target].Depth;
            Seat     = Target;
        }
        else
        {
            NewDepth = Instances[Target].Depth + 1u;
            Seat     = RunEnd(Instances, InstanceCount, Target);
        }
    }
    if (Seat == Lifted || Seat == LiftedEnd)
    {
        if (NewDepth == Instances[Lifted].Depth)
        {
            return false;   // already there
        }
    }

    // Lift the run into scratch, re-based to the seat's depth.
    const int32_t DepthShift = static_cast<int32_t>(NewDepth) - static_cast<int32_t>(Instances[Lifted].Depth);
    std::vector<EditorInstance> Scratch(RunLen);
    std::vector<bool>           LiftedShut(RunLen);
    for (uint32_t k = 0u; k < RunLen; ++k)
    {
        Scratch[k] = Instances[Lifted + k];
        Scratch[k].Depth = static_cast<uint32_t>(static_cast<int32_t>(Scratch[k].Depth) + DepthShift);
        LiftedShut[k] = Shut_[Lifted + k];
    }

    // Close the gap, remembering where the seat moved to.
    for (uint32_t k = LiftedEnd; k < InstanceCount; ++k)
    {
        Instances[k - RunLen] = Instances[k];
        Shut_[k - RunLen]     = Shut_[k];
    }
    if (Seat > Lifted)
    {
        Seat -= RunLen;
    }
    const uint32_t Remaining = InstanceCount - RunLen;

    // Open the seat and drop the run in.
    for (uint32_t k = Remaining; k > Seat; --k)
    {
        Instances[k - 1u + RunLen] = Instances[k - 1u];
        Shut_[k - 1u + RunLen]     = Shut_[k - 1u];
    }
    for (uint32_t k = 0u; k < RunLen; ++k)
    {
        Instances[Seat + k] = Scratch[k];
        Shut_[Seat + k]     = LiftedShut[k];
    }

    // Recount every folder's direct rows, and open the owner it landed under (the page's setParent does).
    for (uint32_t r = 0u; r < InstanceCount; ++r)
    {
        if (Instances[r].Category != EditorInstanceCategory::Folder && Instances[r].KidCount == 0u)
        {
            continue;
        }
        uint32_t Kids = 0u;
        const uint32_t End = RunEnd(Instances, InstanceCount, r);
        for (uint32_t k = r + 1u; k < End; ++k)
        {
            if (Instances[k].Depth == Instances[r].Depth + 1u)
            {
                ++Kids;
            }
        }
        Instances[r].KidCount = Kids;
    }
    const uint32_t Owner = OwnerOf(Instances, Seat);
    if (Owner != kNoEditorInstance)
    {
        Shut_[Owner] = false;
        if (Instances[Owner].Category != EditorInstanceCategory::Folder)
        {
            // A leaf that gained a row counts it, so its chevron appears.
            uint32_t Kids = 0u;
            const uint32_t End = RunEnd(Instances, InstanceCount, Owner);
            for (uint32_t k = Owner + 1u; k < End; ++k)
            {
                if (Instances[k].Depth == Instances[Owner].Depth + 1u)
                {
                    ++Kids;
                }
            }
            Instances[Owner].KidCount = Kids;
        }
    }

    // The pick follows its row.
    for (uint32_t i = 0u; i < PickedCount_; ++i)
    {
        uint32_t P = Picked_[i];
        if (P >= Lifted && P < LiftedEnd)
        {
            P = Seat + (P - Lifted);
        }
        else
        {
            if (P > Lifted)
            {
                P -= RunLen;
            }
            if (P >= Seat)
            {
                P += RunLen;
            }
        }
        Picked_[i] = P;
    }
    Anchor_ = PickedCount_ > 0u ? Picked_[0] : kNoEditorInstance;
    ++OrderRevision_;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           RECORD
//------------------------------------------------------------------------------------------------------------------------

void OutlinerPanel::Record(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    IM_ASSERT(Controls_ != nullptr);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(15.0f / 255.0f, 16.0f / 255.0f, 18.0f / 255.0f, 1.0f));
    const bool Open = ImGui::Begin(WindowTitle_, TabOpen_, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!Open)
    {
        ImGui::End();
        return;
    }
    if (InstanceCount > kMaxEditorInstances)
    {
        InstanceCount = kMaxEditorInstances;
    }

    // Inserted/deleted components must not shift selection onto a different entity.
    if(RosterCount_&&!ExplicitPick_){
        auto Relocate=[&](uint32_t Old){
            if(Old>=RosterCount_)return kNoEditorInstance;
            const uint64_t Key=RosterKeys_[Old];
            if(!Key)return Old<InstanceCount?Old:kNoEditorInstance;
            for(uint32_t I=0;I<InstanceCount;++I)if(Instances[I].InspectorKey==Key)return I;
            return kNoEditorInstance;
        };
        uint32_t Written=0;
        for(uint32_t I=0;I<PickedCount_;++I){const auto Next=Relocate(Picked_[I]);if(Next!=kNoEditorInstance)Picked_[Written++]=Next;}
        PickedCount_=Written;Anchor_=Relocate(Anchor_);Revealed_=Relocate(Revealed_);
    }
    ExplicitPick_=false;
    // Collapse pose belongs to the row, not its transient array index. It survives
    // component insertion/removal, reparenting and renames with the row itself.
    // Collapse pose in, and the open phase advanced toward it. The time constant is ~90 ms to 95 % — long enough
    //    to read as a movement, short enough that a reviewer clicking through folders never waits for it. A row
    //    whose INDEX now holds a different identity snaps to its target instead of animating from the stranger's
    //    phase, which is the same rule the pick relocation above follows.
    {
        const float Step = 1.0f - std::exp(-ImGui::GetIO().DeltaTime * 30.0f);
        for (uint32_t i = 0u; i < InstanceCount; ++i)
        {
            Shut_[i] = Instances[i].Shut;
            const float Want = Shut_[i] ? 0.0f : 1.0f;
            const bool  Same = i < RosterCount_ && RosterKeys_[i] == Instances[i].InspectorKey;
            if (!Same || !std::isfinite(Phase_[i])) { Phase_[i] = Want; continue; }
            Phase_[i] += (Want - Phase_[i]) * Step;
            if (std::fabs(Want - Phase_[i]) < 0.002f) Phase_[i] = Want;
        }
    }

    // The page's keys: Tab compacts (outside a text field), Ctrl+Shift+F lands in the search.
    ImGuiIO& IO = ImGui::GetIO();
    const bool Typing = ImGui::GetCurrentContext()->ActiveId != 0u && ImGui::GetInputTextState(ImGui::GetCurrentContext()->ActiveId) != nullptr;
    if (!Typing && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Tab, false))
    {
        Compact_ = !Compact_;
    }
    if (IO.KeyCtrl && IO.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F, false))
    {
        SearchFocus_ = true;
        Compact_     = false;
    }

    if (DocumentStyle_)
    {
        TakeCensus(Instances, InstanceCount);
    }
    RecordHeader(Instances, InstanceCount);
    if (!Compact_)
    {
        if (DocumentStyle_)
        {
            RecordDocumentTiles(Instances, InstanceCount);
        }
        else
        {
            RecordTiles(Instances, InstanceCount);
        }
    }
    RecordSearch();
    if (!Compact_)
    {
        RecordChips();
    }
    (void)RecordOutline(Instances, InstanceCount);
    if (DocumentStyle_)
    {
        RecordDocumentFooter();
    }
    else
    {
        RecordFooter();
    }
    for(uint32_t i=0;i<InstanceCount;++i){Instances[i].Shut=Shut_[i];RosterKeys_[i]=Instances[i].InspectorKey;}
    RosterCount_=InstanceCount;
    ImGui::End();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          HEADER
//------------------------------------------------------------------------------------------------------------------------
// .panel-head: padding 22px 22px 12px (compact 16 16 8); h1 20px w300 (compact 16) with a 12px t3 sub 4px after
//    its 10px gap; the 28px round compact button on the right, stroked, t3, two 14px bars.

void OutlinerPanel::RecordHeader(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    const float PadX  = Compact_ ? kHeadPadCompX : kHeadPadX;
    const float PadT  = Compact_ ? kHeadPadCompT : kHeadPadTop;
    const float PadB  = Compact_ ? kHeadPadCompB : kHeadPadBot;
    const float TitlePx = Compact_ ? 16.0f : 20.0f;
    const float LineH = 28.0f;   // the button is the tallest thing in the row
    const float RowWidth = ImGui::GetContentRegionAvail().x;

    ImGui::Dummy(ImVec2(RowWidth, PadT + LineH + PadB));
    const ImVec2 Cursor = ImGui::GetItemRectMin();
    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Title = Controls_->QueryTitle();
    ImFont*     Ui    = Controls_->QueryUi();
    const float Cy = Cursor.y + PadT + LineH * 0.5f;

    float X = Cursor.x + PadX;
    DrawSized(Draw, Title, TitlePx, X, Cy, kText, "Outliner");
    X += MeasureSized(Title, TitlePx, "Outliner") + 10.0f + 4.0f;

    // "Scene · N nodes" — the page counts the nodes the census counts (every non-folder row).
    uint32_t Total = 0u;
    for (uint32_t i = 0u; i < InstanceCount; ++i)
    {
        if (Instances[i].Category != EditorInstanceCategory::Folder)
        {
            ++Total;
        }
    }
    if (!Compact_)
    {
        // "Showcase · 142 nodes" — the level name rides the tick readout; without one the head reads "Scene".
        const char* Scene = (Readout_ != nullptr && Readout_->Scene[0] != '\0') ? Readout_->Scene : "Scene";
        char Sub[40];
        if (DocumentStyle_)
        {
            // The HTML's head pill: a hairline capsule, 9 px tracked capitals, "SOLIDARC · DOCUMENT".
            std::snprintf(Sub, sizeof(Sub), "%s \xc2\xb7 document", Scene);
            char Caps[40];
            UpperCopy(Caps, sizeof(Caps), Sub);
            const float PillW = MeasureSpaced(Ui, 9.0f, Caps, 0.9f) + 20.0f;
            const ImVec2 PillMin(X - 2.0f, Cy - 10.0f);
            Draw->AddRect(PillMin, ImVec2(PillMin.x + PillW, PillMin.y + 20.0f), kStroke, 10.0f, 0, 1.0f);
            DrawSpaced(Draw, Ui, 9.0f, ImVec2(PillMin.x + 10.0f, Cy - Ui->CalcTextSizeA(9.0f, FLT_MAX, 0.0f, Caps).y * 0.5f), kT2, Caps, 0.9f);
        }
        else
        {
            std::snprintf(Sub, sizeof(Sub), "%s \xc2\xb7 %u nodes", Scene, Total);
            DrawSized(Draw, Ui, 12.0f, X, Cy + 1.0f, kT3, Sub);
        }
    }
    if (DocumentStyle_)
    {
        // The document head carries no compact button; Tab still narrows the panel.
        ImGui::SetCursorScreenPos(ImVec2(Cursor.x, Cursor.y + PadT + LineH + PadB));
        return;
    }

    // .cbtn: 28 px round, 1 px stroke; compact lights it (text ink on g3), hover the same.
    const ImVec2 BtnMin(Cursor.x + RowWidth - PadX - 28.0f, Cursor.y + PadT);
    const ImVec2 BtnMax(BtnMin.x + 28.0f, BtnMin.y + 28.0f);
    ImGui::SetCursorScreenPos(BtnMin);
    ImGui::InvisibleButton("##compact", ImVec2(28.0f, 28.0f));
    const bool Hot = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked())
    {
        Compact_ = !Compact_;
    }
    const ImVec2 BtnC((BtnMin.x + BtnMax.x) * 0.5f, (BtnMin.y + BtnMax.y) * 0.5f);
    if (Hot || Compact_)
    {
        Draw->AddCircleFilled(BtnC, 14.0f, kG3);
    }
    Draw->AddCircle(BtnC, 14.0f, kStroke, 0, 1.0f);
    DrawIcon(Draw, OutlinerIconCategory::Compact, ImVec2(BtnC.x - 7.0f, BtnC.y - 7.0f), 14.0f,
        (Hot || Compact_) ? kText : kT3);
    if (Hot)
    {
        ImGui::SetTooltip("Compact outliner (Tab)");
    }
    ImGui::SetCursorScreenPos(ImVec2(Cursor.x, Cursor.y + PadT + LineH + PadB));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           TILES
//------------------------------------------------------------------------------------------------------------------------
// .scene-stat: two tiles, 8 px gap, padding 0 14px 10px. .ss: radius 18, stroke, .035 fill, padding 12 14 10,
//    22 px round icon (ok = green with dark check; warn = red wash with red triangle), 12 px t2 label on the
//    second line, and the 30 px w200 numeral right-aligned across both.

void OutlinerPanel::RecordTiles(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    uint32_t Visible = 0u, Hidden = 0u;
    for (uint32_t i = 0u; i < InstanceCount; ++i)
    {
        if (Instances[i].Category == EditorInstanceCategory::Folder)
        {
            continue;
        }
        if (Instances[i].Visible)
        {
            ++Visible;
        }
        else
        {
            ++Hidden;
        }
    }

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    const float TileW = (RowWidth - 2.0f * kSidePad - 8.0f) * 0.5f;
    const float TileH = 12.0f + 22.0f + 4.0f + 16.0f + 10.0f;   // pad · icon row · gap · label line · pad
    ImGui::Dummy(ImVec2(RowWidth, TileH + 10.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();
    ImDrawList* Draw    = ImGui::GetWindowDrawList();
    ImFont*     Ui      = Controls_->QueryUi();
    ImFont*     Display = Controls_->QueryDisplay();

    for (uint32_t t = 0u; t < 2u; ++t)
    {
        const ImVec2 Min(Cursor.x + kSidePad + static_cast<float>(t) * (TileW + 8.0f), Cursor.y);
        const ImVec2 Max(Min.x + TileW, Min.y + TileH);
        Draw->AddRectFilled(Min, Max, kTileBg, 18.0f);
        Draw->AddRect(Min, Max, kStroke, 18.0f, 0, 1.0f);

        const bool  Ok    = t == 0u;
        const bool  Warn  = t == 1u && Hidden > 0u;
        const ImVec2 IcoC(Min.x + 14.0f + 11.0f, Min.y + 12.0f + 11.0f);
        Draw->AddCircleFilled(IcoC, 11.0f, Ok ? kGreen : (Warn ? kErrTile : kTileIco));
        DrawIcon(Draw, Ok ? OutlinerIconCategory::Check : OutlinerIconCategory::Warn, ImVec2(IcoC.x - 7.0f, IcoC.y - 7.0f),
            14.0f, Ok ? kInkOnOk : (Warn ? kRed : kT3));

        DrawSized(Draw, Ui, 12.0f, Min.x + 14.0f, Min.y + 12.0f + 22.0f + 4.0f + 8.0f, kT2, Ok ? "Visible" : "Hidden");

        char Figure[12];
        std::snprintf(Figure, sizeof(Figure), "%u", Ok ? Visible : Hidden);
        const float FigW = MeasureSized(Display, 30.0f, Figure);
        const ImVec2 FigG = Display->CalcTextSizeA(30.0f, FLT_MAX, 0.0f, Figure);
        Draw->AddText(Display, 30.0f, ImVec2(Max.x - 14.0f - FigW, Max.y - 10.0f - FigG.y + 2.0f), kText, Figure);
    }
    ImGui::SetCursorScreenPos(ImVec2(Cursor.x, Cursor.y + TileH + 10.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           SEARCH
//------------------------------------------------------------------------------------------------------------------------
// .search: margin 0 14px 8px, height 40 (compact 32), radius 999, g2 fill, stroke, padding 0 14, 10 px gap after
//    the 16 px glass, 13 px w300 text, t3 placeholder "Search  Ctrl+Shift+F".

void OutlinerPanel::RecordSearch() noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    const float H = Compact_ ? 32.0f : 40.0f;
    ImGui::Dummy(ImVec2(RowWidth, H + 8.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    ImFont*     Ui   = Controls_->QueryUi();

    const uint32_t FilterCount = QueryFilterCount();
    uint32_t Lit = 0u;
    for (uint32_t Slot = 0u; Slot < FilterCount && Slot < kMaxOutlinerFilters; ++Slot)
    {
        if (FilterOn_[Slot])
        {
            ++Lit;
        }
    }

    const char NarrowWord[] = { 'F', 'i', 'l', 't', 'e', 'r', '\0' };
    const float BtnW = Compact_ ? 82.0f : 92.0f;
    const float Gap = 8.0f;
    const ImVec2 Min(Cursor.x + kSidePad, Cursor.y);
    const ImVec2 Max(Cursor.x + RowWidth - kSidePad - BtnW - Gap, Cursor.y + H);
    const ImVec2 BtnMin(Max.x + Gap, Cursor.y);
    const ImVec2 BtnMax(BtnMin.x + BtnW, Cursor.y + H);

    Draw->AddRectFilled(Min, Max, kG2, H * 0.5f);
    Draw->AddRect(Min, Max, kStroke, H * 0.5f, 0, 1.0f);
    const float Cy = (Min.y + Max.y) * 0.5f;
    DrawIcon(Draw, OutlinerIconCategory::Search, ImVec2(Min.x + 14.0f, Cy - 8.0f), 16.0f, kT3);

    const float FieldX = Min.x + 14.0f + 16.0f + 10.0f;
    const float FieldW = Max.x - 14.0f - FieldX;
    ImGui::SetCursorScreenPos(ImVec2(FieldX, Cy - 10.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kText));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushFont(Ui, 13.0f);
    ImGui::SetNextItemWidth(FieldW);
    if (SearchFocus_)
    {
        ImGui::SetKeyboardFocusHere();
        SearchFocus_ = false;
    }
    ImGui::InputText("##search", QueryText_, sizeof(QueryText_));
    const bool Empty = QueryText_[0] == '\0';
    const bool Active = ImGui::IsItemActive();
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    if (Empty && !Active)
    {
        DrawClipped(Draw, Ui, 13.0f, FieldX, Cy, FieldW, kT3,
                    FieldW < 110.0f ? "Search" : "Search  Ctrl+Shift+F");
    }

    ImGui::SetCursorScreenPos(BtnMin);
    ImGui::InvisibleButton("##narrow_menu", ImVec2(BtnW, H));
    const bool Hot = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked())
    {
        NarrowMenuOpen_ = !NarrowMenuOpen_;
        if (NarrowMenuOpen_)
        {
            ImGui::OpenPopup("##narrow_menu_popup");
        }
    }
    const bool PopupOpen = ImGui::IsPopupOpen("##narrow_menu_popup");
    if (!PopupOpen)
    {
        NarrowMenuOpen_ = false;
    }
    Draw->AddRectFilled(BtnMin, BtnMax, (Hot || PopupOpen) ? kG3 : kG2, H * 0.5f);
    Draw->AddRect(BtnMin, BtnMax, (Hot || PopupOpen) ? kStroke2 : kStroke, H * 0.5f, 0, 1.0f);
    DrawIcon(Draw, OutlinerIconCategory::Sliders, ImVec2(BtnMin.x + 11.0f, Cy - 7.0f), 14.0f, (Hot || PopupOpen) ? kText : kT3);
    DrawSized(Draw, Ui, 12.0f, BtnMin.x + 32.0f, Cy, (Hot || PopupOpen || Lit > 0u) ? kText : kT3, NarrowWord);
    const float ArrowX = BtnMax.x - 17.0f;
    const float ArrowY = Cy - 2.0f;
    Draw->AddTriangleFilled(ImVec2(ArrowX - 4.0f, ArrowY), ImVec2(ArrowX + 4.0f, ArrowY),
                            ImVec2(ArrowX, ArrowY + (PopupOpen ? -4.0f : 4.0f)), (Hot || PopupOpen) ? kText : kT3);
    if (Lit > 0u)
    {
        char Count[12];
        std::snprintf(Count, sizeof(Count), "%u", Lit);
        const float CountW = MeasureSized(Ui, 10.0f, Count) + 10.0f;
        Draw->AddRectFilled(ImVec2(BtnMax.x - 28.0f - CountW, Cy - 9.0f), ImVec2(BtnMax.x - 28.0f, Cy + 9.0f),
                            kG3, 9.0f);
        DrawSized(Draw, Ui, 10.0f, BtnMax.x - 28.0f - CountW + 5.0f, Cy, kText, Count);
    }

    ImGui::SetNextWindowPos(ImVec2(BtnMin.x, BtnMax.y + 6.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(170.0f, 0.0f), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(kGlass));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(kStroke2));
    if (ImGui::BeginPopup("##narrow_menu_popup", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImDrawList* PopDraw = ImGui::GetWindowDrawList();
        for (uint32_t Slot = 0u; Slot < FilterCount && Slot < kMaxOutlinerFilters; ++Slot)
        {
            const char* Label = QueryFilterLabel(Slot);
            const ImVec2 P = ImGui::GetCursorScreenPos();
            const float W = ImGui::GetContentRegionAvail().x;
            ImGui::PushID(static_cast<int>(Slot));
            ImGui::InvisibleButton("##choice", ImVec2(W, 28.0f));
            const bool RowHot = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked())
            {
                FilterOn_[Slot] = !FilterOn_[Slot];
                NarrowMenuOpen_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
            if (RowHot || FilterOn_[Slot])
            {
                PopDraw->AddRectFilled(P, ImVec2(P.x + W, P.y + 28.0f), RowHot ? kG3 : kG2, 14.0f);
            }
            PopDraw->AddCircleFilled(ImVec2(P.x + 13.0f, P.y + 14.0f), 3.0f, QueryFilterTint(Slot));
            DrawSized(PopDraw, Ui, 11.0f, P.x + 26.0f, P.y + 14.0f, FilterOn_[Slot] ? kText : kT2, Label);
            if (FilterOn_[Slot])
            {
                DrawIcon(PopDraw, OutlinerIconCategory::Check, ImVec2(P.x + W - 22.0f, P.y + 7.0f), 14.0f, kGreen);
            }
        }
        NarrowMenuOpen_ = true;
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    ImGui::SetCursorScreenPos(ImVec2(Cursor.x, Cursor.y + H + 8.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           CHIPS
//------------------------------------------------------------------------------------------------------------------------
// .filters: 5 px gap, padding 0 14px 8px, wrapping. Each button 26 px tall, padding 0 10, radius 999, 11 px t3,
//    1 px stroke, a 6 px dot in its tint 6 px before the label; on = g3 fill, text ink, stroke2.

void OutlinerPanel::RecordChips() noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    ImFont*     Ui   = Controls_->QueryUi();
    const ImVec2 Start = ImGui::GetCursorScreenPos();

    float X = Start.x + kSidePad;
    float Y = Start.y;
    const float Right = Start.x + RowWidth - kSidePad;
    bool Any = false;
    const uint32_t FilterCount = QueryFilterCount();
    for (uint32_t Slot = 0u; Slot < FilterCount && Slot < kMaxOutlinerFilters; ++Slot)
    {
        if (!FilterOn_[Slot])
        {
            continue;
        }
        Any = true;
        const char* Label = QueryFilterLabel(Slot);
        const float W = 10.0f + 6.0f + 6.0f + MeasureSized(Ui, 11.0f, Label) + 20.0f;
        if (X + W > Right && X > Start.x + kSidePad)
        {
            X = Start.x + kSidePad;
            Y += 26.0f + 5.0f;
        }
        const ImVec2 Min(X, Y);
        const ImVec2 Max(X + W, Y + 26.0f);
        ImGui::SetCursorScreenPos(Min);
        ImGui::PushID(static_cast<int>(Slot));
        ImGui::InvisibleButton("##pill", ImVec2(W, 26.0f));
        if (ImGui::IsItemClicked())
        {
            FilterOn_[Slot] = false;
        }
        const bool Hot = ImGui::IsItemHovered();
        ImGui::PopID();
        Draw->AddRectFilled(Min, Max, Hot ? kG3 : kG2, 13.0f);
        Draw->AddRect(Min, Max, Hot ? kStroke2 : kStroke, 13.0f, 0, 1.0f);
        const float Cy = Y + 13.0f;
        Draw->AddCircleFilled(ImVec2(X + 10.0f + 3.0f, Cy), 3.0f, QueryFilterTint(Slot));
        DrawSized(Draw, Ui, 11.0f, X + 10.0f + 6.0f + 6.0f, Cy, kText, Label);
        Draw->AddLine(ImVec2(Max.x - 13.0f, Cy - 4.0f), ImVec2(Max.x - 7.0f, Cy + 2.0f), Hot ? kText : kT3, 1.2f);
        Draw->AddLine(ImVec2(Max.x - 7.0f, Cy - 4.0f), ImVec2(Max.x - 13.0f, Cy + 2.0f), Hot ? kText : kT3, 1.2f);
        X += W + 5.0f;
    }
    ImGui::SetCursorScreenPos(ImVec2(Start.x, Any ? (Y + 26.0f + 8.0f) : Start.y));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          OUTLINE
//------------------------------------------------------------------------------------------------------------------------
// .tree: flex 1, scrolls, padding 2px 10px 10px, no scrollbar. The walk: a row shows when it or any row under
//    it matches the search and the lit pills (the page shows ancestors of a hit); its rows follow when it stands
//    open or a search is live.

uint32_t OutlinerPanel::RecordOutline(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    const bool Searching = QueryText_[0] != '\0';
    const uint32_t SelectedFilterMask = QuerySelectedFilterMask();
    const bool AnyPill = SelectedFilterMask != 0u;

    // Matches, then ancestors of matches. Folders match only through their rows (the page's 'world' rule).
    uint32_t Hits = 0u;
    for (uint32_t i = 0u; i < InstanceCount; ++i)
    {
        const EditorInstance& Row = Instances[i];
        bool Match = !Searching || ContainsFolded(Row.Label, QueryText_);
        if (Match && AnyPill)
        {
            const uint32_t RowMask = Row.FilterMask != 0u ? Row.FilterMask : RowFilterMask(Row);
            Match = (RowMask & SelectedFilterMask) != 0u;
        }
        if (Match && Row.Category == EditorInstanceCategory::Folder && (Searching || AnyPill))
        {
            Match = false;
        }
        Shown_[i] = Match;
        if (Match)
        {
            ++Hits;
        }
    }
    for (uint32_t i = 0u; i < InstanceCount; ++i)
    {
        if (!Shown_[i])
        {
            continue;
        }
        uint32_t Owner = OwnerOf(Instances, i);
        while (Owner != kNoEditorInstance)
        {
            Shown_[Owner] = true;
            Owner = OwnerOf(Instances, Owner);
        }
    }

    // The tree ends where the foot begins: the sill's own figure, not the content rect, which a
    //    scrollbar's reservation can lift.
    const float Avail = Controls_->QueryFootTop() - ImGui::GetCursorScreenPos().y;
    const float Width = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
    // NoScrollWithMouse: the wheel is taken here instead, so the list can GLIDE to where the notch asked rather
    //    than teleporting a fixed number of pixels per click.
    ImGui::BeginChild("##tree", ImVec2(Width, Avail > 0.0f ? Avail : 1.0f), ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    TreeHovered_ = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    {
        // Re-seat on any scroll this panel did not perform itself — a reveal (SetScrollHereY), a resize, a
        //    rebuilt list. Without this the glide would fight whoever moved the view.
        const float Live = ImGui::GetScrollY();
        if (!ScrollSeated_ || std::fabs(Live - ScrollNow_) > 1.5f)
        {
            ScrollNow_ = ScrollTarget_ = Live;
            ScrollSeated_ = true;
        }
        const float Wheel = ImGui::GetIO().MouseWheel;
        if (Wheel != 0.0f && TreeHovered_ && !ImGui::IsAnyItemActive())
            ScrollTarget_ -= Wheel * (Compact_ ? kRowHCompact : kRowH) * 1.5f;
    }
    Revealing_ = false;
    ImGui::Dummy(ImVec2(Width, 2.0f));

    uint32_t Drawn = 0u;
    uint32_t i = 0u;
    // The fold, as a movement. Each folder's children are drawn at its open phase: full height at 1, nothing at
    //    0, and every height between while the phase travels. The stack carries the product down the tree so a
    //    grandchild folds with its grandparent, and a subtree whose phase has reached 0 is skipped outright —
    //    which is exactly what the old `Shut_ → jump to End` did, only now it is the END of the movement.
    struct FoldFrame { uint32_t End; float Squeeze; };
    FoldFrame Fold[16];
    uint32_t  FoldDepth = 0u;
    float     Squeeze   = 1.0f;
    while (i < InstanceCount)
    {
        while (FoldDepth > 0u && i >= Fold[FoldDepth - 1u].End)
        {
            Squeeze = Fold[--FoldDepth].Squeeze;
        }
        if (!Shown_[i])
        {
            ++i;
            continue;
        }
        const uint32_t End = RunEnd(Instances, InstanceCount, i);
        bool HasKids = false;
        for (uint32_t k = i + 1u; k < End; ++k)
        {
            if (Shown_[k])
            {
                HasKids = true;
                break;
            }
        }
        HasKids = HasKids || (End > i + 1u);
        RecordRow(Instances, InstanceCount, i, End > i + 1u, Squeeze);
        ++Drawn;
        const float Open = Searching ? 1.0f : Phase_[i];
        if (End > i + 1u && Open < 0.999f)
        {
            if (Open <= 0.002f)
            {
                i = End;                       // folded away: the subtree costs nothing, as before
                continue;
            }
            if (FoldDepth < 16u)               // mid-fold: the children draw squeezed
            {
                Fold[FoldDepth++] = { End, Squeeze };
                Squeeze *= Open;
            }
        }
        {
            ++i;
        }
    }
    if (Drawn == 0u)
    {
        RecordEmpty(Width);
    }
    ImGui::Dummy(ImVec2(Width, 10.0f));

    // A drop on empty tree space seats the run at the root.
    if (DragLifted_ != kNoEditorInstance)
    {
        const bool Over = TreeHovered_ && !ImGui::IsAnyItemHovered();
        if (Over)
        {
            ImDrawList* Draw = ImGui::GetWindowDrawList();
            Draw->AddRect(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + Width, ImGui::GetWindowPos().y + ImGui::GetWindowHeight()),
                kStroke2, 14.0f, 0, 1.0f);
            if (ImGui::IsMouseReleased(0))
            {
                MoveRun(Instances, InstanceCount, DragLifted_, kNoEditorInstance, false);
            }
        }
        if (ImGui::IsMouseReleased(0))
        {
            DragLifted_ = kNoEditorInstance;
        }
    }
    // The glide itself, after the rows have measured the content: clamp the target to what exists, ease a fifth
    //    of the way per 60 Hz tick (frame-rate independent), and stop dead once the gap is sub-pixel so a resting
    //    list is bit-stable. A reveal owns the frame it fires on; the re-seat above adopts it on the next.
    {
        const float Limit = ImGui::GetScrollMaxY();
        ScrollTarget_ = ScrollTarget_ < 0.0f ? 0.0f : (ScrollTarget_ > Limit ? Limit : ScrollTarget_);
        if (!Revealing_)
        {
            const float Step = 1.0f - std::exp(-ImGui::GetIO().DeltaTime * 18.0f);
            ScrollNow_ += (ScrollTarget_ - ScrollNow_) * Step;
            if (std::fabs(ScrollTarget_ - ScrollNow_) < 0.4f) ScrollNow_ = ScrollTarget_;
            if (std::fabs(ScrollNow_ - ImGui::GetScrollY()) > 0.01f) ImGui::SetScrollY(ScrollNow_);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    return Hits;
}

void OutlinerPanel::RecordEmpty(float Width) noexcept
{
    // .empty: padding 30px 10px, centred, t3, 12 px.
    ImGui::Dummy(ImVec2(Width, 30.0f + 16.0f + 30.0f));
    const ImVec2 Min = ImGui::GetItemRectMin();
    ImFont* Ui = Controls_->QueryUi();
    const float W = MeasureSized(Ui, 12.0f, "Nothing here.");
    DrawSized(ImGui::GetWindowDrawList(), Ui, 12.0f, Min.x + (Width - W) * 0.5f, Min.y + 30.0f + 8.0f, kT3, "Nothing here.");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            ROW
//------------------------------------------------------------------------------------------------------------------------
// .node: 36 px (compact 30), gap 8, padding 0 6px 0 (8 + depth × 16), radius 11, t2 ink. Hover g2 and text ink;
//    sel .09 white, inset stroke2, 3 px accent bar at the left inset 8 px top and bottom. dim = .4 on the name and
//    glyph. Columns: chev 14 (hidden without rows, turned 90° open) · nico 24 with the 14 px glyph in the accent ·
//    nname 13 px (folder: 11 px uppercase, .1em tracking, t3) + 9 px tag pill · nmeta 11 px t3 tabular · nstat
//    16 px round · eye 24 px round, t3, only on hover / sel / off (off = red, eyeoff glyph). Pinned rows have no eye.

void OutlinerPanel::RecordRow(EditorInstance* Instances, uint32_t InstanceCount, uint32_t Index, bool HasKids,
                              float Squeeze) noexcept
{
    EditorInstance& Row = Instances[Index];
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    ImFont*     Ui   = Controls_->QueryUi();
    ImFont*     Mono = Controls_->QueryMono();
    const float Fold = Squeeze < 0.0f ? 0.0f : (Squeeze > 1.0f ? 1.0f : Squeeze);
    const bool  Cad    = DocumentStyle_;
    const bool  Folder = Row.Category == EditorInstanceCategory::Folder;
    const float Indent = Cad ? kCadStep : 13.0f;
    const float RowH = (Cad ? (Folder ? kCadFolderH : kCadRowH) : (Compact_ ? kRowHCompact : kRowH)) * Fold;
    const float Radius = Cad ? kCadRadius : kRowRadius;
    const float Width = ImGui::GetContentRegionAvail().x;

    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    const ImVec2 Min(Origin.x + kTreePad, Origin.y);
    const ImVec2 Max(Origin.x + Width - kTreePad, Origin.y + RowH);
    ImGui::PushID(static_cast<int>(Index));
    // Keep the row's hit target out of the chevron/eye columns. A full-width
    // InvisibleButton consumed their press before the small controls could see it.
    const float HitLeft=Min.x+8.0f+float(Row.Depth)*Indent+kChevBox+kRowGap;
    const float HitRight=Max.x-(Row.Pinned?0.0f:kEyeBox+6.0f);
    ImGui::SetCursorScreenPos(ImVec2(HitLeft,Min.y));
    ImGui::InvisibleButton("##row", ImVec2(std::max(1.0f,HitRight-HitLeft), RowH), ImGuiButtonFlags_MouseButtonLeft);
    const bool Hot     = ImGui::IsItemHovered();
    const bool Picked  = IsPicked(Index);
    // A fresh pick scrolls into view once, the way the page's select() does; after that the scroll is the user's.
    if (Picked && PickedCount_ > 0u && Picked_[PickedCount_ - 1u] == Index && Revealed_ != Index)
    {
        Revealed_ = Index;
        ImGui::SetScrollHereY(0.5f);
        Revealing_ = true;   // this frame's scroll belongs to the reveal; the glide adopts it next frame
    }
    const bool Dim     = !Row.Visible && !Folder;
    const ImU32 Accent = RowTint(Row);

    // Drag: the page's HTML5 drag. A press that travels starts one; pinned folders never do.
    if (ImGui::IsItemActive() && !Row.Pinned && !Row.Component && DragLifted_ == kNoEditorInstance
        && ImGui::IsMouseDragging(0, 4.0f))
    {
        DragLifted_ = Index;
    }
    const bool Dragging = DragLifted_ == Index;
    const bool DropHere = DragLifted_ != kNoEditorInstance && !Dragging && Hot;
    bool DropBefore = false;
    if (DropHere)
    {
        DropBefore = (ImGui::GetIO().MousePos.y - Min.y) < RowH * 0.28f;
        if (ImGui::IsMouseReleased(0))
        {
            const uint32_t Lifted = DragLifted_;
            DragLifted_ = kNoEditorInstance;
            if (MoveRun(Instances, InstanceCount, Lifted, Index, DropBefore))
            {
                ImGui::PopID();
                return;   // the roster moved under us; this tick's remaining rows draw next tick
            }
        }
    }

    // Ground.
    if (Picked)
    {
        // The document row is the HTML's .row.sel: a white wash and a hairline, no accent bar.
        Draw->AddRectFilled(Min, Max, Cad ? IM_COL32(255, 255, 255, 18) : kSelBg, Radius);
        Draw->AddRect(Min, Max, kStroke2, Radius, 0, 1.0f);
        if (!Cad)
        {
            Draw->AddRectFilled(ImVec2(Min.x, Min.y), ImVec2(Min.x + 3.0f, Max.y), Accent, 0.0f);
        }
    }
    else if (Hot && DragLifted_ == kNoEditorInstance)
    {
        Draw->AddRectFilled(Min, Max, kG2, Radius);
    }
    if (DropHere && !DropBefore)
    {
        Draw->AddRectFilled(Min, Max, WithAlpha(Accent, 31), Radius);   // color-mix 12 %
        Draw->AddRect(Min, Max, Accent, Radius, 0, 1.0f);
    }
    if (DropHere && DropBefore)
    {
        Draw->AddRectFilled(ImVec2(Min.x + 12.0f, Min.y - 1.0f), ImVec2(Max.x - 12.0f, Min.y + 1.0f), kWhite, 2.0f);
    }
    // A folding row loses its ink as it loses its height, and is clipped to the band it still owns so its glyph
    //    and name cannot spill over the neighbour it is sliding behind.
    const float Fade = (Dragging ? 0.35f : 1.0f) * (Fold * Fold);
    const float DimF = Dim ? 0.4f : 1.0f;
    const bool  Folding = Fold < 0.999f;
    if (Folding) Draw->PushClipRect(ImVec2(Min.x - 4.0f, Min.y), ImVec2(Max.x + 4.0f, Max.y), true);
    const ImU32 Ink  = ScaleAlpha((Hot || Picked) ? kText : kT2, Fade);

    // Columns, left to right.
    const float Cy = Min.y + RowH * 0.5f;
    float X = Min.x + 8.0f + static_cast<float>(Row.Depth) * Indent;
    if (Cad && Row.Depth >= 2u && !Folding)
    {
        // .row.child::before: the hairline that ties a child to the row above.
        const float Lx = X - 5.0f;
        Draw->AddLine(ImVec2(Lx, Min.y), ImVec2(Lx, Max.y), kStroke, 1.0f);
    }

    // Chevron.
    {
        const ImVec2 ChevMin(X, Cy - kChevBox * 0.5f);
        if (HasKids)
        {
            ImGui::SetCursorScreenPos(ChevMin);
            ImGui::InvisibleButton("##chev", ImVec2(kChevBox, kChevBox));
            if (ImGui::IsItemClicked())
            {
                Shut_[Index] = !Shut_[Index];
            }
            DrawChevron(Draw, ImVec2(X + kChevBox * 0.5f, Cy), 11.0f, ScaleAlpha(kT3, Fade), Phase_[Index]);
        }
        X += kChevBox + kRowGap;
    }

    // Glyph.
    {
        const ImU32 GlyphInk = ScaleAlpha(Accent, Fade * DimF);
        const IconSymbol Artwork = ArtworkFor(Row);
        const float ArtSize=std::min(kIcoBox,RowH-4.f);
        const ImVec2 ArtMin(X+(kIcoBox-ArtSize)*.5f, Cy-ArtSize*.5f);
        if (Row.Symbol != EditorSymbol::None)
            DrawSymbolArt(Draw, Row, ImVec2(X, Cy - kIcoBox * 0.5f), Cy, Accent, Fade * DimF, HasKids && !Shut_[Index], Cad);
        else if (!IconPresentation::Draw(Draw, Artwork, ArtMin, ArtSize, Fade * DimF))
            DrawIcon(Draw, IconFor(Row), ImVec2(X + (kIcoBox - 14.0f) * 0.5f, Cy - 7.0f), 14.0f, GlyphInk);
        else if (IconPresentation::Result(Artwork) != IconResult::Ready &&
                 ImGui::IsMouseHoveringRect(ArtMin, ImVec2(ArtMin.x+ArtSize, ArtMin.y+ArtSize)))
            ImGui::SetTooltip("%s: %s\n%s", IconArt::Name(Artwork),
                IconArt::ResultName(IconPresentation::Result(Artwork)), IconPresentation::Diagnostic(Artwork));
        X += kIcoBox + kRowGap;
    }

    // Right-hand columns, measured first so the name knows its room.
    float RightX = Max.x - 6.0f;
    const bool ShowEye = !Row.Pinned && (Hot || Picked || !Row.Visible);
    if (!Row.Pinned)
    {
        RightX -= kEyeBox;
        const ImVec2 EyeMin(RightX, Cy - kEyeBox * 0.5f);
        ImGui::SetCursorScreenPos(EyeMin);
        ImGui::InvisibleButton("##eye", ImVec2(kEyeBox, kEyeBox));
        const bool EyeHot = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
        {
            Row.Visible = !Row.Visible;
        }
        if (ShowEye || EyeHot)
        {
            const ImVec2 EyeC(RightX + kEyeBox * 0.5f, Cy);
            if (EyeHot)
            {
                Draw->AddCircleFilled(EyeC, kEyeBox * 0.5f, kG3);
            }
            const ImU32 EyeInk = !Row.Visible ? kRed : (EyeHot ? kText : kT3);
            DrawIcon(Draw, Row.Visible ? OutlinerIconCategory::Eye : OutlinerIconCategory::EyeOff,
                ImVec2(EyeC.x - 6.5f, EyeC.y - 6.5f), 13.0f, ScaleAlpha(EyeInk, Fade));
        }
        RightX -= kRowGap;
    }

    // Folder rows match HTML: artwork + label + visibility, no extra status glyph.
    if (!Folder && Cad)
    {
        EditorStanding Standing = EditorStanding::Ok;
        char Note[24] = {};
        StandingOf(Instances, InstanceCount, Index, HasKids, Standing, Note, sizeof(Note));
        RightX -= kStatBox + 6.0f;
        const RowStatus Status = StatusOf(Row, Standing);
        DrawRowStatus(Draw, Status, ImVec2(RightX + (kStatBox + 6.0f) * 0.5f, Cy), Fade);
        ImGui::SetCursorScreenPos(ImVec2(RightX, Cy - kStatBox * 0.5f));
        ImGui::InvisibleButton("##stat", ImVec2(kStatBox + 6.0f, kStatBox));
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", Note);
        }
        RightX -= kRowGap;
    }
    else if (!Folder)
    {
        EditorStanding Standing = EditorStanding::Ok;
        char Note[24] = {};
        StandingOf(Instances, InstanceCount, Index, HasKids, Standing, Note, sizeof(Note));
        RightX -= kStatBox;
        const ImVec2 StatC(RightX + kStatBox * 0.5f, Cy);
        ImU32 Fill = kGreen, StatInk = kInkOnOk;
        OutlinerIconCategory Mark = OutlinerIconCategory::Check;
        switch (Standing)
        {
        case EditorStanding::Quiet: Fill = kInfoBg; StatInk = kT3;     Mark = OutlinerIconCategory::Dot;  break;
        case EditorStanding::Warn: Fill = kWarnBg; StatInk = kOrange; Mark = OutlinerIconCategory::Warn; break;
        case EditorStanding::Err:  Fill = kErrBg;  StatInk = kRed;    Mark = OutlinerIconCategory::Warn; break;
        default: break;
        }
        Draw->AddCircleFilled(StatC, kStatBox * 0.5f, ScaleAlpha(Fill, Fade));
        DrawIcon(Draw, Mark, ImVec2(StatC.x - 5.5f, StatC.y - 5.5f), 11.0f, ScaleAlpha(StatInk, Fade));
        ImGui::SetCursorScreenPos(ImVec2(RightX, Cy - kStatBox * 0.5f));
        ImGui::InvisibleButton("##stat", ImVec2(kStatBox, kStatBox));
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", Note);
        }
        RightX -= kRowGap;
    }

    // Meta. The document row prints it under the name instead; only a group head keeps it on the right (its count).
    if (!Compact_ && Row.Meta[0] != '\0' && (!Cad || Folder))
    {
        const float MetaPx = Cad ? 10.0f : 11.0f;
        const float W = MeasureSized(Mono, MetaPx, Row.Meta);
        RightX -= W;
        DrawSized(Draw, Mono, MetaPx, RightX, Cy, ScaleAlpha(kT3, Fade), Row.Meta);
        RightX -= kRowGap;
    }

    // Name and tag.
    {
        const float NameW = RightX - X;
        if (Folder && Cad)
        {
            // .grp-h: 11 px, uppercase, .1em tracking, t3.
            char Upper[48];
            UpperCopy(Upper, sizeof(Upper), Row.Label);
            const float Lift = Ui->CalcTextSizeA(11.0f, FLT_MAX, 0.0f, Upper).y * 0.5f;
            DrawSpaced(Draw, Ui, 11.0f, ImVec2(X, Cy - Lift), ScaleAlpha((Hot || Picked) ? kText : kT2, Fade), Upper, 1.1f);
        }
        else if (Folder)
        {
            DrawClipped(Draw, Ui, 13.0f, X, Cy, NameW, ScaleAlpha(Ink, DimF), Row.Label);
        }
        else if (Cad)
        {
            // .row .txt: the name over a mono 10 px meta line.
            DrawClipped(Draw, Ui, 13.0f, X, Cy - 7.0f, NameW, ScaleAlpha(Ink, DimF), Row.Label);
            if (!Compact_ && Row.Meta[0] != '\0')
            {
                DrawClipped(Draw, Mono, 10.0f, X, Cy + 8.0f, NameW, ScaleAlpha(kT3, Fade * DimF), Row.Meta);
            }
        }
        else
        {
            float TagW = 0.0f;
            if (Row.Tag[0] != '\0')
            {
                char TagUpper[8];
                UpperCopy(TagUpper, sizeof(TagUpper), Row.Tag);
                TagW = MeasureSpaced(Ui, 9.0f, TagUpper, 0.7f) + 12.0f;
            }
            const float TextRoom = NameW - (TagW > 0.0f ? TagW + 6.0f : 0.0f);
            DrawClipped(Draw, Ui, 13.0f, X, Cy, TextRoom, ScaleAlpha(Ink, DimF), Row.Label);
            if (TagW > 0.0f)
            {
                char TagUpper[8];
                UpperCopy(TagUpper, sizeof(TagUpper), Row.Tag);
                const float Used = MeasureSized(Ui, 13.0f, Row.Label);
                const float TagX = X + (Used < TextRoom ? Used : TextRoom) + 6.0f;
                const ImVec2 TMin(TagX, Cy - 7.0f);
                const ImVec2 TMax(TagX + TagW, Cy + 7.0f);
                Draw->AddRect(TMin, TMax, ScaleAlpha(kStroke, Fade), 7.0f, 0, 1.0f);
                DrawSpaced(Draw, Ui, 9.0f, ImVec2(TagX + 6.0f, Cy - Ui->CalcTextSizeA(9.0f, FLT_MAX, 0.0f, TagUpper).y * 0.5f),
                    ScaleAlpha(kT3, Fade), TagUpper, 0.7f);
            }
        }
    }

    // The row's own clicks, after the small targets had theirs.
    ImGui::SetCursorScreenPos(Min);
    ImGui::PopID();
    ImGui::PushID(static_cast<int>(Index) + 4096);
    ImGui::InvisibleButton("##rowhit", ImVec2(1.0f, 1.0f));   // keeps the cursor advancing; the hit is the first button
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + RowH));
    ImGui::Dummy(ImVec2(Width, 0.0f));

    // The first InvisibleButton (the row) owns the plain click; it was read at the top through Hot.
    if (Hot && ImGui::IsMouseReleased(0) && DragLifted_ == kNoEditorInstance && !ImGui::IsMouseDragPastThreshold(0, 4.0f))
    {
        HandleRowClick(Index, InstanceCount);
        // The page opens every ancestor of a pick.
        uint32_t Owner = OwnerOf(Instances, Index);
        while (Owner != kNoEditorInstance)
        {
            Shut_[Owner] = false;
            Owner = OwnerOf(Instances, Owner);
        }
    }
    if (Hot && ImGui::IsMouseDoubleClicked(0) && !Row.Pinned && HasKids)
    {
        Shut_[Index] = !Shut_[Index];
    }
    if (Folding) Draw->PopClipRect();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DOCUMENT STYLE
//------------------------------------------------------------------------------------------------------------------------
// SolidArc's HTML outliner: the Figures / Selected tiles under the head, and a census foot in place of the game's readout
//    strip. The census is taken once per tick from the roster, so the tiles, the bar and the counts always agree.

void OutlinerPanel::TakeCensus(const EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    for (uint32_t& Count : CensusSymbol_)
    {
        Count = 0u;
    }
    CensusShown_  = 0u;
    CensusHidden_ = 0u;
    CensusIssues_ = 0u;
    for (uint32_t i = 0u; i < InstanceCount; ++i)
    {
        const EditorInstance& Row = Instances[i];
        if (Row.Category == EditorInstanceCategory::Folder)
        {
            continue;
        }
        ++CensusSymbol_[static_cast<uint32_t>(Row.Symbol) & 7u];
        if (Row.Visible)
        {
            ++CensusShown_;
        }
        else
        {
            ++CensusHidden_;
        }
        if (Row.Visible && (Row.Standing == EditorStanding::Warn || Row.Standing == EditorStanding::Err))
        {
            ++CensusIssues_;
        }
    }
}

void OutlinerPanel::RecordDocumentTiles(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    const float TileW = (RowWidth - 2.0f * kSidePad - 8.0f) * 0.5f;
    const float TileH = 64.0f;
    ImGui::Dummy(ImVec2(RowWidth, TileH + 10.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();
    ImDrawList* Draw    = ImGui::GetWindowDrawList();
    ImFont*     Ui      = Controls_->QueryUi();
    ImFont*     Mono    = Controls_->QueryMono();
    ImFont*     Display = Controls_->QueryDisplay();

    const uint32_t Total  = CensusShown_ + CensusHidden_;
    const uint32_t Picked = PickedCount_;
    const char*    Name   = (Picked > 0u && Picked_[0] < InstanceCount) ? Instances[Picked_[0]].Label : "none";

    for (uint32_t t = 0u; t < 2u; ++t)
    {
        const ImVec2 Min(Cursor.x + kSidePad + static_cast<float>(t) * (TileW + 8.0f), Cursor.y);
        const ImVec2 Max(Min.x + TileW, Min.y + TileH);
        Draw->AddRectFilled(Min, Max, kTileBg, 18.0f);
        Draw->AddRect(Min, Max, kStroke, 18.0f, 0, 1.0f);

        // .tile .l: an 8 px status dot and the label; .s: the 10 px t3 captions beneath; b: the 24 px figure.
        const bool Lit = t == 0u || Picked > 0u;
        Draw->AddCircleFilled(ImVec2(Min.x + 17.0f, Min.y + 17.0f), 4.0f, Lit ? kGreen : kT3);
        DrawSized(Draw, Ui, 11.0f, Min.x + 28.0f, Min.y + 17.0f, kT2, t == 0u ? "Figures" : "Selected");
        char Line[40];
        if (t == 0u)
        {
            std::snprintf(Line, sizeof(Line), "%u visible", CensusShown_);
            DrawSized(Draw, Mono, 10.0f, Min.x + 14.0f, Min.y + 36.0f, kT3, Line);
            std::snprintf(Line, sizeof(Line), "%u hidden", CensusHidden_);
            DrawSized(Draw, Mono, 10.0f, Min.x + 14.0f, Min.y + 49.0f, CensusHidden_ > 0u ? kOrange : kT3, Line);
        }
        else
        {
            DrawClipped(Draw, Mono, 10.0f, Min.x + 14.0f, Min.y + 43.0f, TileW - 28.0f - 30.0f, kT3, Picked > 1u ? "several" : Name);
        }
        char Figure[12];
        std::snprintf(Figure, sizeof(Figure), "%u", t == 0u ? Total : Picked);
        const float FigW = MeasureSized(Display, 28.0f, Figure);
        const ImVec2 FigG = Display->CalcTextSizeA(28.0f, FLT_MAX, 0.0f, Figure);
        Draw->AddText(Display, 28.0f, ImVec2(Max.x - 14.0f - FigW, Max.y - 8.0f - FigG.y + 2.0f), kText, Figure);
    }
    ImGui::SetCursorScreenPos(ImVec2(Cursor.x, Cursor.y + TileH + 10.0f));
}

void OutlinerPanel::RecordDocumentFooter() noexcept
{
    static const EditorReadout Resting = {};
    const EditorReadout& R = Readout_ != nullptr ? *Readout_ : Resting;
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    const float FootTop  = Controls_->QueryFootTop();
    if (ImGui::GetCursorScreenPos().y < FootTop)
    {
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, FootTop));
    }
    const float PadX = 14.0f;
    ImGui::Dummy(ImVec2(RowWidth, kEditorFooterH));
    const ImVec2 Cursor = ImGui::GetItemRectMin();
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    ImFont*     Ui   = Controls_->QueryUi();
    ImFont*     Mono = Controls_->QueryMono();
    Draw->AddRectFilled(Cursor, ImVec2(Cursor.x + RowWidth, Cursor.y + kEditorFooterH), kWash);
    Draw->AddLine(Cursor, ImVec2(Cursor.x + RowWidth, Cursor.y), kStroke, 1.0f);

    // .foot-census: one segment per symbol, as wide as the symbol is populous, in its own colour.
    static const ImU32 SymbolTint[8] = { 0u, IM_COL32(79, 216, 224, 255), IM_COL32(79, 216, 224, 255), IM_COL32(255, 180, 84, 255),
                                       IM_COL32(77, 163, 255, 255), IM_COL32(180, 140, 255, 255), IM_COL32(229, 211, 58, 255),
                                       IM_COL32(255, 107, 138, 255) };
    uint32_t Total = 0u;
    for (uint32_t k = 1u; k < 8u; ++k)
    {
        Total += CensusSymbol_[k];
    }
    const float BarY = Cursor.y + 7.0f;
    const float BarW = RowWidth - 2.0f * PadX;
    Draw->AddRectFilled(ImVec2(Cursor.x + PadX, BarY), ImVec2(Cursor.x + PadX + BarW, BarY + 5.0f), kWash, 3.0f);
    uint32_t Populated = 0u;
    for (uint32_t k = 1u; k < 8u; ++k)
    {
        Populated += CensusSymbol_[k] > 0u ? 1u : 0u;
    }
    float BarX = Cursor.x + PadX;
    const float Room = BarW - 3.0f * static_cast<float>(Populated > 0u ? Populated - 1u : 0u);
    for (uint32_t k = 1u; k < 8u && Total > 0u; ++k)
    {
        if (CensusSymbol_[k] == 0u)
        {
            continue;
        }
        const float W = Room * static_cast<float>(CensusSymbol_[k]) / static_cast<float>(Total);
        Draw->AddRectFilled(ImVec2(BarX, BarY), ImVec2(BarX + W, BarY + 5.0f), SymbolTint[k], 2.5f);
        BarX += W + 3.0f;
    }

    // .foot-row: the live counts left, the verdict right.
    const float Cy = Cursor.y + 28.0f;
    char Text[32];
    float X = Cursor.x + PadX;
    std::snprintf(Text, sizeof(Text), "%u/%u", CensusShown_, CensusShown_ + CensusHidden_);
    DrawSized(Draw, Mono, 10.5f, X, Cy, kText, Text);
    X += MeasureSized(Mono, 10.5f, Text) + 3.0f;
    DrawSized(Draw, Ui, 10.0f, X, Cy, kT3, "shown");
    X += MeasureSized(Ui, 10.0f, "shown") + 12.0f;
    std::snprintf(Text, sizeof(Text), "%u hidden", CensusHidden_);
    DrawSized(Draw, Mono, 10.5f, X, Cy, CensusHidden_ > 0u ? kOrange : kT3, Text);

    float RightX = Cursor.x + RowWidth - PadX;
    const bool Clean = CensusIssues_ == 0u;
    if (Clean)
    {
        std::snprintf(Text, sizeof(Text), "clean");
    }
    else
    {
        std::snprintf(Text, sizeof(Text), "%u issue%s", CensusIssues_, CensusIssues_ > 1u ? "s" : "");
    }
    RightX -= MeasureSized(Mono, 10.5f, Text);
    DrawSized(Draw, Mono, 10.5f, RightX, Cy, Clean ? kGreen : kOrange, Text);
    RightX -= 15.0f;
    DrawIcon(Draw, Clean ? OutlinerIconCategory::Check : OutlinerIconCategory::Warn, ImVec2(RightX, Cy - 6.0f), 12.0f, Clean ? kGreen : kOrange);
    if (R.Triangles > 0u)
    {
        std::snprintf(Text, sizeof(Text), "%.1fk tris", static_cast<double>(R.Triangles) / 1000.0);
        const float W = MeasureSized(Mono, 10.5f, Text);
        if (RightX - 12.0f - W > X + MeasureSized(Mono, 10.5f, "0 hidden") + 8.0f)
        {
            DrawSized(Draw, Mono, 10.5f, RightX - 12.0f - W, Cy, kT3, Text);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          FOOTER
//------------------------------------------------------------------------------------------------------------------------
// .outliner-foot: padding 10px 16px 12px, 1 px stroke above, four columns (compact two, padding 8 14 10), gap 4.
//    .foot-item: 9 px uppercase .1em t3 label, 14 px w300 tabular figure, 10 px t3 unit. Realtime N fps ·
//    Quality tier (11 px) with its pixels (9 px) below · Sun N.N° · Moons N / 4 · Cam x, y, z.

void OutlinerPanel::RecordFooter() noexcept
{
    static const EditorReadout Resting = {};
    const EditorReadout& R = Readout_ != nullptr ? *Readout_ : Resting;
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    // Pinned to the sill: whatever the content above ends at, the foot opens on the shared top row.
    const float FootTop = Controls_->QueryFootTop();
    if (ImGui::GetCursorScreenPos().y < FootTop)
    {
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, FootTop));
    }
    const float PadX = Compact_ ? 14.0f : 16.0f;
    // 7 + 26 + 7 = the shared forty (kEditorFooterH), compact or full. Only the TOP needs a variable: the 26 px row
    //    and the bottom 7 both ride `H` below, so a `PadB` would be a number nothing reads (MSVC C4189).
    const float PadT = 7.0f;
    const uint32_t Items = 5u;   // one line: every figure side by side
    const float H = kEditorFooterH;

    ImGui::Dummy(ImVec2(RowWidth, H));
    const ImVec2 Cursor = ImGui::GetItemRectMin();
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    ImFont*     Ui   = Controls_->QueryUi();
    ImFont*     Mono = Controls_->QueryMono();
    Draw->AddRectFilled(ImVec2(Cursor.x, Cursor.y), ImVec2(Cursor.x + RowWidth, Cursor.y + H), kWash);
    Draw->AddLine(ImVec2(Cursor.x, Cursor.y), ImVec2(Cursor.x + RowWidth, Cursor.y), kStroke, 1.0f);

    // One line, five columns. The camera column is the widest figure, so it takes the room the others leave:
    //    four narrow columns of equal width, the camera the remainder.
    const float Gap   = 4.0f;
    const float Inner = RowWidth - 2.0f * PadX - 4.0f * Gap;
    const float NarrowW = Compact_ ? Inner * 0.17f : Inner * 0.16f;
    const float CamW    = Inner - 4.0f * NarrowW;
    char Fps[12], Sun[16], Moons[12], Cam[32];
    std::snprintf(Fps, sizeof(Fps), "%.0f", static_cast<double>(R.Fps));
    std::snprintf(Sun, sizeof(Sun), "%.1f\xc2\xb0", static_cast<double>(R.SunElevation));
    std::snprintf(Moons, sizeof(Moons), "%u/%u", R.MoonCount, R.MoonCap);
    std::snprintf(Cam, sizeof(Cam), "%.0f, %.1f, %.0f", static_cast<double>(R.Cam[0]), static_cast<double>(R.Cam[1]),
        static_cast<double>(R.Cam[2]));

    const char* Labels[5]  = { "REALTIME", "QUALITY", "SUN", "MOONS", "CAM" };
    const char* Figures[5] = { Fps, R.Quality, Sun, Moons, Cam };
    const char* Units[5]   = { "fps", "", "", "", "" };
    float X = Cursor.x + PadX;
    const float Y = Cursor.y + PadT;
    const EditorFpsBand FpsBand = EditorFpsBandFor(R.Fps);
    for (uint32_t i = 0u; i < Items; ++i)
    {
        const float Room = (i == 4u) ? CamW : NarrowW;
        DrawSpaced(Draw, Ui, 9.0f, ImVec2(X, Y), kT3, Labels[i], 0.9f);
        const float FigPx = 12.0f;
        const float FigY  = Y + 12.0f;
        ImFont* FigFont = (i == 1u) ? Ui : Mono;
        float FigX = X;
        if (i == 0u && FpsBand == EditorFpsBand::Poor)
        {
            FootWarn(Draw, ImVec2(FigX, FigY + 5.0f), 10.0f, kOrange);
            FigX += 13.0f;
        }
        const ImU32 FigTint = (i == 0u)
            ? (FpsBand == EditorFpsBand::Good ? kGreen : (FpsBand == EditorFpsBand::Poor ? kOrange : kText))
            : kText;
        const float FigW = MeasureSized(FigFont, FigPx, Figures[i]);
        DrawClipped(Draw, FigFont, FigPx, FigX, FigY + 6.0f, Room - (FigX - X), FigTint, Figures[i]);
        if (Units[i][0] != '\0' && (FigX - X) + FigW + 3.0f + MeasureSized(Ui, 9.0f, Units[i]) < Room)
            DrawSized(Draw, Ui, 9.0f, FigX + FigW + 3.0f, FigY + 8.0f, kT3, Units[i]);
        X += Room + Gap;
    }
}

} // namespace Frontier
