//============================================================================================================================================
//                                                    VIEWPORTPANEL.CPP
//============================================================================================================================================
// 🧩 Development editor viewport — the scene column.

#include "ViewportPanel.h"

#include "ControlPanel.h"
#include "EditorInstance.h"

#include <imgui.h>
#include <imgui_internal.h>   // ImGuiWindow: the SkipItems early-out

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                          TOKENS
//------------------------------------------------------------------------------------------------------------------------

constexpr ImU32 kView   = IM_COL32(7, 9, 12, 255);
constexpr ImU32 kTile   = IM_COL32(34, 34, 34, 255);
constexpr ImU32 kField  = IM_COL32(0, 0, 0, 255);
constexpr ImU32 kText   = IM_COL32(240, 240, 240, 255);
constexpr ImU32 kDim    = IM_COL32(136, 136, 136, 255);
constexpr ImU32 kFaint  = IM_COL32(92, 92, 92, 255);
constexpr ImU32 kStroke = IM_COL32(255, 255, 255, 13);
constexpr ImU32 kStrong = IM_COL32(46, 46, 46, 255);
constexpr ImU32 kWash   = IM_COL32(255, 255, 255, 5);
constexpr ImU32 kInset  = IM_COL32(26, 26, 26, 255);
constexpr ImU32 kHover  = IM_COL32(34, 34, 34, 255);
constexpr ImU32 kSeated = IM_COL32(42, 42, 42, 255);
constexpr ImU32 kOk     = IM_COL32(34, 197, 94, 255);
constexpr ImU32 kDanger = IM_COL32(239, 68, 68, 255);
constexpr ImU32 kAmber  = IM_COL32(245, 158, 11, 255);
constexpr ImU32 kHi     = IM_COL32(108, 119, 255, 255);
constexpr ImU32 kPrimaryBg   = IM_COL32(108, 119, 255, 33);    // the standing row's indigo wash
constexpr ImU32 kPrimaryOn   = IM_COL32(108, 119, 255, 61);    // picked by keys, it deepens
constexpr ImU32 kPrimaryEdge = IM_COL32(108, 119, 255, 87);    // its indigo hem
constexpr ImU32 kBadBg       = IM_COL32(239, 68, 68, 26);      // the unheard line's red wash
constexpr ImU32 kBadEdge     = IM_COL32(239, 68, 68, 77);
constexpr ImU32 kBadTitle    = IM_COL32(255, 180, 180, 255);

constexpr uint32_t kEdit      = 0u;
constexpr uint32_t kPlay      = 1u;
constexpr uint32_t kSimulate  = 2u;

constexpr ImU32 kMenuHover = IM_COL32(20, 20, 20, 255);
constexpr ImU32 kMenuSel   = IM_COL32(24, 24, 24, 255);

constexpr float kOrbitPi = 3.14159265359f;

// The views menu's rows: two projections, then the six compass snaps. Home (viewpoint 0) keeps the seated
//    figures; each snap carries the euler that faces its side, in the solver's own convention (yaw 0 faces
//    +Y, positive pitch looks up), so a pick poses the fly camera with no conversion at all.
const char* kSnapNames[7] = { "", "Front", "Back", "Right", "Left", "Top", "Bottom" };
struct SnapEuler
{
    float Yaw;
    float Pitch;
};
constexpr SnapEuler kSnaps[7] = {
    { 0.0f, 0.0f },                    // home: unused, the seated figures stay
    { 0.0f, 0.0f },                    // front: faces +Y
    { kOrbitPi, 0.0f },                // back: faces −Y
    { -kOrbitPi * 0.5f, 0.0f },        // right: faces −X
    { kOrbitPi * 0.5f, 0.0f },         // left: faces +X
    { 0.0f, -kOrbitPi * 0.5f },        // top: faces −Z
    { 0.0f, kOrbitPi * 0.5f },         // bottom: faces +Z
};

// A gizmo pad tap snaps its view: the pad on +X looks back down −X, which reads right.
constexpr uint32_t kPadViews[6] = { 3u, 4u, 2u, 1u, 5u, 6u };

// The orbit's basis, the solver's formula: forward off yaw and pitch, right off forward × +Z, up off
//    right × forward. At the poles the first cross collapses, so east stays east off the yaw alone.
void OrbitBasis(float Yaw, float Pitch, float F[3], float R[3], float U[3]) noexcept
{
    const float Cy = std::cos(Yaw), Sy = std::sin(Yaw);
    const float Cp = std::cos(Pitch), Sp = std::sin(Pitch);
    F[0] = Sy * Cp; F[1] = Cy * Cp; F[2] = Sp;
    float Rx = F[1], Ry = -F[0];
    const float Rl = std::sqrt(Rx * Rx + Ry * Ry);
    if (Rl < 1e-4f) { Rx = Cy; Ry = -Sy; }
    else            { Rx /= Rl; Ry /= Rl; }
    R[0] = Rx; R[1] = Ry; R[2] = 0.0f;
    U[0] = Ry * F[2]; U[1] = -Rx * F[2]; U[2] = Rx * F[1] - Ry * F[0];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSPORT GLYPHS
//------------------------------------------------------------------------------------------------------------------------

// The run glyphs, traced from the reference icon sheet (24-space, stroked at 1.9): filled here, because
//    at thirteen pixels a filled mark reads where an outline ghosts.
ImVec2 GlyphDot(const ImVec2& Centre, float Size, float X, float Y) noexcept
{
    const float S = Size / 24.0f;
    return ImVec2(Centre.x + (X - 12.0f) * S, Centre.y + (Y - 12.0f) * S);
}

void PlayGlyph(ImDrawList* Draw, const ImVec2& Centre, float Size, ImU32 Tint) noexcept
{
    Draw->AddTriangleFilled(GlyphDot(Centre, Size, 6.0f, 4.0f), GlyphDot(Centre, Size, 6.0f, 20.0f),
        GlyphDot(Centre, Size, 20.0f, 12.0f), Tint);
}

void PauseGlyph(ImDrawList* Draw, const ImVec2& Centre, float Size, ImU32 Tint) noexcept
{
    const float S = Size / 24.0f;
    Draw->AddRectFilled(GlyphDot(Centre, Size, 6.0f, 4.0f), GlyphDot(Centre, Size, 10.0f, 20.0f), Tint, S);
    Draw->AddRectFilled(GlyphDot(Centre, Size, 14.0f, 4.0f), GlyphDot(Centre, Size, 18.0f, 20.0f), Tint, S);
}

void StopGlyph(ImDrawList* Draw, const ImVec2& Centre, float Size, ImU32 Tint) noexcept
{
    Draw->AddRectFilled(GlyphDot(Centre, Size, 6.0f, 6.0f), GlyphDot(Centre, Size, 18.0f, 18.0f),
        Tint, 2.0f * Size / 24.0f);
}

void StepGlyph(ImDrawList* Draw, const ImVec2& Centre, float Size, ImU32 Tint) noexcept
{
    Draw->AddTriangleFilled(GlyphDot(Centre, Size, 7.0f, 5.0f), GlyphDot(Centre, Size, 7.0f, 19.0f),
        GlyphDot(Centre, Size, 16.0f, 12.0f), Tint);
    Draw->AddRectFilled(GlyphDot(Centre, Size, 17.0f, 5.0f), GlyphDot(Centre, Size, 19.0f, 19.0f), Tint, 0.0f);
}

void SimGlyph(ImDrawList* Draw, const ImVec2& Centre, float Size, ImU32 Tint) noexcept
{
    // The clock face with its arrow: the arc walks the long way round (east through south, west, north),
    //    leaving the north-east quadrant for the arrowhead.
    const float S = Size / 24.0f;
    Draw->PathArcTo(Centre, 9.0f * S, 0.0f, 4.71239f, 0);
    Draw->PathStroke(Tint, 1.9f * S, 0);
    Draw->AddLine(GlyphDot(Centre, Size, 12.0f, 12.0f), GlyphDot(Centre, Size, 12.0f, 7.0f), Tint, 1.9f * S);
    Draw->AddLine(GlyphDot(Centre, Size, 12.0f, 12.0f), GlyphDot(Centre, Size, 15.5f, 14.0f), Tint, 1.9f * S);
    Draw->AddTriangleFilled(GlyphDot(Centre, Size, 17.0f, 3.0f), GlyphDot(Centre, Size, 21.0f, 5.0f),
        GlyphDot(Centre, Size, 17.0f, 7.0f), Tint);
}

void CommandGlyph(ImDrawList* Draw, const ImVec2& Centre, float Size, ImU32 Tint) noexcept
{
    // The console's loop, vertex for vertex from the sheet; the corners stand in for the sheet's arcs.
    constexpr float Loop[10][2] = { { 6.0f, 3.0f }, { 3.0f, 6.0f }, { 3.0f, 18.0f }, { 6.0f, 15.0f },
        { 18.0f, 15.0f }, { 21.0f, 18.0f }, { 21.0f, 6.0f }, { 18.0f, 9.0f }, { 6.0f, 9.0f }, { 6.0f, 3.0f } };
    for (uint32_t i = 0u; i < 10u; ++i)
    {
        Draw->PathLineTo(GlyphDot(Centre, Size, Loop[i][0], Loop[i][1]));
    }
    Draw->PathStroke(Tint, 1.9f * Size / 24.0f, ImDrawFlags_Closed);
}

void CloseGlyph(ImDrawList* Draw, const ImVec2& Centre, float Size, ImU32 Tint) noexcept
{
    // The bad row's cross: two strokes corner to corner of the fourteen box.
    const float S = Size / 24.0f;
    Draw->AddLine(GlyphDot(Centre, Size, 7.0f, 7.0f), GlyphDot(Centre, Size, 17.0f, 17.0f), Tint, 1.9f * S);
    Draw->AddLine(GlyphDot(Centre, Size, 17.0f, 7.0f), GlyphDot(Centre, Size, 7.0f, 17.0f), Tint, 1.9f * S);
}

void RunGlyph(uint32_t Icon, ImDrawList* Draw, const ImVec2& Centre, float Size, ImU32 Tint) noexcept
{
    if (Icon == 0u)      { PlayGlyph(Draw, Centre, Size, Tint); }
    else if (Icon == 1u) { SimGlyph(Draw, Centre, Size, Tint); }
    else if (Icon == 2u) { PauseGlyph(Draw, Centre, Size, Tint); }
    else if (Icon == 3u) { StepGlyph(Draw, Centre, Size, Tint); }
    else if (Icon == 4u) { StopGlyph(Draw, Centre, Size, Tint); }
    else                 { CommandGlyph(Draw, Centre, Size, Tint); }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SPACED CAPS
//------------------------------------------------------------------------------------------------------------------------

// Letterspaced caps, the footer labels and chips: the reference spaces its micro-caps by a pixel and change.
float SpacedCapsWidth(ImFont* Font, const char* Text, float Tracking) noexcept
{
    ImGui::PushFont(Font);
    float Advance = 0.0f;
    for (const char* P = Text; *P != '\0';)
    {
        // Whole glyphs at a time: the middle dot walks through untouched.
        uint32_t Len = 1u;
        const unsigned char Lead = static_cast<unsigned char>(*P);
        if ((Lead & 0xE0u) == 0xC0u)      { Len = 2u; }
        else if ((Lead & 0xF0u) == 0xE0u) { Len = 3u; }
        else if ((Lead & 0xF8u) == 0xF0u) { Len = 4u; }
        char Upper[8] = {};
        for (uint32_t i = 0u; i < Len && P[i] != '\0'; ++i)
        {
            Upper[i] = (i == 0u) ? static_cast<char>(std::toupper(Lead)) : P[i];
        }
        Advance += Font->CalcTextSizeA(Font->LegacySize, FLT_MAX, 0.0f, Upper).x + Tracking;
        P += Len;
    }
    ImGui::PopFont();
    return Advance > 0.0f ? Advance - Tracking : 0.0f;
}

void SpacedCaps(ImDrawList* Draw, ImFont* Font, const char* Text, const ImVec2& At, ImU32 Tint,
                float Tracking) noexcept
{
    ImGui::PushFont(Font);
    float Advance = 0.0f;
    for (const char* P = Text; *P != '\0';)
    {
        uint32_t Len = 1u;
        const unsigned char Lead = static_cast<unsigned char>(*P);
        if ((Lead & 0xE0u) == 0xC0u)      { Len = 2u; }
        else if ((Lead & 0xF0u) == 0xE0u) { Len = 3u; }
        else if ((Lead & 0xF8u) == 0xF0u) { Len = 4u; }
        char Upper[8] = {};
        for (uint32_t i = 0u; i < Len && P[i] != '\0'; ++i)
        {
            Upper[i] = (i == 0u) ? static_cast<char>(std::toupper(Lead)) : P[i];
        }
        Draw->AddText(ImVec2(At.x + Advance, At.y), Tint, Upper);
        Advance += Font->CalcTextSizeA(Font->LegacySize, FLT_MAX, 0.0f, Upper).x + Tracking;
        P += Len;
    }
    ImGui::PopFont();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  QUICK COMMANDS
//------------------------------------------------------------------------------------------------------------------------

struct QuickCommand
{
    const char* Label;
    const char* Sub;
};

constexpr QuickCommand kQuick[6] = {
    { "Play \xe2\x80\x94 run through a camera", "transport" },
    { "Simulate \xe2\x80\x94 run the world", "transport" },
    { "Pause / resume", "transport" },
    { "Step one frame", "transport" },
    { "Stop and restore", "transport" },
    { "Realtime viewport", "viewport" },
};

// The empty line offers nine sayable things, ours named for the Cornell shelf.
constexpr const char* kExamples[9] = {
    "find tall box",
    "rotate tall box 40 degrees on z",
    "move sphere 2 m on x",
    "add sphere at x 3 y 2 z -1",
    "enable physics on selected objects",
    "isolate selection",
    "set time to golden hour",
    "hide moon",
    "scale cone 2x",
};

// The verb words the line listens for: pipe-kept keys, a usage, and the help the row hangs right.
struct VerbWord
{
    const char* Keys;
    const char* Usage;
    const char* Help;
};

constexpr VerbWord kVerbs[15] = {
    { "find|locate|select|where is|go to|show me|pick", "find <entity>",
      "select it, reveal it in the tree and frame it" },
    { "rotate|turn|spin|yaw|pitch|roll", "rotate <entity> 40 degrees on z",
      "degrees by default, radians if you say so" },
    { "move|translate|shift|nudge|push|place|put", "move <entity> 2 m on x",
      "or \"move cube to x 4 y 1 z 0\"" },
    { "scale|resize|grow|shrink", "scale <entity> 2x", "uniform, or add \"on y\" for one axis" },
    { "add|create|spawn|new|insert|drop", "add sphere at x 3 y 2 z -1", "any entity type, anywhere" },
    { "enable physics|disable physics|turn on physics|turn off physics|add physics|remove physics|physics",
      "enable physics on <entity>", "bodies fall and settle while the world runs" },
    { "exit isolation|unisolate|leave isolation|clear isolation|show everything", "exit isolation",
      "bring the rest of the world back" },
    { "delete from ram|remove from ram|delete from memory|purge|wipe|free|destroy|nuke",
      "delete from ram <entity>", "deletes it and disposes its GPU + RAM buffers for good" },
    { "hide|unhide|show", "hide <entity>", "visibility, same as the eye in the outliner" },
    { "frame|focus|look at|zoom to", "frame <entity|everything>", "" },
    { "set time|time|set the time|make it", "set time to golden hour",
      "a clock time, or sunrise / noon / dusk / midnight" },
    { "play|run", "play", "run the world through a camera" },
    { "close popups|close all popups|clear popups", "close popups", "" },
    { "set|make", "set roughness of <entity> to 0.2", "any property on any entity" },
    { "help|commands|what can i say|?", "help", "" },
};


bool InfixMatch(const char* Label, const char* Text) noexcept
{
    if (Text[0] == '\0')
    {
        return true;
    }
    for (const char* P = Label; *P != '\0'; ++P)
    {
        const char* A = P;
        const char* B = Text;
        while (*A != '\0' && *B != '\0'
            && std::tolower(static_cast<unsigned char>(*A)) == std::tolower(static_cast<unsigned char>(*B)))
        {
            ++A;
            ++B;
        }
        if (*B == '\0')
        {
            return true;
        }
    }
    return false;
}

bool StartsFolded(const char* Str, const char* Prefix) noexcept
{
    while (*Prefix != '\0')
    {
        if (*Str == '\0'
            || std::tolower(static_cast<unsigned char>(*Str)) != std::tolower(static_cast<unsigned char>(*Prefix)))
        {
            return false;
        }
        ++Str;
        ++Prefix;
    }
    return true;
}

// A verb word answers when one of its pipe-kept keys opens with the typed word, or — past two
//    letters — carries the typed first word inside.
bool KeysHit(const char* Keys, const char* Text, const char* First) noexcept
{
    const size_t Eaten = std::strlen(Text);
    const char* At = Keys;
    for (;;)
    {
        const char* Bar = std::strchr(At, '|');
        const size_t Span = (Bar != nullptr) ? static_cast<size_t>(Bar - At) : std::strlen(At);
        char Key[32] = {};
        if (Span < sizeof(Key))
        {
            std::strncpy(Key, At, Span);
            if (StartsFolded(Key, Text) || (Eaten > 2u && InfixMatch(Key, First)))
            {
                return true;
            }
        }
        if (Bar == nullptr)
        {
            return false;
        }
        At = Bar + 1;
    }
}

// While the word still grows into a verb key, the line withholds its scold.
bool GrowingWord(const char* Text) noexcept
{
    if (Text[0] == '\0')
    {
        return false;
    }
    for (uint32_t v = 0u; v < 15u; ++v)
    {
        const char* At = kVerbs[v].Keys;
        for (;;)
        {
            const char* Bar = std::strchr(At, '|');
            const size_t Span = (Bar != nullptr) ? static_cast<size_t>(Bar - At) : std::strlen(At);
            char Key[32] = {};
            if (Span < sizeof(Key))
            {
                std::strncpy(Key, At, Span);
                if (StartsFolded(Key, Text) && std::strlen(Key) != std::strlen(Text))
                {
                    return true;
                }
            }
            if (Bar == nullptr)
            {
                break;
            }
            At = Bar + 1;
        }
    }
    return false;
}

// The usage sheds its bracketed tail: 'find <entity>' offers 'find '.
void VerbInsert(const char* Usage, char* Out, size_t OutSize) noexcept
{
    size_t Eaten = 0u;
    while (Usage[Eaten] != '\0' && Usage[Eaten] != '<' && Eaten + 1u < OutSize)
    {
        Out[Eaten] = Usage[Eaten];
        ++Eaten;
    }
    while (Eaten > 0u && Out[Eaten - 1u] == ' ')
    {
        --Eaten;
    }
    if (Eaten + 1u < OutSize)
    {
        Out[Eaten] = ' ';
        ++Eaten;
    }
    Out[Eaten] = '\0';
}

// The completion a row pours into the line: examples whole, usages shed, entries found.
void SugInsertText(uint32_t Sort, uint32_t At, const char* EntryLabel, char* Out, size_t OutSize) noexcept
{
    if (Sort == 1u)
    {
        std::snprintf(Out, OutSize, "%s", kExamples[At]);
    }
    else if (Sort == 2u)
    {
        VerbInsert(kVerbs[At].Usage, Out, OutSize);
    }
    else if (Sort == 3u)
    {
        std::snprintf(Out, OutSize, "find %s", EntryLabel);
    }
    else
    {
        Out[0] = '\0';
    }
}


} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                           WIRING
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::AssignControls(ControlPanel* Controls) noexcept
{
    Controls_ = Controls;
}

void ViewportPanel::AssignView(const unsigned char* Rgba, uint32_t Width, uint32_t Height) noexcept
{
    ViewRgba_    = (Rgba != nullptr && Width > 0u && Height > 0u) ? Rgba : nullptr;
    ViewTexture_ = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(ViewRgba_));
    ViewW_       = (ViewRgba_ != nullptr) ? Width : 0u;
    ViewH_       = (ViewRgba_ != nullptr) ? Height : 0u;
}

void ViewportPanel::AssignViewTexture(ImTextureID View, uint32_t Width, uint32_t Height) noexcept
{
    ViewTexture_ = View;
    ViewW_       = Width;
    ViewH_       = Height;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           RECORD
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::SeatViewportOrbit(const ViewportOrbit& Seated) noexcept
{
    Orbit_ = Seated;
    Home_  = Seated;
}

void ViewportPanel::Record(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    IM_ASSERT(Controls_ != nullptr);
    if (!ImGui::Begin("Viewport", nullptr))
    {
        ImGui::End();
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 0.0f));
    RecordBar();
    RecordView();
    RecordCommand(Instances, InstanceCount);
    RecordFooter(Instances, InstanceCount);
    ImGui::PopStyleVar();
    ImGui::End();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         TRANSPORT
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::SetTransport(uint32_t Mode) noexcept
{
    // A run is always realtime; coming home unpauses. The world snapshot the reference keeps belongs to
    //    the engine wiring, so the UI keeps the mode machine and its two rules.
    Transport_ = Mode;
    if (Mode == kEdit)
    {
        Paused_ = false;
    }
    else
    {
        Realtime_ = true;
        Paused_   = false;
    }
}

void ViewportPanel::SetPaused(bool Paused) noexcept
{
    Paused_ = Paused;
}

void ViewportPanel::SetRealtime(bool Realtime) noexcept
{
    Realtime_ = Realtime;
}

void ViewportPanel::StepOnce() noexcept
{
    // One tick past the pause.
    if (!Paused_)
    {
        Paused_ = true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         HEADER BAR
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::RecordBar() noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 44.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Ui    = Controls_->QueryUi();
    ImFont*     Small = Controls_->QuerySmall();

    const ImVec2 TileMin(Cursor.x, Cursor.y + 8.0f);
    const ImVec2 TileMax(Cursor.x + 28.0f, Cursor.y + 36.0f);
    Draw->AddRectFilled(TileMin, TileMax, kTile, 8.0f);
    Draw->AddRect(TileMin, TileMax, kStroke, 8.0f);
    ImGui::PushFont(Ui);
    const ImVec2 FGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, "F");
    Draw->AddText(ImVec2(Cursor.x + (28.0f - FGlyph.x) * 0.5f, Cursor.y + 8.0f + (28.0f - FGlyph.y) * 0.5f),
        kText, "F");
    ImGui::PopFont();

    // Degenerate stages hold the bar's height and rest: the bar needs ~560 px, and narrower columns keep
    //    the brand alone rather than tripping a cursor-boundary assert on the way past the right edge.
    if (RowWidth < 560.0f)
    {
        return;
    }

    float X = Cursor.x + 36.0f;
    const float BtnY = Cursor.y + 8.0f;

    bool* Docks[2] = { &DockLeft_, &DockRight_ };
    for (uint32_t i = 0u; i < 2u; ++i)
    {
        ImGui::SetCursorScreenPos(ImVec2(X, BtnY));
        char DockId[10] = {};
        std::snprintf(DockId, sizeof(DockId), "##dk%u", i);
        ImGui::InvisibleButton(DockId, ImVec2(28.0f, 28.0f));
        const bool Hot = ImGui::IsItemHovered();
        if (Hot && ImGui::IsMouseClicked(0))
        {
            *Docks[i] = !*Docks[i];
        }
        // Decorative: the dock columns are fixed while the layout is under review.
        const ImVec2 Centre(X + 14.0f, BtnY + 14.0f);
        Draw->AddCircleFilled(Centre, 14.0f, Hot ? IM_COL32(255, 255, 255, 24) : IM_COL32(255, 255, 255, 12));
        Draw->AddRect(ImVec2(Centre.x - 6.0f, Centre.y - 5.0f), ImVec2(Centre.x + 6.0f, Centre.y + 5.0f),
            *Docks[i] ? kDim : kFaint, 2.0f, 0, 1.4f);
        const float BarX = (i == 0u) ? (Centre.x - 6.0f) : (Centre.x + 3.0f);
        Draw->AddRectFilled(ImVec2(BarX, Centre.y - 5.0f), ImVec2(BarX + 3.0f, Centre.y + 5.0f),
            *Docks[i] ? kText : kFaint);
        X += 36.0f;
    }

    ImGui::PushFont(Small);
    const ImVec2 MarkGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Markers");
    ImGui::PopFont();
    const float MarkW = MarkGlyph.x + 24.0f;
    ImGui::SetCursorScreenPos(ImVec2(X, BtnY + 3.0f));
    ImGui::InvisibleButton("##markers", ImVec2(MarkW, 22.0f));
    const bool MarkHot = ImGui::IsItemHovered();
    if (MarkHot && ImGui::IsMouseClicked(0))
    {
        MarkersOn_ = !MarkersOn_;
    }
    Draw->AddRectFilled(ImVec2(X, BtnY + 3.0f), ImVec2(X + MarkW, BtnY + 25.0f),
        MarkersOn_ ? IM_COL32(26, 26, 26, 255) : IM_COL32(255, 255, 255, 8), 11.0f);
    Draw->AddRect(ImVec2(X, BtnY + 3.0f), ImVec2(X + MarkW, BtnY + 25.0f),
        MarkersOn_ ? kStrong : kStroke, 11.0f);
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(X + 12.0f, BtnY + 3.0f + (22.0f - MarkGlyph.y) * 0.5f),
        MarkersOn_ ? kText : kDim, "Markers");
    ImGui::PopFont();
    X += MarkW + 8.0f;

    // The views menu: the pill reads the orbit (home shows the projection, a snap its compass name),
    //    and opens the eight rows — two projections over the six snaps — with a check on each half of
    //    the pose. A projection row restores the seated home under it; a snap keeps the projection.
    char ViewLabel[32] = {};
    if (Orbit_.ViewPoint == 0u)
        std::snprintf(ViewLabel, sizeof(ViewLabel), "%s", Orbit_.Ortho ? "Orthographic" : "Perspective");
    else
        std::snprintf(ViewLabel, sizeof(ViewLabel), "%s %s", kSnapNames[Orbit_.ViewPoint],
                      Orbit_.Ortho ? "Ortho" : "Persp");
    ImGui::PushFont(Small);
    const ImVec2 ViewGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, ViewLabel);
    ImGui::PopFont();
    const float ViewW = ViewGlyph.x + 44.0f;
    ImGui::SetCursorScreenPos(ImVec2(X, BtnY + 3.0f));
    ImGui::InvisibleButton("##viewbutton", ImVec2(ViewW, 22.0f));
    const bool ViewHot = ImGui::IsItemHovered();
    if (ViewHot && ImGui::IsMouseClicked(0))
        ImGui::OpenPopup("##viewmenu");
    Draw->AddRectFilled(ImVec2(X, BtnY + 3.0f), ImVec2(X + ViewW, BtnY + 25.0f),
        ViewHot ? IM_COL32(255, 255, 255, 24) : IM_COL32(255, 255, 255, 12), 11.0f);
    Draw->AddRect(ImVec2(X, BtnY + 3.0f), ImVec2(X + ViewW, BtnY + 25.0f), kStroke, 11.0f);
    ImGui::PushFont(Small);
    Draw->AddText(ImVec2(X + 12.0f, BtnY + 3.0f + (22.0f - ViewGlyph.y) * 0.5f), kDim, ViewLabel);
    ImGui::PopFont();

    const float ViewTurnTarget = ViewMenuWasOpen_ ? 1.0f : 0.0f;
    const float ViewTurnStep   = ImGui::GetIO().DeltaTime / 0.2f;
    if (ViewChevronAnim_ < ViewTurnTarget)
    {
        ViewChevronAnim_ += ViewTurnStep;
        if (ViewChevronAnim_ > ViewTurnTarget)
            ViewChevronAnim_ = ViewTurnTarget;
    }
    else if (ViewChevronAnim_ > ViewTurnTarget)
    {
        ViewChevronAnim_ -= ViewTurnStep;
        if (ViewChevronAnim_ < ViewTurnTarget)
            ViewChevronAnim_ = ViewTurnTarget;
    }
    const float ViewTurn   = ViewChevronAnim_ * ViewChevronAnim_ * (3.0f - 2.0f * ViewChevronAnim_);
    const float ViewChevX  = X + ViewW - 15.0f;
    const float ViewChevY  = BtnY + 14.0f;
    const float ViewTipY   = ViewChevY - 1.5f + ViewTurn * 4.0f;
    const float ViewElbowY = ViewChevY + 2.5f - ViewTurn * 4.0f;
    Draw->AddLine(ImVec2(ViewChevX - 4.0f, ViewTipY), ImVec2(ViewChevX, ViewElbowY), kDim, 1.8f);
    Draw->AddLine(ImVec2(ViewChevX, ViewElbowY), ImVec2(ViewChevX + 4.0f, ViewTipY), kDim, 1.8f);

    const double ViewMenuNow = ImGui::GetTime();
    float ViewMenuFade = 1.0f;
    if (ViewMenuWasOpen_)
    {
        float T = static_cast<float>((ViewMenuNow - ViewMenuOpenedAt_) / 0.14);
        T            = T < 0.0f ? 0.0f : (T > 1.0f ? 1.0f : T);
        ViewMenuFade = T * T * (3.0f - 2.0f * T);
    }
    ImGui::SetNextWindowPos(ImVec2(X, BtnY + 33.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(190.0f, 0.0f), ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.0f, 0.0f, 0.0f, ViewMenuFade));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.180f, 0.180f, 0.180f, ViewMenuFade));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 20.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 2.0f));
    const bool ViewMenuOpen = ImGui::BeginPopup("##viewmenu");
    if (ViewMenuOpen && !ViewMenuWasOpen_)
    {
        ViewMenuOpenedAt_ = ViewMenuNow;
        ViewMenuFade      = 0.0f;
    }
    ViewMenuWasOpen_ = ViewMenuOpen;
    if (ViewMenuOpen)
    {
        ImDrawList* MenuDraw = ImGui::GetWindowDrawList();
        ImGui::PushFont(Small);
        const float MenuWidth = ImGui::GetContentRegionAvail().x;
        for (uint32_t r = 0u; r < 8u; ++r)
        {
            if (r == 2u)
            {
                ImGui::Dummy(ImVec2(MenuWidth, 7.0f));
                const ImVec2 SepMin = ImGui::GetItemRectMin();
                const ImVec2 SepMax = ImGui::GetItemRectMax();
                MenuDraw->AddLine(ImVec2(SepMin.x + 8.0f, (SepMin.y + SepMax.y) * 0.5f),
                    ImVec2(SepMax.x - 8.0f, (SepMin.y + SepMax.y) * 0.5f),
                    ControlPanel::FadeTint(kStroke, ViewMenuFade));
            }
            char RowLabel[24] = {};
            bool Ticked = false;
            if (r == 0u)
            {
                std::snprintf(RowLabel, sizeof(RowLabel), "Perspective");
                Ticked = !Orbit_.Ortho;
            }
            else if (r == 1u)
            {
                std::snprintf(RowLabel, sizeof(RowLabel), "Orthographic");
                Ticked = Orbit_.Ortho;
            }
            else
            {
                std::snprintf(RowLabel, sizeof(RowLabel), "%s", kSnapNames[r - 1u]);
                Ticked = Orbit_.ViewPoint == r - 1u;
            }
            ImGui::Dummy(ImVec2(MenuWidth, 28.0f));
            const ImVec2 RowMin = ImGui::GetItemRectMin();
            const ImVec2 RowMax = ImGui::GetItemRectMax();
            ImGui::SetCursorScreenPos(RowMin);
            char RowId[12] = {};
            std::snprintf(RowId, sizeof(RowId), "##v%ui", r);
            ImGui::InvisibleButton(RowId, ImVec2(MenuWidth, 28.0f));
            const bool Hovered = ImGui::IsItemHovered();
            if (Hovered && ImGui::IsMouseClicked(0))
            {
                if (r < 2u)
                {
                    Orbit_.Yaw      = Home_.Yaw;
                    Orbit_.Pitch    = Home_.Pitch;
                    Orbit_.Distance = Home_.Distance;
                    Orbit_.Target[0] = Home_.Target[0];
                    Orbit_.Target[1] = Home_.Target[1];
                    Orbit_.Target[2] = Home_.Target[2];
                    Orbit_.Ortho     = (r == 1u);
                    Orbit_.ViewPoint = 0u;
                }
                else
                {
                    Orbit_.Yaw       = kSnaps[r - 1u].Yaw;
                    Orbit_.Pitch     = kSnaps[r - 1u].Pitch;
                    Orbit_.ViewPoint = r - 1u;
                }
                ++Orbit_.Revision;
                ImGui::CloseCurrentPopup();
            }
            if (Ticked)
                MenuDraw->AddRectFilled(RowMin, RowMax, ControlPanel::FadeTint(kMenuSel, ViewMenuFade), 14.0f);
            else if (Hovered)
                MenuDraw->AddRectFilled(RowMin, RowMax, ControlPanel::FadeTint(kMenuHover, ViewMenuFade), 14.0f);
            if (Ticked)
                MenuDraw->AddCircleFilled(ImVec2(RowMin.x + 16.0f, (RowMin.y + RowMax.y) * 0.5f), 3.5f,
                    ControlPanel::FadeTint(kHi, ViewMenuFade));
            const ImVec2 OptGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, RowLabel);
            MenuDraw->AddText(ImVec2(RowMin.x + 30.0f, RowMin.y + (28.0f - OptGlyph.y) * 0.5f),
                ControlPanel::FadeTint(Ticked || Hovered ? kText : kDim, ViewMenuFade), RowLabel);
        }
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);

    // The transport strip: five runs in a seated pill, the realtime lamp, and the status chip. The
    //    reference's order — play, simulate, pause, step, stop — and its rules: step waits on pause, stop
    //    waits on a run, realtime rests while running.
    const bool Running = (Transport_ != kEdit);
    const char* ChipLabel = Paused_ ? "Paused" : (Transport_ == kPlay) ? "Play"
        : (Transport_ == kSimulate)   ? "Simulate"
        : (Realtime_ ? "Edit" : "Edit \xc2\xb7 static");

    struct RunButton
    {
        const char* Id;
        uint32_t    Icon;
        const char* Tip;
    };
    static constexpr RunButton kRuns[5] = {
        { "##tplay", 0u, "Play \xe2\x80\x94 run the world through a scene camera  (Alt P)" },
        { "##tsim", 1u, "Simulate \xe2\x80\x94 run the world, keep the editor camera  (Alt S)" },
        { "##tpause", 2u, "Pause / resume  (P)" },
        { "##tstep", 3u, "Advance one frame  (.)" },
        { "##tstop", 4u, "Stop \xe2\x80\x94 restore the editor  (Esc)" },
    };

    constexpr float kBtnW = 28.0f, kBtnH = 26.0f, kBtnGap = 2.0f, kStripH = 30.0f, kStripPad = 4.0f;
    const float ChipW = SpacedCapsWidth(Small, ChipLabel, 1.3f) + 20.0f;
    const float RtW   = SpacedCapsWidth(Small, "Realtime", 1.1f) + 32.0f;
    const float Inner = 5.0f * kBtnW + 4.0f * kBtnGap + kBtnGap + 11.0f + kBtnGap + RtW + kBtnGap + ChipW
        + kBtnGap + kBtnW;
    const float StripW = Inner + 2.0f * kStripPad;
    const float StripX = Cursor.x + RowWidth - StripW;
    const float StripY = Cursor.y + 7.0f;
    Draw->AddRectFilled(ImVec2(StripX, StripY), ImVec2(StripX + StripW, StripY + kStripH), kInset, 15.0f);
    Draw->AddRect(ImVec2(StripX, StripY), ImVec2(StripX + StripW, StripY + kStripH), kStroke, 15.0f);

    float BX = StripX + kStripPad;
    const float TBtnY = StripY + 2.0f;
    for (uint32_t i = 0u; i < 5u; ++i)
    {
        ImGui::SetCursorScreenPos(ImVec2(BX, TBtnY));
        ImGui::InvisibleButton(kRuns[i].Id, ImVec2(kBtnW, kBtnH));
        const bool Hot = ImGui::IsItemHovered();

        bool Disabled = false;
        bool On       = false;
        if (i == 0u)      { On = (Transport_ == kPlay); }
        else if (i == 1u) { On = (Transport_ == kSimulate); }
        else if (i == 2u) { On = Paused_; }
        else if (i == 3u) { Disabled = !Paused_; }
        else              { Disabled = !Running; }

        if (Hot && !Disabled)
        {
            ImGui::SetTooltip("%s", kRuns[i].Tip);
            if (ImGui::IsMouseClicked(0))
            {
                if (i == 0u)      { SetTransport(Transport_ == kPlay ? kEdit : kPlay); }
                else if (i == 1u) { SetTransport(Transport_ == kSimulate ? kEdit : kSimulate); }
                else if (i == 2u) { SetPaused(!Paused_); }
                else if (i == 3u) { StepOnce(); }
                else              { SetTransport(kEdit); }
            }
        }

        ImU32 Bg = IM_COL32(0, 0, 0, 0);
        ImU32 GlyphTint = kDim;
        if (Disabled)
        {
            GlyphTint = IM_COL32(136, 136, 136, 77);
        }
        else if (On)
        {
            if (i == 0u)      { Bg = kOk; GlyphTint = IM_COL32(4, 20, 10, 255); }
            else if (i == 1u) { Bg = kHi; GlyphTint = IM_COL32(255, 255, 255, 255); }
            else              { Bg = kAmber; GlyphTint = IM_COL32(26, 18, 4, 255); }
        }
        else if (Hot)
        {
            if (i == 0u)      { Bg = IM_COL32(34, 197, 94, 41); GlyphTint = IM_COL32(126, 231, 165, 255); }
            else if (i == 1u) { Bg = IM_COL32(108, 119, 255, 46); GlyphTint = IM_COL32(174, 180, 255, 255); }
            else if (i == 2u) { Bg = IM_COL32(245, 158, 11, 41); GlyphTint = IM_COL32(246, 198, 106, 255); }
            else if (i == 4u) { Bg = IM_COL32(239, 68, 68, 41); GlyphTint = IM_COL32(255, 155, 155, 255); }
            else              { Bg = kHover; GlyphTint = kText; }
        }
        if (Bg != IM_COL32(0, 0, 0, 0))
        {
            Draw->AddRectFilled(ImVec2(BX, TBtnY), ImVec2(BX + kBtnW, TBtnY + kBtnH), Bg, 13.0f);
        }
        uint32_t Icon = kRuns[i].Icon;
        if (i == 2u && Paused_)
        {
            Icon = 0u;   // paused, the button offers the way back: the reference swaps in play
        }
        RunGlyph(Icon, Draw, ImVec2(BX + kBtnW * 0.5f, TBtnY + kBtnH * 0.5f), 13.0f, GlyphTint);
        BX += kBtnW + kBtnGap;
    }

    BX += kBtnGap;
    Draw->AddLine(ImVec2(BX + 5.0f, StripY + 7.0f), ImVec2(BX + 5.0f, StripY + 23.0f), kStroke);
    BX += 11.0f + kBtnGap;

    ImGui::SetCursorScreenPos(ImVec2(BX, TBtnY));
    ImGui::InvisibleButton("##trealtime", ImVec2(RtW, kBtnH));
    const bool RtHot = ImGui::IsItemHovered();
    const bool RtOff = Running;
    if (RtHot && !RtOff)
    {
        ImGui::SetTooltip("Realtime viewport \xe2\x80\x94 animate and redraw continuously  (Ctrl R)");
        if (ImGui::IsMouseClicked(0))
        {
            SetRealtime(!Realtime_);
        }
    }
    if (Realtime_)
    {
        Draw->AddRectFilled(ImVec2(BX, TBtnY), ImVec2(BX + RtW, TBtnY + kBtnH),
            IM_COL32(255, 255, 255, 31), 13.0f);
    }
    else if (RtHot && !RtOff)
    {
        Draw->AddRectFilled(ImVec2(BX, TBtnY), ImVec2(BX + RtW, TBtnY + kBtnH), kHover, 13.0f);
    }
    const ImVec2 Led(BX + 13.0f, TBtnY + 13.0f);
    if (Realtime_)
    {
        Draw->AddCircleFilled(Led, 6.0f, IM_COL32(34, 197, 94, 80));
        Draw->AddCircleFilled(Led, 3.0f, kOk);
    }
    else
    {
        Draw->AddCircleFilled(Led, 3.0f, IM_COL32(58, 58, 58, 255));
    }
    ImGui::PushFont(Small);
    const ImVec2 RtGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Realtime");
    ImGui::PopFont();
    const bool RtDim = RtOff && !Realtime_;
    SpacedCaps(Draw, Small, "Realtime", ImVec2(BX + 22.0f, TBtnY + (kBtnH - RtGlyph.y) * 0.5f),
        RtDim ? IM_COL32(136, 136, 136, 77) : (Realtime_ ? kText : kDim), 1.1f);
    BX += RtW + kBtnGap;

    const float ChipY = StripY + 4.0f;
    ImU32 ChipBg = IM_COL32(0, 0, 0, 0);
    ImU32 ChipTint = kFaint;
    if (Paused_)                       { ChipBg = IM_COL32(245, 158, 11, 51); ChipTint = IM_COL32(246, 198, 106, 255); }
    else if (Transport_ == kPlay)      { ChipBg = IM_COL32(34, 197, 94, 51); ChipTint = IM_COL32(126, 231, 165, 255); }
    else if (Transport_ == kSimulate)  { ChipBg = IM_COL32(108, 119, 255, 51); ChipTint = IM_COL32(174, 180, 255, 255); }
    if (ChipBg != IM_COL32(0, 0, 0, 0))
    {
        Draw->AddRectFilled(ImVec2(BX, ChipY), ImVec2(BX + ChipW, ChipY + 22.0f), ChipBg, 11.0f);
    }
    SpacedCaps(Draw, Small, ChipLabel, ImVec2(BX + 10.0f, ChipY + (22.0f - RtGlyph.y) * 0.5f), ChipTint, 1.3f);

    // The gear: slides the Control Centre shade open and shut through the shared open figure.
    const float GX = BX + ChipW + kBtnGap;
    ImGui::SetCursorScreenPos(ImVec2(GX, TBtnY));
    ImGui::InvisibleButton("##tsettings", ImVec2(kBtnW, kBtnH));
    const bool GearHot = ImGui::IsItemHovered();
    if (GearHot)
    {
        ImGui::SetTooltip("Viewport settings");
        if (ImGui::IsMouseClicked(0) && ShadeOpen_ != nullptr)
        {
            *ShadeOpen_ = !*ShadeOpen_;
        }
    }
    const bool GearOn = (ShadeOpen_ != nullptr && *ShadeOpen_);
    if (GearHot || GearOn)
    {
        Draw->AddCircleFilled(ImVec2(GX + kBtnW * 0.5f, TBtnY + kBtnH * 0.5f), 12.0f,
            GearHot ? kHover : IM_COL32(255, 255, 255, 16));
    }
    const ImVec2 GearC(GX + kBtnW * 0.5f, TBtnY + kBtnH * 0.5f);
    const ImU32  GearTint = (GearHot || GearOn) ? kText : kDim;
    Draw->AddCircle(GearC, 5.0f, GearTint, 24, 1.4f);
    for (uint32_t Tooth = 0u; Tooth < 8u; ++Tooth)
    {
        const float A = static_cast<float>(Tooth) * 0.7853982f;
        const ImVec2 Tip(GearC.x + 8.2f * std::cos(A), GearC.y + 8.2f * std::sin(A));
        const ImVec2 Root(GearC.x + 5.6f * std::cos(A), GearC.y + 5.6f * std::sin(A));
        Draw->AddLine(Root, Tip, GearTint, 1.6f);
    }
    Draw->AddCircleFilled(GearC, 1.6f, GearTint);

    const ImVec2 WinPos = ImGui::GetWindowPos();
    const ImVec2 WinSize = ImGui::GetWindowSize();
    Draw->AddLine(ImVec2(WinPos.x, Cursor.y + 44.0f), ImVec2(WinPos.x + WinSize.x, Cursor.y + 44.0f), kStroke);
    ImGui::SetCursorScreenPos(ImVec2(Cursor.x, Cursor.y + 44.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          THE VIEW
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::RecordView() noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    const float ViewH    = ImGui::GetContentRegionAvail().y - 40.0f - 40.0f;
    if (ViewH < 40.0f)
    {
        return;
    }

    ImGui::Dummy(ImVec2(RowWidth, ViewH));
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Ui    = Controls_->QueryUi();
    ImFont*     Small = Controls_->QuerySmall();
    Draw->AddRectFilled(Min, Max, kView, 12.0f);
    if (ViewTexture_ != static_cast<ImTextureID>(0))
        Draw->AddImage(ViewTexture_, Min, Max);
    Draw->AddRect(Min, Max, kStroke, 12.0f);

    if (ViewTexture_ == static_cast<ImTextureID>(0))
    {
        ImGui::PushFont(Ui);
        const ImVec2 HintGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, "The Cornell Box renders here");
        Draw->AddText(ImVec2(Min.x + (RowWidth - HintGlyph.x) * 0.5f, Min.y + ViewH * 0.5f - 22.0f),
            kDim, "The Cornell Box renders here");
        ImGui::PopFont();
        ImGui::PushFont(Small);
        const ImVec2 SubGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "in the engine build");
        Draw->AddText(ImVec2(Min.x + (RowWidth - SubGlyph.x) * 0.5f, Min.y + ViewH * 0.5f + 2.0f),
            kFaint, "in the engine build");
        ImGui::PopFont();
    }

    // The orbit gizmo, Blender's compass: the three axes through the orbit's basis, pads on all six
    //    ends, letters on the positive three. A pad tap snaps its view, a drag orbits, and the wheel
    //    dollies over the view. Front pads read bright, back pads dim, and the hot pad rings.
    float Gf[3], Gr[3], Gu[3];
    OrbitBasis(Orbit_.Yaw, Orbit_.Pitch, Gf, Gr, Gu);
    const ImVec2 OrbC(Max.x - 52.0f, Max.y - 52.0f);
    constexpr float kArm = 20.0f;
    struct PadDot { float X; float Y; bool Front; };
    PadDot Pads[6];
    constexpr float kAxes[6][3] = { { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
                                    { 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
                                    { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } };
    for (uint32_t i = 0u; i < 6u; ++i)
    {
        const float Dx = kAxes[i][0] * Gr[0] + kAxes[i][1] * Gr[1] + kAxes[i][2] * Gr[2];
        const float Dy = kAxes[i][0] * Gu[0] + kAxes[i][1] * Gu[1] + kAxes[i][2] * Gu[2];
        const float Toward = -(kAxes[i][0] * Gf[0] + kAxes[i][1] * Gf[1] + kAxes[i][2] * Gf[2]);
        Pads[i].X     = OrbC.x + Dx * kArm;
        Pads[i].Y     = OrbC.y - Dy * kArm;
        Pads[i].Front = Toward > 0.0f;
    }
    ImGui::SetCursorScreenPos(ImVec2(OrbC.x - 48.0f, OrbC.y - 48.0f));
    ImGui::InvisibleButton("##orb", ImVec2(96.0f, 96.0f));
    const bool OrbHover = ImGui::IsItemHovered();
    OrbHot_ = 0u;
    if (OrbHover || OrbHeld_)
    {
        const ImVec2 Mouse = ImGui::GetIO().MousePos;
        float Best = 14.0f * 14.0f;
        for (uint32_t i = 0u; i < 6u; ++i)
        {
            const float Hx = Mouse.x - Pads[i].X, Hy = Mouse.y - Pads[i].Y;
            const float D2 = Hx * Hx + Hy * Hy;
            if (D2 < Best) { Best = D2; OrbHot_ = i + 1u; }
        }
    }
    if (OrbHover && ImGui::IsMouseClicked(0))
    {
        OrbHeld_  = true;
        OrbMoved_ = false;
        OrbDownX_ = ImGui::GetIO().MousePos.x;
        OrbDownY_ = ImGui::GetIO().MousePos.y;
    }
    if (OrbHeld_)
    {
        if (!ImGui::IsMouseDown(0))
        {
            if (!OrbMoved_ && OrbHot_ > 0u)
            {
                const uint32_t V = kPadViews[OrbHot_ - 1u];
                Orbit_.Yaw       = kSnaps[V].Yaw;
                Orbit_.Pitch     = kSnaps[V].Pitch;
                Orbit_.ViewPoint = V;
                ++Orbit_.Revision;
            }
            OrbHeld_ = false;
        }
        else
        {
            const ImVec2 Mouse = ImGui::GetIO().MousePos;
            if (!OrbMoved_ && (std::fabs(Mouse.x - OrbDownX_) + std::fabs(Mouse.y - OrbDownY_)) > 4.0f)
                OrbMoved_ = true;
            if (OrbMoved_)
            {
                const ImVec2 Delta = ImGui::GetIO().MouseDelta;
                Orbit_.Yaw   -= Delta.x * 0.008f;
                Orbit_.Pitch += Delta.y * 0.008f;
                if (Orbit_.Pitch > 1.55f)  Orbit_.Pitch = 1.55f;
                if (Orbit_.Pitch < -1.55f) Orbit_.Pitch = -1.55f;
                while (Orbit_.Yaw > kOrbitPi)  Orbit_.Yaw -= 2.0f * kOrbitPi;
                while (Orbit_.Yaw < -kOrbitPi) Orbit_.Yaw += 2.0f * kOrbitPi;
                Orbit_.ViewPoint = 0u;
                ++Orbit_.Revision;
            }
        }
    }
    constexpr ImU32 kAxisTint[3] = { IM_COL32(239, 83, 80, 255),
                                     IM_COL32(105, 208, 109, 255),
                                     IM_COL32(91, 140, 255, 255) };
    for (uint32_t a = 0u; a < 3u; ++a)
        Draw->AddLine(ImVec2(Pads[2u * a].X, Pads[2u * a].Y),
            ImVec2(Pads[2u * a + 1u].X, Pads[2u * a + 1u].Y),
            ControlPanel::FadeTint(kAxisTint[a], 0.55f), 2.0f);
    for (uint32_t i = 0u; i < 6u; ++i)
    {
        const ImU32 Tint = ControlPanel::FadeTint(kAxisTint[i / 2u], Pads[i].Front ? 1.0f : 0.35f);
        Draw->AddCircleFilled(ImVec2(Pads[i].X, Pads[i].Y), 7.0f, Tint);
        if (OrbHot_ == i + 1u)
            Draw->AddCircle(ImVec2(Pads[i].X, Pads[i].Y), 10.0f, IM_COL32(255, 255, 255, 200), 0, 1.6f);
    }
    const char* kAxisNames[3] = { "X", "Y", "Z" };
    ImGui::PushFont(Small);
    for (uint32_t a = 0u; a < 3u; ++a)
    {
        const ImU32 Tint = ControlPanel::FadeTint(kAxisTint[a], Pads[2u * a].Front ? 1.0f : 0.4f);
        Draw->AddText(ImVec2(Pads[2u * a].X + 9.0f, Pads[2u * a].Y - 7.0f), Tint, kAxisNames[a]);
    }
    ImGui::PopFont();
    // The wheel dollies over the view: crowd the target or back off, the compass snap staying put.
    if (ImGui::IsMouseHoveringRect(Min, Max) && ImGui::GetIO().MouseWheel != 0.0f)
    {
        Orbit_.Distance *= ImGui::GetIO().MouseWheel > 0.0f ? 0.9f : 1.1f;
        if (Orbit_.Distance < 0.5f)  Orbit_.Distance = 0.5f;
        if (Orbit_.Distance > 60.0f) Orbit_.Distance = 60.0f;
        ++Orbit_.Revision;
    }
    LastW_ = Max.x - Min.x;
    LastH_ = Max.y - Min.y;
    ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       COMMAND LINE
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::RecordCommand(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    if ((ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper) && ImGui::IsKeyPressed(ImGuiKey_K, false))
    {
        FocusCommand_ = true;
        SugShut_      = false;
    }

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 40.0f));
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Ui    = Controls_->QueryUi();
    ImFont*     Small = Controls_->QuerySmall();
    const ImVec2 WinPos  = ImGui::GetWindowPos();
    const ImVec2 WinSize = ImGui::GetWindowSize();
    const float  WinX    = WinPos.x;
    const float  WinX1   = WinPos.x + WinSize.x;
    Draw->AddRectFilled(ImVec2(WinX, Min.y), ImVec2(WinX1, Max.y), kWash);
    Draw->AddLine(ImVec2(WinX, Min.y), ImVec2(WinX1, Min.y), kStroke);

    if (std::strcmp(CommandText_, LastPaint_) != 0)
    {
        SugShut_ = false;   // moved text reopens the stack a run had shut
        std::snprintf(LastPaint_, sizeof(LastPaint_), "%s", CommandText_);
    }

    const double Now    = ImGui::GetTime();
    const bool   EchoOn = (CommandEcho_[0] != '\0') && (Now < EchoUntil_);

    // The right cluster first, so the field takes the middle: run, kbd, echo.
    const ImVec2 RunMin(WinX1 - 8.0f - 28.0f, Min.y + 6.0f);
    ImFont* MonoSmall = Controls_->QueryMonoSmall();
    ImGui::PushFont(MonoSmall);
    const ImVec2 CmdGlyph = MonoSmall->CalcTextSizeA(MonoSmall->LegacySize, FLT_MAX, 0.0f, "\xe2\x8c\x98");
    ImGui::PopFont();
    ImGui::PushFont(Small);
    const ImVec2 KbdGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "K");
    ImGui::PopFont();
    const ImVec2 KbdSize(CmdGlyph.x + KbdGlyph.x + 12.0f, KbdGlyph.y + 4.0f);
    const ImVec2 KbdMin(RunMin.x - 10.0f - KbdSize.x, Min.y + (40.0f - KbdSize.y) * 0.5f);
    float EchoW = 0.0f;
    if (EchoOn)
    {
        ImGui::PushFont(Small);
        EchoW = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, CommandEcho_).x;
        ImGui::PopFont();
        if (EchoW > RowWidth * 0.42f)
        {
            EchoW = RowWidth * 0.42f;
        }
    }
    const float FieldX0 = WinX + 10.0f + 20.0f + 10.0f;
    const float FieldX1 = (EchoOn ? (KbdMin.x - 10.0f - EchoW) : KbdMin.x) - 10.0f;

    const bool BadShown = (SugCount_ > 0u && SugRows_[0].Sort == 4u);
    const ImU32 PromptTint = BadShown ? kDanger : (CommandFocus_ ? kHi : kFaint);
    CommandGlyph(Draw, ImVec2(WinX + 20.0f, Min.y + 20.0f), 14.0f, PromptTint);

    ImGui::SetCursorScreenPos(ImVec2(FieldX0, Min.y + 9.0f));
    ImGui::PushItemWidth(FieldX1 - FieldX0);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 4.0f));
    ImGui::PushFont(Ui);
    if (FocusCommand_)
    {
        ImGui::SetKeyboardFocusHere();
        FocusCommand_ = false;
    }
    static constexpr char kPlaceholder[] = "Say what you want \xe2\x80\x94 \xe2\x80\x9crotate cube 40 degrees on Z\xe2\x80\x9d, " "\xe2\x80\x9c" "add sphere at x 3 y 2 z -1\xe2\x80\x9d";
    const bool Done = ImGui::InputTextWithHint("##cmd", kPlaceholder, CommandText_, sizeof(CommandText_),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion
            | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackAlways,
        &ViewportPanel::ConsoleCallback, this);
    const bool Focused = ImGui::IsItemFocused();
    if ((CommandFocus_ || Focused) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        // Clear and shut; the caret rests where it was, and the stack stays shut till the text moves.
        CommandText_[0] = '\0';
        Ghost_[0]       = '\0';
        SugShut_        = true;
        SugUntil_       = 0.0;
    }
    if (CommandFocus_ && !Focused)
    {
        SugUntil_ = Now + 0.12;
    }
    CommandFocus_ = Focused;
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::PopItemWidth();

    if (Done)
    {
        // Enter runs the standing row, even on an empty line — the reference's own quirk, kept.
        RunSugRow(SugIndex_, Instances, InstanceCount);
    }

    if (Focused && Ghost_[0] != '\0')
    {
        ImGui::PushFont(Ui);
        const ImVec2 TypedW = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, CommandText_);
        Draw->AddText(ImVec2(FieldX0 + 2.0f + TypedW.x, Min.y + 9.0f + 4.0f),
            IM_COL32(92, 92, 92, 166), Ghost_);
        ImGui::PopFont();
    }

    if (EchoOn)
    {
        ImGui::PushFont(Small);
        const ImVec2 EchoGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, CommandEcho_);
        Draw->PushClipRect(ImVec2(FieldX1 + 10.0f, Min.y), ImVec2(KbdMin.x - 10.0f, Max.y), true);
        Draw->AddText(ImVec2(FieldX1 + 10.0f, Min.y + (40.0f - EchoGlyph.y) * 0.5f), kDim, CommandEcho_);
        Draw->PopClipRect();
        ImGui::PopFont();
    }

    ImGui::SetCursorScreenPos(KbdMin);
    ImGui::InvisibleButton("##kcmd", KbdSize);
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
    {
        FocusCommand_ = true;
        SugShut_      = false;
    }
    Draw->AddRectFilled(KbdMin, ImVec2(KbdMin.x + KbdSize.x, KbdMin.y + KbdSize.y), kField, 6.0f);
    Draw->AddRect(KbdMin, ImVec2(KbdMin.x + KbdSize.x, KbdMin.y + KbdSize.y), kStroke, 6.0f);
    ImGui::PushFont(Small);
    Draw->AddText(MonoSmall, MonoSmall->LegacySize, ImVec2(KbdMin.x + 6.0f, KbdMin.y + 2.0f), kFaint,
        "\xe2\x8c\x98");
    Draw->AddText(ImVec2(KbdMin.x + 6.0f + CmdGlyph.x, KbdMin.y + 2.0f), kFaint, "K");
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(RunMin);
    ImGui::InvisibleButton("##cmdrun", ImVec2(28.0f, 28.0f));
    const bool RunHot = ImGui::IsItemHovered();
    if (RunHot)
    {
        ImGui::SetTooltip("Run  (Enter)");
        if (ImGui::IsMouseClicked(0))
        {
            RunSugRow(SugIndex_, Instances, InstanceCount);
        }
    }
    const ImVec2 RunCentre(RunMin.x + 14.0f, RunMin.y + 14.0f);
    if (RunHot)
    {
        Draw->AddCircleFilled(RunCentre, 14.0f, kHover);
    }
    PlayGlyph(Draw, RunCentre, 13.0f, RunHot ? kText : kDim);

    const bool SugOpen = (Focused && !SugShut_) || (Now < SugUntil_);
    if (SugOpen)
    {
        PaintSuggestions(Instances, InstanceCount);

        // The stack rises out of the console with eight pixels of air on three sides: the head caps,
        //    then rows with their glyph, title, and right-hung sub.
        constexpr float kRowH = 36.0f, kHeadH = 24.0f, kPad = 6.0f;
        const float StackH  = kPad + kHeadH + static_cast<float>(SugCount_) * kRowH + kPad;
        const float StackX0 = WinX + 8.0f;
        const float StackX1 = WinX1 - 8.0f;
        const float StackY1 = Min.y - 6.0f;
        const float StackY0 = StackY1 - StackH;
        Draw->AddRectFilled(ImVec2(StackX0, StackY0), ImVec2(StackX1, StackY1),
            IM_COL32(12, 12, 12, 247), 18.0f);
        Draw->AddRect(ImVec2(StackX0, StackY0), ImVec2(StackX1, StackY1), kStrong, 18.0f);

        const char* Head = (SugCount_ == 0u) ? "Nothing matches \xe2\x80\x94 try \xe2\x80\x9chelp\xe2\x80\x9d"
            : (CommandText_[0] != '\0' ? "What this will do" : "Say something like");
        SpacedCaps(Draw, Small, Head, ImVec2(StackX0 + 16.0f, StackY0 + 13.0f), kFaint, 1.3f);

        for (uint32_t r = 0u; r < SugCount_; ++r)
        {
            const float RowY0 = StackY0 + kPad + kHeadH + static_cast<float>(r) * kRowH;
            const ImVec2 RowMin(StackX0 + kPad, RowY0);
            const ImVec2 RowMax(StackX1 - kPad, RowY0 + kRowH);
            ImGui::SetCursorScreenPos(RowMin);
            char SugId[10] = {};
            std::snprintf(SugId, sizeof(SugId), "##sug%u", r);
            ImGui::InvisibleButton(SugId, ImVec2(RowMax.x - RowMin.x, kRowH));
            const bool RowHot = ImGui::IsItemHovered();

            const uint32_t Sort = SugRows_[r].Sort;
            const uint32_t At   = SugRows_[r].At;
            const bool Bad      = (Sort == 4u);
            const bool On       = (r == SugIndex_);
            // The standing row carries the indigo till the English reader lands; the keys deepen it, and
            //    the pointer leaves it alone. Grey rows keep the old rule: hover beats picked.
            const bool Primary = (r == 0u && CommandText_[0] != '\0');
            if (Primary)
            {
                Draw->AddRectFilled(RowMin, RowMax, Bad ? kBadBg : (On ? kPrimaryOn : kPrimaryBg), 13.0f);
                Draw->AddRect(RowMin, RowMax, Bad ? kBadEdge : kPrimaryEdge, 13.0f);
            }
            else if (RowHot)
            {
                Draw->AddRectFilled(RowMin, RowMax, IM_COL32(27, 27, 27, 255), 13.0f);
            }
            else if (On)
            {
                Draw->AddRectFilled(RowMin, RowMax, kSeated, 13.0f);
            }

            const char* EntryLabel = (Sort == 3u && At < InstanceCount) ? Instances[At].Label : "";
            char EntrySub[64] = {};
            const char* Title = "";
            const char* Sub   = "";
            if (Sort == 0u)
            {
                Title = QuickLabels_[At];
                Sub   = kQuick[At].Sub;
            }
            else if (Sort == 1u)
            {
                Title = kExamples[At];
                Sub   = "try this";
            }
            else if (Sort == 2u)
            {
                Title = kVerbs[At].Usage;
                Sub   = kVerbs[At].Help[0] != '\0' ? kVerbs[At].Help : "command";
            }
            else if (Sort == 3u)
            {
                Title = EntryLabel;
                std::snprintf(EntrySub, sizeof(EntrySub), "%s \xc2\xb7 find and frame",
                    At < InstanceCount ? EditorInstanceLabel(Instances[At].Category) : "");
                Sub = EntrySub;
            }
            else
            {
                Title = "I did not understand that";
                Sub   = "not understood";
            }

            const ImVec2 IconCentre(RowMin.x + 19.0f, RowY0 + kRowH * 0.5f);
            if (Sort == 3u && At < InstanceCount)
            {
                const float* Tint = Instances[At].Tint;
                Draw->AddCircleFilled(IconCentre, 5.0f,
                    IM_COL32(static_cast<int>(Tint[0] * 255.0f), static_cast<int>(Tint[1] * 255.0f),
                        static_cast<int>(Tint[2] * 255.0f), 255));
            }
            else if (Bad)
            {
                CloseGlyph(Draw, IconCentre, 14.0f, kDim);
            }
            else
            {
                CommandGlyph(Draw, IconCentre, 14.0f, kDim);
            }

            ImGui::PushFont(Ui);
            const ImVec2 TitleGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, Title);
            Draw->AddText(ImVec2(RowMin.x + 37.0f, RowY0 + (kRowH - TitleGlyph.y) * 0.5f),
                Bad ? kBadTitle : kText, Title);
            ImGui::PopFont();
            ImGui::PushFont(Small);
            const ImVec2 SubGlyph = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Sub);
            ImGui::PopFont();
            const float SubW = SpacedCapsWidth(Small, Sub, 0.7f);
            SpacedCaps(Draw, Small, Sub,
                ImVec2(RowMax.x - 12.0f - SubW, RowY0 + (kRowH - SubGlyph.y) * 0.5f), kFaint, 0.7f);

            if (RowHot)
            {
                SugIndex_ = r;
                if (ImGui::IsMouseClicked(0))
                {
                    RunSugRow(r, Instances, InstanceCount);
                    break;   // the run repaints the rows; the rest redraw next tick, shut
                }
            }
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y));
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      SUGGESTIONS
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::PaintSuggestions(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    if (std::strcmp(CommandText_, "help") == 0)
    {
        // The reference's help clears the line and shows the whole table.
        CommandText_[0] = '\0';
    }
    for (uint32_t i = 0u; i < 6u; ++i)
    {
        if (i == 5u)
        {
            std::snprintf(QuickLabels_[i], sizeof(QuickLabels_[i]), "Realtime viewport: turn %s",
                Realtime_ ? "off" : "on");
        }
        else
        {
            std::snprintf(QuickLabels_[i], sizeof(QuickLabels_[i]), "%s", kQuick[i].Label);
        }
    }

    if (std::strcmp(CommandText_, LastSugText_) != 0)
    {
        // Fresh text re-seats the standing row, the way a repaint does.
        SugIndex_ = 0u;
        std::snprintf(LastSugText_, sizeof(LastSugText_), "%s", CommandText_);
    }

    SugCount_ = 0u;
    if (CommandText_[0] == '\0')
    {
        for (uint32_t i = 0u; i < 9u; ++i)
        {
            SugRows_[SugCount_].Sort = 1u;
            SugRows_[SugCount_].At   = static_cast<uint8_t>(i);
            ++SugCount_;
        }
    }
    else
    {
        char First[128] = {};
        size_t Span = 0u;
        while (CommandText_[Span] != '\0' && CommandText_[Span] != ' ' && Span + 1u < sizeof(First))
        {
            First[Span] = CommandText_[Span];
            ++Span;
        }
        // Verb words first, then entries, then the quick rows: five entries and five quick at most,
        //    nine rows in all. The tables share no titles, so no doubles survive the merge.
        for (uint32_t v = 0u; v < 15u && SugCount_ < 9u; ++v)
        {
            if (KeysHit(kVerbs[v].Keys, CommandText_, First))
            {
                SugRows_[SugCount_].Sort = 2u;
                SugRows_[SugCount_].At   = static_cast<uint8_t>(v);
                ++SugCount_;
            }
        }
        uint32_t Entries = 0u;
        for (uint32_t i = 0u; i < InstanceCount && SugCount_ < 9u && Entries < 5u; ++i)
        {
            if (InfixMatch(Instances[i].Label, CommandText_))
            {
                SugRows_[SugCount_].Sort = 3u;
                SugRows_[SugCount_].At   = static_cast<uint8_t>(i);
                ++SugCount_;
                ++Entries;
            }
        }
        uint32_t Quicks = 0u;
        for (uint32_t i = 0u; i < 6u && SugCount_ < 9u && Quicks < 5u; ++i)
        {
            if (InfixMatch(QuickLabels_[i], CommandText_))
            {
                SugRows_[SugCount_].Sort = 0u;
                SugRows_[SugCount_].At   = static_cast<uint8_t>(i);
                ++SugCount_;
                ++Quicks;
            }
        }
        if (SugCount_ == 0u && !GrowingWord(CommandText_))
        {
            // Nothing answers and no verb still grows: the red row says so.
            SugRows_[0].Sort = 4u;
            SugRows_[0].At   = 0u;
            SugCount_        = 1u;
        }
    }
    if (SugCount_ > 0u && SugIndex_ >= SugCount_)
    {
        SugIndex_ = 0u;
    }

    Ghost_[0] = '\0';
    if (CommandText_[0] != '\0' && SugCount_ > 0u)
    {
        // The ghost completes only what a row offers to pour: examples, usages, found entries.
        const size_t Eaten = std::strlen(CommandText_);
        char Offer[128] = {};
        for (uint32_t r = 0u; r < SugCount_; ++r)
        {
            const uint32_t Sort = SugRows_[r].Sort;
            const uint32_t At   = SugRows_[r].At;
            const char* EntryLabel = (Sort == 3u && At < InstanceCount) ? Instances[At].Label : "";
            SugInsertText(Sort, At, EntryLabel, Offer, sizeof(Offer));
            if (Offer[0] != '\0' && StartsFolded(Offer, CommandText_) && std::strlen(Offer) > Eaten)
            {
                std::snprintf(Ghost_, sizeof(Ghost_), "%s", Offer + Eaten);
                break;
            }
        }
    }
}

void ViewportPanel::RunSugRow(uint32_t Row, EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    if (Row >= SugCount_)
    {
        return;
    }
    const uint32_t Sort = SugRows_[Row].Sort;
    const uint32_t At   = SugRows_[Row].At;
    if (Sort == 4u)
    {
        // The red row never runs; it says the line went unheard and keeps it.
        std::snprintf(CommandEcho_, sizeof(CommandEcho_), "I did not understand that");
        EchoUntil_ = ImGui::GetTime() + 3.2;
        return;
    }
    if (Sort == 1u || Sort == 2u || Sort == 3u)
    {
        // An offer, not an action: the line drinks it and the caret parks past its tail.
        const char* EntryLabel = (Sort == 3u && At < InstanceCount) ? Instances[At].Label : "";
        char Offer[128] = {};
        SugInsertText(Sort, At, EntryLabel, Offer, sizeof(Offer));
        std::snprintf(CommandText_, sizeof(CommandText_), "%s", Offer);
        CaretToEnd_ = true;
        SugShut_    = false;
        PaintSuggestions(Instances, InstanceCount);
        return;
    }

    const uint32_t Cmd = At;
    if (Cmd == 0u)      { SetTransport(kPlay); }
    else if (Cmd == 1u) { SetTransport(kSimulate); }
    else if (Cmd == 2u) { SetPaused(!Paused_); }
    else if (Cmd == 3u) { StepOnce(); }
    else if (Cmd == 4u) { SetTransport(kEdit); }
    else if (Cmd == 5u) { SetRealtime(!Realtime_); }

    if (CommandText_[0] != '\0'
        && (PastCount_ == 0u || std::strcmp(CommandText_, CommandPast_[PastCount_ - 1u]) != 0))
    {
        if (PastCount_ == 8u)
        {
            for (uint32_t i = 0u; i < 7u; ++i)
            {
                std::snprintf(CommandPast_[i], sizeof(CommandPast_[i]), "%s", CommandPast_[i + 1u]);
            }
            --PastCount_;
        }
        std::snprintf(CommandPast_[PastCount_], sizeof(CommandPast_[PastCount_]), "%s", CommandText_);
        ++PastCount_;
    }
    PastAt_ = -1;
    std::snprintf(CommandEcho_, sizeof(CommandEcho_), "%s", QuickLabels_[Cmd]);
    EchoUntil_ = ImGui::GetTime() + 3.2;
    CommandText_[0] = '\0';
    Ghost_[0]       = '\0';
    SugShut_        = true;
    PaintSuggestions(Instances, InstanceCount);
}

int ViewportPanel::ConsoleCallback(ImGuiInputTextCallbackData* Edit) noexcept
{
    auto* Self = static_cast<ViewportPanel*>(Edit->UserData);
    if (Edit->EventFlag == ImGuiInputTextFlags_CallbackAlways)
    {
        if (Self->CaretToEnd_)
        {
            Edit->CursorPos      = Edit->BufTextLen;
            Edit->SelectionStart = Edit->CursorPos;
            Edit->SelectionEnd   = Edit->CursorPos;
            Self->CaretToEnd_    = false;
        }
        return 0;
    }
    if (Edit->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
    {
        if (Self->Ghost_[0] != '\0')
        {
            Edit->InsertChars(Edit->BufTextLen, Self->Ghost_);
        }
    }
    else if (Edit->EventFlag == ImGuiInputTextFlags_CallbackHistory)
    {
        if (Self->CommandText_[0] == '\0' && Self->PastCount_ > 0u)
        {
            // An empty line walks the earlier lines, newest first.
            if (Edit->EventKey == ImGuiKey_UpArrow)
            {
                if (Self->PastAt_ + 1 < static_cast<int32_t>(Self->PastCount_))
                {
                    ++Self->PastAt_;
                }
            }
            else if (Self->PastAt_ > -1)
            {
                --Self->PastAt_;
            }
            const char* Line = (Self->PastAt_ < 0)
                ? ""
                : Self->CommandPast_[Self->PastCount_ - 1u - static_cast<uint32_t>(Self->PastAt_)];
            Edit->DeleteChars(0, Edit->BufTextLen);
            Edit->InsertChars(0, Line);
        }
        else
        {
            if (Edit->EventKey == ImGuiKey_UpArrow)
            {
                if (Self->SugIndex_ > 0u)
                {
                    --Self->SugIndex_;
                }
            }
            else if (Self->SugIndex_ + 1u < Self->SugCount_)
            {
                ++Self->SugIndex_;
            }
        }
    }
    return 0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           FOOTER
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::RecordFooter(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, 40.0f));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Small = Controls_->QuerySmall();
    const ImVec2 FootPos = ImGui::GetWindowPos();
    const ImVec2 FootSize = ImGui::GetWindowSize();
    const float  FootX0 = FootPos.x;
    const float  FootX1 = FootPos.x + FootSize.x;
    // The footer sits on the panel's own sill: its hem runs edge to edge and its wash pours to the
    //    sill's foot, with no pad to float it.
    Draw->AddRectFilled(ImVec2(FootX0, Cursor.y), ImVec2(FootX1, FootPos.y + FootSize.y), kWash);
    Draw->AddLine(ImVec2(FootX0, Cursor.y), ImVec2(FootX1, Cursor.y), kStroke);

    // The counters. Tris and camera wait on engine counters, so they keep the reference's own pre-paint
    //    dash; physics and isolation hide at zero, under the reference's show rule.
    const char* Dash = "\xe2\x80\x94";
    char PerfText[16] = {}, PerfSub[16] = {}, EntsText[16] = {}, EntsSub[32] = {};
    const float Fps = ImGui::GetIO().Framerate;
    std::snprintf(PerfText, sizeof(PerfText), "%.0f", static_cast<double>(Fps));
    std::snprintf(PerfSub, sizeof(PerfSub), "%.1f ms",
        static_cast<double>(Fps > 0.0f ? 1000.0f / Fps : 0.0f));
    uint32_t Shown = 0u;
    for (uint32_t i = 0u; i < InstanceCount; ++i)
    {
        if (Instances[i].Visible)
        {
            ++Shown;
        }
    }
    std::snprintf(EntsText, sizeof(EntsText), "%u", InstanceCount);
    std::snprintf(EntsSub, sizeof(EntsSub), "%u visible", Shown);
    struct Counter
    {
        const char* Label;
        const char* Text;
        const char* Sub;
        bool        Warn;
        bool        Dim;
        bool        Opt;
    };
    char CamText[32] = {};
    std::snprintf(CamText, sizeof(CamText), "%+.0f\xc2\xb0 %+.0f\xc2\xb0 %.1fm",
        static_cast<double>(Orbit_.Yaw * 57.29578f), static_cast<double>(Orbit_.Pitch * 57.29578f),
        static_cast<double>(Orbit_.Distance));
    const Counter Counters[4] = {
        { "FPS", PerfText, PerfSub, Fps > 0.0f && Fps < 24.0f, false, false },
        { "TRIS", Dash, "", false, false, false },
        { "INSTANCES", EntsText, EntsSub, false, false, false },
        { "CAMERA", CamText, "", false, false, true },
    };

    ImGui::PushFont(Small);
    const float DotW  = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, " \xc2\xb7 ").x;
    const float TextH = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Ag").y;
    ImGui::PopFont();
    const auto CounterWidth = [&](const Counter& Cell, bool WithSub) -> float
    {
        float W = SpacedCapsWidth(Small, Cell.Label, 1.2f) + 6.0f;
        ImGui::PushFont(Small);
        W += Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Cell.Text).x;
        if (WithSub && Cell.Sub[0] != '\0')
        {
            W += DotW + Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Cell.Sub).x;
        }
        ImGui::PopFont();
        return W;
    };

    // fitStats: the camera cell steps aside first, then the secondary halves.
    uint32_t Mode = 2u;
    for (uint32_t Try = 0u; Try < 3u; ++Try)
    {
        const bool KeepCam = (Try == 0u);
        const bool KeepSub = (Try < 2u);
        float W = 4.0f;
        for (uint32_t i = 0u; i < 4u; ++i)
        {
            if (Counters[i].Opt && !KeepCam)
            {
                continue;
            }
            W += CounterWidth(Counters[i], KeepSub) + 20.0f;
        }
        W -= 10.0f;   // the last cell keeps its left pad only
        if (W <= FootSize.x || Try == 2u)
        {
            Mode = Try;
            break;
        }
    }

    const bool  KeepCam = (Mode == 0u);
    const bool  KeepSub = (Mode < 2u);
    const float FootH   = FootPos.y + FootSize.y - Cursor.y;
    const float TextY   = Cursor.y + (FootH - TextH) * 0.5f;
    const float MidY    = Cursor.y + FootH * 0.5f;
    float X = FootX0 + 4.0f;
    bool First = true;
    for (uint32_t i = 0u; i < 4u; ++i)
    {
        if (Counters[i].Opt && !KeepCam)
        {
            continue;
        }
        if (!First)
        {
            Draw->AddLine(ImVec2(X - 10.0f, MidY - 6.0f), ImVec2(X - 10.0f, MidY + 6.0f), kStroke);
        }
        First = false;
        const float LabelW = SpacedCapsWidth(Small, Counters[i].Label, 1.2f);
        SpacedCaps(Draw, Small, Counters[i].Label, ImVec2(X, TextY), kFaint, 1.2f);
        const ImU32 TextTint = Counters[i].Warn ? kAmber : (Counters[i].Dim ? kDim : kText);
        ImGui::PushFont(Small);
        Draw->AddText(ImVec2(X + LabelW + 6.0f, TextY), TextTint, Counters[i].Text);
        const float TextW = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Counters[i].Text).x;
        if (KeepSub && Counters[i].Sub[0] != '\0')
        {
            char Joined[48] = {};
            std::snprintf(Joined, sizeof(Joined), " \xc2\xb7 %s", Counters[i].Sub);
            Draw->AddText(ImVec2(X + LabelW + 6.0f + TextW, TextY), kFaint, Joined);
        }
        ImGui::PopFont();
        X += CounterWidth(Counters[i], KeepSub) + 20.0f;
    }
}

} // namespace Frontier
