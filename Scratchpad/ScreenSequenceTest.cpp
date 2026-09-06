//============================================================================================================================================
//                                                    SCREENSEQUENCETEST.CPP
//============================================================================================================================================
// 🧩 The P3 gate. Screens fade, slide and wipe; transitions finish; and a moving screen cannot be clicked.
//
//    Transition bugs are quiet. A screen that never quite reaches 1.0 leaves the panel permanently unclickable; a
//    slide that offsets from the previous frame instead of from the layout walks a little further every cycle and
//    only becomes obvious after a minute; a retired screen that stays a pointer target lets a user click a control
//    that is not on screen. None of those throw, so each has its own check below.
//
//    Also asserts the promise the P0 plan made about this phase: P3 adds NO field to the GPU slot. If
//    InterfaceInstanceFigure moved, the director broke a contract three phases old.
//
//    Build: bash Scratchpad/CheckScreenSequence.sh

#include "SpatialInterface/InterfaceScreenSequence.h"
#include "SpatialInterface/InterfaceLayoutCodec.h"
#include "SpatialInterface/InterfaceSequence.h"
#include "SpatialInterface/InterfaceStructure.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int Failures = 0;

void CheckTrue(const char* Name, bool Condition)
{
    std::printf("  %-66s %s\n", Name, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

void CheckNear(const char* Name, float Value, float Target, float Tolerance)
{
    const bool Ok = std::fabs(Value - Target) <= Tolerance;
    std::printf("  %-66s %s  (%.4f vs %.4f)\n", Name, Ok ? "PASS" : "FAIL", Value, Target);
    if (!Ok) ++Failures;
}

// Two screens, one figure each, at known placements.
struct Fixture
{
    Frontier::InterfaceStructure Structure;
    uint32_t ScreenA = 0u;
    uint32_t ScreenB = 0u;

    explicit Fixture(Frontier::TransitionCategory Category, float Travel = 0.08f)
    {
        Frontier::InterfaceFigure Card;
        Card.HalfWidth  = 0.10f;
        Card.HalfHeight = 0.05f;
        Card.PointerTarget = true;

        Card.Placement.Origin = Frontier::PlaneOrigin{ 0.20f, 0.30f, 0.0f };
        ScreenA = Structure.Construct(Card);
        Card.Placement.Origin = Frontier::PlaneOrigin{ -0.20f, -0.30f, 0.0f };
        ScreenB = Structure.Construct(Card);

        Frontier::TransitionConfiguration Transition;
        Transition.Category = Category;
        Transition.Seconds  = 0.20f;
        Transition.Travel   = Travel;
        Director.Construct(0u, { ScreenA }, Transition);
        Director.Construct(1u, { ScreenB }, Transition);
    }

    void Run(float Seconds, float Step = 1.0f / 60.0f)
    {
        for (float Elapsed = 0.0f; Elapsed < Seconds; Elapsed += Step)
            Director.AdvanceScreens(Structure, Step);
    }

    Frontier::InterfaceScreenSequence Director;
};

} // namespace

int main()
{
    std::printf("\n=== P3 screen director gate ===\n\n");

    //──────────────────────────────────────────────────────────────────────────
    // ① The contract P0 made: P3 adds no field to the GPU slot.
    //──────────────────────────────────────────────────────────────────────────
    CheckTrue("the GPU slot is still 112 bytes (P3 added no field)",
              sizeof(Frontier::InterfaceInstanceFigure) == 112u);

    //──────────────────────────────────────────────────────────────────────────
    // ② A fade reaches 1.0 exactly, and reaches 0.0 exactly.
    //──────────────────────────────────────────────────────────────────────────
    {
        Fixture F(Frontier::TransitionCategory::Fade);
        CheckTrue("nothing is present before the first Present", F.Director.QueryPresence(0u) == 0.0f);

        F.Director.Present(0u);
        F.Run(0.05f);
        CheckTrue("a transition reports itself in progress", F.Director.IsTransitioning());
        CheckTrue("a moving screen is not interactive",
                  F.Director.QueryInteractiveScreen() == Frontier::InterfaceScreenSequence::NoScreen);

        F.Run(0.60f);
        CheckNear("the fade lands exactly on 1.0", F.Director.QueryPresence(0u), 1.0f, 1.0e-5f);
        CheckTrue("the transition finishes",       !F.Director.IsTransitioning());
        CheckTrue("the arrived screen is interactive", F.Director.QueryInteractiveScreen() == 0u);
        CheckNear("the figure is fully opaque", F.Structure.Query(F.ScreenA).Opacity, 1.0f, 1.0e-4f);

        // Switching away must retire the old screen completely.
        F.Director.Present(1u);
        F.Run(0.60f);
        CheckNear("the departed screen lands exactly on 0.0", F.Director.QueryPresence(0u), 0.0f, 1.0e-5f);
        CheckTrue("the departed figure is switched off", !F.Structure.Query(F.ScreenA).Visible);
        CheckTrue("the arriving figure is on",            F.Structure.Query(F.ScreenB).Visible);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ③ A retired screen is not clickable.
    //──────────────────────────────────────────────────────────────────────────
    {
        Fixture F(Frontier::TransitionCategory::Fade);
        F.Director.Present(0u);
        F.Run(0.60f);
        CheckTrue("the present screen keeps its pointer target",
                  F.Structure.Query(F.ScreenA).PointerTarget);

        F.Director.Present(1u);
        F.Run(0.05f);
        CheckTrue("a departing screen loses its pointer target",
                  !F.Structure.Query(F.ScreenA).PointerTarget);
        CheckTrue("an arriving screen is not yet a pointer target",
                  !F.Structure.Query(F.ScreenB).PointerTarget);

        // The latching bug: gating with `x = x && present` clears the flag on the first departure and never
        //    restores it, so a screen returned to is permanently dead. Only a round trip exposes it.
        F.Run(0.60f);
        F.Director.Present(0u);
        F.Run(0.60f);
        CheckTrue("a screen returned to is clickable again (no latch)",
                  F.Structure.Query(F.ScreenA).PointerTarget);
        CheckTrue("and the screen left behind is not",
                  !F.Structure.Query(F.ScreenB).PointerTarget);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ④ A slide must return to the LAYOUT position, not drift.
    //──────────────────────────────────────────────────────────────────────────
    // The first implementation wrote Origin.X += Travel * (1 - eased) every frame, which accumulates: the screen
    //    walks further right on every cycle and never comes home. Cycling repeatedly is the only way to see it.
    {
        Fixture F(Frontier::TransitionCategory::SlideX, 0.08f);
        const float RestX = F.Structure.Query(F.ScreenA).Placement.Origin.X;

        for (int Cycle = 0; Cycle < 6; ++Cycle)
        {
            F.Director.Present(0u);
            F.Run(0.40f);
            F.Director.Present(1u);
            F.Run(0.40f);
        }
        F.Director.Present(0u);
        F.Run(0.40f);

        CheckNear("a slide returns to its layout position after 6 cycles",
                  F.Structure.Query(F.ScreenA).Placement.Origin.X, RestX, 1.0e-4f);
    }

    // And mid-slide it really is displaced.
    {
        Fixture F(Frontier::TransitionCategory::SlideX, 0.08f);
        const float RestX = F.Structure.Query(F.ScreenA).Placement.Origin.X;
        F.Director.Present(0u);
        F.Run(0.05f);
        CheckTrue("a sliding screen is displaced while it moves",
                  std::fabs(F.Structure.Query(F.ScreenA).Placement.Origin.X - RestX) > 0.01f);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑤ A wipe opens the clip rectangle across the figure.
    //──────────────────────────────────────────────────────────────────────────
    {
        Fixture F(Frontier::TransitionCategory::Wipe);
        F.Director.Present(0u);
        F.Run(0.02f);
        const float Early = F.Structure.Query(F.ScreenA).ClipMaximumX;
        F.Run(0.60f);
        const float Late  = F.Structure.Query(F.ScreenA).ClipMaximumX;

        CheckTrue("a wipe starts clipped near the left edge",
                  Early < -F.Structure.Query(F.ScreenA).HalfWidth * 0.5f);
        CheckNear("a wipe finishes fully open", Late, F.Structure.Query(F.ScreenA).HalfWidth, 1.0e-4f);
        CheckTrue("a wipe opens rather than closes", Late > Early);
        CheckNear("a wiped figure keeps full opacity", F.Structure.Query(F.ScreenA).Opacity, 1.0f, 1.0e-4f);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑥ Immediate is instant, and NoScreen dismisses everything.
    //──────────────────────────────────────────────────────────────────────────
    {
        Fixture F(Frontier::TransitionCategory::Immediate);
        F.Director.Present(0u);
        F.Director.AdvanceScreens(F.Structure, 1.0f / 60.0f);
        CheckNear("an immediate screen arrives in one frame", F.Director.QueryPresence(0u), 1.0f, 1.0e-5f);
        CheckTrue("and is interactive at once", F.Director.QueryInteractiveScreen() == 0u);

        F.Director.Present(Frontier::InterfaceScreenSequence::NoScreen);
        F.Run(0.60f);
        CheckNear("NoScreen retires everything", F.Director.QueryPresence(0u), 0.0f, 1.0e-5f);
        CheckTrue("nothing is interactive afterwards",
                  F.Director.QueryInteractiveScreen() == Frontier::InterfaceScreenSequence::NoScreen);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑦ Screens still compose to ONE draw — the batcher must not be disturbed.
    //──────────────────────────────────────────────────────────────────────────
    {
        Fixture F(Frontier::TransitionCategory::Fade);
        F.Director.Present(0u);
        F.Run(0.60f);

        Frontier::InterfaceSequence Composition;
        Frontier::InterfaceViewConfiguration View;
        View.EyeZ = 1.0f; View.ForwardY = 1.0f;
        Composition.AssignView(View);
        Composition.Advance(F.Structure, 0.0);

        std::printf("  with one screen present: %u instances, %u draw(s), %u skipped\n",
                    Composition.QueryInstanceCount(), Composition.QueryMetrics().DrawCount,
                    Composition.QueryMetrics().SkippedCount);
        CheckTrue("screens still cost one draw", Composition.QueryMetrics().DrawCount == 1u);
        // The retired screen must be skipped entirely rather than uploaded transparent.
        CheckTrue("the retired screen is not uploaded", Composition.QueryInstanceCount() == 1u);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑧ Redeclaring a screen replaces it rather than duplicating.
    //──────────────────────────────────────────────────────────────────────────
    {
        Fixture F(Frontier::TransitionCategory::Fade);
        const uint32_t Before = F.Director.QueryScreenCount();
        Frontier::TransitionConfiguration Replacement;
        Replacement.Category = Frontier::TransitionCategory::Wipe;
        F.Director.Construct(0u, { F.ScreenA }, Replacement);
        CheckTrue("redeclaring a screen does not duplicate it", F.Director.QueryScreenCount() == Before);
    }

    std::printf(Failures ? "\n>>> %d FAILURE(S)\n\n" : "\n>>> ALL PASS (0 failures)\n\n", Failures);
    return Failures ? 1 : 0;
}
