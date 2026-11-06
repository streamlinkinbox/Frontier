/*==========================================================================================================================================
                                                   FLUIDPLUMEEDITORWORKSPACE.CPP
==========================================================================================================================================*/
// 🧩 The Smoke & fire editor's layout: Outliner, viewport with HUD, Log Stream, Inspector and the libraries — the Particle editor's window, on one
//    FluidPlume record.

#include "Engine/Interface/FluidPlumeEditor/FluidPlumeEditorWorkspace.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/ControlTheme.h"
#include "Engine/Interface/Controls/GlyphSymbol.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/PhysicalDynamics/FluidDynamics/GasParameterCatalogue.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace Frontier
{

namespace
{

using namespace FluidDynamics;

constexpr float OutlinerWidth  = 292.0f;
constexpr float InspectorWidth = 404.0f;
constexpr float LogHeight      = 150.0f;
constexpr float HeaderHeight   = 54.0f;
constexpr float Gutter         = 10.0f;

constexpr ImU32 FireTint      = IM_COL32(232, 131, 58, 255);    // [-] the demo's Effects accent #e8833a
constexpr ImU32 SourceTint    = IM_COL32(232, 194, 122, 255);   // [-] amber, as the Particle editor tints an accelerating force
constexpr ImU32 GasTint       = IM_COL32(143, 211, 196, 255);   // [-] green, as it tints a flow
constexpr ImU32 AppearanceTint = IM_COL32(159, 180, 214, 255);  // [-] the demo's Render accent #9fb4d6
constexpr ImU32 SmokeSwatch   = IM_COL32(190, 196, 206, 255);

struct Look
{
    InspectorPalette Palette = InspectorPalette::Resolve();
    EditorTypefaces  Faces   = EditorTypefaces::Resolve();
    float            Scale   = 1.0f;

    ImFont* Pick(ImFont* Font) const { return Font ? Font : ImGui::GetFont(); }
    void    Text(ImDrawList* Ink, ImFont* Font, float Size, ImVec2 At, ImU32 Colour, const char* Words) const
    {
        Ink->AddText(Pick(Font), Size * Scale, At, Colour, Words);
    }
    float Measure(ImFont* Font, float Size, const char* Words) const
    {
        return Pick(Font)->CalcTextSizeA(Size * Scale, FLT_MAX, 0.0f, Words).x;
    }
};

// 📝 What a pill is wide: its words and, with a glyph, the glyph's room; a compact pill is the glyph alone.
float MeasurePillWidth(const Look& Style, const char* Label, bool WithGlyph, bool Compact)
{
    if (Compact && WithGlyph) return 36.0f * Style.Scale;
    return Style.Measure(Style.Faces.Medium, 11.0f, Label) + (WithGlyph ? 34.0f : 22.0f) * Style.Scale;
}

// 📝 A rounded pill button that remembers where it lay; true when pressed. A compact pill shows its glyph alone and names itself in a tooltip.
bool ConstructPillButton(const Look& Style, ImDrawList* Ink, std::vector<FluidPlumeControlRectangle>& Registry, const char* Name, ImVec2 At, const char* Label,
                         bool Active, const char* Glyph = nullptr, float* OutWidth = nullptr, bool Compact = false)
{
    const float Height = 30.0f * Style.Scale;
    const float Width  = MeasurePillWidth(Style, Label, Glyph != nullptr, Compact);
    ImGui::SetCursorScreenPos(At);
    char Id[96];
    std::snprintf(Id, sizeof(Id), "##Plume%s", Name);
    const bool Pressed = ImGui::InvisibleButton(Id, ImVec2(Width, Height));
    const bool Hovered = ImGui::IsItemHovered();
    if (Compact && Glyph != nullptr && Hovered) ImGui::SetTooltip("%s", Label);
    Registry.push_back({ Name, At, ImVec2(At.x + Width, At.y + Height) });
    Ink->AddRectFilled(At, ImVec2(At.x + Width, At.y + Height), Active ? Style.Palette.SegmentedActiveBg : Hovered ? Style.Palette.RowHoverBg : Style.Palette.CountCardBg,
                       9.0f * Style.Scale);
    Ink->AddRect(At, ImVec2(At.x + Width, At.y + Height), Style.Palette.InsetFieldBorder, 9.0f * Style.Scale);
    if (Compact && Glyph != nullptr)
    {
        GlyphSymbol::Construct(Ink, Glyph, ImVec2(At.x + 11.0f * Style.Scale, At.y + 8.0f * Style.Scale), 14.0f * Style.Scale, 1.4f,
                               Active ? Style.Palette.TextPrimary : Style.Palette.TextSecondary);
        if (OutWidth) *OutWidth = Width;
        return Pressed;
    }
    float Left = At.x + 11.0f * Style.Scale;
    if (Glyph)
    {
        GlyphSymbol::Construct(Ink, Glyph, ImVec2(Left, At.y + 8.0f * Style.Scale), 14.0f * Style.Scale, 1.4f, Style.Palette.TextSecondary);
        Left += 19.0f * Style.Scale;
    }
    Style.Text(Ink, Style.Faces.Medium, 11.0f, ImVec2(Left, At.y + 8.5f * Style.Scale), Active ? Style.Palette.TextPrimary : Style.Palette.TextSecondary, Label);
    if (OutWidth) *OutWidth = Width;
    return Pressed;
}

std::string ResolveClock()
{
    const std::time_t Now = std::time(nullptr);
    std::tm           Local{};
#if defined(_WIN32)
    localtime_s(&Local, &Now);
#else
    localtime_r(&Now, &Local);
#endif
    char Text[16];
    std::snprintf(Text, sizeof(Text), "%02d:%02d:%02d", Local.tm_hour, Local.tm_min, Local.tm_sec);
    return Text;
}

std::string Lowered(std::string Text)
{
    std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char Letter) { return static_cast<char>(std::tolower(Letter)); });
    return Text;
}

// 📝 Words cut to a width: the longest start that fits with "..." after it (whole UTF-8 characters), or the words whole when they fit.
std::string FitText(const Look& Style, ImFont* Font, float Size, std::string Words, float Room)
{
    if (Style.Measure(Font, Size, Words.c_str()) <= Room) return Words;
    while (!Words.empty() && Style.Measure(Font, Size, (Words + "...").c_str()) > Room)
    {
        Words.pop_back();
        while (!Words.empty() && (static_cast<unsigned char>(Words.back()) & 0xC0u) == 0x80u) Words.pop_back();   // [-] a continuation byte belongs to the character before it
        if (!Words.empty() && static_cast<unsigned char>(Words.back()) >= 0xC0u) Words.pop_back();
    }
    return Words + "...";
}

const char* ResolveGroupGlyph(const std::string& Group)
{
    if (Group == "Fire") return "Flame";
    if (Group == "Wind") return "Wind";
    return "CloudFog";
}

ImU32 ResolveGroupTint(const std::string& Group)
{
    if (Group == "Fire") return FireTint;
    if (Group == "Wind") return GasTint;
    return SmokeSwatch;
}

const char* ResolveEmitterName(uint32_t Emitter)
{
    return Emitter == 0 ? "Off" : Emitter == 1 ? "Smoke plume" : "Fire";
}

std::string ResolveGradeName(uint32_t Grade)
{
    const auto& Grades = QueryPlumeGrades();
    return std::string(Grades[std::min<size_t>(Grade, Grades.size() - 1)].Name);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    STATE
//------------------------------------------------------------------------------------------------------------------------

FluidPlumeEditorWorkspace::FluidPlumeEditorWorkspace()
{
    AppendLog("PLUME", "Smoke & fire editor", "the HMI prototype's page: 33 readings, six looks, four grades");
}

void FluidPlumeEditorWorkspace::AttachRecord(FluidPlumeConfiguration& InRecord, std::string InName)
{
    if (Record != &InRecord)
    {
        Record        = &InRecord;
        OpenedEmitter = Record->TestEmitter;
        AppendLog("RECORD", "Editing " + InName, std::to_string(FluidPlumeLibrary::CountDifferences(Record->Gas, ConstructGasParameters())) + " of " + std::to_string(GasParameterCount) +
                                                      " readings differ from the defaults");
        // 📝 With a viewport the window opens with the smoke plume running, so the readings can be judged as they are edited; the record's own emitter
        //    (off) comes back when the window closes.
        if (Presenter && Record->TestEmitter == 0)
        {
            Record->TestEmitter = 1;
            AppendLog("EMITTER", std::string("Test emitter: ") + ResolveEmitterName(1), "on while the editor is open");
        }
    }
    RecordName = std::move(InName);
}

void FluidPlumeEditorWorkspace::DetachRecord() noexcept
{
    if (Record != nullptr) Record->TestEmitter = OpenedEmitter;
    Record       = nullptr;
    LibraryShown = false;
    LoggedDevice = false;
    Image        = PlumeViewportImage{};
}

void FluidPlumeEditorWorkspace::AppendLog(std::string Category, std::string Text, std::string Detail, bool Warm)
{
    Log.push_back({ ResolveClock(), std::move(Category), std::move(Text), std::move(Detail), Warm });
    if (Log.size() > 200) Log.erase(Log.begin());
}

void FluidPlumeEditorWorkspace::AssignReadout(const FluidPlumeReadout& Readout)
{
    Assigned = Readout;
}

void FluidPlumeEditorWorkspace::RevokeReadout() noexcept
{
    Assigned.reset();
}

const FluidPlumeReadout* FluidPlumeEditorWorkspace::ResolveReadout() const noexcept
{
    if (Image.Solving) return &Image.Readout;
    return Assigned ? &*Assigned : nullptr;
}

const FluidPlumeReadout* FluidPlumeEditorWorkspace::QueryReadout() const noexcept
{
    return ResolveReadout();
}

std::vector<std::string> FluidPlumeEditorWorkspace::TakeHostRequests()
{
    std::vector<std::string> Taken;
    Taken.swap(HostRequests);
    return Taken;
}

const FluidPlumeControlRectangle* FluidPlumeEditorWorkspace::FindControl(std::string_view Name) const noexcept
{
    for (const FluidPlumeControlRectangle& Control : Controls)
    {
        if (Control.Name == Name) return &Control;
    }
    return nullptr;
}

bool FluidPlumeEditorWorkspace::Press(const char* Name, ImVec2 Minimum, ImVec2 Size)
{
    ImGui::SetCursorScreenPos(Minimum);
    char Id[96];
    std::snprintf(Id, sizeof(Id), "##Plume%s", Name);
    const bool Pressed = ImGui::InvisibleButton(Id, Size);
    Controls.push_back({ Name, Minimum, ImVec2(Minimum.x + Size.x, Minimum.y + Size.y) });
    return Pressed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    VERBS
//------------------------------------------------------------------------------------------------------------------------

bool FluidPlumeEditorWorkspace::Perform(std::string_view Action)
{
    const auto Colon    = Action.find(':');
    const auto Verb     = Action.substr(0, Colon);
    const auto Argument = Colon == std::string_view::npos ? std::string_view{} : Action.substr(Colon + 1);
    // 📝 The window's own view and sheet need no record; every edit of the plume does.
    if (Verb == "library")
    {
        LibraryShown = Argument != "off";
        return true;
    }
    if (Verb == "library-tab")
    {
        if (Argument != "looks" && Argument != "grades") return false;
        LibraryTab = std::string(Argument);
        return true;
    }
    if (Verb == "library-group")
    {
        LibraryGroup = Argument.empty() ? std::string("All") : std::string(Argument);
        return true;
    }
    if (Verb == "library-search")
    {
        std::snprintf(LibraryQuery, sizeof(LibraryQuery), "%s", std::string(Argument).c_str());
        return true;
    }
    if (Verb == "pause" || Verb == "play")
    {
        Playing = Verb == "play";
        return true;
    }
    if (Verb == "camera")
    {
        double NewYaw = Yaw, NewPitch = Pitch, NewDistance = Distance;
        if (std::sscanf(std::string(Argument).c_str(), "%lf,%lf,%lf", &NewYaw, &NewPitch, &NewDistance) < 2) return false;
        Yaw      = NewYaw;
        Pitch    = std::clamp(NewPitch, -0.1, 1.5);
        Distance = std::clamp(NewDistance, 3.0, 40.0);
        return true;
    }
    if (Verb == "close")
    {
        LibraryShown = false;
        return true;
    }
    if (Verb == "select")
    {
        FluidPlumePage Page;
        if (!FluidPlumePages::ResolvePage(Argument, Page)) return false;
        Selected = std::string(Argument);
        return true;
    }
    if (Record == nullptr) return false;
    FluidPlumeContext Context{ *Record, ResolveReadout(), Requests };
    if (Verb == "look")
    {
        const FluidPlumeLook* Look = FluidPlumeLibrary::FindLook(Argument);
        if (Look == nullptr) return false;
        FluidPlumePages::ApplyLook(Context, *Look);
        LibraryShown = false;
        return true;
    }
    if (Verb == "defaults")
    {
        FluidPlumePages::RestoreDefaults(Context);
        return true;
    }
    if (Verb == "enabled")
    {
        const bool Wanted = Argument == "on" ? true : Argument == "off" ? false : !Record->Enabled;
        if (Wanted != Record->Enabled) FluidPlumePages::ToggleEnabled(Context);
        return true;
    }
    if (Verb == "emitter")
    {
        if (Argument == "toggle") FluidPlumePages::ToggleEmitter(Context);
        else if (Argument == "cycle") FluidPlumePages::ChooseEmitter(Context, (Record->TestEmitter + 1u) % 3u);
        else if (Argument == "off") FluidPlumePages::ChooseEmitter(Context, 0u);
        else if (Argument == "smoke") FluidPlumePages::ChooseEmitter(Context, 1u);
        else if (Argument == "fire") FluidPlumePages::ChooseEmitter(Context, 2u);
        else return false;
        return true;
    }
    if (Verb == "clear-smoke")
    {
        // 📝 A presenter carries the clear on its next refresh; a host without one takes it from TakeHostRequests.
        ClearPending = true;
        if (!Presenter)
        {
            HostRequests.push_back("clear-smoke");
            if (HostRequests.size() > 16) HostRequests.erase(HostRequests.begin());
        }
        AppendLog("SOLVER", "Smoke cleared", ResolveReadout() == nullptr ? "no solver is attached" : "the lattices were emptied", true);
        return true;
    }
    // 📝 What a page or the Outliner changed leaves a notice, so the Log Stream says it once, whoever made the edit.
    if (Verb == "noted")
    {
        const auto        Next = Argument.find(':');
        const auto        Kind = Argument.substr(0, Next);
        const std::string Rest = Next == std::string_view::npos ? std::string() : std::string(Argument.substr(Next + 1));
        if (Kind == "look")
        {
            const FluidPlumeLook* Look = FluidPlumeLibrary::FindLook(Rest);
            if (Look == nullptr) return false;
            AppendLog("LOOK", "Applied " + Look->Name, std::to_string(Look->Differences) + " of " + std::to_string(GasParameterCount) + " readings differ from the defaults");
            return true;
        }
        if (Kind == "defaults")
        {
            AppendLog("RECORD", "Defaults restored", "all " + std::to_string(GasParameterCount) + " readings");
            return true;
        }
        if (Kind == "enabled")
        {
            AppendLog("RECORD", Rest == "on" ? "Plume enabled" : "Plume disabled", RecordName, Rest != "on");
            return true;
        }
        if (Kind == "emitter")
        {
            AppendLog("EMITTER", std::string("Test emitter: ") + ResolveEmitterName(static_cast<uint32_t>(std::atoi(Rest.c_str()))), "at the middle of the stage");
            return true;
        }
        return false;
    }
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    REFRESH
//------------------------------------------------------------------------------------------------------------------------

void FluidPlumeEditorWorkspace::Present(uint32_t Width, uint32_t Height, double DeltaSeconds)
{
    if (DeltaSeconds > 1.0e-6) RefreshRate += (std::min(1.0 / DeltaSeconds, 240.0) - RefreshRate) * 0.1;
    // 📝 Time runs only while the stage plays, and a stalled refresh (a window dragged, a breakpoint) is cut to a tenth of a second.
    const double Elapsed = Playing ? std::clamp(DeltaSeconds, 0.0, 0.1) : 0.0;
    StageSeconds += Elapsed;
    if (!Presenter)
    {
        ClearPending  = false;   // [-] a clear with nobody to carry it out is for the host to take, not for a presenter that comes later
        Image.Failure = "The plume viewport needs the editor's Vulkan device.";
        return;
    }
    PlumeViewportRequest Request;
    Request.Gas         = &Record->Gas;
    Request.Enabled     = Record->Enabled;
    Request.TestEmitter = Record->TestEmitter;
    Request.Seconds     = Elapsed;
    Request.Clear       = ClearPending;
    Request.Extent      = { Width, Height };
    Request.Yaw         = Yaw;
    Request.Pitch       = Pitch;
    Request.Distance    = Distance;
    ClearPending        = false;
    LastRequest         = Request;
    Image               = Presenter(Request);
    if (!Image.Adapter.empty() && !LoggedDevice)
    {
        LoggedDevice = true;
        char Detail[96];
        std::snprintf(Detail, sizeof(Detail), "%s grade \xC2\xB7 %s lattice", ResolveGradeName(Image.Readout.Grade).c_str(),
                      FluidPlumePages::FormatLatticeExtent(Image.Readout.Grade).c_str());
        AppendLog("PLUME", "Vulkan ready \xC2\xB7 " + Image.Adapter, Detail);
    }
}

void FluidPlumeEditorWorkspace::Construct(ImVec2 Minimum, ImVec2 Maximum, float Scale, double DeltaSeconds)
{
    ImDrawList* Ink = ImGui::GetWindowDrawList();
    Controls.clear();
    if (Record == nullptr)
    {
        const Look Style{ InspectorPalette::Resolve(), EditorTypefaces::Resolve(), Scale };
        Style.Text(Ink, Style.Faces.Medium, 13.0f, ImVec2(Minimum.x + 24.0f, Minimum.y + 24.0f), Style.Palette.WarmAmber, "No FluidPlume record is attached to the editor.");
        return;
    }
    const float  Gap      = Gutter * Scale;
    const ImVec2 LeftMax(Minimum.x + OutlinerWidth * Scale, Maximum.y);
    const ImVec2 RightMin(Maximum.x - InspectorWidth * Scale, Minimum.y);
    const ImVec2 CentreMin(LeftMax.x + Gap, Minimum.y);
    const ImVec2 CentreMax(RightMin.x - Gap, Maximum.y - LogHeight * Scale - Gap);
    const ImVec2 LogMin(CentreMin.x, CentreMax.y + Gap);
    const ImVec2 LogMax(CentreMax.x, Maximum.y);

    // 📝 The image follows the stage's pixels, at the window's framebuffer scale.
    StageMinimum          = ImVec2(CentreMin.x + 8.0f * Scale, CentreMin.y + HeaderHeight * Scale);
    StageMaximum          = ImVec2(CentreMax.x - 8.0f * Scale, CentreMax.y - 26.0f * Scale);
    const float    Density = ImGui::GetWindowViewport() ? ImGui::GetWindowViewport()->FramebufferScale.y : 1.0f;
    const uint32_t Width   = static_cast<uint32_t>(std::max(64.0f, (StageMaximum.x - StageMinimum.x) * Density));
    const uint32_t Height  = static_cast<uint32_t>(std::max(64.0f, (StageMaximum.y - StageMinimum.y) * Density));
    Present(Width, Height, DeltaSeconds);

    ConstructOutliner(Ink, Minimum, LeftMax, Scale);
    ConstructViewport(Ink, CentreMin, CentreMax, Scale);
    ConstructLogStream(Ink, LogMin, LogMax, Scale);
    ConstructInspector(RightMin, Maximum, Scale);
    if (LibraryShown) ConstructLibrarySheet(Minimum, Maximum, Scale);

    // 📝 Space pauses, E turns the test emitter (off, smoke, fire), C clears the smoke, L shows the libraries, Escape closes them.
    if (!ImGui::GetIO().WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) Requests.push_back(Playing ? "pause" : "play");
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) Requests.push_back("emitter:cycle");
        if (ImGui::IsKeyPressed(ImGuiKey_C, false)) Requests.push_back("clear-smoke");
        if (ImGui::IsKeyPressed(ImGuiKey_L, false)) Requests.push_back(LibraryShown ? "library:off" : "library:on");
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && LibraryShown) LibraryShown = false;
    }
    // 📝 An edit leaves its notice in the queue; two rounds settle a verb and the notice it raises.
    for (int Round = 0; Round < 3 && !Requests.empty(); ++Round)
    {
        std::vector<std::string> Pending;
        Pending.swap(Requests);
        for (const std::string& Action : Pending) Perform(Action);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    OUTLINER
//------------------------------------------------------------------------------------------------------------------------

void FluidPlumeEditorWorkspace::ConstructOutliner(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, float Scale)
{
    const Look Style{ InspectorPalette::Resolve(), EditorTypefaces::Resolve(), Scale };
    const auto& P = Style.Palette;
    Ink->AddRectFilled(Minimum, Maximum, P.ColumnBg, 14.0f * Scale);
    Ink->AddRect(Minimum, Maximum, P.EdgeBorder, 14.0f * Scale);
    float       Top  = Minimum.y + 16.0f * Scale;
    const float Left = Minimum.x + 16.0f * Scale, Right = Maximum.x - 16.0f * Scale;
    Style.Text(Ink, Style.Faces.Medium, 22.0f, ImVec2(Left, Top), P.TextPrimary, "Outliner");
    char Subtitle[96];
    std::snprintf(Subtitle, sizeof(Subtitle), "%s \xC2\xB7 %zu records", RecordName.c_str(), FluidPlumePageCount);
    Style.Text(Ink, Style.Faces.Regular, 11.0f, ImVec2(Left, Top + 30.0f * Scale), P.TextSecondary, Subtitle);
    Top += 56.0f * Scale;

    // 📝 The two count cards: the emitters the running plume counts, and the readings edited from their defaults.
    const FluidPlumeReadout* Readout = ResolveReadout();
    const uint32_t           Edited  = FluidPlumeLibrary::CountDifferences(Record->Gas, ConstructGasParameters());
    const float              CardW   = (Right - Left - 8.0f * Scale) / 2.0f;
    for (int Card = 0; Card < 2; ++Card)
    {
        const ImVec2 Corner(Left + Card * (CardW + 8.0f * Scale), Top);
        Ink->AddRectFilled(Corner, ImVec2(Corner.x + CardW, Corner.y + 58.0f * Scale), P.CountCardBg, 12.0f * Scale);
        Ink->AddRect(Corner, ImVec2(Corner.x + CardW, Corner.y + 58.0f * Scale), P.CountCardBorder, 12.0f * Scale);
        GlyphSymbol::Construct(Ink, Card == 0 ? "Eye" : "SlidersHorizontal", ImVec2(Corner.x + 12.0f * Scale, Corner.y + 10.0f * Scale), 16.0f * Scale, 1.4f,
                               Card == 0 ? (Readout != nullptr ? P.LiveGreen : P.TextMuted) : P.WarmAmber);
        Style.Text(Ink, Style.Faces.Regular, 10.5f, ImVec2(Corner.x + 12.0f * Scale, Corner.y + 36.0f * Scale), P.TextSecondary, Card == 0 ? "Live sources" : "Edited readings");
        char Figure[16];
        if (Card == 0 && Readout == nullptr) std::snprintf(Figure, sizeof(Figure), "\xE2\x80\x93");
        else std::snprintf(Figure, sizeof(Figure), "%u", Card == 0 ? Readout->Sources : Edited);
        Style.Text(Ink, Style.Faces.Light, 26.0f, ImVec2(Corner.x + CardW - Style.Measure(Style.Faces.Light, 26.0f, Figure) - 12.0f * Scale, Corner.y + 14.0f * Scale), P.TextPrimary,
                   Figure);
    }
    Top += 70.0f * Scale;
    float Width = 0.0f;
    if (ConstructPillButton(Style, Ink, Controls, "library", ImVec2(Left, Top), "Libraries", LibraryShown, "Library", &Width)) Requests.push_back("library:on");
    Top += 42.0f * Scale;

    // 📝 The tree: the domain, the sources, the gas — each row a part of the record, its figures read from the readings.
    const auto Row = [&](const char* Token, const char* Glyph, ImU32 Tint, const char* Name, const std::string& Meta, const char* EyeVerb, bool EyeOn)
    {
        const float  Height = 40.0f * Scale;
        const ImVec2 RowMin(Left - 4.0f * Scale, Top);
        const ImVec2 RowMax(Right + 4.0f * Scale, Top + Height);
        const bool   Chosen = Selected == Token;
        ImGui::SetCursorScreenPos(RowMin);
        ImGui::PushID(Token);
        const bool Pressed = ImGui::InvisibleButton("##Row", ImVec2(RowMax.x - RowMin.x - (EyeVerb ? 30.0f : 0.0f) * Scale, Height));
        const bool Hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        Controls.push_back({ std::string("row:") + Token, RowMin, RowMax });
        if (Pressed) Requests.push_back(std::string("select:") + Token);
        if (Chosen || Hovered) Ink->AddRectFilled(RowMin, RowMax, Chosen ? P.RowSelectedTop : P.RowHoverBg, 9.0f * Scale);
        if (Chosen) Ink->AddRect(RowMin, RowMax, P.RowSelectedBorder, 9.0f * Scale);
        const ImVec2 Tile(Left, Top + 7.0f * Scale);
        Ink->AddRectFilled(Tile, ImVec2(Tile.x + 26.0f * Scale, Tile.y + 26.0f * Scale), P.CountCardBg, 7.0f * Scale);
        GlyphSymbol::Construct(Ink, Glyph, ImVec2(Tile.x + 5.0f * Scale, Tile.y + 5.0f * Scale), 16.0f * Scale, 1.4f, Tint);
        Ink->PushClipRect(ImVec2(Left + 36.0f * Scale, Top), ImVec2(Right - (EyeVerb ? 26.0f : 0.0f) * Scale, Top + Height), true);
        Style.Text(Ink, Style.Faces.Medium, 12.5f, ImVec2(Left + 36.0f * Scale, Top + 5.0f * Scale), P.TextPrimary, Name);
        Style.Text(Ink, Style.Faces.Regular, 10.0f, ImVec2(Left + 36.0f * Scale, Top + 22.0f * Scale), P.TextSecondary, Meta.c_str());
        Ink->PopClipRect();
        if (EyeVerb != nullptr)
        {
            const ImVec2 EyeAt(Right - 18.0f * Scale, Top + 12.0f * Scale);
            ImGui::SetCursorScreenPos(ImVec2(EyeAt.x - 4.0f * Scale, EyeAt.y - 4.0f * Scale));
            ImGui::PushID(Token);
            if (ImGui::InvisibleButton("##Eye", ImVec2(24.0f * Scale, 24.0f * Scale))) Requests.push_back(EyeVerb);
            ImGui::PopID();
            Controls.push_back({ std::string("eye:") + Token, ImVec2(EyeAt.x - 4.0f * Scale, EyeAt.y - 4.0f * Scale), ImVec2(EyeAt.x + 20.0f * Scale, EyeAt.y + 20.0f * Scale) });
            GlyphSymbol::Construct(Ink, EyeOn ? "Eye" : "EyeOff", EyeAt, 15.0f * Scale, 1.3f, EyeOn ? P.TextSecondary : P.TextFaint);
        }
        Top += Height + 2.0f * Scale;
    };
    const auto Group = [&](const char* Title, size_t Count)
    {
        Style.Text(Ink, Style.Faces.SemiBold, 9.5f, ImVec2(Left, Top + 4.0f * Scale), P.TextMuted, Title);
        char Figure[16];
        std::snprintf(Figure, sizeof(Figure), "%zu", Count);
        Style.Text(Ink, Style.Faces.Regular, 9.5f, ImVec2(Right - Style.Measure(Style.Faces.Regular, 9.5f, Figure), Top + 4.0f * Scale), P.TextMuted, Figure);
        Top += 22.0f * Scale;
    };

    const ImVec2 ListMin(Minimum.x + 2.0f * Scale, Top);
    const ImVec2 ListMax(Maximum.x - 2.0f * Scale, Maximum.y - 40.0f * Scale);
    ImGui::SetCursorScreenPos(ListMin);
    ImGui::BeginChild("##PlumeOutlinerList", ImVec2(ListMax.x - ListMin.x, ListMax.y - ListMin.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    Ink = ImGui::GetWindowDrawList();
    Top = ImGui::GetCursorScreenPos().y;
    const GasParameters& Gas   = Record->Gas;
    const uint32_t       Grade = Readout != nullptr ? Readout->Grade : 2u;
    char                 Meta[128];
    Group("DOMAIN", 1);
    std::snprintf(Meta, sizeof(Meta), "%s \xC2\xB7 %s \xC2\xB7 %s", FluidPlumePages::FormatLatticeExtent(Grade).c_str(), ResolveGradeName(Grade).c_str(),
                  Readout == nullptr ? "no solver" : Readout->Resident ? "resident" : "idle");
    Row("Domain", "Box", FireTint, "Smoke & fire", Meta, "enabled:toggle", Record->Enabled);
    Top += 6.0f * Scale;
    Group("SOURCES", 2);
    std::snprintf(Meta, sizeof(Meta), "Emission %.2f\xC3\x97 \xC2\xB7 soot %.2f\xC3\x97", Gas.Emission, Gas.Soot);
    Row("Sources", "Droplets", SourceTint, "Tyre sources", Meta, nullptr, true);
    std::snprintf(Meta, sizeof(Meta), "%s", ResolveEmitterName(Record->TestEmitter));
    Row("Emitter", "Puff", SourceTint, "Test emitter", Meta, "emitter:toggle", Record->TestEmitter != 0);
    Top += 6.0f * Scale;
    Group("GAS", 3);
    std::snprintf(Meta, sizeof(Meta), "Buoyancy %.1f \xC2\xB7 wind %.1f m/s", Gas.Buoyancy, Gas.WindSpeed);
    Row("Simulation", "Wind", GasTint, "Simulation", Meta, nullptr, true);
    std::snprintf(Meta, sizeof(Meta), "Density %.2f\xC3\x97 \xC2\xB7 albedo %.2f", Gas.Density, Gas.Albedo);
    Row("Appearance", "CloudFog", AppearanceTint, "Appearance", Meta, nullptr, true);
    std::snprintf(Meta, sizeof(Meta), "Core %.0f K \xC2\xB7 tip %.0f K", Gas.FlameCore, Gas.FlameTip);
    Row("Fire", "Flame", FireTint, "Fire", Meta, nullptr, true);
    Top += 6.0f * Scale;
    const ImVec2 AddMin(Left - 4.0f * Scale, Top);
    const ImVec2 AddMax(Right + 4.0f * Scale, Top + 36.0f * Scale);
    if (Press("library-add", AddMin, ImVec2(AddMax.x - AddMin.x, AddMax.y - AddMin.y))) Requests.push_back("library:on");
    Ink->AddRect(AddMin, AddMax, ImGui::IsItemHovered() ? P.TextMuted : P.InsetFieldBorder, 10.0f * Scale, 0, 1.0f);
    const char* AddLabel = "+  Apply a look from the library";
    Style.Text(Ink, Style.Faces.Medium, 11.5f, ImVec2((AddMin.x + AddMax.x - Style.Measure(Style.Faces.Medium, 11.5f, AddLabel)) / 2.0f, AddMin.y + 10.0f * Scale), P.TextSecondary, AddLabel);
    ImGui::SetCursorScreenPos(ImVec2(AddMin.x, AddMax.y + 8.0f * Scale));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    ImGui::EndChild();

    Ink = ImGui::GetWindowDrawList();
    char Footer[64];
    std::snprintf(Footer, sizeof(Footer), "RECORDS %zu   \xC2\xB7   SELECTED 1", FluidPlumePageCount);
    Style.Text(Ink, Style.Faces.Monospace, 9.5f, ImVec2(Left, Maximum.y - 26.0f * Scale), P.TextMuted, Footer);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    VIEWPORT
//------------------------------------------------------------------------------------------------------------------------

void FluidPlumeEditorWorkspace::ConstructViewport(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, float Scale)
{
    const Look  Style{ InspectorPalette::Resolve(), EditorTypefaces::Resolve(), Scale };
    const auto& P = Style.Palette;
    Ink->AddRectFilled(Minimum, Maximum, P.ColumnBg, 14.0f * Scale);
    Ink->AddRect(Minimum, Maximum, P.EdgeBorder, 14.0f * Scale);

    // 📝 The bar: the title, the live pill, Pause and Clear smoke, and the test emitter's three states. It is planned from the widths it needs: where the
    //    column is narrower than the whole bar it gives up, in turn, the title's second line (1), the words of Pause and Clear smoke (2), the pill's
    //    words and the title's text (3) and the words of the Smoke and Fire pills (4).
    const float  Left       = Minimum.x + 14.0f * Scale;
    const float  BarY       = Minimum.y + 12.0f * Scale;
    const bool   Live       = Image.Texture != 0;
    const char*  Subtitle   = "Eulerian smoke \xC2\xB7 combustion \xC2\xB7 tyre sources";
    const char*  Pill       = Live ? "VULKAN \xC2\xB7 LIVE" : "NO DEVICE";
    const char*  PauseLabel = Playing ? "Pause" : "Play";
    const float  TitleText  = Style.Measure(Style.Faces.Medium, 13.0f, "Smoke & fire");
    const float  SubText    = Style.Measure(Style.Faces.Regular, 10.0f, Subtitle);
    const float  PillText   = Style.Measure(Style.Faces.Medium, 10.0f, Pill) + 30.0f * Scale;
    const auto   Needed     = [&](int Squeeze) {
        const float Title = Squeeze >= 3 ? 40.0f * Scale : 40.0f * Scale + (Squeeze >= 1 ? TitleText : std::max(TitleText, SubText));
        const float Dot   = Squeeze >= 3 ? 22.0f * Scale : PillText;
        return Title + 12.0f * Scale + Dot + 12.0f * Scale + MeasurePillWidth(Style, PauseLabel, true, Squeeze >= 2) + 6.0f * Scale +
               MeasurePillWidth(Style, "Clear smoke", true, Squeeze >= 2) + 14.0f * Scale + MeasurePillWidth(Style, "Off", false, false) + 3.0f * Scale +
               MeasurePillWidth(Style, "Smoke", true, Squeeze >= 4) + 3.0f * Scale + MeasurePillWidth(Style, "Fire", true, Squeeze >= 4);
    };
    const float Available = Maximum.x - 14.0f * Scale - Left;
    int         Squeeze     = 0;
    while (Squeeze < 4 && Needed(Squeeze) > Available) ++Squeeze;
    Ink->AddRectFilled(ImVec2(Left, BarY), ImVec2(Left + 30.0f * Scale, BarY + 30.0f * Scale), P.CountCardBg, 8.0f * Scale);
    GlyphSymbol::Construct(Ink, "Flame", ImVec2(Left + 7.0f * Scale, BarY + 7.0f * Scale), 16.0f * Scale, 1.4f, FireTint);
    float Cursor = Left + 40.0f * Scale;
    if (Squeeze < 3)
    {
        Style.Text(Ink, Style.Faces.Medium, 13.0f, ImVec2(Cursor, BarY + (Squeeze >= 1 ? 7.0f : 1.0f) * Scale), P.TextPrimary, "Smoke & fire");
        if (Squeeze < 1) Style.Text(Ink, Style.Faces.Regular, 10.0f, ImVec2(Cursor, BarY + 18.0f * Scale), P.TextSecondary, Subtitle);
        Cursor += (Squeeze >= 1 ? TitleText : std::max(TitleText, SubText)) + 12.0f * Scale;
    }
    else
        Cursor += 12.0f * Scale;
    const float PillW = Squeeze >= 3 ? 22.0f * Scale : PillText;
    Ink->AddRectFilled(ImVec2(Cursor, BarY + 3.0f * Scale), ImVec2(Cursor + PillW, BarY + 27.0f * Scale), IM_COL32(18, 40, 28, 255), 12.0f * Scale);
    Ink->AddCircleFilled(ImVec2(Cursor + 11.0f * Scale + (Squeeze >= 3 ? 0.0f : 1.0f * Scale), BarY + 15.0f * Scale), 3.5f * Scale, Live ? P.LiveGreen : P.WarmAmber);
    if (Squeeze < 3) Style.Text(Ink, Style.Faces.Medium, 10.0f, ImVec2(Cursor + 21.0f * Scale, BarY + 8.5f * Scale), Live ? P.LiveGreen : P.WarmAmber, Pill);
    Cursor += PillW + 12.0f * Scale;
    float Width = 0.0f;
    if (ConstructPillButton(Style, Ink, Controls, "pause", ImVec2(Cursor, BarY), PauseLabel, !Playing, Playing ? "Pause" : "Play", &Width, Squeeze >= 2))
        Requests.push_back(Playing ? "pause" : "play");
    Cursor += Width + 6.0f * Scale;
    if (ConstructPillButton(Style, Ink, Controls, "clear", ImVec2(Cursor, BarY), "Clear smoke", false, "RotateCcw", &Width, Squeeze >= 2)) Requests.push_back("clear-smoke");
    Cursor += Width + 14.0f * Scale;
    constexpr const char* Emitters[3] = { "Off", "Smoke", "Fire" };
    constexpr const char* Verbs[3]    = { "emitter:off", "emitter:smoke", "emitter:fire" };
    for (uint32_t Emitter = 0; Emitter < 3; ++Emitter)
    {
        char Name[24];
        std::snprintf(Name, sizeof(Name), "emitter:%u", Emitter);
        if (ConstructPillButton(Style, Ink, Controls, Name, ImVec2(Cursor, BarY), Emitters[Emitter], Record->TestEmitter == Emitter, Emitter == 1 ? "Puff" : Emitter == 2 ? "Flame" : nullptr,
                                &Width, Squeeze >= 4))
            Requests.push_back(Verbs[Emitter]);
        Cursor += Width + 3.0f * Scale;
    }

    // 📝 The image and its readouts.
    const ImVec2 StageMin = StageMinimum;
    const ImVec2 StageMax = StageMaximum;
    Ink->AddRectFilled(StageMin, StageMax, IM_COL32(9, 9, 12, 255), 8.0f * Scale);
    if (Image.Texture != 0)
    {
        Ink->AddImageRounded(static_cast<ImTextureID>(Image.Texture), StageMin, StageMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 8.0f * Scale);
    }
    else
    {
        // 📝 Where there is no picture the stage says why, in its middle; the Outliner, the Inspector and the libraries go on working.
        const char*  Reason = !Image.Failure.empty() ? Image.Failure.c_str() : "Starting the plume\xE2\x80\xA6";
        const float  Middle = (StageMin.x + StageMax.x) * 0.5f;
        const float  Centre = (StageMin.y + StageMax.y) * 0.5f;
        GlyphSymbol::Construct(Ink, "Flame", ImVec2(Middle - 14.0f * Scale, Centre - 58.0f * Scale), 28.0f * Scale, 1.3f, IM_COL32(120, 90, 62, 255));
        Style.Text(Ink, Style.Faces.Medium, 13.0f, ImVec2(Middle - Style.Measure(Style.Faces.Medium, 13.0f, Reason) * 0.5f, Centre - 14.0f * Scale), P.WarmAmber, Reason);
        const char* First  = "The record is still edited here.";
        const char* Second = "The Outliner, the Inspector and the libraries work without it.";
        Style.Text(Ink, Style.Faces.Regular, 11.0f, ImVec2(Middle - Style.Measure(Style.Faces.Regular, 11.0f, First) * 0.5f, Centre + 10.0f * Scale), P.TextSecondary, First);
        Style.Text(Ink, Style.Faces.Regular, 11.0f, ImVec2(Middle - Style.Measure(Style.Faces.Regular, 11.0f, Second) * 0.5f, Centre + 28.0f * Scale), P.TextSecondary, Second);
    }
    Ink->PushClipRect(StageMin, StageMax, true);

    // 📝 Orbit: drag turns the camera about the emitter and the wheel zooms.
    ImGui::SetCursorScreenPos(StageMin);
    ImGui::InvisibleButton("##PlumeOrbit", ImVec2(StageMax.x - StageMin.x, StageMax.y - StageMin.y), ImGuiButtonFlags_MouseButtonLeft);
    Controls.push_back({ "orbit", StageMin, StageMax });
    if (ImGui::IsItemActive())
    {
        const ImVec2 Delta = ImGui::GetIO().MouseDelta;
        Yaw -= Delta.x * 0.006;
        Pitch = std::clamp(Pitch + Delta.y * 0.006, -0.1, 1.5);
    }
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f) Distance = std::clamp(Distance * std::exp(-ImGui::GetIO().MouseWheel * 0.1), 3.0, 40.0);

    // 📝 The HUD: what the solver is doing, then the lattice it is doing it on. It is shown with a picture or with a host's readout; a stage that has
    //    neither says so in its middle and carries no figures. RATE is the interface's own smoothed rate; REFRESH is the presenter's last refresh.
    const FluidPlumeReadout* Readout = ResolveReadout();
    if (Live || Readout != nullptr)
    {
        char Sources[16], Ticks[16], Hud[200];
        if (Readout != nullptr)
        {
            std::snprintf(Sources, sizeof(Sources), "%u", Readout->Sources);
            std::snprintf(Ticks, sizeof(Ticks), "%u", Readout->Ticks);
        }
        else
        {
            std::snprintf(Sources, sizeof(Sources), "\xE2\x80\x93");
            std::snprintf(Ticks, sizeof(Ticks), "\xE2\x80\x93");
        }
        if (Live)
            std::snprintf(Hud, sizeof(Hud), "SOURCES %s   STEPS %s   RATE %.0f fps   REFRESH %.1f ms   t %.1f s", Sources, Ticks, RefreshRate, Image.RefreshMilliseconds, StageSeconds);
        else
            std::snprintf(Hud, sizeof(Hud), "SOURCES %s   STEPS %s", Sources, Ticks);
        const ImVec2 HudAt(StageMin.x + 12.0f * Scale, StageMin.y + 12.0f * Scale);
        const float  HudW = Style.Measure(Style.Faces.Monospace, 10.5f, Hud) + 20.0f * Scale;
        Ink->AddRectFilled(HudAt, ImVec2(HudAt.x + HudW, HudAt.y + 26.0f * Scale), IM_COL32(12, 13, 17, 215), 7.0f * Scale);
        Style.Text(Ink, Style.Faces.Monospace, 10.5f, ImVec2(HudAt.x + 10.0f * Scale, HudAt.y + 6.5f * Scale), P.TextPrimary, Hud);
        const uint32_t    Grade = Readout != nullptr ? Readout->Grade : 2u;
        const PlumeGrade& Tier = QueryPlumeGrades()[std::min<size_t>(Grade, 3u)];
        char              Lattice[160];
        std::snprintf(Lattice, sizeof(Lattice), "%s \xC2\xB7 %s @ %.0f cm \xC2\xB7 %u sweeps \xC2\xB7 %u samples \xC2\xB7 %s", ResolveGradeName(Grade).c_str(), FluidPlumePages::FormatLatticeExtent(Grade).c_str(),
                      Tier.CellExtent * 100.0, Tier.PressureSweeps, Tier.MarchSamples, Readout == nullptr ? "NO SOLVER" : Readout->Resident ? "RESIDENT" : "IDLE");
        const ImVec2 LatticeAt(HudAt.x, HudAt.y + 34.0f * Scale);
        const float  LatticeW = Style.Measure(Style.Faces.Monospace, 10.0f, Lattice) + 20.0f * Scale;
        Ink->AddRectFilled(LatticeAt, ImVec2(LatticeAt.x + LatticeW, LatticeAt.y + 24.0f * Scale), IM_COL32(12, 13, 17, 215), 7.0f * Scale);
        Style.Text(Ink, Style.Faces.Monospace, 10.0f, ImVec2(LatticeAt.x + 10.0f * Scale, LatticeAt.y + 5.5f * Scale), P.TextSecondary, Lattice);
    }
    const char* Hint = "Drag to orbit \xC2\xB7 wheel zooms \xC2\xB7 Space pauses \xC2\xB7 E turns the emitter \xC2\xB7 C clears \xC2\xB7 L libraries";
    const float HintW = Style.Measure(Style.Faces.Regular, 10.0f, Hint);
    Style.Text(Ink, Style.Faces.Regular, 10.0f, ImVec2(StageMax.x - HintW - 14.0f * Scale, StageMax.y - 20.0f * Scale), IM_COL32(200, 205, 214, 170), Hint);
    Ink->PopClipRect();

    // 📝 The footer: the adapter on the left, the stage's size and state on the right; where they do not both fit the adapter is cut short.
    char Right[160];
    std::snprintf(Right, sizeof(Right), "STAGE %.0f \xC3\x97 %.0f \xC2\xB7 %s \xC2\xB7 %s", StageMax.x - StageMin.x, StageMax.y - StageMin.y, Playing ? "RUNNING" : "PAUSED", Record->Enabled ? "ENABLED" : "DISABLED");
    const float       RightW = Style.Measure(Style.Faces.Monospace, 9.5f, Right);
    const std::string AdapterLine  = std::string("VULKAN   ADAPTER ") + (Image.Adapter.empty() ? "\xE2\x80\x94" : Image.Adapter);
    const std::string Fitted = FitText(Style, Style.Faces.Monospace, 9.5f, AdapterLine, (StageMax.x - StageMin.x) - RightW - 24.0f * Scale);
    Style.Text(Ink, Style.Faces.Monospace, 9.5f, ImVec2(StageMin.x + 6.0f * Scale, Maximum.y - 19.0f * Scale), P.TextMuted, Fitted.c_str());
    Style.Text(Ink, Style.Faces.Monospace, 9.5f, ImVec2(StageMax.x - RightW - 6.0f * Scale, Maximum.y - 19.0f * Scale), P.TextMuted, Right);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LOG STREAM
//------------------------------------------------------------------------------------------------------------------------

void FluidPlumeEditorWorkspace::ConstructLogStream(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, float Scale)
{
    const Look  Style{ InspectorPalette::Resolve(), EditorTypefaces::Resolve(), Scale };
    const auto& P = Style.Palette;
    Ink->AddRectFilled(Minimum, Maximum, P.ColumnBg, 14.0f * Scale);
    Ink->AddRect(Minimum, Maximum, P.EdgeBorder, 14.0f * Scale);
    const float Left = Minimum.x + 14.0f * Scale;
    Style.Text(Ink, Style.Faces.Medium, 13.0f, ImVec2(Left, Minimum.y + 12.0f * Scale), P.TextPrimary, "Log Stream");
    const float Pill = Left + Style.Measure(Style.Faces.Medium, 13.0f, "Log Stream") + 12.0f * Scale;
    Ink->AddRectFilled(ImVec2(Pill, Minimum.y + 10.0f * Scale), ImVec2(Pill + 50.0f * Scale, Minimum.y + 30.0f * Scale), IM_COL32(18, 40, 28, 255), 10.0f * Scale);
    Ink->AddCircleFilled(ImVec2(Pill + 10.0f * Scale, Minimum.y + 20.0f * Scale), 3.0f * Scale, P.LiveGreen);
    Style.Text(Ink, Style.Faces.Medium, 9.5f, ImVec2(Pill + 18.0f * Scale, Minimum.y + 14.0f * Scale), P.LiveGreen, "LIVE");
    char Count[32];
    std::snprintf(Count, sizeof(Count), "%zu entries", Log.size());
    Style.Text(Ink, Style.Faces.Regular, 10.0f, ImVec2(Maximum.x - Style.Measure(Style.Faces.Regular, 10.0f, Count) - 16.0f * Scale, Minimum.y + 15.0f * Scale), P.TextSecondary, Count);
    float        Top   = Minimum.y + 44.0f * Scale;
    const size_t Rows  = static_cast<size_t>(std::max(0.0f, (Maximum.y - Top - 6.0f * Scale) / (24.0f * Scale)));
    const size_t First = Log.size() > Rows ? Log.size() - Rows : 0;
    for (size_t Index = Log.size(); Index-- > First;)
    {
        const FluidPlumeLogLine& Line = Log[Index];
        Ink->AddCircleFilled(ImVec2(Left + 4.0f * Scale, Top + 8.0f * Scale), 3.0f * Scale, Line.Warm ? P.WarmAmber : P.LiveGreen);
        Style.Text(Ink, Style.Faces.Monospace, 9.5f, ImVec2(Left + 18.0f * Scale, Top + 2.0f * Scale), P.TextMuted, Line.Time.c_str());
        const float  CategoryW = Style.Measure(Style.Faces.Medium, 8.5f, Line.Category.c_str()) + 16.0f * Scale;
        const ImVec2 Chip(Left + 86.0f * Scale, Top);
        Ink->AddRect(Chip, ImVec2(Chip.x + CategoryW, Chip.y + 17.0f * Scale), P.InsetFieldBorder, 8.0f * Scale);
        Style.Text(Ink, Style.Faces.Medium, 8.5f, ImVec2(Chip.x + 8.0f * Scale, Chip.y + 3.5f * Scale), P.TextSecondary, Line.Category.c_str());
        // 📝 The message and its detail are cut to the card: in a narrow column the detail goes first.
        const float       TextX   = Chip.x + std::max(CategoryW, 84.0f * Scale) + 12.0f * Scale;
        const float       Room    = Maximum.x - 14.0f * Scale - TextX;
        const std::string Message = FitText(Style, Style.Faces.Medium, 11.0f, Line.Text, Room);
        Style.Text(Ink, Style.Faces.Medium, 11.0f, ImVec2(TextX, Top + 1.5f * Scale), P.TextPrimary, Message.c_str());
        const float MessageW = Style.Measure(Style.Faces.Medium, 11.0f, Message.c_str());
        if (!Line.Detail.empty() && Message == Line.Text && Room - MessageW - 8.0f * Scale > 60.0f * Scale)
        {
            const std::string Detail = FitText(Style, Style.Faces.Regular, 10.5f, "\xC2\xB7 " + Line.Detail, Room - MessageW - 8.0f * Scale);
            Style.Text(Ink, Style.Faces.Regular, 10.5f, ImVec2(TextX + MessageW + 8.0f * Scale, Top + 2.0f * Scale), P.TextSecondary, Detail.c_str());
        }
        Top += 24.0f * Scale;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

void FluidPlumeEditorWorkspace::ConstructInspector(ImVec2 Minimum, ImVec2 Maximum, float Scale)
{
    const Look  Style{ InspectorPalette::Resolve(), EditorTypefaces::Resolve(), Scale };
    ImDrawList* Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Minimum, Maximum, Style.Palette.ColumnBg, 14.0f * Scale);
    Ink->AddRect(Minimum, Maximum, Style.Palette.EdgeBorder, 14.0f * Scale);
    ImGui::SetCursorScreenPos(ImVec2(Minimum.x + 1.0f, Minimum.y + 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild("##PlumeInspector", ImVec2(Maximum.x - Minimum.x - 2.0f, Maximum.y - Minimum.y - 2.0f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground))
    {
        const ControlTheme Theme = ControlTheme::ResolveNative();
        ControlColumn      Page(Theme, Style.Faces, Scale);
        FluidPlumeContext  Context{ *Record, ResolveReadout(), Requests };
        FluidPlumePage     Which = FluidPlumePage::Domain;
        (void)FluidPlumePages::ResolvePage(Selected, Which);
        FluidPlumePages::ConstructWindowPage(Page, Context, Which);
        Page.Finalize();
        // 📝 A page that scrolls shows where it is: a slim thumb at its right edge (the scrollbar itself is hidden).
        const float Scrollable = ImGui::GetScrollMaxY();
        if (Scrollable > 0.0f)
        {
            const ImVec2 Origin = ImGui::GetWindowPos();
            const ImVec2 Extent = ImGui::GetWindowSize();
            const float  Thumb  = std::max(24.0f * Scale, Extent.y * Extent.y / (Extent.y + Scrollable));
            const float  At     = Origin.y + (Extent.y - Thumb) * (ImGui::GetScrollY() / Scrollable);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(Origin.x + Extent.x - 7.0f * Scale, At), ImVec2(Origin.x + Extent.x - 4.0f * Scale, At + Thumb), IM_COL32(255, 255, 255, 56),
                                                      2.0f * Scale);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIBRARIES
//------------------------------------------------------------------------------------------------------------------------

void FluidPlumeEditorWorkspace::ConstructLibrarySheet(ImVec2 Minimum, ImVec2 Maximum, float Scale)
{
    const Look  Style{ InspectorPalette::Resolve(), EditorTypefaces::Resolve(), Scale };
    const auto& P = Style.Palette;
    LibraryCards  = 0;
    LibraryMarked.clear();
    // 📝 The mark on a card, a green dot and its words at the card's right edge (the dot is drawn: DM Sans has no ●).
    const auto ConstructMark = [&](ImDrawList* Sheet, ImVec2 Corner, const char* Words) {
        const float Right = Corner.x - 12.0f * Scale;
        const float Wide  = Style.Measure(Style.Faces.Medium, 9.5f, Words);
        Sheet->AddCircleFilled(ImVec2(Right - Wide - 8.0f * Scale, Corner.y + 6.0f * Scale), 3.0f * Scale, P.LiveGreen);
        Style.Text(Sheet, Style.Faces.Medium, 9.5f, ImVec2(Right - Wide, Corner.y), P.LiveGreen, Words);
    };
    // A child over the whole editor, created last so it draws above the columns and takes the pointer from them.
    ImGui::SetCursorScreenPos(Minimum);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild("##PlumeLibraryVeil", ImVec2(Maximum.x - Minimum.x, Maximum.y - Minimum.y), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImDrawList* Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(Minimum, Maximum, IM_COL32(4, 5, 8, 175), 14.0f * Scale);
        const float  SheetW = std::min(980.0f * Scale, Maximum.x - Minimum.x - 80.0f * Scale);
        // 📝 The sheet is as tall as its taller tab needs (two rows of looks or of grades) up to 680 px, so the same size holds whatever the filter shows.
        const size_t Rows   = std::max((FluidPlumeLibrary::QueryLooks().size() + 2) / 3, (QueryPlumeGrades().size() + 2) / 3);
        const float  Wanted = 156.0f + static_cast<float>(Rows) * (128.0f + 10.0f) - 10.0f + 16.0f + 12.0f;
        const float  SheetH = std::min({ Wanted * Scale, 680.0f * Scale, Maximum.y - Minimum.y - 60.0f * Scale });
        const ImVec2 SheetMin((Minimum.x + Maximum.x - SheetW) / 2.0f, (Minimum.y + Maximum.y - SheetH) / 2.0f);
        const ImVec2 SheetMax(SheetMin.x + SheetW, SheetMin.y + SheetH);
        // 📝 A click on the veil outside the sheet closes the libraries. The button lies under every control of the sheet, which are submitted after
        //    it: it allows them to overlap it, or it would keep the pointer from them.
        ImGui::SetCursorScreenPos(Minimum);
        ImGui::SetNextItemAllowOverlap();
        if (ImGui::InvisibleButton("##PlumeLibraryOutside", ImVec2(Maximum.x - Minimum.x, Maximum.y - Minimum.y)))
        {
            const ImVec2 Pointer = ImGui::GetIO().MousePos;
            if (Pointer.x < SheetMin.x || Pointer.x > SheetMax.x || Pointer.y < SheetMin.y || Pointer.y > SheetMax.y) LibraryShown = false;
        }
        Controls.push_back({ "library-veil", Minimum, Maximum });
        Controls.push_back({ "library-sheet", SheetMin, SheetMax });
        Ink->AddRectFilled(SheetMin, SheetMax, P.ColumnBg, 16.0f * Scale);
        Ink->AddRect(SheetMin, SheetMax, P.EdgeBorder, 16.0f * Scale);
        const float Left = SheetMin.x + 22.0f * Scale;
        Style.Text(Ink, Style.Faces.Medium, 20.0f, ImVec2(Left, SheetMin.y + 18.0f * Scale), P.TextPrimary, "Libraries");
        char Subtitle[128];
        std::snprintf(Subtitle, sizeof(Subtitle), "%zu looks in %zu groups \xC2\xB7 %zu grades \xC2\xB7 the HMI prototype's, unchanged", FluidPlumeLibrary::QueryLooks().size(),
                      FluidPlumeLibrary::QueryGroups().size(), QueryPlumeGrades().size());
        Style.Text(Ink, Style.Faces.Regular, 11.0f, ImVec2(Left, SheetMin.y + 46.0f * Scale), P.TextSecondary, Subtitle);
        float Width = 0.0f;
        if (ConstructPillButton(Style, Ink, Controls, "library-close", ImVec2(SheetMax.x - 120.0f * Scale, SheetMin.y + 18.0f * Scale), "Close (Esc)", false, "X", &Width))
            LibraryShown = false;
        // 📝 The search field in the inspector's inset look.
        ImGui::SetCursorScreenPos(ImVec2(SheetMax.x - 420.0f * Scale, SheetMin.y + 18.0f * Scale));
        ImGui::SetNextItemWidth(280.0f * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f * Scale, 8.0f * Scale));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(P.InsetFieldBg));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImGui::ColorConvertU32ToFloat4(P.InsetFieldBg));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImGui::ColorConvertU32ToFloat4(P.InsetFieldBg));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(P.InsetFieldBorder));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(P.TextPrimary));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::ColorConvertU32ToFloat4(P.TextMuted));
        ImGui::PushFont(Style.Pick(Style.Faces.Regular), 11.5f * Scale);
        ImGui::InputTextWithHint("##PlumeLibrarySearch", LibraryTab == "looks" ? "Search the looks" : "Search the grades", LibraryQuery, sizeof(LibraryQuery));
        Controls.push_back({ "library-search", ImGui::GetItemRectMin(), ImGui::GetItemRectMax() });
        ImGui::PopFont();
        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(3);

        // 📝 The two libraries as tabs, then the looks' group chips (or the grades' note).
        float TabX = Left;
        const float TabY = SheetMin.y + 78.0f * Scale;
        for (const auto& [Key, Label] : { std::pair<const char*, const char*>{ "looks", "Looks" }, std::pair<const char*, const char*>{ "grades", "Grades" } })
        {
            char Name[40];
            std::snprintf(Name, sizeof(Name), "library-tab:%s", Key);
            if (ConstructPillButton(Style, Ink, Controls, Name, ImVec2(TabX, TabY), Label, LibraryTab == Key, std::string_view(Key) == "looks" ? "Sparkles" : "Gauge", &Width))
                LibraryTab = Key;
            TabX += Width + 6.0f * Scale;
        }
        float ChipX = Left;
        float ChipY = TabY + 42.0f * Scale;
        if (LibraryTab == "looks")
        {
            std::vector<std::string> Groups = { "All" };
            Groups.insert(Groups.end(), FluidPlumeLibrary::QueryGroups().begin(), FluidPlumeLibrary::QueryGroups().end());
            for (const std::string& Group : Groups)
            {
                const float W = Style.Measure(Style.Faces.Medium, 10.5f, Group.c_str()) + 20.0f * Scale;
                if (ChipX + W > SheetMax.x - 22.0f * Scale)
                {
                    ChipX = Left;
                    ChipY += 30.0f * Scale;
                }
                if (Press(("library-group:" + Group).c_str(), ImVec2(ChipX, ChipY), ImVec2(W, 24.0f * Scale))) LibraryGroup = Group;
                const bool Active = LibraryGroup == Group;
                Ink->AddRectFilled(ImVec2(ChipX, ChipY), ImVec2(ChipX + W, ChipY + 24.0f * Scale), Active ? P.SegmentedActiveBg : P.CountCardBg, 12.0f * Scale);
                Style.Text(Ink, Style.Faces.Medium, 10.5f, ImVec2(ChipX + 10.0f * Scale, ChipY + 5.0f * Scale), Active ? P.TextPrimary : P.TextSecondary, Group.c_str());
                ChipX += W + 6.0f * Scale;
            }
        }
        else
        {
            Style.Text(Ink, Style.Faces.Regular, 11.0f, ImVec2(Left, ChipY + 4.0f * Scale), P.TextSecondary,
                       "The plume's four quality grades. SmokeSequence builds Low, Medium or High from the Render settings' quality tier; none is picked here.");
        }

        // 📝 The cards scroll in their own child.
        const ImVec2 CardsMin(Left, ChipY + 36.0f * Scale);
        const ImVec2 CardsMax(SheetMax.x - 22.0f * Scale, SheetMax.y - 16.0f * Scale);
        ImGui::SetCursorScreenPos(CardsMin);
        if (ImGui::BeginChild("##PlumeLibraryCards", ImVec2(CardsMax.x - CardsMin.x, CardsMax.y - CardsMin.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground))
        {
            ImDrawList*       Sheet  = ImGui::GetWindowDrawList();
            const ImVec2      Origin = ImGui::GetCursorScreenPos();
            const float       CardW  = (CardsMax.x - CardsMin.x - 20.0f * Scale) / 3.0f;
            const std::string Query  = Lowered(LibraryQuery);
            int               Shown  = 0;
            if (LibraryTab == "looks")
            {
                const float           CardH = 128.0f * Scale;
                const FluidPlumeLook* Held  = FluidPlumeLibrary::MatchLook(Record->Gas);
                for (const FluidPlumeLook& Look : FluidPlumeLibrary::QueryLooks())
                {
                    if (LibraryGroup != "All" && Look.Group != LibraryGroup) continue;
                    if (!Query.empty() && Lowered(Look.Name + " " + Look.Group + " " + Look.Lead + " " + Look.Changes).find(Query) == std::string::npos) continue;
                    const int    Column = Shown % 3, Line = Shown / 3;
                    const ImVec2 CardMin(Origin.x + Column * (CardW + 10.0f * Scale), Origin.y + Line * (CardH + 10.0f * Scale));
                    const ImVec2 CardMax(CardMin.x + CardW, CardMin.y + CardH);
                    ImGui::SetCursorScreenPos(CardMin);
                    ImGui::PushID(Look.Identifier.c_str());
                    const bool Pressed = ImGui::InvisibleButton("##Card", ImVec2(CardW, CardH));
                    const bool Hovered = ImGui::IsItemHovered();
                    ImGui::PopID();
                    Controls.push_back({ "library-card:" + Look.Identifier, CardMin, CardMax });
                    if (Pressed) Requests.push_back("look:" + Look.Identifier);
                    Sheet->AddRectFilled(CardMin, CardMax, Hovered ? P.RowHoverBg : P.CountCardBg, 12.0f * Scale);
                    Sheet->AddRect(CardMin, CardMax, Hovered ? P.RowSelectedBorder : P.CountCardBorder, 12.0f * Scale);
                    const ImU32 Tint = ResolveGroupTint(Look.Group);
                    Sheet->AddRectFilled(ImVec2(CardMin.x + 12.0f * Scale, CardMin.y + 12.0f * Scale), ImVec2(CardMin.x + 38.0f * Scale, CardMin.y + 38.0f * Scale),
                                         (Tint & 0x00FFFFFFu) | (64u << 24), 8.0f * Scale);
                    GlyphSymbol::Construct(Sheet, ResolveGroupGlyph(Look.Group), ImVec2(CardMin.x + 17.0f * Scale, CardMin.y + 17.0f * Scale), 16.0f * Scale, 1.4f, Tint);
                    Style.Text(Sheet, Style.Faces.Medium, 12.5f, ImVec2(CardMin.x + 48.0f * Scale, CardMin.y + 11.0f * Scale), P.TextPrimary, Look.Name.c_str());
                    char Facts[96];
                    if (Look.Differences == 0) std::snprintf(Facts, sizeof(Facts), "%s \xC2\xB7 the defaults", Look.Group.c_str());
                    else std::snprintf(Facts, sizeof(Facts), "%s \xC2\xB7 %u readings", Look.Group.c_str(), Look.Differences);
                    Style.Text(Sheet, Style.Faces.Regular, 10.0f, ImVec2(CardMin.x + 48.0f * Scale, CardMin.y + 28.0f * Scale), P.TextSecondary, Facts);
                    if (Held == &Look)
                    {
                        LibraryMarked = Look.Identifier;
                        ConstructMark(Sheet, ImVec2(CardMax.x, CardMin.y + 14.0f * Scale), "Applied");
                    }
                    const std::string Blurb = Look.Changes.empty() ? Look.Lead : Look.Lead + " " + Look.Changes;
                    Sheet->PushClipRect(ImVec2(CardMin.x + 12.0f * Scale, CardMin.y + 46.0f * Scale), ImVec2(CardMax.x - 10.0f * Scale, CardMax.y - 8.0f * Scale), true);
                    Sheet->AddText(Style.Pick(Style.Faces.Regular), 10.0f * Scale, ImVec2(CardMin.x + 12.0f * Scale, CardMin.y + 48.0f * Scale), P.TextMuted, Blurb.c_str(), nullptr,
                                   CardW - 24.0f * Scale);
                    Sheet->PopClipRect();
                    ++Shown;
                }
                ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + ((Shown + 2) / 3) * (CardH + 10.0f * Scale)));
            }
            else
            {
                const float              CardH   = 96.0f * Scale;
                const FluidPlumeReadout* Readout = ResolveReadout();
                const auto&              Grades  = QueryPlumeGrades();
                for (size_t Index = 0; Index < Grades.size(); ++Index)
                {
                    const PlumeGrade& Grade = Grades[Index];
                    char              Facts[128];
                    std::snprintf(Facts, sizeof(Facts), "%s", FluidPlumePages::FormatLatticeExtent(static_cast<uint32_t>(Index)).c_str());
                    if (!Query.empty() && Lowered(std::string(Grade.Name) + " " + Facts).find(Query) == std::string::npos) continue;
                    const int    Column = Shown % 3, Line = Shown / 3;
                    const ImVec2 CardMin(Origin.x + Column * (CardW + 10.0f * Scale), Origin.y + Line * (CardH + 10.0f * Scale));
                    const ImVec2 CardMax(CardMin.x + CardW, CardMin.y + CardH);
                    Controls.push_back({ std::string("library-grade:") + std::string(Grade.Name), CardMin, CardMax });
                    Sheet->AddRectFilled(CardMin, CardMax, P.CountCardBg, 12.0f * Scale);
                    Sheet->AddRect(CardMin, CardMax, P.CountCardBorder, 12.0f * Scale);
                    Sheet->AddRectFilled(ImVec2(CardMin.x + 12.0f * Scale, CardMin.y + 12.0f * Scale), ImVec2(CardMin.x + 38.0f * Scale, CardMin.y + 38.0f * Scale),
                                         (GasTint & 0x00FFFFFFu) | (64u << 24), 8.0f * Scale);
                    GlyphSymbol::Construct(Sheet, "Gauge", ImVec2(CardMin.x + 17.0f * Scale, CardMin.y + 17.0f * Scale), 16.0f * Scale, 1.4f, GasTint);
                    Style.Text(Sheet, Style.Faces.Medium, 12.5f, ImVec2(CardMin.x + 48.0f * Scale, CardMin.y + 11.0f * Scale), P.TextPrimary, std::string(Grade.Name).c_str());
                    char Cells[96];
                    std::snprintf(Cells, sizeof(Cells), "%s cells \xC2\xB7 %.0f cm", Facts, Grade.CellExtent * 100.0);
                    Style.Text(Sheet, Style.Faces.Regular, 10.0f, ImVec2(CardMin.x + 48.0f * Scale, CardMin.y + 28.0f * Scale), P.TextSecondary, Cells);
                    char Work[96];
                    std::snprintf(Work, sizeof(Work), "%u pressure sweeps \xC2\xB7 %u march samples", Grade.PressureSweeps, Grade.MarchSamples);
                    Style.Text(Sheet, Style.Faces.Regular, 10.0f, ImVec2(CardMin.x + 12.0f * Scale, CardMin.y + 56.0f * Scale), P.TextMuted, Work);
                    if (Readout != nullptr && Readout->Grade == Index)
                    {
                        LibraryMarked = std::string(Grade.Name);
                        ConstructMark(Sheet, ImVec2(CardMax.x, CardMin.y + 14.0f * Scale), "In use");
                    }
                    ++Shown;
                }
                ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + ((Shown + 2) / 3) * (CardH + 10.0f * Scale)));
            }
            LibraryCards = static_cast<size_t>(Shown);
            if (Shown == 0)
                Style.Text(Sheet, Style.Faces.Regular, 11.0f, ImVec2(Origin.x + 4.0f * Scale, Origin.y + 8.0f * Scale), P.TextMuted, "Nothing in this library matches.");
            ImGui::Dummy(ImVec2(1.0f, 4.0f * Scale));
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

} // namespace Frontier

#endif
