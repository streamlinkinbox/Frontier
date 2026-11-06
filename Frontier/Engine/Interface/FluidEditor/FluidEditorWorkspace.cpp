/*==========================================================================================================================================
                                                     FLUIDEDITORWORKSPACE.CPP
==========================================================================================================================================*/
// 🧩 The Fluid editor's state, verbs and refresh, and its Outliner, Log Stream and Inspector. The viewport, the transport, the pointer tools and the preset
//    library are in FluidEditorViewport.cpp. The behaviour follows FluidPanel.js; what each verb does is named there next to it.

#include "Engine/Interface/FluidEditor/FluidEditorWorkspace.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/ControlTheme.h"
#include "Engine/Interface/Controls/GlyphSymbol.h"
#include "Engine/Interface/InspectorControls.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace Frontier
{

namespace
{

using namespace FluidEditor;

constexpr float OutlinerWidth  = 292.0f;
constexpr float InspectorWidth = 404.0f;
constexpr float LogHeight      = 150.0f;
constexpr float HeaderHeight   = 54.0f;
constexpr float Gutter         = 10.0f;

constexpr ImU32 FireTint     = IM_COL32(232, 131, 58, 255);    // [-] the Effects accent #e8833a
constexpr ImU32 SmokeTint    = IM_COL32(190, 196, 206, 255);
constexpr ImU32 ColliderTint = IM_COL32(159, 180, 214, 255);   // [-] the Render accent #9fb4d6
constexpr ImU32 SunTint      = IM_COL32(232, 194, 122, 255);   // [-] amber

using WindowControls::ConstructPillButton;
using WindowControls::Look;
using WindowControls::ResolveClock;

ImU32 ResolvePartTint(const FluidVolumeConfiguration& Volume, FluidPart Part)
{
    switch (Part)
    {
    case FluidPart::Domain: return FireTint;
    case FluidPart::Emitter: return Volume.Gas.EmitterFuel == 0.0f ? SmokeTint : FireTint;
    case FluidPart::Collider: return ColliderTint;
    case FluidPart::Sun: return SunTint;
    }
    return FireTint;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    STATE
//------------------------------------------------------------------------------------------------------------------------

FluidEditorWorkspace::FluidEditorWorkspace()
{
    AppendLog("FLUID", "Fluid editor", "the prototype's page: 85 settings, 18 presets, one domain, one source, one collider");
}

void FluidEditorWorkspace::AttachRecord(FluidVolumeConfiguration& InRecord, std::string InName)
{
    if (Record != &InRecord)
    {
        Record = &InRecord;
        AppendLog("RECORD", "Editing " + InName, FluidEditorPages::FormatLattice(Record->Gas.LatticeResolution) + " lattice " + "\xC2\xB7 " +
                                                     (Record->Preset.empty() ? std::string("no preset") : Record->Preset));
        // 📝 The window opens the way the prototype's page does: the emitter selected on its Source tab, the camera standing back at 4.1 and looking at the
        //    domain's middle, the light as it is remembered for its eye.
        Selected          = FluidPart::Emitter;
        Tab               = FluidTab::Source;
        Memory            = FluidEditorMemory{};
        Memory.SavedSun   = Record->Gas.SunIntensity != 0.0f ? Record->Gas.SunIntensity : 2.2f;
        Memory.EmitterName = Record->Gas.EmitterFuel == 0.0f ? "Smoke emitter" : "Fire emitter";
        Camera             = PyroOrbitCamera{};
        Camera.TargetDistance = 4.1f;
        Camera.TargetCentre   = { 0.0f, 0.3f, 0.0f };
        View               = "perspective";
        LibraryMarked      = Record->Preset;
        Opened             = true;
    }
    RecordName = std::move(InName);
}

void FluidEditorWorkspace::DetachRecord() noexcept
{
    Record       = nullptr;
    LibraryShown = false;
    LoggedDevice = false;
    Brush        = FluidBrushRequest{};
    DragMode     = 0;
    PendingBursts.clear();
    PendingSteps = 0;
    ClearPending = false;
    Image        = FluidViewportImage{};
}

void FluidEditorWorkspace::AppendLog(std::string Category, std::string Text, std::string Detail, bool Warm)
{
    Log.push_back({ ResolveClock(), std::move(Category), std::move(Text), std::move(Detail), Warm });
    if (Log.size() > 200) Log.erase(Log.begin());
}

void FluidEditorWorkspace::AssignReadout(const FluidVolumeReadout& Readout)
{
    Assigned = Readout;
}

void FluidEditorWorkspace::RevokeReadout() noexcept
{
    Assigned.reset();
}

const FluidVolumeReadout* FluidEditorWorkspace::ResolveReadout() const noexcept
{
    if (Image.Solving) return &Image.Readout;
    return Assigned ? &*Assigned : nullptr;
}

const FluidVolumeReadout* FluidEditorWorkspace::QueryReadout() const noexcept
{
    return ResolveReadout();
}

std::vector<std::string> FluidEditorWorkspace::TakeHostRequests()
{
    std::vector<std::string> Taken;
    Taken.swap(HostRequests);
    return Taken;
}

const FluidControlRectangle* FluidEditorWorkspace::FindControl(std::string_view Name) const noexcept
{
    for (const FluidControlRectangle& Control : Controls)
    {
        if (Control.Name == Name) return &Control;
    }
    return nullptr;
}

bool FluidEditorWorkspace::Press(const char* Name, ImVec2 Minimum, ImVec2 Size)
{
    ImGui::SetCursorScreenPos(Minimum);
    char Id[96];
    std::snprintf(Id, sizeof(Id), "##Fluid%s", Name);
    const bool Pressed = ImGui::InvisibleButton(Id, Size);
    Controls.push_back({ Name, Minimum, ImVec2(Minimum.x + Size.x, Minimum.y + Size.y) });
    return Pressed;
}

void FluidEditorWorkspace::ResolveBoundsBox(std::array<float, 3>& OutMinimum, std::array<float, 3>& OutMaximum) const noexcept
{
    // 📝 Engine.getBoundsBox: the domain stands on the floor at y = -0.6, centred on x and z; with a solver its width and height are the surged ones.
    const FluidVolumeReadout* Readout = ResolveReadout();
    const float               Width   = Readout != nullptr && Readout->BoundsWidth > 0.0f ? Readout->BoundsWidth : (Record != nullptr ? Record->Gas.BoundsWidth : 1.85f);
    const float               Height  = Readout != nullptr && Readout->BoundsHeight > 0.0f ? Readout->BoundsHeight : (Record != nullptr ? Record->Gas.BoundsHeight : 2.10f);
    OutMinimum = { -0.5f * Width, -0.6f, -0.5f * Width };
    OutMaximum = { 0.5f * Width, -0.6f + Height, 0.5f * Width };
}

void FluidEditorWorkspace::ResolveFocus(bool UseSettings)
{
    if (Record == nullptr) return;
    const FluidVolumeReadout* Readout = UseSettings ? nullptr : ResolveReadout();
    const float               Width   = Readout != nullptr && Readout->BoundsWidth > 0.0f ? Readout->BoundsWidth : Record->Gas.BoundsWidth;
    const float               Height  = Readout != nullptr && Readout->BoundsHeight > 0.0f ? Readout->BoundsHeight : Record->Gas.BoundsHeight;
    Camera.FocusVolume(Width, Height);
    View = "perspective";
}

void FluidEditorWorkspace::ApplyTool(const char* Tool)
{
    // 📝 ApplyParameter interactionMode: a new tool drops the brush the old one held.
    Record->Gas.InteractionMode = Tool;
    Record->AdvanceRevision();
    Brush.Held = false;
    DragMode   = 0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    VERBS
//------------------------------------------------------------------------------------------------------------------------

bool FluidEditorWorkspace::Apply(std::string_view Action)
{
    const auto Colon    = Action.find(':');
    const auto Verb     = Action.substr(0, Colon);
    const auto Argument = Colon == std::string_view::npos ? std::string_view{} : Action.substr(Colon + 1);
    // 📝 The window's own view, sheet and camera need no record; every edit of the volume does.
    if (Verb == "library")
    {
        LibraryShown = Argument != "off";
        return true;
    }
    if (Verb == "library-group")
    {
        LibraryGroup = Argument.empty() ? std::string("all") : std::string(Argument);
        return true;
    }
    if (Verb == "library-search")
    {
        std::snprintf(LibraryQuery, sizeof(LibraryQuery), "%s", std::string(Argument).c_str());
        return true;
    }
    if (Verb == "close")
    {
        LibraryShown = false;
        return true;
    }
    if (Verb == "select")
    {
        FluidPart Part;
        if (!FluidEditorPages::ResolvePart(Argument, Part)) return false;
        // 📝 SelectObject: the part's own tab opens, and the groups the person had opened or closed are forgotten.
        Selected = Part;
        Tab      = FluidEditorPages::ResolveOpeningTab(Part);
        Memory.Groups.clear();
        return true;
    }
    if (Verb == "tab")
    {
        FluidTab Wanted;
        if (!FluidEditorPages::ResolveTab(Argument, Wanted)) return false;
        Tab = Wanted;
        Memory.Groups.clear();
        return true;
    }
    if (Verb == "burst-kind")
    {
        if (Argument != "burst" && Argument != "shrapnel" && Argument != "salvo" && Argument != "collide" && Argument != "mushroom") return false;
        BurstKind = std::string(Argument);
        return true;
    }
    if (Record == nullptr) return false;
    GasSettings&        Gas = Record->Gas;
    FluidEditorContext  Context{ *Record, Memory, ResolveReadout(), Requests };
    if (Verb == "preset")
    {
        if (FindGasPreset(std::string(Argument).c_str()) == nullptr) return false;
        FluidEditorPages::ApplyPreset(Context, Argument);
        LibraryMarked = Record->Preset;
        LibraryShown  = false;
        return true;
    }
    if (Verb == "defaults")
    {
        FluidEditorPages::RestoreTab(Context, Selected, Tab);
        return true;
    }
    if (Verb == "enabled")
    {
        const bool Wanted = Argument == "on" ? true : Argument == "off" ? false : !Record->Enabled;
        if (Wanted != Record->Enabled) FluidEditorPages::ToggleEnabled(Context);
        return true;
    }
    if (Verb == "part")
    {
        // "part:<part>:toggle" (the eye)
        const auto Next = Argument.find(':');
        FluidPart  Part;
        if (Next == std::string_view::npos || !FluidEditorPages::ResolvePart(Argument.substr(0, Next), Part) || Part == FluidPart::Domain) return false;
        FluidEditorPages::TogglePart(Context, Part);
        return true;
    }
    if (Verb == "add") return FluidEditorPages::AddPart(Context, Argument);
    if (Verb == "remove")
    {
        FluidPart Part;
        if (!FluidEditorPages::ResolvePart(Argument, Part) || (Part != FluidPart::Emitter && Part != FluidPart::Collider)) return false;
        FluidEditorPages::RemovePart(Context, Part);
        return true;
    }
    if (Verb == "play" || Verb == "pause" || Verb == "toggle-play")
    {
        const bool Paused = Verb == "pause" ? true : Verb == "play" ? false : !Gas.Paused;
        if (Paused != Gas.Paused)
        {
            Gas.Paused = Paused;
            Record->AdvanceRevision();
        }
        return true;
    }
    if (Verb == "step")
    {
        // 📝 The S key: pause, then one step of 1/60 s (this.SingleStep).
        if (!Gas.Paused)
        {
            Gas.Paused = true;
            Record->AdvanceRevision();
        }
        ++PendingSteps;
        if (!Presenter) HostRequests.push_back("step");
        return true;
    }
    if (Verb == "restart")
    {
        // 📝 Restart: the bursts waiting are dropped, the lattice emptied, the clock and the step count at zero, the brush off.
        ClearPending = true;
        PendingBursts.clear();
        PendingSteps = 0;
        Brush.Held   = false;
        if (!Presenter) HostRequests.push_back("restart");
        AppendLog("SOLVER", "Simulation restarted", ResolveReadout() == nullptr ? "no solver is attached" : "the lattice was emptied", true);
        return true;
    }
    if (Verb == "burst" || Verb == "burst-at")
    {
        FluidBurstRequest Burst;
        if (Verb == "burst-at")
        {
            float Point[3] = { 0.5f, 0.18f, 0.5f };
            if (std::sscanf(std::string(Argument).c_str(), "%f,%f,%f", &Point[0], &Point[1], &Point[2]) != 3) return false;
            Burst.Pattern  = "burst";
            Burst.AtPoint  = true;
            Burst.PointUvw = { Point[0], Point[1], Point[2] };
        }
        else
            Burst.Pattern = Argument.empty() ? BurstKind : std::string(Argument);
        if (Gas.Paused) AppendLog("BURST", "Burst queued", "press Space or step once to simulate", true);
        else AppendLog("BURST", FluidEditorPages::ResolveBurstLabel(Burst.Pattern), Burst.AtPoint ? "at the pointer" : "the pattern's own places");
        if (!Presenter) HostRequests.push_back(Verb == "burst-at" ? "burst-at:" + std::string(Argument) : "burst:" + Burst.Pattern);
        PendingBursts.push_back(std::move(Burst));
        return true;
    }
    if (Verb == "speed")
    {
        const float Wanted = static_cast<float>(std::atof(std::string(Argument).c_str()));
        if (!(Wanted > 0.0f)) return false;
        Gas.TimeScale = Wanted;
        Record->AdvanceRevision();
        return true;
    }
    if (Verb == "tool")
    {
        if (Argument == "orbit") ApplyTool("orbit");
        else if (Argument == "flamethrower") ApplyTool("flamethrower");
        else if (Argument == "detonate") ApplyTool("detonate");
        else return false;
        return true;
    }
    if (Verb == "turntable")
    {
        const bool Wanted = Argument == "on" ? true : Argument == "off" ? false : !Gas.AutoTurntable;
        if (Wanted != Gas.AutoTurntable)
        {
            Gas.AutoTurntable = Wanted;
            Record->AdvanceRevision();
        }
        return true;
    }
    if (Verb == "bounds")
    {
        Gas.BoundingBoxShown = !Gas.BoundingBoxShown;
        Record->AdvanceRevision();
        return true;
    }
    if (Verb == "view")
    {
        if (Argument == "perspective") ResolveFocus(false);
        else if (Argument == "front" || Argument == "side" || Argument == "top")
        {
            Camera.ApplyPresetView(Argument == "front" ? 1 : Argument == "side" ? 2 : 3, 0.0f, 0.0f);
            View = std::string(Argument);
        }
        else
            return false;
        return true;
    }
    if (Verb == "camera")
    {
        if (Argument == "fit") ResolveFocus(false);
        else if (Argument == "fit-settings") ResolveFocus(true);
        else if (Argument == "snap") Camera.Snap();
        else
        {
            float First = 0.0f, Second = 0.0f;
            const std::string Spelt(Argument);
            if (std::sscanf(Spelt.c_str(), "orbit,%f,%f", &First, &Second) == 2) Camera.OrbitByPixels(First, Second);
            else if (std::sscanf(Spelt.c_str(), "pan,%f,%f", &First, &Second) == 2) Camera.PanByPixels(First, Second);
            else if (std::sscanf(Spelt.c_str(), "zoom,%f", &First) == 1) Camera.ZoomByWheel(std::clamp(First, -100.0f, 100.0f));
            else return false;
        }
        return true;
    }
    // 📝 What a page or the Outliner changed leaves a notice, so the Log Stream says it once, whoever made the edit.
    if (Verb == "noted")
    {
        const auto        Next = Argument.find(':');
        const auto        Kind = Argument.substr(0, Next);
        const std::string Rest = Next == std::string_view::npos ? std::string() : std::string(Argument.substr(Next + 1));
        if (Kind == "preset")
        {
            const GasPreset* Preset = FindGasPreset(Rest.c_str());
            if (Preset == nullptr) return false;
            AppendLog("PRESET", std::string("Loaded ") + Preset->Name, Preset->Description);
            return true;
        }
        if (Kind == "defaults")
        {
            AppendLog("RECORD", "Tab reset", Rest + " settings back to the editor's initial values");
            return true;
        }
        if (Kind == "enabled")
        {
            AppendLog("RECORD", Rest == "on" ? "Volume enabled" : "Volume disabled", RecordName, Rest != "on");
            return true;
        }
        if (Kind == "part")
        {
            const auto Split = Rest.find(':');
            AppendLog("SCENE", Rest.substr(0, Split) + (Rest.substr(Split + 1) == "on" ? " on" : " off"), RecordName, Rest.substr(Split + 1) != "on");
            return true;
        }
        if (Kind == "add")
        {
            AppendLog("SCENE", "Scene slot updated", "existing simulation preserved");
            return true;
        }
        if (Kind == "grid")
        {
            AppendLog("SOLVER", "Grid rebuilt at " + Rest + "\xC2\xB3", "simulation restarted", true);
            return true;
        }
        return false;
    }
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    REFRESH
//------------------------------------------------------------------------------------------------------------------------

void FluidEditorWorkspace::Present(uint32_t Width, uint32_t Height, double DeltaSeconds)
{
    GasSettings& Gas   = Record->Gas;
    // 📝 Frame(Time): Delta = max(0.001, dt); the turntable turns the aim, then the camera eases toward it by min(Delta, 0.1).
    const double Delta = std::max(0.001, DeltaSeconds);
    if (Gas.AutoTurntable) Camera.AdvanceTurntable(static_cast<float>(Delta));
    Camera.Advance(static_cast<float>(std::min(Delta, 0.1)));
    Camera.Aspect = static_cast<float>(Width) / static_cast<float>(std::max<uint32_t>(Height, 1u));

    // 📝 The FPS readout counts refreshes over half a second; with the governor on and the simulation running, it moves the march steps one notch
    //    (4) toward the rate: under 35 fps down to 36, from 58 fps up to 96.
    ElapsedSeconds += Delta;
    ++FramesCounted;
    if (ElapsedSeconds >= 0.5)
    {
        FramesPerSecond = static_cast<int>(std::lround(static_cast<double>(FramesCounted) / ElapsedSeconds));
        FramesCounted   = 0;
        ElapsedSeconds  = 0.0;
        if (Gas.AutoGpuGovernor && !Gas.Paused)
        {
            if (FramesPerSecond < 35 && Gas.RaymarchSteps > 36) Gas.RaymarchSteps = std::max(36, Gas.RaymarchSteps - 4);
            else if (FramesPerSecond >= 58 && Gas.RaymarchSteps < 96) Gas.RaymarchSteps = std::min(96, Gas.RaymarchSteps + 4);
        }
    }

    if (!Presenter)
    {
        // 📝 Nobody to carry the verbs out: the host takes them from TakeHostRequests (they were queued there when raised).
        ClearPending = false;
        PendingBursts.clear();
        PendingSteps  = 0;
        Image.Failure = "The Fluid viewport needs the editor's Vulkan device.";
        return;
    }
    FluidViewportRequest Request;
    Request.Settings = &Gas;
    Request.Enabled  = Record->Enabled;
    // 📝 The engine clamps a long refresh to 0.033 s (stepSimulation); a single step is 1/60 s whatever the refresh was.
    Request.Seconds  = (!Gas.Paused && Record->Enabled) ? std::min(Delta, 0.033) : 0.0;
    Request.Steps    = Record->Enabled ? PendingSteps : 0u;
    Request.Clear    = ClearPending;
    Request.Bursts   = std::move(PendingBursts);
    Request.Brush    = Brush;
    Request.Camera   = Camera;
    Request.Extent   = { Width, Height };
    ClearPending     = false;
    PendingSteps     = 0;
    PendingBursts.clear();
    LastRequest = Request;
    Image       = Presenter(Request);
    if (!Image.Adapter.empty() && !LoggedDevice)
    {
        LoggedDevice = true;
        AppendLog("FLUID", "Vulkan ready \xC2\xB7 " + Image.Adapter, FluidEditorPages::FormatLattice(static_cast<int32_t>(Image.Readout.Resolution)) + " lattice");
    }
}

void FluidEditorWorkspace::Construct(ImVec2 Minimum, ImVec2 Maximum, float Scale, double DeltaSeconds)
{
    ImDrawList* Ink = ImGui::GetWindowDrawList();
    Controls.clear();
    if (Record == nullptr)
    {
        const Look Style{ InspectorPalette::Resolve(), EditorTypefaces::Resolve(), Scale };
        Style.Text(Ink, Style.Faces.Medium, 13.0f, ImVec2(Minimum.x + 24.0f, Minimum.y + 24.0f), Style.Palette.WarmAmber, "No FluidVolume record is attached to the editor.");
        return;
    }
    const float  Gap      = Gutter * Scale;
    const ImVec2 LeftMax(Minimum.x + OutlinerWidth * Scale, Maximum.y);
    const ImVec2 RightMin(Maximum.x - InspectorWidth * Scale, Minimum.y);
    const ImVec2 CentreMin(LeftMax.x + Gap, Minimum.y);
    const ImVec2 CentreMax(RightMin.x - Gap, Maximum.y - LogHeight * Scale - Gap);
    const ImVec2 LogMin(CentreMin.x, CentreMax.y + Gap);
    const ImVec2 LogMax(CentreMax.x, Maximum.y);

    // 📝 The picture follows the viewport's pixels, at the window's framebuffer scale; the transport lies under it inside the same shell.
    const float TransportHeight = 96.0f * Scale;
    ViewportMinimum             = ImVec2(CentreMin.x + 8.0f * Scale, CentreMin.y + HeaderHeight * Scale);
    ViewportMaximum             = ImVec2(CentreMax.x - 8.0f * Scale, CentreMax.y - TransportHeight - 6.0f * Scale);
    const float    Density = ImGui::GetWindowViewport() ? ImGui::GetWindowViewport()->FramebufferScale.y : 1.0f;
    const uint32_t Width   = static_cast<uint32_t>(std::max(64.0f, (ViewportMaximum.x - ViewportMinimum.x) * Density));
    const uint32_t Height  = static_cast<uint32_t>(std::max(64.0f, (ViewportMaximum.y - ViewportMinimum.y) * Density));
    Present(Width, Height, DeltaSeconds);

    ConstructOutliner(Ink, Minimum, LeftMax, Scale);
    ConstructViewport(Ink, CentreMin, CentreMax, Scale);
    ConstructLogStream(Ink, LogMin, LogMax, Scale);
    ConstructInspector(RightMin, Maximum, Scale);
    if (LibraryShown) ConstructLibrarySheet(Minimum, Maximum, Scale);

    // 📝 The prototype's keys (FluidPanel.js KeyPress): Space plays or pauses, S steps, R restarts, D bursts, F focuses, B shows the bounds, T turns the
    //    table, 1 2 3 choose the tool, / opens the presets, Escape closes them. Nothing fires while a field has the keyboard or a modifier is held.
    ImGuiIO& IO = ImGui::GetIO();
    if (!IO.WantTextInput && !IO.KeyCtrl && !IO.KeyAlt && !IO.KeySuper)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) Requests.push_back("toggle-play");
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) Requests.push_back("step");
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) Requests.push_back("restart");
        if (ImGui::IsKeyPressed(ImGuiKey_D, false)) Requests.push_back("burst");
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) Requests.push_back("camera:fit");
        if (ImGui::IsKeyPressed(ImGuiKey_B, false)) Requests.push_back("bounds:toggle");
        if (ImGui::IsKeyPressed(ImGuiKey_T, false)) Requests.push_back("turntable:toggle");
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) Requests.push_back("tool:orbit");
        if (ImGui::IsKeyPressed(ImGuiKey_2, false)) Requests.push_back("tool:flamethrower");
        if (ImGui::IsKeyPressed(ImGuiKey_3, false)) Requests.push_back("tool:detonate");
        if (ImGui::IsKeyPressed(ImGuiKey_Slash, false)) Requests.push_back("library:on");
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && LibraryShown) LibraryShown = false;
    }
    // 📝 An edit leaves its notice in the queue; three rounds settle a verb and the notices it raises.
    for (int Round = 0; Round < 3 && !Requests.empty(); ++Round)
    {
        std::vector<std::string> Pending;
        Pending.swap(Requests);
        for (const std::string& Action : Pending) Apply(Action);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    OUTLINER
//------------------------------------------------------------------------------------------------------------------------

void FluidEditorWorkspace::ConstructOutliner(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, float Scale)
{
    const Look Style{ InspectorPalette::Resolve(), EditorTypefaces::Resolve(), Scale };
    const auto& P = Style.Palette;
    Ink->AddRectFilled(Minimum, Maximum, P.ColumnBg, 14.0f * Scale);
    Ink->AddRect(Minimum, Maximum, P.EdgeBorder, 14.0f * Scale);
    float       Top  = Minimum.y + 16.0f * Scale;
    const float Left = Minimum.x + 16.0f * Scale, Right = Maximum.x - 16.0f * Scale;
    Style.Text(Ink, Style.Faces.Medium, 22.0f, ImVec2(Left, Top), P.TextPrimary, "Outliner");

    // 📝 The parts the tree lists: the domain, the emitter, the collider (only while there is one or it is selected) and the light.
    std::vector<FluidPart> Listed;
    for (size_t Index = 0; Index < FluidPartCount; ++Index)
    {
        const FluidPart Part = static_cast<FluidPart>(Index);
        if (FluidEditorPages::QueryPartListed(*Record, Part, Selected)) Listed.push_back(Part);
    }
    size_t Enabled = 0;
    for (FluidPart Part : Listed)
    {
        const bool On = Part == FluidPart::Domain ? Record->Enabled : FluidEditorPages::QueryPartEnabled(*Record, Part);
        if (On) ++Enabled;
    }
    char Subtitle[96];
    std::snprintf(Subtitle, sizeof(Subtitle), "%s \xC2\xB7 %zu parts", RecordName.c_str(), Listed.size());
    Style.Text(Ink, Style.Faces.Regular, 11.0f, ImVec2(Left, Top + 30.0f * Scale), P.TextSecondary, Subtitle);
    Top += 56.0f * Scale;

    // 📝 The two count cards (the prototype's enabled and disabled counts).
    const float CardW = (Right - Left - 8.0f * Scale) / 2.0f;
    for (int Card = 0; Card < 2; ++Card)
    {
        const ImVec2 Corner(Left + Card * (CardW + 8.0f * Scale), Top);
        Ink->AddRectFilled(Corner, ImVec2(Corner.x + CardW, Corner.y + 58.0f * Scale), P.CountCardBg, 12.0f * Scale);
        Ink->AddRect(Corner, ImVec2(Corner.x + CardW, Corner.y + 58.0f * Scale), P.CountCardBorder, 12.0f * Scale);
        GlyphSymbol::Construct(Ink, Card == 0 ? "Eye" : "EyeOff", ImVec2(Corner.x + 12.0f * Scale, Corner.y + 10.0f * Scale), 16.0f * Scale, 1.4f, Card == 0 ? P.LiveGreen : P.WarmAmber);
        Style.Text(Ink, Style.Faces.Regular, 10.5f, ImVec2(Corner.x + 12.0f * Scale, Corner.y + 36.0f * Scale), P.TextSecondary, Card == 0 ? "Enabled" : "Disabled");
        char Figure[16];
        std::snprintf(Figure, sizeof(Figure), "%zu", Card == 0 ? Enabled : Listed.size() - Enabled);
        Style.Text(Ink, Style.Faces.Light, 26.0f, ImVec2(Corner.x + CardW - Style.Measure(Style.Faces.Light, 26.0f, Figure) - 12.0f * Scale, Corner.y + 14.0f * Scale), P.TextPrimary, Figure);
    }
    Top += 70.0f * Scale;
    float Width = 0.0f;
    if (ConstructPillButton(Style, Ink, Controls, "library", ImVec2(Left, Top), "Presets", LibraryShown, "Library", &Width, false, "Fluid")) Requests.push_back("library:on");
    Top += 42.0f * Scale;

    // 📝 The tree, grouped as the prototype groups it: gas (domain, emitter), geometry (collider), light (sun).
    const auto Row = [&](FluidPart Part, const char* Badge)
    {
        const char*       Token   = FluidEditorPages::ResolveToken(Part);
        const bool        On      = Part == FluidPart::Domain ? Record->Enabled : FluidEditorPages::QueryPartEnabled(*Record, Part);
        const float       Height  = 40.0f * Scale;
        const ImVec2      RowMin(Left - 4.0f * Scale, Top);
        const ImVec2      RowMax(Right + 4.0f * Scale, Top + Height);
        const bool        Chosen  = Selected == Part;
        const std::string Name    = FluidEditorPages::ResolvePartName(Memory, Part);
        const std::string Figures = FluidEditorPages::FormatPartFigures(*Record, Part);
        ImGui::SetCursorScreenPos(RowMin);
        ImGui::PushID(Token);
        const bool Pressed = ImGui::InvisibleButton("##Row", ImVec2(RowMax.x - RowMin.x - 30.0f * Scale, Height));
        const bool Hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        Controls.push_back({ std::string("row:") + Token, RowMin, RowMax });
        if (Pressed) Requests.push_back(std::string("select:") + Token);
        if (Chosen || Hovered) Ink->AddRectFilled(RowMin, RowMax, Chosen ? P.RowSelectedTop : P.RowHoverBg, 9.0f * Scale);
        if (Chosen) Ink->AddRect(RowMin, RowMax, P.RowSelectedBorder, 9.0f * Scale);
        const ImVec2 Tile(Left, Top + 7.0f * Scale);
        Ink->AddRectFilled(Tile, ImVec2(Tile.x + 26.0f * Scale, Tile.y + 26.0f * Scale), P.CountCardBg, 7.0f * Scale);
        GlyphSymbol::Construct(Ink, FluidEditorPages::ResolvePartGlyph(*Record, Part), ImVec2(Tile.x + 5.0f * Scale, Tile.y + 5.0f * Scale), 16.0f * Scale, 1.4f,
                               On ? ResolvePartTint(*Record, Part) : P.TextFaint);
        Ink->PushClipRect(ImVec2(Left + 36.0f * Scale, Top), ImVec2(Right - (Badge[0] != '\0' ? 86.0f : 26.0f) * Scale, Top + Height), true);
        Style.Text(Ink, Style.Faces.Medium, 12.5f, ImVec2(Left + 36.0f * Scale, Top + 5.0f * Scale), On ? P.TextPrimary : P.TextSecondary, Name.c_str());
        Style.Text(Ink, Style.Faces.Regular, 10.0f, ImVec2(Left + 36.0f * Scale, Top + 22.0f * Scale), P.TextSecondary, Figures.c_str());
        Ink->PopClipRect();
        const ImVec2 EyeAt(Right - 18.0f * Scale, Top + 12.0f * Scale);
        ImGui::SetCursorScreenPos(ImVec2(EyeAt.x - 4.0f * Scale, EyeAt.y - 4.0f * Scale));
        ImGui::PushID(Token);
        if (ImGui::InvisibleButton("##Eye", ImVec2(24.0f * Scale, 24.0f * Scale)))
            Requests.push_back(Part == FluidPart::Domain ? std::string("enabled:toggle") : std::string("part:") + Token + ":toggle");
        ImGui::PopID();
        Controls.push_back({ std::string("eye:") + Token, ImVec2(EyeAt.x - 4.0f * Scale, EyeAt.y - 4.0f * Scale), ImVec2(EyeAt.x + 20.0f * Scale, EyeAt.y + 20.0f * Scale) });
        GlyphSymbol::Construct(Ink, On ? "Eye" : "EyeOff", EyeAt, 15.0f * Scale, 1.3f, On ? P.TextSecondary : P.TextFaint);
        // 📝 The domain's row carries the prototype's VOLUME badge, between its name and the eye.
        if (Badge[0] != '\0')
        {
            const float  BadgeW = Style.Measure(Style.Faces.Medium, 8.0f, Badge) + 12.0f * Scale;
            const ImVec2 BadgeMin(Right - 30.0f * Scale - BadgeW, Top + 12.0f * Scale);
            Ink->AddRect(BadgeMin, ImVec2(BadgeMin.x + BadgeW, BadgeMin.y + 16.0f * Scale), P.InsetFieldBorder, 8.0f * Scale);
            Style.Text(Ink, Style.Faces.Medium, 8.0f, ImVec2(BadgeMin.x + 6.0f * Scale, BadgeMin.y + 3.5f * Scale), P.TextSecondary, Badge);
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
    const ImVec2 ListMax(Maximum.x - 2.0f * Scale, Maximum.y - 48.0f * Scale);
    ImGui::SetCursorScreenPos(ListMin);
    ImGui::BeginChild("##FluidOutlinerList", ImVec2(ListMax.x - ListMin.x, ListMax.y - ListMin.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    Ink = ImGui::GetWindowDrawList();
    Top = ImGui::GetCursorScreenPos().y;
    Group("GAS", 2);
    Row(FluidPart::Domain, "VOLUME");
    Row(FluidPart::Emitter, "");
    if (FluidEditorPages::QueryPartListed(*Record, FluidPart::Collider, Selected))
    {
        Top += 6.0f * Scale;
        Group("GEOMETRY", 1);
        Row(FluidPart::Collider, "");
    }
    Top += 6.0f * Scale;
    Group("LIGHT", 1);
    Row(FluidPart::Sun, "");
    Top += 10.0f * Scale;

    // 📝 Add to scene: the prototype's menu (a fire or smoke emitter, a sphere, a cylinder or a deflector slab as the collider), as a row of pills.
    Style.Text(Ink, Style.Faces.SemiBold, 9.5f, ImVec2(Left, Top + 4.0f * Scale), P.TextMuted, "ADD TO SCENE");
    Top += 24.0f * Scale;
    struct AddEntry
    {
        const char* Token;
        const char* Label;
        const char* Glyph;
    };
    static constexpr AddEntry Additions[5] = { { "fire", "Fire", "Flame" }, { "smoke", "Smoke", "CloudFog" }, { "sphere", "Sphere", "Sphere" }, { "cylinder", "Cylinder", "Cylinder" }, { "slab", "Slab", "Square" } };
    float       AddX = Left;
    for (const AddEntry& Entry : Additions)
    {
        const float Needed = WindowControls::MeasurePillWidth(Style, Entry.Label, true, false);
        if (AddX + Needed > Right + 4.0f * Scale)
        {
            AddX = Left;
            Top += 36.0f * Scale;
        }
        char Name[32];
        std::snprintf(Name, sizeof(Name), "add:%s", Entry.Token);
        if (ConstructPillButton(Style, Ink, Controls, Name, ImVec2(AddX, Top), Entry.Label, false, Entry.Glyph, &Width, false, "Fluid")) Requests.push_back(std::string("add:") + Entry.Token);
        AddX += Width + 6.0f * Scale;
    }
    Top += 40.0f * Scale;
    ImGui::SetCursorScreenPos(ImVec2(ListMin.x, Top));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    ImGui::EndChild();

    Ink = ImGui::GetWindowDrawList();
    // 📝 The prototype's two footnotes, one under the other: they are longer than the column together.
    Style.Text(Ink, Style.Faces.Monospace, 9.0f, ImVec2(Left, Maximum.y - 38.0f * Scale), P.TextMuted, "EDITING LIVE SIMULATION");
    Style.Text(Ink, Style.Faces.Monospace, 9.0f, ImVec2(Left, Maximum.y - 24.0f * Scale), P.TextMuted, "ONE DOMAIN \xC2\xB7 ONE SOURCE \xC2\xB7 ONE COLLIDER");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LOG STREAM
//------------------------------------------------------------------------------------------------------------------------

void FluidEditorWorkspace::ConstructLogStream(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, float Scale)
{
    const Look Style{ InspectorPalette::Resolve(), EditorTypefaces::Resolve(), Scale };
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
        const FluidLogLine& Line = Log[Index];
        Ink->AddCircleFilled(ImVec2(Left + 4.0f * Scale, Top + 8.0f * Scale), 3.0f * Scale, Line.Warm ? P.WarmAmber : P.LiveGreen);
        Style.Text(Ink, Style.Faces.Monospace, 9.5f, ImVec2(Left + 18.0f * Scale, Top + 2.0f * Scale), P.TextMuted, Line.Time.c_str());
        const float  CategoryW = Style.Measure(Style.Faces.Medium, 8.5f, Line.Category.c_str()) + 16.0f * Scale;
        const ImVec2 Chip(Left + 86.0f * Scale, Top);
        Ink->AddRect(Chip, ImVec2(Chip.x + CategoryW, Chip.y + 17.0f * Scale), P.InsetFieldBorder, 8.0f * Scale);
        Style.Text(Ink, Style.Faces.Medium, 8.5f, ImVec2(Chip.x + 8.0f * Scale, Chip.y + 3.5f * Scale), P.TextSecondary, Line.Category.c_str());
        // 📝 The message and its detail are cut to the card: in a narrow column the detail goes first.
        const float       TextX   = Chip.x + std::max(CategoryW, 84.0f * Scale) + 12.0f * Scale;
        const float       Room    = Maximum.x - 14.0f * Scale - TextX;
        const std::string Message = WindowControls::FitText(Style, Style.Faces.Medium, 11.0f, Line.Text, Room);
        Style.Text(Ink, Style.Faces.Medium, 11.0f, ImVec2(TextX, Top + 1.5f * Scale), P.TextPrimary, Message.c_str());
        const float MessageW = Style.Measure(Style.Faces.Medium, 11.0f, Message.c_str());
        if (!Line.Detail.empty() && Message == Line.Text && Room - MessageW - 8.0f * Scale > 60.0f * Scale)
        {
            const std::string Detail = WindowControls::FitText(Style, Style.Faces.Regular, 10.5f, "\xC2\xB7 " + Line.Detail, Room - MessageW - 8.0f * Scale);
            Style.Text(Ink, Style.Faces.Regular, 10.5f, ImVec2(TextX + MessageW + 8.0f * Scale, Top + 2.0f * Scale), P.TextSecondary, Detail.c_str());
        }
        Top += 24.0f * Scale;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

void FluidEditorWorkspace::ConstructInspector(ImVec2 Minimum, ImVec2 Maximum, float Scale)
{
    const Look  Style{ InspectorPalette::Resolve(), EditorTypefaces::Resolve(), Scale };
    ImDrawList* Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Minimum, Maximum, Style.Palette.ColumnBg, 14.0f * Scale);
    Ink->AddRect(Minimum, Maximum, Style.Palette.EdgeBorder, 14.0f * Scale);
    ImGui::SetCursorScreenPos(ImVec2(Minimum.x + 1.0f, Minimum.y + 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild("##FluidInspector", ImVec2(Maximum.x - Minimum.x - 2.0f, Maximum.y - Minimum.y - 2.0f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground))
    {
        const ControlTheme Theme = ControlTheme::ResolveNative();
        ControlColumn      Page(Theme, Style.Faces, Scale);
        FluidEditorContext Context{ *Record, Memory, ResolveReadout(), Requests };
        FluidEditorPages::ConstructWindowPage(Page, Context, Selected, Tab);
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

} // namespace Frontier

#endif
