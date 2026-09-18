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
constexpr float kRowH         = 36.0f;
constexpr float kRowHCompact  = 30.0f;
constexpr float kRowRadius    = 0.0f;    // rows are rectangles: selection, hover and drop ground all square
constexpr float kRowGap       = 8.0f;
constexpr float kChevBox      = 14.0f;
constexpr float kIcoBox       = 24.0f;
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

// Rotated chevron: the page turns the same glyph 90° when a row stands open.
void DrawChevron(ImDrawList* Draw, const ImVec2& Centre, float Size, ImU32 Tint, bool Open) noexcept
{
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
            Points[i] = Open ? ImVec2(Centre.x - Ly, Centre.y + Lx) : ImVec2(Centre.x + Lx, Centre.y + Ly);
        }
        if (Contour.Count >= 2u)
        {
            Draw->AddPolyline(Points, static_cast<int>(Contour.Count), Tint, ImDrawFlags_None, Thick);
            Draw->AddCircleFilled(Points[0], Thick * 0.5f, Tint);
            Draw->AddCircleFilled(Points[Contour.Count - 1u], Thick * 0.5f, Tint);
        }
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

ImU32 PillTint(EditorNarrowing Narrowing) noexcept
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

const char* PillLabel(EditorNarrowing Narrowing) noexcept
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
}

void OutlinerPanel::AssignTabOpen(bool* Open) noexcept
{
    TabOpen_ = Open;
}

void OutlinerPanel::AssignReadout(const EditorReadout* Readout) noexcept
{
    Readout_ = Readout;
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

void OutlinerPanel::PickInstance(uint32_t Index) noexcept
{
    Picked_[0]   = Index;
    PickedCount_ = 1u;
    Anchor_      = Index;
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
    if (!IsPicked(Index) && PickedCount_ < kMaxEditorPicked)
    {
        Picked_[PickedCount_++] = Index;
    }
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
    if (Lifted >= InstanceCount || Instances[Lifted].Pinned)
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
        if (Target >= InstanceCount || (Target >= Lifted && Target < LiftedEnd))
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
    for (uint32_t k = 0u; k < RunLen; ++k)
    {
        Scratch_[k] = Instances[Lifted + k];
        Scratch_[k].Depth = static_cast<uint32_t>(static_cast<int32_t>(Scratch_[k].Depth) + DepthShift);
    }
    bool LiftedShut[kMaxEditorInstances] = {};
    for (uint32_t k = 0u; k < RunLen; ++k)
    {
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
        Instances[Seat + k] = Scratch_[k];
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
    const bool Open = ImGui::Begin("Outliner", TabOpen_, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
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

    // The feed's opening pose, taken the first time a row is seen; after that the pose is the user's.
    for (uint32_t i = 0u; i < InstanceCount; ++i)
    {
        if (!PoseSeated_[i])
        {
            PoseSeated_[i] = true;
            Shut_[i]       = Instances[i].Shut;
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

    RecordHeader(Instances, InstanceCount);
    if (!Compact_)
    {
        RecordTiles(Instances, InstanceCount);
    }
    RecordSearch();
    if (!Compact_)
    {
        RecordChips();
    }
    (void)RecordOutline(Instances, InstanceCount);
    RecordFooter();
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
        std::snprintf(Sub, sizeof(Sub), "%s \xc2\xb7 %u nodes", Scene, Total);
        DrawSized(Draw, Ui, 12.0f, X, Cy + 1.0f, kT3, Sub);
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

    const ImVec2 Min(Cursor.x + kSidePad, Cursor.y);
    const ImVec2 Max(Cursor.x + RowWidth - kSidePad, Cursor.y + H);
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
        DrawSized(Draw, Ui, 13.0f, FieldX, Cy, kT3, "Search  Ctrl+Shift+F");
    }
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
    for (uint32_t n = 1u; n < static_cast<uint32_t>(EditorNarrowing::Count); ++n)
    {
        const EditorNarrowing Narrowing = static_cast<EditorNarrowing>(n);
        const char* Label = PillLabel(Narrowing);
        const float W = 10.0f + 6.0f + 6.0f + MeasureSized(Ui, 11.0f, Label) + 10.0f;
        if (X + W > Right && X > Start.x + kSidePad)
        {
            X = Start.x + kSidePad;
            Y += 26.0f + 5.0f;
        }
        const ImVec2 Min(X, Y);
        const ImVec2 Max(X + W, Y + 26.0f);
        ImGui::SetCursorScreenPos(Min);
        ImGui::PushID(static_cast<int>(n));
        ImGui::InvisibleButton("##pill", ImVec2(W, 26.0f));
        if (ImGui::IsItemClicked())
        {
            NarrowOn_[n] = !NarrowOn_[n];
        }
        ImGui::PopID();
        const bool On = NarrowOn_[n];
        if (On)
        {
            Draw->AddRectFilled(Min, Max, kG3, 13.0f);
        }
        Draw->AddRect(Min, Max, On ? kStroke2 : kStroke, 13.0f, 0, 1.0f);
        const float Cy = Y + 13.0f;
        Draw->AddCircleFilled(ImVec2(X + 10.0f + 3.0f, Cy), 3.0f, PillTint(Narrowing));
        DrawSized(Draw, Ui, 11.0f, X + 10.0f + 6.0f + 6.0f, Cy, On ? kText : kT3, Label);
        X += W + 5.0f;
    }
    ImGui::SetCursorScreenPos(ImVec2(Start.x, Y + 26.0f + 8.0f));
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
    bool AnyPill = false;
    for (uint32_t n = 0u; n < static_cast<uint32_t>(EditorNarrowing::Count); ++n)
    {
        AnyPill = AnyPill || NarrowOn_[n];
    }

    // Matches, then ancestors of matches. Folders match only through their rows (the page's 'world' rule).
    uint32_t Hits = 0u;
    for (uint32_t i = 0u; i < InstanceCount; ++i)
    {
        const EditorInstance& Row = Instances[i];
        bool Match = !Searching || ContainsFolded(Row.Label, QueryText_);
        if (Match && AnyPill)
        {
            const EditorNarrowing N = NarrowingFor(Row);
            Match = N != EditorNarrowing::Auto && NarrowOn_[static_cast<uint32_t>(N)];
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
    ImGui::BeginChild("##tree", ImVec2(Width, Avail > 0.0f ? Avail : 1.0f), ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar);
    TreeHovered_ = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImGui::Dummy(ImVec2(Width, 2.0f));

    uint32_t Drawn = 0u;
    uint32_t i = 0u;
    while (i < InstanceCount)
    {
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
        RecordRow(Instances, InstanceCount, i, End > i + 1u);
        ++Drawn;
        if (Shut_[i] && !Searching)
        {
            i = End;
        }
        else
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

void OutlinerPanel::RecordRow(EditorInstance* Instances, uint32_t InstanceCount, uint32_t Index, bool HasKids) noexcept
{
    EditorInstance& Row = Instances[Index];
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    ImFont*     Ui   = Controls_->QueryUi();
    ImFont*     Mono = Controls_->QueryMono();
    const float RowH = Compact_ ? kRowHCompact : kRowH;
    const float Width = ImGui::GetContentRegionAvail().x;

    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    const ImVec2 Min(Origin.x + kTreePad, Origin.y);
    const ImVec2 Max(Origin.x + Width - kTreePad, Origin.y + RowH);
    ImGui::PushID(static_cast<int>(Index));
    ImGui::SetCursorScreenPos(Min);
    ImGui::InvisibleButton("##row", ImVec2(Max.x - Min.x, RowH), ImGuiButtonFlags_MouseButtonLeft);
    const bool Hot     = ImGui::IsItemHovered();
    const bool Picked  = IsPicked(Index);
    // A fresh pick scrolls into view once, the way the page's select() does; after that the scroll is the user's.
    if (Picked && PickedCount_ > 0u && Picked_[PickedCount_ - 1u] == Index && Revealed_ != Index)
    {
        Revealed_ = Index;
        ImGui::SetScrollHereY(0.5f);
    }
    const bool Folder  = Row.Category == EditorInstanceCategory::Folder;
    const bool Dim     = !Row.Visible && !Folder;
    const bool IsOpen  = !Shut_[Index];
    const ImU32 Accent = RowTint(Row);

    // Drag: the page's HTML5 drag. A press that travels starts one; pinned folders never do.
    if (ImGui::IsItemActive() && !Row.Pinned && DragLifted_ == kNoEditorInstance
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
        Draw->AddRectFilled(Min, Max, kSelBg, kRowRadius);
        Draw->AddRect(Min, Max, kStroke2, kRowRadius, 0, 1.0f);
        Draw->AddRectFilled(ImVec2(Min.x, Min.y), ImVec2(Min.x + 3.0f, Max.y), Accent, 0.0f);
    }
    else if (Hot && DragLifted_ == kNoEditorInstance)
    {
        Draw->AddRectFilled(Min, Max, kG2, kRowRadius);
    }
    if (DropHere && !DropBefore)
    {
        Draw->AddRectFilled(Min, Max, WithAlpha(Accent, 31), kRowRadius);   // color-mix 12 %
        Draw->AddRect(Min, Max, Accent, kRowRadius, 0, 1.0f);
    }
    if (DropHere && DropBefore)
    {
        Draw->AddRectFilled(ImVec2(Min.x + 12.0f, Min.y - 1.0f), ImVec2(Max.x - 12.0f, Min.y + 1.0f), kWhite, 2.0f);
    }
    const float Fade = Dragging ? 0.35f : 1.0f;
    const float DimF = Dim ? 0.4f : 1.0f;
    const ImU32 Ink  = ScaleAlpha((Hot || Picked) ? kText : kT2, Fade);

    // Columns, left to right.
    const float Cy = Min.y + RowH * 0.5f;
    float X = Min.x + 8.0f + static_cast<float>(Row.Depth) * 16.0f;

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
            DrawChevron(Draw, ImVec2(X + kChevBox * 0.5f, Cy), 11.0f, ScaleAlpha(kT3, Fade), IsOpen);
        }
        X += kChevBox + kRowGap;
    }

    // Glyph.
    {
        const ImU32 GlyphInk = ScaleAlpha(Accent, Fade * DimF);
        DrawIcon(Draw, IconFor(Row), ImVec2(X + (kIcoBox - 14.0f) * 0.5f, Cy - 7.0f), 14.0f, GlyphInk);
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

    // Standing dot.
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

    // Meta.
    if (!Compact_ && Row.Meta[0] != '\0')
    {
        const float W = MeasureSized(Mono, 11.0f, Row.Meta);
        RightX -= W;
        DrawSized(Draw, Mono, 11.0f, RightX, Cy, ScaleAlpha(kT3, Fade), Row.Meta);
        RightX -= kRowGap;
    }

    // Name and tag.
    {
        const float NameW = RightX - X;
        if (Folder)
        {
            char Upper[48];
            UpperCopy(Upper, sizeof(Upper), Row.Label);
            DrawSpaced(Draw, Ui, 11.0f, ImVec2(X, Cy - Ui->CalcTextSizeA(11.0f, FLT_MAX, 0.0f, Upper).y * 0.5f),
                ScaleAlpha(kT3, Fade * DimF), Upper, 1.1f);
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
