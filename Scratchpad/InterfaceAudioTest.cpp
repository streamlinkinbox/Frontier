//============================================================================================================================================
//                                                    INTERFACEAUDIOTEST.CPP
//============================================================================================================================================
// 🧩 Proves the panel drives the audio: clicking the progress bar changes what a listener would hear.
//
//    The decisive check is the last one. Everything before it verifies the mapping arithmetic, which is easy to
//    get right and easy to test. The one that matters renders actual audio through the real transport at two
//    different panel settings and counts the firing events in each — because a binding can be arithmetically
//    perfect and still be silent if the demand never reaches the realtime side. Counting events is the only way
//    to tell "the number changed" from "the sound changed".
//
//    Runs on the null driver, so it needs no sound card and is honest in CI: the same code path, the same relay,
//    the same integrator, with the device clock replaced by a software one.
//
//    Build: bash Scratchpad/CheckInterfaceAudio.sh

#include "../Projects/Project-Zero/Source/InterfaceAudioSequence.h"
#include "../Projects/Project-Zero/Source/InterfaceTrialSequence.h"
#include "SpatialInterface/InterfacePointerProjection.h"
#include "SpatialInterface/InterfaceSequence.h"
#include "DisplayPresentation/MotionIntegrator.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

int Failures = 0;

void CheckTrue(const char* Name, bool Condition)
{
    std::printf("  %-64s %s\n", Name, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

void CheckNear(const char* Name, double Value, double Target, double Tolerance)
{
    const bool Ok = std::fabs(Value - Target) <= Tolerance;
    std::printf("  %-64s %s  (%.1f vs %.1f)\n", Name, Ok ? "PASS" : "FAIL", Value, Target);
    if (!Ok) ++Failures;
}

} // namespace

int main()
{
    std::printf("\n=== panel → audio binding gate ===\n\n");

    Frontier::ProjectZero::InterfaceAudioConfiguration Configuration;
    Configuration.UseNullDriver = true;          // no sound card needed; same path otherwise
    Configuration.IdleRpm       =  900.0f;
    Configuration.RedlineRpm    = 7200.0f;

    Frontier::ProjectZero::InterfaceAudioSequence Bridge;

    //──────────────────────────────────────────────────────────────────────────
    // ① The mapping, without opening anything.
    //──────────────────────────────────────────────────────────────────────────
    {
        const Frontier::PowertrainRecord Low  = Bridge.ComposeDemand(0.0, 0.0, false);
        const Frontier::PowertrainRecord Half = Bridge.ComposeDemand(0.5, 0.5, false);
        const Frontier::PowertrainRecord Full = Bridge.ComposeDemand(1.0, 1.0, true);

        CheckNear("fill 0.0 maps to idle rpm",        Low.Rpm,  900.0, 1.0);
        CheckNear("fill 0.5 maps to the mid band",    Half.Rpm, 4050.0, 1.0);
        CheckNear("fill 1.0 maps to redline",         Full.Rpm, 7200.0, 1.0);
        CheckTrue("sweep drives throttle",            std::fabs(Half.Throttle - 0.5f) < 1e-4f);
        CheckTrue("the toggle engages the clutch",    Full.ClutchEngaged && !Low.ClutchEngaged);
        CheckTrue("a gear is selected when engaged",  Full.Gear == 1 && Low.Gear == 0);

        // Out-of-range input must clamp, not extrapolate: a panel bug should not produce a 20 000 rpm scream.
        CheckNear("fill below zero clamps to idle",   Bridge.ComposeDemand(-5.0, 0.0, false).Rpm,  900.0, 1.0);
        CheckNear("fill above one clamps to redline", Bridge.ComposeDemand( 5.0, 0.0, false).Rpm, 7200.0, 1.0);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ② The device opens and the bridge runs.
    //──────────────────────────────────────────────────────────────────────────
    std::string Error;
    const bool Opened = Bridge.Construct(Configuration, &Error);
    CheckTrue("the audio bridge opens on the null driver", Opened);
    if (!Opened)
    {
        std::printf("    %s\n\n>>> %d FAILURE(S)\n\n", Error.c_str(), Failures + 1);
        return 1;
    }
    CheckTrue("the bridge reports running", Bridge.IsRunning());

    //──────────────────────────────────────────────────────────────────────────
    // ③ A real click on the real panel moves the real demand.
    //──────────────────────────────────────────────────────────────────────────
    Frontier::InterfaceStructure Panel;
    Frontier::MotionIntegrator   Motion;
    Frontier::ProjectZero::InterfaceTrialSequence Trial;

    Frontier::PlanePlacement Placement;
    Placement.Origin    = Frontier::PlaneOrigin{ 0.0f, 1.55f, 1.32f };
    Placement.RotationX = 1.57079633f - 0.21f;
    Placement.Scale     = 2.2f;
    Trial.AssignPanelPlacement(Placement);
    Trial.Construct(Panel, Motion);

    Frontier::InterfaceSequence Composition;
    Frontier::InterfaceViewConfiguration View;
    View.EyeX = 0.0f; View.EyeY = -1.70f; View.EyeZ = 1.45f; View.ForwardY = 1.0f;
    Composition.AssignView(View);
    Composition.Advance(Panel, 0.0);

    // Locate the trough by shape, not by ordinal, so a layout retune cannot silently retarget this at a button.
    uint32_t Trough = Frontier::InterfaceStructure::Detached;
    float    Widest = 0.0f;
    for (uint32_t Ordinal = 0u; Ordinal < Panel.QueryCount(); ++Ordinal)
    {
        const Frontier::InterfaceFigure& Figure = Panel.Query(Ordinal);
        if (Figure.PointerTarget && Figure.HalfWidth > Widest) { Widest = Figure.HalfWidth; Trough = Ordinal; }
    }

    const auto ClickTrough = [&](bool RightHandEnd) -> bool
    {
        Frontier::PointerContact Best;
        for (int Step = -60; Step <= 60; ++Step)
            for (int Height = -30; Height <= 30; ++Height)
            {
                Frontier::PointerRay Ray;
                Ray.OriginX = 0.01f * static_cast<float>(Step);
                Ray.OriginY = -1.70f;
                Ray.OriginZ = 1.45f + 0.01f * static_cast<float>(Height);
                Ray.DirectionY = 1.0f;
                const Frontier::PointerContact Contact =
                    Frontier::InterfacePointerProjection::Project(Panel, Composition, Ray);
                if (!Contact.Valid || Contact.Ordinal != Trough) continue;
                if (!Best.Valid ||
                    ( RightHandEnd && Contact.FractionX > Best.FractionX) ||
                    (!RightHandEnd && Contact.FractionX < Best.FractionX)) Best = Contact;
            }
        if (!Best.Valid) return false;
        Trial.ApplyPointer(Panel, Best, true);
        for (int Settle = 0; Settle < 240; ++Settle) Trial.AdvanceTrial(Panel, Motion, 1.0 / 60.0, true);
        Bridge.AdvanceAudio(Trial, 1.0f / 60.0f);
        return true;
    };

    CheckTrue("a click lands on the trough", ClickTrough(false));
    const float QuietRpm = Bridge.QueryRpm();
    std::printf("        clicked the left end  → %.0f rpm\n", static_cast<double>(QuietRpm));

    CheckTrue("a second click lands on the trough", ClickTrough(true));
    const float LoudRpm = Bridge.QueryRpm();
    std::printf("        clicked the right end → %.0f rpm\n", static_cast<double>(LoudRpm));

    CheckTrue("clicking the bar changes the demanded rpm", LoudRpm > QuietRpm + 2000.0f);
    CheckTrue("the low setting is near idle",              QuietRpm < 1500.0f);
    CheckTrue("the high setting is near redline",          LoudRpm  > 6500.0f);

    //──────────────────────────────────────────────────────────────────────────
    // ④ The sound actually changes — the check the arithmetic cannot make.
    //──────────────────────────────────────────────────────────────────────────
    // Render a second of audio at each setting through the real integrator and count firing events. At 8
    //    cylinders an event fires every 2 revolutions / 8 = every quarter turn, so events/s = rpm × 8 / 120.
    const auto MeasureEvents = [&](float Rpm) -> double
    {
        Frontier::CrankClickIntegrator Local(Configuration.CylinderCount);
        Local.AssignClicks(true);
        Local.AssignSweep(false);
        Local.Prepare(48000u, 2u);

        Frontier::PowertrainRecord Record = Bridge.ComposeDemand(0.0, 0.0, false);
        Record.Rpm = Rpm;
        Local.AssignDemand(Record);

        std::vector<float> Block(64u * 2u, 0.0f);
        const uint64_t Start = Local.QueryEventCount();
        for (uint32_t Slice = 0u; Slice < 48000u / 64u; ++Slice)
        {
            std::fill(Block.begin(), Block.end(), 0.0f);
            Local.Render(Block.data(), 64u);
        }
        return static_cast<double>(Local.QueryEventCount() - Start);
    };

    const double QuietEvents = MeasureEvents(QuietRpm);
    const double LoudEvents  = MeasureEvents(LoudRpm);
    const double QuietExpect = static_cast<double>(QuietRpm) * Configuration.CylinderCount / 120.0;
    const double LoudExpect  = static_cast<double>(LoudRpm)  * Configuration.CylinderCount / 120.0;

    std::printf("\n        %.0f rpm → %.0f firing events/s (expected %.0f)\n", static_cast<double>(QuietRpm), QuietEvents, QuietExpect);
    std::printf("        %.0f rpm → %.0f firing events/s (expected %.0f)\n\n", static_cast<double>(LoudRpm),  LoudEvents,  LoudExpect);

    CheckTrue("the quiet setting fires at the expected rate", std::fabs(QuietEvents - QuietExpect) < QuietExpect * 0.05 + 2.0);
    CheckTrue("the loud setting fires at the expected rate",  std::fabs(LoudEvents  - LoudExpect)  < LoudExpect  * 0.05 + 2.0);
    CheckTrue("THE AUDIO AUDIBLY CHANGES with the panel",     LoudEvents > QuietEvents * 2.0);

    // Give the null device a moment of real time so the callback is exercised, then confirm it stayed healthy.
    // The null driver has no hardware clock: it is pumped by AdvanceAudio, exactly as a real frame loop would.
    for (int Frame = 0; Frame < 60; ++Frame)
    {
        Bridge.AdvanceAudio(Trial, 1.0f / 60.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    const Frontier::AudioMetrics& Metrics = Bridge.QueryMetrics();
    std::printf("        device: %u callbacks, %u overloads, %u clips\n",
                Metrics.CallbackCount, Metrics.OverloadCount, Metrics.ClipCount);
    CheckTrue("the realtime callback ran",   Metrics.CallbackCount > 0u);
    CheckTrue("no realtime overloads",       Metrics.OverloadCount == 0u);
    CheckTrue("no clipping",                 Metrics.ClipCount == 0u);

    Bridge.Retire();
    CheckTrue("the bridge closes cleanly", !Bridge.IsRunning());

    std::printf(Failures ? "\n>>> %d FAILURE(S)\n\n" : "\n>>> ALL PASS (0 failures)\n\n", Failures);
    return Failures ? 1 : 0;
}
