//============================================================================================================================================
//                                                    VIEWPORTPANEL.CPP
//============================================================================================================================================
// 🧩 Development editor viewport — the scene column.

#include "ViewportPanel.h"

#include "ControlPanel.h"
#include "EditorInstance.h"
#include "ConstructWorld.h"

#include <imgui.h>
#include <imgui_internal.h>   // ImGuiWindow: the SkipItems early-out

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cwchar>
#endif

namespace Frontier {

namespace {
// Open the separately linked CAD authoring window without blocking Frontier's event loop.
bool OpenSolidArcWindow() noexcept
{
#ifdef _WIN32
    wchar_t Executable[32768]{};
    const DWORD Length = GetModuleFileNameW(nullptr, Executable, 32768);
    if (Length == 0 || Length >= 32768) return false;
    wchar_t* Separator = std::wcsrchr(Executable, L'\\');
    if (!Separator) return false;
    constexpr wchar_t Suffix[] = L"\\SolidArc\\SolidArc.exe";
    if (size_t(Separator - Executable) + sizeof(Suffix) / sizeof(wchar_t) > 32768) return false;
    std::wcscpy(Separator, Suffix);
    STARTUPINFOW Startup{};
    Startup.cb = sizeof(Startup);
    PROCESS_INFORMATION Process{};
    if (!CreateProcessW(Executable, nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &Startup, &Process)) return false;
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    return true;
#else
    return false;
#endif
}


//------------------------------------------------------------------------------------------------------------------------
//                                                          TOKENS
//------------------------------------------------------------------------------------------------------------------------

constexpr ImU32 kView   = IM_COL32(7, 9, 12, 255);
constexpr ImU32 kGreen  = IM_COL32(0x34, 0xC7, 0x59, 255);
constexpr float kConsoleH = 40.0f;   // the command bar's own height, reserved and drawn
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

// The rail: 44 px of controls over a 2 px convergence hairline (Docs/Design/ViewportHeader.html, option A).
constexpr float kBarHeight      = 44.0f;
constexpr float kHairlineHeight = 2.0f;

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

void ViewportPanel::AssignTabOpen(bool* Open) noexcept
{
    TabOpen_ = Open;
}

void ViewportPanel::AssignWindowTitle(const char* Title) noexcept
{
    WindowTitle_ = (Title != nullptr && Title[0] != '\0') ? Title : "Viewport";
}

void ViewportPanel::AssignView(const unsigned char* Rgba, uint32_t Width, uint32_t Height) noexcept
{
    ViewRgba_    = (Rgba != nullptr && Width > 0u && Height > 0u) ? Rgba : nullptr;
    ViewTexture_ = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(ViewRgba_));
    ViewW_       = (ViewRgba_ != nullptr) ? Width : 0u;
    ViewH_       = (ViewRgba_ != nullptr) ? Height : 0u;
}

void ViewportPanel::AssignViewTexture(ImTextureID View, uint32_t Width, uint32_t Height, uint32_t StorageWidth, uint32_t StorageHeight) noexcept
{
    ViewTexture_ = View;
    ViewW_       = Width;
    ViewH_       = Height;
    StorageW_    = (StorageWidth > 0u) ? StorageWidth : Width;
    StorageH_    = (StorageHeight > 0u) ? StorageHeight : Height;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           RECORD
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::SeatViewportOrbit(const ViewportOrbit& Seated) noexcept
{
    Orbit_ = Seated;
    Home_  = Seated;
}

void ViewportPanel::AssignReadout(const EditorReadout* Readout) noexcept
{
    Readout_ = Readout;
}

void ViewportPanel::Record(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    IM_ASSERT(Controls_ != nullptr);
    if (!ImGui::Begin(WindowTitle_, TabOpen_, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
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
    SimulationStep_ = false;
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
    SimulationStep_ = Transport_ != kEdit;
    // One tick past the pause.
    if (!Paused_)
    {
        Paused_ = true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         HEADER BAR
//------------------------------------------------------------------------------------------------------------------------

namespace
{
// The web viewport's nine rail icons, drawn live from its own strokes in a 24-unit box: the four selection modes on a
//    shared cube, the two shadings and the three gizmos.
enum class RailIcon : uint32_t { SelectBody, SelectFace, SelectEdge, SelectVertex, ShadeWire, ShadeMatcap, GizmoMove, GizmoRotate, GizmoScale };

void DrawRailIcon(ImDrawList* Draw, RailIcon Icon, const ImVec2& C, float Size, ImU32 Ink) noexcept
{
    const float U     = Size / 24.0f;
    const float Thick = 1.7f * U < 1.15f ? 1.15f : 1.7f * U;
    auto P = [&](float X, float Y) noexcept { return ImVec2(C.x + (X - 12.0f) * U, C.y + (Y - 12.0f) * U); };
    auto Fainter = [&](float Fraction) noexcept
    {
        const uint32_t Alpha = static_cast<uint32_t>(static_cast<float>((Ink >> 24u) & 0xFFu) * Fraction);
        return (Ink & 0x00FFFFFFu) | (Alpha << 24u);
    };
    auto Stroke = [&](float X0, float Y0, float X1, float Y1, ImU32 Tint, float Width) noexcept { Draw->AddLine(P(X0, Y0), P(X1, Y1), Tint, Width); };
    auto Dashed = [&](float X0, float Y0, float X1, float Y1, ImU32 Tint) noexcept
    {
        // stroke-dasharray 2 2.4 in box units.
        const float Dx = X1 - X0, Dy = Y1 - Y0;
        const float Length = std::sqrt(Dx * Dx + Dy * Dy);
        for (float At = 0.0f; At < Length; At += 4.4f)
        {
            const float To = std::min(At + 2.0f, Length);
            Stroke(X0 + Dx * At / Length, Y0 + Dy * At / Length, X0 + Dx * To / Length, Y0 + Dy * To / Length, Tint, Thick);
        }
    };
    auto Poly = [&](const float (*Points)[2], uint32_t Count, bool Shut, ImU32 Tint) noexcept
    {
        Draw->PathClear();
        for (uint32_t i = 0u; i < Count; ++i)
            Draw->PathLineTo(P(Points[i][0], Points[i][1]));
        Draw->PathStroke(Tint, Shut ? ImDrawFlags_Closed : ImDrawFlags_None, Thick);
    };
    static const float Hex[6][2] = { { 12.0f, 3.0f }, { 20.0f, 7.5f }, { 20.0f, 16.5f }, { 12.0f, 21.0f }, { 4.0f, 16.5f }, { 4.0f, 7.5f } };
    static const float Spine[3][2] = { { 4.0f, 7.5f }, { 12.0f, 12.0f }, { 20.0f, 7.5f } };
    switch (Icon)
    {
    case RailIcon::SelectBody:
        Poly(Hex, 6u, true, Ink);
        Poly(Spine, 3u, false, Ink);
        Stroke(12.0f, 12.0f, 12.0f, 21.0f, Ink, Thick);
        break;
    case RailIcon::SelectFace:
    {
        Poly(Hex, 6u, true, Fainter(0.45f));
        const ImVec2 Top[4] = { P(4.0f, 7.5f), P(12.0f, 12.0f), P(20.0f, 7.5f), P(12.0f, 3.0f) };
        Draw->AddConvexPolyFilled(Top, 4, Ink);
        Draw->AddPolyline(Top, 4, Ink, ImDrawFlags_Closed, 1.4f * U < 1.0f ? 1.0f : 1.4f * U);
        break;
    }
    case RailIcon::SelectEdge:
        Poly(Hex, 6u, true, Fainter(0.45f));
        Stroke(12.0f, 12.0f, 12.0f, 21.0f, Ink, 3.0f * U);
        break;
    case RailIcon::SelectVertex:
        Poly(Hex, 6u, true, Fainter(0.45f));
        Draw->AddCircleFilled(P(12.0f, 12.0f), 2.6f * U, Ink, 14);
        Draw->AddCircleFilled(P(12.0f, 3.0f), 1.8f * U, Ink, 12);
        Draw->AddCircleFilled(P(4.0f, 16.5f), 1.8f * U, Ink, 12);
        Draw->AddCircleFilled(P(20.0f, 16.5f), 1.8f * U, Ink, 12);
        break;
    case RailIcon::ShadeWire:
        Poly(Hex, 6u, true, Ink);
        Poly(Spine, 3u, false, Ink);
        Stroke(12.0f, 12.0f, 12.0f, 21.0f, Ink, Thick);
        Dashed(12.0f, 12.0f, 4.0f, 16.5f, Fainter(0.7f));
        Dashed(12.0f, 12.0f, 20.0f, 16.5f, Fainter(0.7f));
        Dashed(12.0f, 3.0f, 12.0f, 12.0f, Fainter(0.7f));
        break;
    case RailIcon::ShadeMatcap:
        Draw->AddCircle(P(12.0f, 12.0f), 8.5f * U, Ink, 28, Thick);
        Draw->PathClear();
        Draw->PathLineTo(P(7.6f, 10.2f));
        Draw->PathBezierQuadraticCurveTo(P(8.34f, 7.49f), P(11.0f, 6.6f), 8);
        Draw->PathStroke(Ink, ImDrawFlags_None, 2.0f * U);
        Draw->AddCircleFilled(P(9.3f, 9.2f), 0.9f * U, Ink, 8);
        break;
    case RailIcon::GizmoMove:
    {
        static const float Head[8][4] = { { 12, 3, 9, 6 }, { 12, 3, 15, 6 }, { 12, 21, 9, 18 }, { 12, 21, 15, 18 },
                                          { 3, 12, 6, 9 }, { 3, 12, 6, 15 }, { 21, 12, 18, 9 }, { 21, 12, 18, 15 } };
        Stroke(12.0f, 3.0f, 12.0f, 21.0f, Ink, Thick);
        Stroke(3.0f, 12.0f, 21.0f, 12.0f, Ink, Thick);
        for (const auto& Arrow : Head)
            Stroke(Arrow[0], Arrow[1], Arrow[2], Arrow[3], Ink, Thick);
        break;
    }
    case RailIcon::GizmoRotate:
    {
        // M20 12 a8 8 0 1 1 -2.6 -5.9: seven-eighths of a circle, clockwise from three o'clock.
        Draw->PathClear();
        Draw->PathArcTo(P(12.0f, 12.0f), 8.0f * U, 0.0f, 5.454f, 24);
        Draw->PathStroke(Ink, ImDrawFlags_None, Thick);
        static const float Head[3][2] = { { 20.0f, 4.0f }, { 20.0f, 9.0f }, { 15.0f, 9.0f } };
        Poly(Head, 3u, false, Ink);
        break;
    }
    case RailIcon::GizmoScale:
    {
        Draw->AddRect(P(3.5f, 9.5f), P(14.5f, 20.5f), Ink, 1.5f * U, 0, Thick);
        Stroke(13.0f, 11.0f, 20.5f, 3.5f, Ink, Thick);
        static const float Head[3][2] = { { 15.0f, 3.5f }, { 20.5f, 3.5f }, { 20.5f, 9.0f } };
        Poly(Head, 3u, false, Ink);
        break;
    }
    }
}
}

namespace
{
// The Construct menu's tile symbols, drawn from the web catalogue's own strokes in its 24-unit box.
void DrawConstructGlyph(ImDrawList* Draw, ConstructGlyph Glyph, const ImVec2& C, float Size, ImU32 Ink) noexcept
{
    const float U     = Size / 24.0f;
    const float Thick = 1.5f * U < 1.2f ? 1.2f : 1.5f * U;
    auto P = [&](float X, float Y) noexcept { return ImVec2(C.x + (X - 12.0f) * U, C.y + (Y - 12.0f) * U); };
    auto Stroke = [&](float X0, float Y0, float X1, float Y1) noexcept { Draw->AddLine(P(X0, Y0), P(X1, Y1), Ink, Thick); };
    auto Dot = [&](float X, float Y, float R) noexcept { Draw->AddCircleFilled(P(X, Y), R * U, Ink, 10); };
    auto Poly = [&](const float (*Points)[2], uint32_t Count, bool Shut) noexcept
    {
        Draw->PathClear();
        for (uint32_t i = 0u; i < Count; ++i)
            Draw->PathLineTo(P(Points[i][0], Points[i][1]));
        Draw->PathStroke(Ink, Shut ? ImDrawFlags_Closed : ImDrawFlags_None, Thick);
    };
    // An ellipse (or a half of it) about (Cx, Cy), as a polyline: From and To are angles in turns.
    auto Oval = [&](float Cx, float Cy, float Rx, float Ry, float From, float To) noexcept
    {
        Draw->PathClear();
        const int Steps = 28;
        for (int i = 0; i <= Steps; ++i)
        {
            const float A = (From + (To - From) * static_cast<float>(i) / static_cast<float>(Steps)) * 6.2831853f;
            Draw->PathLineTo(P(Cx + Rx * std::cos(A), Cy + Ry * std::sin(A)));
        }
        Draw->PathStroke(Ink, (To - From) >= 0.999f ? ImDrawFlags_Closed : ImDrawFlags_None, Thick);
    };
    static const float Hex[6][2] = { { 12.0f, 3.0f }, { 20.0f, 7.5f }, { 20.0f, 16.5f }, { 12.0f, 21.0f }, { 4.0f, 16.5f }, { 4.0f, 7.5f } };
    switch (Glyph)
    {
    case ConstructGlyph::Plane:
    {
        static const float Quad[4][2] = { { 3.0f, 16.0f }, { 9.0f, 7.0f }, { 21.0f, 7.0f }, { 15.0f, 16.0f } };
        Poly(Quad, 4u, true);
        break;
    }
    case ConstructGlyph::Empty:
        Stroke(12.0f, 4.0f, 12.0f, 9.0f);
        Stroke(12.0f, 15.0f, 12.0f, 20.0f);
        Stroke(4.0f, 12.0f, 9.0f, 12.0f);
        Stroke(15.0f, 12.0f, 20.0f, 12.0f);
        Dot(12.0f, 12.0f, 1.2f);
        break;
    case ConstructGlyph::Line:
        Stroke(5.0f, 19.0f, 19.0f, 5.0f);
        Dot(5.0f, 19.0f, 1.6f);
        Dot(19.0f, 5.0f, 1.6f);
        break;
    case ConstructGlyph::Polyline:
    {
        static const float Zig[5][2] = { { 3.0f, 17.0f }, { 8.0f, 8.0f }, { 12.0f, 14.0f }, { 16.0f, 6.0f }, { 21.0f, 17.0f } };
        Poly(Zig, 5u, false);
        break;
    }
    case ConstructGlyph::Rectangle:
        Draw->AddRect(P(4.0f, 6.0f), P(20.0f, 18.0f), Ink, 0.0f, 0, Thick);
        break;
    case ConstructGlyph::CentreRectangle:
        Draw->AddRect(P(4.0f, 6.0f), P(20.0f, 18.0f), Ink, 0.0f, 0, Thick);
        Stroke(12.0f, 10.0f, 12.0f, 14.0f);
        Stroke(10.0f, 12.0f, 14.0f, 12.0f);
        break;
    case ConstructGlyph::Slot:
        Draw->AddRect(P(4.0f, 8.0f), P(20.0f, 16.0f), Ink, 4.0f * U, 0, Thick);
        Dot(8.0f, 12.0f, 1.2f);
        Dot(16.0f, 12.0f, 1.2f);
        break;
    case ConstructGlyph::Circle:
        Draw->AddCircle(P(12.0f, 12.0f), 8.0f * U, Ink, 28, Thick);
        Dot(12.0f, 12.0f, 1.2f);
        break;
    case ConstructGlyph::Arc:
        Draw->PathClear();
        Draw->PathArcTo(P(11.5f, 11.5f), 9.19f * U, 2.3561945f, 5.4977871f, 20);
        Draw->PathStroke(Ink, ImDrawFlags_None, Thick);
        Dot(5.0f, 18.0f, 1.5f);
        Dot(18.0f, 5.0f, 1.5f);
        Dot(12.0f, 14.0f, 1.2f);
        break;
    case ConstructGlyph::Ellipse:
        Oval(12.0f, 12.0f, 9.0f, 5.5f, 0.0f, 1.0f);
        break;
    case ConstructGlyph::Polygon:
        Poly(Hex, 6u, true);
        break;
    case ConstructGlyph::Spline:
        Draw->PathClear();
        Draw->PathLineTo(P(3.0f, 17.0f));
        Draw->PathBezierCubicCurveTo(P(7.0f, 7.0f), P(13.0f, 21.0f), P(21.0f, 9.0f), 14);
        Draw->PathStroke(Ink, ImDrawFlags_None, Thick);
        break;
    case ConstructGlyph::ControlCurve:
        Draw->PathClear();
        Draw->PathLineTo(P(3.0f, 17.0f));
        Draw->PathBezierCubicCurveTo(P(7.0f, 7.0f), P(13.0f, 21.0f), P(21.0f, 9.0f), 14);
        Draw->PathStroke(Ink, ImDrawFlags_None, Thick);
        Dot(7.0f, 7.0f, 1.5f);
        Dot(13.0f, 21.0f, 1.5f);
        break;
    case ConstructGlyph::Box:
    {
        Poly(Hex, 6u, true);
        static const float Spine[3][2] = { { 4.0f, 7.5f }, { 12.0f, 12.0f }, { 20.0f, 7.5f } };
        Poly(Spine, 3u, false);
        Stroke(12.0f, 12.0f, 12.0f, 21.0f);
        break;
    }
    case ConstructGlyph::Sphere:
        Draw->AddCircle(P(12.0f, 12.0f), 8.5f * U, Ink, 28, Thick);
        Oval(12.0f, 12.0f, 8.5f, 3.0f, 0.0f, 0.5f);
        break;
    case ConstructGlyph::Cylinder:
        Oval(12.0f, 6.0f, 7.0f, 2.5f, 0.0f, 1.0f);
        Stroke(5.0f, 6.0f, 5.0f, 18.0f);
        Stroke(19.0f, 6.0f, 19.0f, 18.0f);
        Oval(12.0f, 18.0f, 7.0f, 2.5f, 0.0f, 0.5f);
        break;
    case ConstructGlyph::Cone:
        Stroke(12.0f, 3.5f, 4.5f, 18.0f);
        Stroke(12.0f, 3.5f, 19.5f, 18.0f);
        Oval(12.0f, 18.0f, 7.5f, 2.6f, 0.0f, 0.5f);
        break;
    case ConstructGlyph::Torus:
        Oval(12.0f, 12.0f, 9.5f, 6.0f, 0.0f, 1.0f);
        Oval(12.0f, 12.0f, 4.0f, 2.0f, 0.0f, 1.0f);
        break;
    case ConstructGlyph::Patch:
    {
        static const float Top[4][2]   = { { 3.0f, 8.0f }, { 10.0f, 4.0f }, { 21.0f, 8.0f }, { 14.0f, 12.0f } };
        static const float Left[4][2]  = { { 3.0f, 8.0f }, { 3.0f, 16.0f }, { 14.0f, 20.0f }, { 14.0f, 12.0f } };
        static const float Right[3][2] = { { 21.0f, 8.0f }, { 21.0f, 16.0f }, { 14.0f, 20.0f } };
        Poly(Top, 4u, true);
        Poly(Left, 4u, false);
        Poly(Right, 3u, false);
        break;
    }
    }
}
}

void ViewportPanel::AssignConstructTiles(const ViewportConstructTile* Tiles, uint32_t Count) noexcept
{
    ConstructTiles_     = Tiles;
    ConstructTileCount_ = Count < kConstructTileCap ? Count : kConstructTileCap;
}

bool ViewportPanel::QueryConstructPick(uint32_t* Index) noexcept
{
    if (ConstructPick_ == 0xFFFFFFFFu)
        return false;
    *Index         = ConstructPick_;
    ConstructPick_ = 0xFFFFFFFFu;
    return true;
}

bool ViewportPanel::QueryConstructTileCentre(uint32_t Index, float* X, float* Y) const noexcept
{
    if (Index >= ConstructTileCount_ || !ConstructTileSeen_[Index])
        return false;
    *X = ConstructTileX_[Index];
    *Y = ConstructTileY_[Index];
    return true;
}

bool ViewportPanel::QueryConstructSectionCentre(uint32_t Section, float* X, float* Y) const noexcept
{
    if (!ConstructShown_ || Section >= ConstructRailCount_)
        return false;
    *X = ConstructRailX_[Section];
    *Y = ConstructRailY_[Section];
    return true;
}

// The Construct menu, a popup that hangs from the chip: a rail of sections on the left (each with its tile count), the
//    section's tiles in four columns on the right, a hint line under them. A click on a tile is the pick; the popup
//    closes with it, as the web catalogue does when it is not pinned.
void ViewportPanel::DrawConstructMenu(float MenuX, float MenuY) noexcept
{
    ConstructShown_ = false;
    for (uint32_t i = 0u; i < kConstructTileCap; ++i)
        ConstructTileSeen_[i] = false;
    constexpr float kMenuW = 520.0f, kMenuH = 330.0f, kHeadH = 40.0f, kRailW = 132.0f, kFootH = 28.0f;
    ImGui::SetNextWindowPos(ImVec2(MenuX, MenuY), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(kMenuW, kMenuH), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(14, 16, 20, 250));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 34));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool Open = ImGui::BeginPopup("##solidarc_construct_menu", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (Open)
    {
        ConstructShown_ = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            ImGui::CloseCurrentPopup();
        // The visible catalogue shortcuts perform the same action as clicking a tile.
        if (!ImGui::GetIO().WantTextInput)
        {
            const ImGuiKey Keys[] = { ImGuiKey_L, ImGuiKey_R, ImGuiKey_C, ImGuiKey_A, ImGuiKey_E, ImGuiKey_P };
            const uint32_t Tiles[] = { ImGui::GetIO().KeyShift ? 2u : 1u, 3u, 6u, 7u, 8u, 9u };
            for (uint32_t Index = 0u; Index < 6u; ++Index)
                if (ImGui::IsKeyPressed(Keys[Index], false))
                {
                    ConstructPick_ = Tiles[Index];
                    ImGui::CloseCurrentPopup();
                    break;
                }
        }
        ImDrawList* Draw  = ImGui::GetWindowDrawList();
        ImFont*     Small = Controls_->QuerySmall() != nullptr ? Controls_->QuerySmall() : ImGui::GetFont();
        ImFont*     Mono  = Controls_->QueryMono() != nullptr ? Controls_->QueryMono() : Small;
        const ImVec2 Origin = ImGui::GetWindowPos();
        auto TextCentred = [&](const char* Text, float Cx, float Y, ImU32 Tint, ImFont* Face, float Px) noexcept
        {
            const ImVec2 T = Face->CalcTextSizeA(Px, FLT_MAX, 0.0f, Text);
            Draw->AddText(Face, Px, ImVec2(Cx - T.x * 0.5f, Y), Tint, Text);
        };

        // The sections, in the order their first tile appears.
        const char* Sections[8] = {};
        uint32_t    Counts[8]   = {};
        uint32_t    SectionCount = 0u;
        for (uint32_t i = 0u; i < ConstructTileCount_; ++i)
        {
            uint32_t At = 0u;
            while (At < SectionCount && std::strcmp(Sections[At], ConstructTiles_[i].Section) != 0)
                ++At;
            if (At == SectionCount && SectionCount < 8u)
                Sections[SectionCount++] = ConstructTiles_[i].Section;
            if (At < 8u)
                ++Counts[At];
        }
        ConstructRailCount_ = SectionCount;
        if (ConstructSection_ >= SectionCount)
            ConstructSection_ = 0u;

        // Head.
        Draw->AddRectFilled(Origin, ImVec2(Origin.x + kMenuW, Origin.y + kHeadH), IM_COL32(255, 255, 255, 8), 14.0f, ImDrawFlags_RoundCornersTop);
        Draw->AddLine(ImVec2(Origin.x, Origin.y + kHeadH), ImVec2(Origin.x + kMenuW, Origin.y + kHeadH), kStroke);
        Draw->AddCircleFilled(ImVec2(Origin.x + 20.0f, Origin.y + kHeadH * 0.5f), 4.0f, IM_COL32(79, 216, 224, 255));
        Draw->AddText(Small, 11.0f, ImVec2(Origin.x + 34.0f, Origin.y + kHeadH * 0.5f - 6.5f), kText, "Construct");
        const char* Hint = "Tab";
        const ImVec2 HintSize = Mono->CalcTextSizeA(9.0f, FLT_MAX, 0.0f, Hint);
        Draw->AddRectFilled(ImVec2(Origin.x + kMenuW - 20.0f - HintSize.x, Origin.y + 12.0f), ImVec2(Origin.x + kMenuW - 12.0f, Origin.y + 28.0f), IM_COL32(255, 255, 255, 16), 5.0f);
        Draw->AddText(Mono, 9.0f, ImVec2(Origin.x + kMenuW - 16.0f - HintSize.x, Origin.y + 20.0f - HintSize.y * 0.5f), kDim, Hint);

        // The rail.
        Draw->AddRectFilled(ImVec2(Origin.x, Origin.y + kHeadH), ImVec2(Origin.x + kRailW, Origin.y + kMenuH), IM_COL32(0, 0, 0, 46), 14.0f, ImDrawFlags_RoundCornersBottomLeft);
        Draw->AddLine(ImVec2(Origin.x + kRailW, Origin.y + kHeadH), ImVec2(Origin.x + kRailW, Origin.y + kMenuH), kStroke);
        for (uint32_t r = 0u; r < SectionCount; ++r)
        {
            const float Rx = Origin.x + 6.0f, Ry = Origin.y + kHeadH + 8.0f + static_cast<float>(r) * 38.0f;
            char Id[40];
            std::snprintf(Id, sizeof(Id), "##construct_section%u", r);
            ImGui::SetCursorScreenPos(ImVec2(Rx, Ry));
            ImGui::InvisibleButton(Id, ImVec2(kRailW - 12.0f, 36.0f));
            const bool Hot = ImGui::IsItemHovered();
            if (Hot && ImGui::IsMouseClicked(0))
                ConstructSection_ = r;
            const bool On = ConstructSection_ == r;
            if (On || Hot)
                Draw->AddRectFilled(ImVec2(Rx, Ry), ImVec2(Rx + kRailW - 12.0f, Ry + 36.0f), On ? IM_COL32(255, 255, 255, 26) : IM_COL32(255, 255, 255, 12), 7.0f);
            Draw->AddText(Small, 11.0f, ImVec2(Rx + 10.0f, Ry + 18.0f - 6.5f), On || Hot ? kText : kDim, Sections[r]);
            char Count[12];
            std::snprintf(Count, sizeof(Count), "%u", Counts[r]);
            const ImVec2 CountSize = Mono->CalcTextSizeA(9.0f, FLT_MAX, 0.0f, Count);
            Draw->AddText(Mono, 9.0f, ImVec2(Rx + kRailW - 12.0f - 8.0f - CountSize.x, Ry + 18.0f - CountSize.y * 0.5f), kFaint, Count);
            ConstructRailX_[r] = Rx + 20.0f;
            ConstructRailY_[r] = Ry + 18.0f;
        }

        // The tiles of the shown section.
        constexpr float kPad = 10.0f, kGapT = 6.0f, kTileH = 72.0f;
        const float GridX = Origin.x + kRailW + kPad;
        const float GridY = Origin.y + kHeadH + kPad;
        const float TileW = (kMenuW - kRailW - 2.0f * kPad - 3.0f * kGapT) / 4.0f;
        uint32_t Slot = 0u;
        for (uint32_t i = 0u; i < ConstructTileCount_; ++i)
        {
            if (SectionCount == 0u || std::strcmp(ConstructTiles_[i].Section, Sections[ConstructSection_]) != 0)
                continue;
            const float Tx = GridX + static_cast<float>(Slot % 4u) * (TileW + kGapT);
            const float Ty = GridY + static_cast<float>(Slot / 4u) * (kTileH + kGapT);
            ++Slot;
            char Id[40];
            std::snprintf(Id, sizeof(Id), "##construct_tile%u", i);
            ImGui::SetCursorScreenPos(ImVec2(Tx, Ty));
            ImGui::InvisibleButton(Id, ImVec2(TileW, kTileH));
            const bool Hot = ImGui::IsItemHovered();
            Draw->AddRectFilled(ImVec2(Tx, Ty), ImVec2(Tx + TileW, Ty + kTileH), Hot ? IM_COL32(255, 255, 255, 30) : IM_COL32(255, 255, 255, 12), 8.0f);
            Draw->AddRect(ImVec2(Tx, Ty), ImVec2(Tx + TileW, Ty + kTileH), Hot ? IM_COL32(255, 255, 255, 60) : kStroke, 8.0f);
            DrawConstructGlyph(Draw, ConstructTiles_[i].Glyph, ImVec2(Tx + TileW * 0.5f, Ty + 28.0f), 24.0f, kText);
            TextCentred(ConstructTiles_[i].Label, Tx + TileW * 0.5f, Ty + 50.0f, Hot ? kText : kDim, Small, 10.0f);
            if (ConstructTiles_[i].Key != nullptr && ConstructTiles_[i].Key[0] != '\0')
            {
                const ImVec2 KeySize = Mono->CalcTextSizeA(9.0f, FLT_MAX, 0.0f, ConstructTiles_[i].Key);
                Draw->AddRectFilled(ImVec2(Tx + TileW - 8.0f - KeySize.x, Ty + 5.0f), ImVec2(Tx + TileW - 3.0f, Ty + 18.0f), IM_COL32(255, 255, 255, 16), 5.0f);
                Draw->AddText(Mono, 9.0f, ImVec2(Tx + TileW - 5.5f - KeySize.x, Ty + 11.5f - KeySize.y * 0.5f), kFaint, ConstructTiles_[i].Key);
            }
            ConstructTileX_[i]    = Tx + TileW * 0.5f;
            ConstructTileY_[i]    = Ty + kTileH * 0.5f;
            ConstructTileSeen_[i] = true;
            if (Hot && ImGui::IsMouseClicked(0))
            {
                ConstructPick_ = i;
                ImGui::CloseCurrentPopup();
            }
        }

        // The foot.
        Draw->AddLine(ImVec2(Origin.x + kRailW, Origin.y + kMenuH - kFootH), ImVec2(Origin.x + kMenuW, Origin.y + kMenuH - kFootH), kStroke);
        Draw->AddText(Small, 10.0f, ImVec2(Origin.x + kRailW + 12.0f, Origin.y + kMenuH - kFootH * 0.5f - 6.0f), kFaint, "Click a tile: it is placed at the next free spot on the workplane");
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void ViewportPanel::RecordSolidArcBar() noexcept
{
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Small = Controls_->QuerySmall() != nullptr ? Controls_->QuerySmall() : ImGui::GetFont();

    constexpr float kToolPx = 9.0f;
    constexpr float kToolH = 22.0f;
    constexpr float kGap = 8.0f;
    constexpr float kSegH = 30.0f;     // the web .seg capsule: 24 px buttons inside 3 px of padding
    constexpr float kSegBtnW = 32.0f;
    constexpr float kIconPx = 17.0f;
    auto TextWidth = [Small, kToolPx](const char* Text) noexcept -> float
    {
        return Small->CalcTextSizeA(kToolPx, FLT_MAX, 0.0f, Text).x;
    };
    auto TextAt = [Draw, Small, kToolPx, kToolH](const char* Text, float X, float Y, ImU32 Tint) noexcept
    {
        const ImVec2 T = Small->CalcTextSizeA(kToolPx, FLT_MAX, 0.0f, Text);
        Draw->AddText(Small, kToolPx, ImVec2(X, Y + (kToolH - T.y) * 0.5f), Tint, Text);
    };
    const float ConstructW = TextWidth("Construct") + 22.0f;
    const float BarH = 38.0f;

    ImGui::Dummy(ImVec2(RowWidth, BarH));
    const ImVec2 Cursor = ImGui::GetItemRectMin();
    const float StartX = Cursor.x + 10.0f;
    const float EndX = Cursor.x + RowWidth - 10.0f;
    const float MidY = Cursor.y + BarH * 0.5f;
    float X = StartX;

    // The Construct chip: the plus and the word. A click or Tab hangs the menu from it; a click away or Esc closes it.
    float MenuX = X, MenuY = Cursor.y + BarH + 2.0f;
    if (X + ConstructW <= EndX)
    {
        const float Y = MidY - kToolH * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        ImGui::InvisibleButton("##solidarc_construct", ImVec2(ConstructW, kToolH));
        const bool Hot = ImGui::IsItemHovered();
        const bool Shown = ImGui::IsPopupOpen("##solidarc_construct_menu");
        if ((Hot && ImGui::IsMouseClicked(0) && !Shown) || (ImGui::IsWindowHovered() && ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !Shown))
            ImGui::OpenPopup("##solidarc_construct_menu");
        Draw->AddRectFilled(ImVec2(X, Y), ImVec2(X + ConstructW, Y + kToolH), (Hot || Shown) ? IM_COL32(255, 255, 255, 34) : IM_COL32(0, 0, 0, 95), kToolH * 0.5f);
        Draw->AddRect(ImVec2(X, Y), ImVec2(X + ConstructW, Y + kToolH), (Hot || Shown) ? IM_COL32(255, 255, 255, 44) : kStroke, kToolH * 0.5f);
        const ImU32 Cyan = IM_COL32(79, 216, 224, 255);
        Draw->AddLine(ImVec2(X + 10.0f, MidY), ImVec2(X + 18.0f, MidY), Cyan, 1.5f);
        Draw->AddLine(ImVec2(X + 14.0f, MidY - 4.0f), ImVec2(X + 14.0f, MidY + 4.0f), Cyan, 1.5f);
        TextAt("Construct", X + 24.0f, Y, kText);
        X += ConstructW + kGap;
    }
    DrawConstructMenu(MenuX, MenuY);

    // One capsule of icon buttons. Mask segments toggle with Shift held, as the web rail's modes combine; the rest pick one.
    auto Segment = [&](const char* Group, const RailIcon* Icons, const char* const* Titles, uint32_t Count, uint32_t* Picked, bool Combine) noexcept
    {
        const float Width = Count * kSegBtnW + (Count - 1u) * 3.0f + 6.0f;
        if (X + Width > EndX)
            return;
        const float Y = MidY - kSegH * 0.5f;
        Draw->AddRectFilled(ImVec2(X, Y), ImVec2(X + Width, Y + kSegH), IM_COL32(0, 0, 0, 90), kSegH * 0.5f);
        Draw->AddRect(ImVec2(X, Y), ImVec2(X + Width, Y + kSegH), kStroke, kSegH * 0.5f);
        for (uint32_t i = 0u; i < Count; ++i)
        {
            const float Bx = X + 3.0f + i * (kSegBtnW + 3.0f);
            const float By = Y + 3.0f;
            char Id[48];
            std::snprintf(Id, sizeof(Id), "##%s%u", Group, i);
            ImGui::SetCursorScreenPos(ImVec2(Bx, By));
            ImGui::InvisibleButton(Id, ImVec2(kSegBtnW, 24.0f));
            const bool Hot = ImGui::IsItemHovered();
            if (Hot && ImGui::IsMouseClicked(0))
            {
                if (Combine && ImGui::GetIO().KeyShift)
                {
                    const uint32_t Next = *Picked ^ (1u << i);
                    *Picked = Next != 0u ? Next : *Picked;
                }
                else
                    *Picked = Combine ? (1u << i) : i;
            }
            const bool On = Combine ? ((*Picked >> i) & 1u) != 0u : *Picked == i;
            if (On)
                Draw->AddRectFilled(ImVec2(Bx, By), ImVec2(Bx + kSegBtnW, By + 24.0f), IM_COL32(255, 255, 255, 36), 12.0f);
            DrawRailIcon(Draw, Icons[i], ImVec2(Bx + kSegBtnW * 0.5f, By + 12.0f), kIconPx, On || Hot ? kText : kDim);
            if (Hot)
                ImGui::SetTooltip("%s", Titles[i]);
        }
        X += Width + kGap;
    };

    static const RailIcon      kSelectIcons[4] = { RailIcon::SelectBody, RailIcon::SelectFace, RailIcon::SelectEdge, RailIcon::SelectVertex };
    static const char* const   kSelectTitles[4] = { "Body  1", "Face  2", "Edge  3", "Vertex  4" };
    static const RailIcon      kShadeIcons[2] = { RailIcon::ShadeWire, RailIcon::ShadeMatcap };
    static const char* const   kShadeTitles[2] = { "Wireframe", "Matcap" };
    static const RailIcon      kGizmoIcons[3] = { RailIcon::GizmoMove, RailIcon::GizmoRotate, RailIcon::GizmoScale };
    static const char* const   kGizmoTitles[3] = { "Move  G", "Rotate  Shift+R", "Scale  S" };

    Segment("solidarc_select", kSelectIcons, kSelectTitles, 4u, &SolidArcSelectMask_, true);
    const char* Combine = "shift combines";
    if (X + TextWidth(Combine) + kGap <= EndX)
    {
        TextAt(Combine, X, MidY - kToolH * 0.5f, kFaint);
        X += TextWidth(Combine) + kGap;
    }
    Segment("solidarc_shade", kShadeIcons, kShadeTitles, 2u, &SolidArcShade_, false);
    Segment("solidarc_gizmo", kGizmoIcons, kGizmoTitles, 3u, &SolidArcGizmo_, false);

    Draw->AddLine(ImVec2(Cursor.x, Cursor.y + BarH), ImVec2(Cursor.x + RowWidth, Cursor.y + BarH), kStroke);
}

void ViewportPanel::RecordBar() noexcept
{
    if (Chrome_ == ViewportPanelChrome::SolidArcCad)
    {
        RecordSolidArcBar();
        return;
    }

    // ══════════════════════════════════════════════════════════════════════════════════════════════════════
    //  THE RAIL — three zones (Docs/Design/ViewportHeader.html, option A)
    // ══════════════════════════════════════════════════════════════════════════════════════════════════════
    //  left   the scene      brand · panel pair · Add
    //  centre the run        Edit | Simulate | Play, and pause/step/stop ONLY while something runs
    //  right  the view       projection · markers · status (Live / Static / Held + samples) · settings
    //
    //  What this replaced, and why: five transport buttons of which three were dead whenever the world was
    //  stopped (which is most of the time), plus the state written in three places at once — the highlighted
    //  run button, the Realtime pill and a separate Edit/Play/Paused chip. The mode pill IS the state, so it
    //  is said once; the three run-only buttons arrive when they mean something and leave when they do not.
    //  The freed pixels went to the two facts this renderer actually has and never showed: how many samples
    //  the frame has accumulated, and whether it is still converging (the hairline under the rail).
    //
    //  Nothing about the transport's BEHAVIOUR moved: SetTransport / SetPaused / StepOnce / SetRealtime keep
    //  their rules (step waits on pause, stop waits on a run, realtime rests while running) and their keys.

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, kBarHeight + kHairlineHeight));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Ui    = Controls_->QueryUi();
    ImFont*     Small = Controls_->QuerySmall();
    ImFont*     Mono  = Controls_->QueryMono();

    const ImVec2 TileMin(Cursor.x, Cursor.y + 8.0f);
    const ImVec2 TileMax(Cursor.x + 28.0f, Cursor.y + 36.0f);
    Draw->AddRectFilled(TileMin, TileMax, kTile, 8.0f);
    Draw->AddRect(TileMin, TileMax, kStroke, 8.0f);
    ImGui::PushFont(Ui);
    const ImVec2 FGlyph = Ui->CalcTextSizeA(Ui->LegacySize, FLT_MAX, 0.0f, "F");
    Draw->AddText(ImVec2(Cursor.x + (28.0f - FGlyph.x) * 0.5f, Cursor.y + 8.0f + (28.0f - FGlyph.y) * 0.5f),
        kText, "F");
    ImGui::PopFont();

    // The hairline is drawn whatever the width: it is also the rail's bottom rule.
    const float HairY = Cursor.y + kBarHeight;
    const ImVec2 WinPos  = ImGui::GetWindowPos();
    const ImVec2 WinSize = ImGui::GetWindowSize();
    {
        const float Target   = RenderTarget_ > 0u ? static_cast<float>(RenderTarget_) : 256.0f;
        float       Fraction = static_cast<float>(RenderSamples_) / Target;
        const bool  Refining = Fraction >= 1.0f;
        Fraction = Fraction < 0.0f ? 0.0f : (Fraction > 1.0f ? 1.0f : Fraction);
        Draw->AddLine(ImVec2(WinPos.x, HairY), ImVec2(WinPos.x + WinSize.x, HairY), kStroke);
        if (RenderSamples_ > 0u)
        {
            // Teal while the first pass fills, blue once it is past the initial accumulation and only
            //    refining — the same distinction the notification makes ("refinement continues").
            const ImU32 Ink = Refining ? IM_COL32(79, 154, 216, 255) : IM_COL32(89, 201, 165, 255);
            Draw->AddRectFilled(ImVec2(WinPos.x, HairY), ImVec2(WinPos.x + WinSize.x * Fraction, HairY + kHairlineHeight), Ink);
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(Cursor.x, Cursor.y + kBarHeight + kHairlineHeight));

    // Degenerate stages hold the bar's height and rest: the rail needs ~560 px, and narrower columns keep
    //    the brand alone rather than tripping a cursor-boundary assert on the way past the right edge.
    if (RowWidth < 560.0f)
    {
        return;
    }

    const float BtnY = Cursor.y + 9.0f;                  // 26 px controls, centred in the 44 px rail
    const bool  Running = (Transport_ != kEdit);

    // ── measuring ─────────────────────────────────────────────────────────────────────────────────────────
    // Labels retire in one order as the column narrows — markers, then add, then the projection, then the
    //    two inactive modes — and the measurement below is the only place that order lives.
    const char* ModeLabels[3] = { "Edit", "Simulate", "Play" };
    const char* PlayLabel     = (Transport_ == kPlay && Paused_) ? "Paused" : "Play";
    const char* SimLabel      = (Transport_ == kSimulate && Paused_) ? "Paused" : "Simulate";
    ModeLabels[1] = SimLabel;
    ModeLabels[2] = PlayLabel;

    char ViewLabel[32] = {};
    if (Orbit_.ViewPoint == 0u)
        std::snprintf(ViewLabel, sizeof(ViewLabel), "%s", Orbit_.Ortho ? "Orthographic" : "Perspective");
    else
        std::snprintf(ViewLabel, sizeof(ViewLabel), "%s %s", kSnapNames[Orbit_.ViewPoint],
                      Orbit_.Ortho ? "Ortho" : "Persp");
    char ViewShort[16] = {};
    std::snprintf(ViewShort, sizeof(ViewShort), "%s", Orbit_.Ortho ? "Ortho" : "Persp");
    char FovLabel[12] = {};
    if (FieldOfView_ > 1.0f && !Orbit_.Ortho) std::snprintf(FovLabel, sizeof(FovLabel), "%.0f\xc2\xb0", static_cast<double>(FieldOfView_));

    // The status reads the run first, then the clock: one label, never two.
    const char* StatusLabel = Paused_ ? "Held" : (Running ? "Running" : (Realtime_ ? "Live" : "Static"));
    char SampleText[16] = {};
    if (RenderSamples_ >= 1000u)
        std::snprintf(SampleText, sizeof(SampleText), "%u %03u", RenderSamples_ / 1000u, RenderSamples_ % 1000u);
    else
        std::snprintf(SampleText, sizeof(SampleText), "%u", RenderSamples_);
    ImGui::PushFont(Mono);
    const float SampleW = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, SampleText).x;
    ImGui::PopFont();

    const float TransportW = 11.0f + 3.0f * 28.0f + 2.0f * 2.0f + 4.0f;   // rule + pause/step/stop
    float AddW = 0.0f, MarkW = 0.0f, ViewW = 0.0f, StatusW = 0.0f;
    float ModeW[3] = { 0.0f, 0.0f, 0.0f }, ModePillW = 0.0f;
    float LeftW = 0.0f, CentreW = 0.0f, RightW = 0.0f;
    uint32_t Squeeze = 0u;
    for (;; ++Squeeze)
    {
        AddW  = Squeeze >= 2u ? 28.0f : SpacedCapsWidth(Small, "Add", 1.1f) + 34.0f;
        MarkW = Squeeze >= 1u ? 28.0f : SpacedCapsWidth(Small, "Markers", 1.1f) + 34.0f;
        ViewW = (Squeeze >= 3u ? SpacedCapsWidth(Small, ViewShort, 1.1f)
                               : SpacedCapsWidth(Small, ViewLabel, 1.1f)) + 34.0f;
        if (FovLabel[0] && Squeeze < 3u) ViewW += SpacedCapsWidth(Small, FovLabel, 1.1f) + 8.0f;
        for (uint32_t M = 0u; M < 3u; ++M)
        {
            const bool Selected = (M == 0u && !Running) || (M == 1u && Transport_ == kSimulate) || (M == 2u && Transport_ == kPlay);
            ModeW[M] = (Squeeze >= 4u && !Selected && M > 0u)
                     ? 26.0f
                     : SpacedCapsWidth(Small, ModeLabels[M], 1.1f) + (M > 0u ? 30.0f : 24.0f);
        }
        ModePillW = ModeW[0] + ModeW[1] + ModeW[2] + 4.0f * 2.0f;
        StatusW   = 9.0f + 7.0f + 8.0f + SpacedCapsWidth(Small, StatusLabel, 1.1f) + 8.0f + SampleW + 10.0f;
        LeftW     = 28.0f + 10.0f + 56.0f + 6.0f + AddW;                       // brand · panel pair · add
        CentreW   = ModePillW + TransportW * TransportOpen_;
        RightW    = ViewW + 4.0f + MarkW + 12.0f + StatusW + 4.0f + 28.0f;
        if (LeftW + CentreW + RightW + 40.0f <= RowWidth || Squeeze >= 4u) break;
    }

    // ── the centre's arrival ──────────────────────────────────────────────────────────────────────────────
    {
        const float Want = Running ? 1.0f : 0.0f;
        const float Step = ImGui::GetIO().DeltaTime / 0.18f;
        if (TransportOpen_ < Want) TransportOpen_ = std::min(Want, TransportOpen_ + Step);
        else if (TransportOpen_ > Want) TransportOpen_ = std::max(Want, TransportOpen_ - Step);
    }

    // ── shared pill ───────────────────────────────────────────────────────────────────────────────────────
    const auto Pill = [&](const char* Id, float X, float W, bool On, ImU32 OnBg, ImU32 Edge, bool* OutHot) -> bool
    {
        ImGui::SetCursorScreenPos(ImVec2(X, BtnY));
        ImGui::InvisibleButton(Id, ImVec2(W, 26.0f));
        const bool Hot = ImGui::IsItemHovered();
        if (OutHot) *OutHot = Hot;
        const ImU32 Bg = On ? OnBg : (Hot ? kHover : IM_COL32(255, 255, 255, 8));
        Draw->AddRectFilled(ImVec2(X, BtnY), ImVec2(X + W, BtnY + 26.0f), Bg, 13.0f);
        Draw->AddRect(ImVec2(X, BtnY), ImVec2(X + W, BtnY + 26.0f), On ? Edge : kStroke, 13.0f);
        return Hot && ImGui::IsMouseClicked(0);
    };

    // ══ LEFT ══════════════════════════════════════════════════════════════════════════════════════════════
    float X = Cursor.x + 38.0f;

    // The two dock toggles, now one segmented pair rather than two loose discs.
    {
        const float PairW = 56.0f;
        Draw->AddRectFilled(ImVec2(X, BtnY), ImVec2(X + PairW, BtnY + 26.0f), IM_COL32(18, 20, 22, 255), 13.0f);
        Draw->AddRect(ImVec2(X, BtnY), ImVec2(X + PairW, BtnY + 26.0f), kStroke, 13.0f);
        bool* Docks[2] = { &DockLeft_, &DockRight_ };
        for (uint32_t i = 0u; i < 2u; ++i)
        {
            const float SegX = X + 2.0f + static_cast<float>(i) * 26.0f;
            ImGui::SetCursorScreenPos(ImVec2(SegX, BtnY + 2.0f));
            char DockId[10] = {};
            std::snprintf(DockId, sizeof(DockId), "##dk%u", i);
            ImGui::InvisibleButton(DockId, ImVec2(26.0f, 22.0f));
            const bool Hot = ImGui::IsItemHovered();
            if (Hot)
            {
                ImGui::SetTooltip("%s", i == 0u ? "Outliner column" : "Inspector column");
                if (ImGui::IsMouseClicked(0)) *Docks[i] = !*Docks[i];
            }
            if (*Docks[i])      Draw->AddRectFilled(ImVec2(SegX, BtnY + 2.0f), ImVec2(SegX + 26.0f, BtnY + 24.0f), kStrong, 11.0f);
            else if (Hot)       Draw->AddRectFilled(ImVec2(SegX, BtnY + 2.0f), ImVec2(SegX + 26.0f, BtnY + 24.0f), kHover, 11.0f);
            const ImVec2 C(SegX + 13.0f, BtnY + 13.0f);
            Draw->AddRect(ImVec2(C.x - 6.0f, C.y - 5.0f), ImVec2(C.x + 6.0f, C.y + 5.0f),
                *Docks[i] ? kDim : kFaint, 2.0f, 0, 1.4f);
            const float BarX = (i == 0u) ? (C.x - 6.0f) : (C.x + 3.0f);
            Draw->AddRectFilled(ImVec2(BarX, C.y - 5.0f), ImVec2(BarX + 3.0f, C.y + 5.0f), *Docks[i] ? kText : kFaint);
        }
        X += PairW + 6.0f;
    }

    // Add.
    {
        bool AddHot = false;
        const float AddX = X;
        if (Pill("##addbutton", AddX, AddW, false, 0u, kStroke, &AddHot)) ImGui::OpenPopup("##addmenu");
        if (AddHot) ImGui::SetTooltip("Add an object  (Shift A)");
        const ImU32 Ink = AddHot ? kText : kDim;
        const ImVec2 PlusC(AddX + (Squeeze >= 2u ? 14.0f : 15.0f), BtnY + 13.0f);
        Draw->AddLine(ImVec2(PlusC.x - 4.0f, PlusC.y), ImVec2(PlusC.x + 4.0f, PlusC.y), Ink, 1.6f);
        Draw->AddLine(ImVec2(PlusC.x, PlusC.y - 4.0f), ImVec2(PlusC.x, PlusC.y + 4.0f), Ink, 1.6f);
        if (Squeeze < 2u)
        {
            ImGui::PushFont(Small);
            const float CapH = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Add").y;
            ImGui::PopFont();
            SpacedCaps(Draw, Small, "Add", ImVec2(AddX + 26.0f, BtnY + (26.0f - CapH) * 0.5f), Ink, 1.1f);
        }
        X = AddX;   // the add menu's popup body below anchors on X / BtnY
    ImGui::SetNextWindowPos(ImVec2(X, BtnY + 33.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(220.0f, 0.0f), ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.06f, 0.07f, 0.08f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.25f, 0.28f, 0.8f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));
    bool SolidArcFailed = false;
    if (ImGui::BeginPopup("##addmenu"))
    {
        ImGui::PushFont(Small);
        if (ImGui::BeginMenu("Lights"))
        {
            auto AddLight=[&](ConstructKind Kind,const char* Name){if(!ConstructionWorld_)return;ConstructRequest R;R.Kind=Kind;R.Name=Name;if(ConstructEntity(*ConstructionWorld_,R))ConstructionChanged_=true;};
            if (ImGui::MenuItem("Directional Light (Sun)")) AddLight(ConstructKind::DirectionalLight,"Directional Light");
            if (ImGui::MenuItem("Point Light")) AddLight(ConstructKind::PointLight,"Point Light");
            if (ImGui::MenuItem("Spot Light")) AddLight(ConstructKind::SpotLight,"Spot Light");
            if (ImGui::MenuItem("Rect / Area Light")) AddLight(ConstructKind::RectangleLight,"Area Light");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("World & Celestial"))
        {
            if (ImGui::MenuItem("Atmosphere Medium")) {}
            if (ImGui::MenuItem("Sky Atmosphere")) {}
            if (ImGui::MenuItem("Cloud Layer")) {}
            if (ImGui::MenuItem("Local Volumetric Fog")) {}
            if (ImGui::MenuItem("Wind Field")) {}
            if (ImGui::MenuItem("Rainbow")) {}
            if (ImGui::MenuItem("Lens Flare")) {}
            if (ImGui::MenuItem("Moon / Satellite")) {}
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Cameras"))
        {
            if (ImGui::MenuItem("Main Camera")) {}
            if (ImGui::MenuItem("Cine Camera (35mm)")) {}
            if (ImGui::MenuItem("Cine Camera (50mm)")) {}
            if (ImGui::MenuItem("Cine Camera (85mm)")) {}
            if (ImGui::MenuItem("Post Process Volume")) {}
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Geometry Primitives"))
        {
            if (ImGui::MenuItem("Plane / Ground")) {}
            if (ImGui::MenuItem("Cube / Box")) {}
            if (ImGui::MenuItem("Sphere")) {}
            if (ImGui::MenuItem("Cylinder")) {}
            if (ImGui::MenuItem("Cone")) {}
            if (ImGui::MenuItem("Torus")) {}
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Authoring Tools"))
        {
            if (ImGui::MenuItem("Open in SolidArc") && !OpenSolidArcWindow())
                SolidArcFailed = true;
            ImGui::EndMenu();
        }
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    if (SolidArcFailed) ImGui::OpenPopup("SolidArc unavailable");
    if (ImGui::BeginPopupModal("SolidArc unavailable", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Build Frontier Release with SolidArc and keep SolidArc/ beside Frontier.exe.");
        if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
        X = AddX + AddW;
    }

    // ══ CENTRE ════════════════════════════════════════════════════════════════════════════════════════════
    const float RightX  = Cursor.x + RowWidth - RightW;
    float       CentreX = Cursor.x + (RowWidth - CentreW) * 0.5f;
    CentreX = std::max(CentreX, X + 16.0f);
    CentreX = std::min(CentreX, RightX - CentreW - 16.0f);

    {
        // The mode pill. Three segments over the existing Transport_/Paused_ state — no new state at all.
        const float PillX = CentreX;
        Draw->AddRectFilled(ImVec2(PillX, BtnY), ImVec2(PillX + ModePillW, BtnY + 26.0f), IM_COL32(18, 20, 22, 255), 13.0f);
        Draw->AddRect(ImVec2(PillX, BtnY), ImVec2(PillX + ModePillW, BtnY + 26.0f), kStroke, 13.0f);
        float SegX = PillX + 2.0f;
        for (uint32_t M = 0u; M < 3u; ++M)
        {
            const bool Selected = (M == 0u && !Running) || (M == 1u && Transport_ == kSimulate) || (M == 2u && Transport_ == kPlay);
            const bool Glyphed  = (Squeeze >= 4u && !Selected && M > 0u);
            const float SegW    = ModeW[M];
            ImGui::SetCursorScreenPos(ImVec2(SegX, BtnY + 2.0f));
            char SegId[12] = {};
            std::snprintf(SegId, sizeof(SegId), "##mode%u", M);
            ImGui::InvisibleButton(SegId, ImVec2(SegW, 22.0f));
            const bool Hot = ImGui::IsItemHovered();
            if (Hot)
            {
                ImGui::SetTooltip("%s", M == 0u ? "Edit \xe2\x80\x94 the editor owns the world  (Esc)"
                                      : M == 1u ? "Simulate \xe2\x80\x94 run the world, keep the editor camera  (Alt S)"
                                                : "Play \xe2\x80\x94 run the world through a scene camera  (Alt P)");
                if (ImGui::IsMouseClicked(0))
                {
                    if (M == 0u)      SetTransport(kEdit);
                    else if (M == 1u) SetTransport(Transport_ == kSimulate ? kEdit : kSimulate);
                    else              SetTransport(Transport_ == kPlay ? kEdit : kPlay);
                }
            }
            ImU32 SegBg = IM_COL32(0, 0, 0, 0), SegInk = kDim;
            if (Selected && Paused_ && M > 0u)      { SegBg = IM_COL32(245, 158, 11, 56); SegInk = IM_COL32(246, 198, 106, 255); }
            else if (Selected && M == 2u)           { SegBg = IM_COL32(34, 197, 94, 56);  SegInk = IM_COL32(126, 231, 165, 255); }
            else if (Selected && M == 1u)           { SegBg = IM_COL32(108, 119, 255, 56); SegInk = IM_COL32(174, 180, 255, 255); }
            else if (Selected)                      { SegBg = kStrong; SegInk = kText; }
            else if (Hot)                           { SegBg = kHover;  SegInk = kText; }
            if (SegBg != IM_COL32(0, 0, 0, 0))
                Draw->AddRectFilled(ImVec2(SegX, BtnY + 2.0f), ImVec2(SegX + SegW, BtnY + 24.0f), SegBg, 11.0f);
            float TextX = SegX + 12.0f;
            if (Glyphed)
            {
                // The tightest rail keeps the two runs as their own glyphs — the same play / simulate marks
                //    the old five-button strip drew, so nothing has to be re-learned.
                RunGlyph(M == 1u ? 1u : 0u, Draw, ImVec2(SegX + SegW * 0.5f, BtnY + 13.0f), 12.0f, SegInk);
            }
            else if (M > 0u)
            {
                Draw->AddCircleFilled(ImVec2(SegX + 11.0f, BtnY + 13.0f), 3.0f,
                    Selected ? SegInk : IM_COL32(92, 92, 92, 255));
                TextX = SegX + 20.0f;
            }
            if (!Glyphed)
            {
                ImGui::PushFont(Small);
                const float CapH = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, ModeLabels[M]).y;
                ImGui::PopFont();
                SpacedCaps(Draw, Small, ModeLabels[M], ImVec2(TextX, BtnY + (26.0f - CapH) * 0.5f), SegInk, 1.1f);
            }
            SegX += SegW + 2.0f;
        }

        // Pause / step / stop — only while something runs, and eased in with the pill's own width.
        if (TransportOpen_ > 0.002f)
        {
            Draw->PushClipRect(ImVec2(PillX + ModePillW, BtnY - 2.0f),
                               ImVec2(PillX + ModePillW + TransportW * TransportOpen_ + 1.0f, BtnY + 28.0f), true);
            float TX = PillX + ModePillW + 4.0f;
            Draw->AddLine(ImVec2(TX + 3.0f, BtnY + 6.0f), ImVec2(TX + 3.0f, BtnY + 20.0f), kStroke);
            TX += 11.0f;
            struct Run { const char* Id; uint32_t Icon; const char* Tip; };
            static constexpr Run kRuns[3] = {
                { "##tpause", 2u, "Pause / resume  (P)" },
                { "##tstep",  3u, "Advance one frame  (.)" },
                { "##tstop",  4u, "Stop \xe2\x80\x94 restore the editor  (Esc)" },
            };
            const bool Arriving = TransportOpen_ < 0.6f;   // half-drawn buttons do not take clicks
            for (uint32_t i = 0u; i < 3u; ++i)
            {
                const bool Disabled = Arriving || (i == 1u && !Paused_) || (i != 1u && !Running);
                ImGui::SetCursorScreenPos(ImVec2(TX, BtnY));
                ImGui::InvisibleButton(kRuns[i].Id, ImVec2(28.0f, 26.0f));
                const bool Hot = ImGui::IsItemHovered() && !Disabled;
                if (Hot)
                {
                    ImGui::SetTooltip("%s", kRuns[i].Tip);
                    if (ImGui::IsMouseClicked(0))
                    {
                        if (i == 0u)      SetPaused(!Paused_);
                        else if (i == 1u) StepOnce();
                        else              SetTransport(kEdit);
                    }
                }
                ImU32 Bg = IM_COL32(0, 0, 0, 0), Ink = kDim;
                if (Disabled)                 Ink = IM_COL32(136, 136, 136, 70);
                else if (i == 0u && Paused_)  { Bg = kAmber; Ink = IM_COL32(26, 18, 4, 255); }
                else if (Hot && i == 2u)      { Bg = IM_COL32(239, 68, 68, 41); Ink = IM_COL32(255, 155, 155, 255); }
                else if (Hot)                 { Bg = kHover; Ink = kText; }
                if (Bg != IM_COL32(0, 0, 0, 0))
                    Draw->AddRectFilled(ImVec2(TX, BtnY), ImVec2(TX + 28.0f, BtnY + 26.0f), Bg, 13.0f);
                RunGlyph(i == 0u && Paused_ ? 0u : kRuns[i].Icon, Draw, ImVec2(TX + 14.0f, BtnY + 13.0f), 13.0f, Ink);
                TX += 30.0f;
            }
            Draw->PopClipRect();
        }
    }

    // ══ RIGHT ═════════════════════════════════════════════════════════════════════════════════════════════
    X = RightX;

    // The projection pill and its menu.
    {
        bool ViewHot = false;
        const float ViewX = X;
        if (Pill("##viewbutton", ViewX, ViewW, false, 0u, kStroke, &ViewHot)) ImGui::OpenPopup("##viewmenu");
        ImGui::PushFont(Small);
        const float CapH = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "X").y;
        ImGui::PopFont();
        const char* Shown = Squeeze >= 3u ? ViewShort : ViewLabel;
        SpacedCaps(Draw, Small, Shown, ImVec2(ViewX + 12.0f, BtnY + (26.0f - CapH) * 0.5f), ViewHot ? kText : kDim, 1.1f);
        if (FovLabel[0] && Squeeze < 3u)
            SpacedCaps(Draw, Small, FovLabel,
                ImVec2(ViewX + 12.0f + SpacedCapsWidth(Small, Shown, 1.1f) + 8.0f, BtnY + (26.0f - CapH) * 0.5f), kFaint, 1.1f);
        X = ViewX;   // the views menu's popup body below anchors on X / BtnY / ViewW
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
        X = ViewX + ViewW + 4.0f;
    }

    // Markers.
    {
        bool MarkHot = false;
        if (Pill("##markers", X, MarkW, MarkersOn_, kStrong, kStrong, &MarkHot)) MarkersOn_ = !MarkersOn_;
        if (MarkHot) ImGui::SetTooltip("Volume markers in the view");
        const ImU32 Ink = MarkersOn_ ? kText : (MarkHot ? kText : kDim);
        Draw->AddCircle(ImVec2(X + 15.0f, BtnY + 13.0f), 5.0f, Ink, 16, 1.4f);
        Draw->AddCircleFilled(ImVec2(X + 15.0f, BtnY + 13.0f), 1.6f, Ink);
        if (Squeeze < 1u)
        {
            ImGui::PushFont(Small);
            const float CapH = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, "Markers").y;
            ImGui::PopFont();
            SpacedCaps(Draw, Small, "Markers", ImVec2(X + 26.0f, BtnY + (26.0f - CapH) * 0.5f), Ink, 1.1f);
        }
        X += MarkW + 6.0f;
    }

    Draw->AddLine(ImVec2(X + 3.0f, BtnY + 5.0f), ImVec2(X + 3.0f, BtnY + 21.0f), kStroke);
    X += 12.0f;

    // The status: the clock, the run and the sample count in one readout. Clicking it toggles realtime, which
    //    is what the separate Realtime pill used to do — the label already says which state that is.
    {
        bool StatusHot = false;
        ImGui::SetCursorScreenPos(ImVec2(X, BtnY));
        ImGui::InvisibleButton("##status", ImVec2(StatusW, 26.0f));
        StatusHot = ImGui::IsItemHovered();
        if (StatusHot)
        {
            ImGui::SetTooltip("%u accumulated samples \xc2\xb7 last restart: %s\n%s",
                RenderSamples_, RenderRestart_ ? RenderRestart_ : "none",
                Running ? "The world is running" : "Click to hold the clock (realtime on/off)  (Ctrl R)");
            if (ImGui::IsMouseClicked(0) && !Running) SetRealtime(!Realtime_);
        }
        if (StatusHot) Draw->AddRectFilled(ImVec2(X, BtnY), ImVec2(X + StatusW, BtnY + 26.0f), kHover, 13.0f);
        Draw->AddRect(ImVec2(X, BtnY), ImVec2(X + StatusW, BtnY + 26.0f), kStroke, 13.0f);

        const ImVec2 Led(X + 13.0f, BtnY + 13.0f);
        ImU32 LedInk = kOk, Halo = IM_COL32(34, 197, 94, 70);
        if (Paused_)        { LedInk = kAmber; Halo = IM_COL32(245, 158, 11, 70); }
        else if (Running)   { LedInk = kHi;    Halo = IM_COL32(108, 119, 255, 70); }
        else if (!Realtime_){ LedInk = IM_COL32(92, 92, 92, 255); Halo = IM_COL32(0, 0, 0, 0); }
        if (Halo != IM_COL32(0, 0, 0, 0)) Draw->AddCircleFilled(Led, 6.0f, Halo);
        Draw->AddCircleFilled(Led, 3.0f, LedInk);

        ImGui::PushFont(Small);
        const float CapH = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, StatusLabel).y;
        ImGui::PopFont();
        SpacedCaps(Draw, Small, StatusLabel, ImVec2(X + 22.0f, BtnY + (26.0f - CapH) * 0.5f), kDim, 1.1f);
        ImGui::PushFont(Mono);
        const ImVec2 NumSize = Mono->CalcTextSizeA(Mono->LegacySize, FLT_MAX, 0.0f, SampleText);
        Draw->AddText(Mono, Mono->LegacySize, ImVec2(X + StatusW - 10.0f - NumSize.x, BtnY + (26.0f - NumSize.y) * 0.5f),
            RenderSamples_ > 0u ? kText : kFaint, SampleText);
        ImGui::PopFont();
        X += StatusW + 4.0f;
    }

    // The gear: slides the Control Centre shade open and shut through the shared open figure.
    {
        const float GX = X;
        ImGui::SetCursorScreenPos(ImVec2(GX, BtnY));
        ImGui::InvisibleButton("##tsettings", ImVec2(28.0f, 26.0f));
        const bool GearHot = ImGui::IsItemHovered();
        if (GearHot)
        {
            ImGui::SetTooltip("Viewport settings");
            if (ImGui::IsMouseClicked(0)) ImGui::OpenPopup("##viewport-settings");
        }
        const bool GearOn = ImGui::IsPopupOpen("##viewport-settings");
        if (GearHot || GearOn)
            Draw->AddCircleFilled(ImVec2(GX + 14.0f, BtnY + 13.0f), 12.0f, GearHot ? kHover : IM_COL32(255, 255, 255, 16));
        const ImVec2 GearC(GX + 14.0f, BtnY + 13.0f);
        const ImU32  GearTint = (GearHot || GearOn) ? kText : kDim;
        Draw->AddCircle(GearC, 5.0f, GearTint, 24, 1.4f);
        for (uint32_t Tooth = 0u; Tooth < 8u; ++Tooth)
        {
            const float A = static_cast<float>(Tooth) * 0.7853982f;
            Draw->AddLine(ImVec2(GearC.x + 5.6f * std::cos(A), GearC.y + 5.6f * std::sin(A)),
                          ImVec2(GearC.x + 8.2f * std::cos(A), GearC.y + 8.2f * std::sin(A)), GearTint, 1.6f);
        }
        Draw->AddCircleFilled(GearC, 1.6f, GearTint);
        if (ImGui::BeginPopup("##viewport-settings"))
        {
            ImGui::TextDisabled("VIEWPORT / SETTINGS");
            if (Readout_ && Readout_->DiagnosticsOpen)
                ImGui::MenuItem("Statistics / Debug", "F3", Readout_->DiagnosticsOpen);
            ImGui::Checkbox("Realtime", &Realtime_);
            if (ShadeOpen_ && ImGui::MenuItem("Display and rendering settings")) *ShadeOpen_ = true;
            ImGui::EndPopup();
        }

    }

    ImGui::SetCursorScreenPos(ImVec2(Cursor.x, Cursor.y + kBarHeight + kHairlineHeight));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          THE VIEW
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::RecordView() noexcept
{
    CadContact_ = {};
    const float RowWidth = ImGui::GetContentRegionAvail().x;
    // The view reaches exactly the foot's top row, taking the room the shut console leaves.
    const float ViewH =
        Controls_->QueryFootTop() - ImGui::GetCursorScreenPos().y - (ConsoleOpen_ ? kConsoleH : 0.0f);
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
    {
        const float UMax = (StorageW_ > 0u && ViewW_ > 0u) ? std::min(1.0f, static_cast<float>(ViewW_) / static_cast<float>(StorageW_)) : 1.0f;
        const float VMax = (StorageH_ > 0u && ViewH_ > 0u) ? std::min(1.0f, static_cast<float>(ViewH_) / static_cast<float>(StorageH_)) : 1.0f;
        Draw->AddImage(ViewTexture_, Min, Max, ImVec2(0.0f, 0.0f), ImVec2(UMax, VMax));
    }
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
    // SolidArc is a CAD viewport: it carries no orbit compass in its corner, so nothing here may take its clicks.
    bool OrbHover = false;
    float Gf[3], Gr[3], Gu[3];
    OrbitBasis(Orbit_.Yaw, Orbit_.Pitch, Gf, Gr, Gu); // the pan below reads this basis whether or not the compass is drawn
    if (Chrome_ != ViewportPanelChrome::SolidArcCad)
    {
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
        OrbHover = ImGui::IsItemHovered();
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
    }

    const bool BillboardHover=MarkersOn_?Billboards.Draw(Draw,Min,Max,!OrbHover&&!OrbHeld_&&!CanvasDragging_):(Billboards.ClearFrame(),false);

    // The shared panel owns only pointer capture; the SolidArc host owns the camera and gizmo geometry.
    if (Chrome_ == ViewportPanelChrome::SolidArcCad)
    {
        const ImGuiIO& Contact = ImGui::GetIO();
        const bool CanvasHover = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(Min, Max) && !BillboardHover;
        CadContact_.Hover = CanvasHover;
        const ImVec2 Pointer = Contact.MousePos;
        const float SpanX = std::max(1.0f, Max.x - Min.x);
        const float SpanY = std::max(1.0f, Max.y - Min.y);
        CadContact_.U = std::clamp((Pointer.x - Min.x) / SpanX, 0.0f, 1.0f);
        CadContact_.V = std::clamp((Pointer.y - Min.y) / SpanY, 0.0f, 1.0f);
        CadContact_.Snap = Contact.KeyCtrl || Contact.KeySuper;
        if (CanvasHover && !CanvasDragging_ &&
            (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1) || ImGui::IsMouseClicked(2)))
        {
            CanvasDragging_ = true;
            PressLeft_ = ImGui::IsMouseClicked(0) && !ImGui::IsMouseClicked(1) && !ImGui::IsMouseClicked(2);
            PressMoved_ = false;
            CadPan_ = ImGui::IsMouseClicked(1) || ImGui::IsMouseClicked(2) || (PressLeft_ && Contact.KeyShift && !Contact.KeyCtrl);
            CadOrbit_ = PressLeft_ && Contact.KeyAlt && !Contact.KeyShift;
            PressBox_ = PressLeft_ && Contact.KeyCtrl && !Contact.KeyAlt;
            PressX_ = Pointer.x;
            PressY_ = Pointer.y;
            CadContact_.Start = true;
        }
        if (CanvasDragging_)
        {
            CadContact_.Left = PressLeft_;
            CadContact_.Pan = CadPan_;
            CadContact_.Orbit = CadOrbit_;
            CadContact_.Box = PressBox_;
            const float DragX = Pointer.x - PressX_;
            const float DragY = Pointer.y - PressY_;
            if (DragX * DragX + DragY * DragY > 16.0f) PressMoved_ = true;
            CadContact_.Travelled = PressMoved_;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                CadContact_.Cancel = true;
                CanvasDragging_ = false;
                PressLeft_ = false;
                PressBox_ = false;
            }
            else if (!ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(1) && !ImGui::IsMouseDown(2))
            {
                CadContact_.End = true;
                CanvasDragging_ = false;
                if (PressLeft_ && PressBox_ && PressMoved_)
                {
                    BoxLive_ = true;
                    BoxExtend_ = Contact.KeyShift;
                    BoxSubtract_ = Contact.KeyAlt;
                    BoxU0_ = std::clamp((std::min(PressX_, Pointer.x) - Min.x) / SpanX, 0.0f, 1.0f);
                    BoxU1_ = std::clamp((std::max(PressX_, Pointer.x) - Min.x) / SpanX, 0.0f, 1.0f);
                    BoxV0_ = std::clamp((std::min(PressY_, Pointer.y) - Min.y) / SpanY, 0.0f, 1.0f);
                    BoxV1_ = std::clamp((std::max(PressY_, Pointer.y) - Min.y) / SpanY, 0.0f, 1.0f);
                }
                else if (PressLeft_ && !PressMoved_ && !CadOrbit_)
                {
                    TapLive_ = true;
                    TapAdditive_ = Contact.KeyShift || Contact.KeyCtrl;
                    TapU_ = std::clamp((PressX_ - Min.x) / SpanX, 0.0f, 1.0f);
                    TapV_ = std::clamp((PressY_ - Min.y) / SpanY, 0.0f, 1.0f);
                }
                PressLeft_ = false;
                PressBox_ = false;
            }
            else if (!CadContact_.Start)
            {
                CadContact_.DeltaX = Contact.MouseDelta.x;
                CadContact_.DeltaY = Contact.MouseDelta.y;
                CadContact_.Move = PressMoved_ && (Contact.MouseDelta.x != 0.0f || Contact.MouseDelta.y != 0.0f);
                if (PressBox_ && PressMoved_)
                {
                    const ImVec2 CornerA(std::max(Min.x, std::min(PressX_, Pointer.x)), std::max(Min.y, std::min(PressY_, Pointer.y)));
                    const ImVec2 CornerB(std::min(Max.x, std::max(PressX_, Pointer.x)), std::min(Max.y, std::max(PressY_, Pointer.y)));
                    Draw->AddRectFilled(CornerA, CornerB, IM_COL32(255, 180, 84, 34));
                    Draw->AddRect(CornerA, CornerB, IM_COL32(255, 180, 84, 220));
                }
            }
        }
        if (CanvasHover) CadContact_.Wheel = Contact.MouseWheel;
    }
    else if (ImGui::IsMouseHoveringRect(Min, Max) && ImGui::GetIO().MouseWheel != 0.0f)
    {
        Orbit_.Distance *= ImGui::GetIO().MouseWheel > 0.0f ? 0.88f : 1.13f;
        if (Orbit_.Distance < 0.2f) Orbit_.Distance = 0.2f;
        if (Orbit_.Distance > 120.0f) Orbit_.Distance = 120.0f;
        ++Orbit_.Revision;
    }

    // The view's pointer, published for the game: the live aim every hovered tick, and the tap on the click
    //    that lands on the scene itself — never on the compass, and never while another widget holds the
    //    pointer. Fractions rather than pixels, so the game maps them onto whatever it renders at.
    AimLive_ = false;
    {
        const ImVec2 Mouse   = ImGui::GetIO().MousePos;
        const float  Across  = Max.x - Min.x;
        const float  Down    = Max.y - Min.y;
        const bool   OverView = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(Min, Max) && !OrbHover && !OrbHeld_ && !BillboardHover;
        if (OverView && Across > 1.0f && Down > 1.0f)
        {
            AimLive_ = true;
            AimU_    = (Mouse.x - Min.x) / Across;
            AimV_    = (Mouse.y - Min.y) / Down;
            if (Chrome_ == ViewportPanelChrome::FrontierGame && ImGui::IsMouseClicked(0))
            {
                TapLive_     = true;
                TapAdditive_ = ImGui::GetIO().KeyShift;
                TapU_        = AimU_;
                TapV_        = AimV_;
            }
        }
    }

    LastX_ = Min.x;
    LastY_ = Min.y;
    LastW_ = Max.x - Min.x;
    LastH_ = Max.y - Min.y;
    ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE VIEW'S POINTER
//------------------------------------------------------------------------------------------------------------------------

bool ViewportPanel::QueryViewTap(float* AcrossU, float* DownV, bool* Additive) noexcept
{
    if (!TapLive_)
        return false;
    TapLive_ = false;
    if (AcrossU)  *AcrossU  = TapU_;
    if (DownV)    *DownV    = TapV_;
    if (Additive) *Additive = TapAdditive_;
    return true;
}

bool ViewportPanel::QueryViewBox(float* U0, float* V0, float* U1, float* V1, bool* Extend, bool* Subtract) noexcept
{
    if (!BoxLive_)
        return false;
    BoxLive_ = false;
    if (U0) *U0 = BoxU0_;
    if (V0) *V0 = BoxV0_;
    if (U1) *U1 = BoxU1_;
    if (V1) *V1 = BoxV1_;
    if (Extend)   *Extend   = BoxExtend_;
    if (Subtract) *Subtract = BoxSubtract_;
    return true;
}

bool ViewportPanel::QueryViewAim(float* AcrossU, float* DownV) const noexcept
{
    if (!AimLive_)
        return false;
    if (AcrossU) *AcrossU = AimU_;
    if (DownV)   *DownV   = AimV_;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       COMMAND LINE
//------------------------------------------------------------------------------------------------------------------------

void ViewportPanel::RecordCommand(EditorInstance* Instances, uint32_t InstanceCount) noexcept
{
    if ((ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper) && ImGui::IsKeyPressed(ImGuiKey_K, false))
    {
        // Ctrl+K raises the console and lands the caret; a second chord sends it home.
        ConsoleOpen_  = !ConsoleOpen_;
        FocusCommand_ = ConsoleOpen_;
        SugShut_      = false;
    }
    if (!ConsoleOpen_)
    {
        return;
    }

    const float RowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(RowWidth, kConsoleH));
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Ui    = Controls_->QueryUi();
    ImFont*     Small = Controls_->QuerySmall();
    const ImVec2 WinPos  = ImGui::GetWindowPos();
    const ImVec2 WinSize = ImGui::GetWindowSize();
    const float  WinX    = WinPos.x;
    const float  WinX1   = WinPos.x + WinSize.x;
    Draw->AddRectFilled(ImVec2(WinX, Min.y), ImVec2(WinX1, Max.y), IM_COL32(0, 0, 0, 255));
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
    const ImVec2 KbdMin(RunMin.x - 10.0f - KbdSize.x, Min.y + (kConsoleH - KbdSize.y) * 0.5f);
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
        Draw->AddText(ImVec2(FieldX1 + 10.0f, Min.y + (kConsoleH - EchoGlyph.y) * 0.5f), kDim, CommandEcho_);
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
            IM_COL32(0, 0, 0, 255), 18.0f);
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
    // Pinned to the sill: whatever the content above ends at, the foot opens on the shared top row.
    const float FootTop = Controls_->QueryFootTop();
    if (ImGui::GetCursorScreenPos().y < FootTop)
    {
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, FootTop));
    }
    ImGui::Dummy(ImVec2(RowWidth, kEditorFooterH));
    const ImVec2 Cursor = ImGui::GetItemRectMin();

    ImDrawList* Draw  = ImGui::GetWindowDrawList();
    ImFont*     Small = Controls_->QuerySmall();
    const ImVec2 FootPos = ImGui::GetWindowPos();
    const ImVec2 FootSize = ImGui::GetWindowSize();
    const float  FootX0 = FootPos.x;
    const float  FootX1 = FootPos.x + FootSize.x;
    // The footer sits on the panel's own sill: its hem runs edge to edge and its wash pours exactly
    //    the shared forty, no further — the three panels' hems draw one unbroken line.
    Draw->AddRectFilled(ImVec2(FootX0, Cursor.y), ImVec2(FootX1, Cursor.y + kEditorFooterH), kWash);
    Draw->AddLine(ImVec2(FootX0, Cursor.y), ImVec2(FootX1, Cursor.y), kStroke);

    // The counters. The triangle figure rides the readout the project seats; unseated it keeps the
    //    reference's own resting dash, and physics and isolation hide at zero, under the show rule.
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
    const uint32_t TriTotal = (Readout_ != nullptr) ? Readout_->Triangles : 0u;
    char             TrisText[16] = {};
    const char*      TrisFig = Dash;
    if (TriTotal > 0u)
    {
        std::snprintf(TrisText, sizeof(TrisText), "%u", TriTotal);
        TrisFig = TrisText;
    }
    const EditorFpsBand FpsBand = EditorFpsBandFor(Fps);
    const Counter Counters[4] = {
        { "FPS", PerfText, PerfSub, FpsBand == EditorFpsBand::Poor, false, false },
        { "TRIS", TrisFig, "", false, false, false },
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
        if (Cell.Warn)
        {
            W += 13.0f;   // the warning triangle and its air
        }
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
    const float FootH   = kEditorFooterH;
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
        float FigX = X + LabelW + 6.0f;
        if (Counters[i].Warn)
        {
            FootWarn(Draw, ImVec2(FigX, MidY - 5.0f), 10.0f, kAmber);
            FigX += 13.0f;
        }
        const ImU32 TextTint = Counters[i].Warn ? kAmber
            : ((i == 0u && FpsBand == EditorFpsBand::Good) ? kGreen : (Counters[i].Dim ? kDim : kText));
        ImGui::PushFont(Small);
        Draw->AddText(ImVec2(FigX, TextY), TextTint, Counters[i].Text);
        const float TextW = Small->CalcTextSizeA(Small->LegacySize, FLT_MAX, 0.0f, Counters[i].Text).x;
        if (KeepSub && Counters[i].Sub[0] != '\0')
        {
            char Joined[48] = {};
            std::snprintf(Joined, sizeof(Joined), " \xc2\xb7 %s", Counters[i].Sub);
            Draw->AddText(ImVec2(FigX + TextW, TextY), kFaint, Joined);
        }
        ImGui::PopFont();
        X += CounterWidth(Counters[i], KeepSub) + 20.0f;
    }
}

} // namespace Frontier
