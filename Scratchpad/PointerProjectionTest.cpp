//============================================================================================================================================
//                                                   POINTERPROJECTIONTEST.CPP
//============================================================================================================================================
// 🧩 The P2 gate. Proves a world ray lands on the right figure, at the right place on it, with the right sign.
//
//    Interaction fails quietly. A hit-test that is mirrored, or off by a corner radius, or that picks the housing
//    behind a knob, produces a UI that "mostly works" — and the bug surfaces later as a control that feels wrong
//    rather than as an error. So this asserts the things that would otherwise be discovered by hand:
//      · a ray down the middle hits the figure the pointer is over, not the one behind it
//      · the local coordinate has the correct SIGN in both axes (a mirrored panel is the classic failure)
//      · rounded corners are respected — a ray just outside the corner arc misses, though it is inside the extent
//      · figures without PointerTarget are ignored entirely
//      · the depth order is nearest-first, so a raised knob beats its bed
//      · a ray pointing away, or parallel to the plane, reports nothing rather than a hit behind the eye
//
//    Build: bash Scratchpad/CheckPointerProjection.sh

#include "SpatialInterface/InterfacePointerProjection.h"
#include "SpatialInterface/InterfaceSequence.h"
#include "SpatialInterface/InterfaceStructure.h"
#include "DisplayPresentation/MotionIntegrator.h"
#include "../Projects/Project-Zero/Source/InterfaceTrialSequence.h"

#include <cmath>
#include <cstdio>

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

// A ray fired straight along +Y at a given height and lateral offset — the showroom camera's axis.
Frontier::PointerRay AimAt(float X, float Z)
{
    Frontier::PointerRay Ray;
    Ray.OriginX = X; Ray.OriginY = -1.70f; Ray.OriginZ = Z;
    Ray.DirectionX = 0.0f; Ray.DirectionY = 1.0f; Ray.DirectionZ = 0.0f;
    return Ray;
}

} // namespace

int main()
{
    std::printf("\n=== P2 pointer projection gate ===\n\n");

    //──────────────────────────────────────────────────────────────────────────
    // A panel standing upright at y = 1.5, facing −Y, with two controls on it.
    //──────────────────────────────────────────────────────────────────────────
    Frontier::InterfaceStructure Structure;

    Frontier::InterfaceFigure Housing;
    Housing.Category      = Frontier::InterfaceCategory::Surface;
    Housing.HalfWidth     = 0.40f;
    Housing.HalfHeight    = 0.25f;
    Housing.PointerTarget = true;
    Housing.Placement.Origin    = Frontier::PlaneOrigin{ 0.0f, 1.50f, 1.30f };
    Housing.Placement.RotationX = 1.57079633f;          // stand it up: local +Y → world +Z, face → −Y
    const uint32_t HousingOrdinal = Structure.Construct(Housing);

    // A button to the RIGHT of centre and ABOVE it, lifted slightly off the housing.
    Frontier::InterfaceFigure Button;
    Button.Category      = Frontier::InterfaceCategory::Surface;
    Button.HalfWidth     = 0.08f;
    Button.HalfHeight    = 0.05f;
    Button.PointerTarget = true;
    Button.Placement.Origin = Frontier::PlaneOrigin{ 0.18f, 0.10f, 0.010f };
    const uint32_t ButtonOrdinal = Structure.Construct(Button);
    (void)Structure.Attach(ButtonOrdinal, HousingOrdinal);

    // A decorative lamp that must NEVER be picked.
    Frontier::InterfaceFigure Lamp;
    Lamp.Category      = Frontier::InterfaceCategory::Lamp;
    Lamp.HalfWidth     = 0.06f;
    Lamp.HalfHeight    = 0.06f;
    Lamp.PointerTarget = false;                          // deliberately not a target
    Lamp.Placement.Origin = Frontier::PlaneOrigin{ -0.20f, 0.10f, 0.010f };
    const uint32_t LampOrdinal = Structure.Construct(Lamp);
    (void)Structure.Attach(LampOrdinal, HousingOrdinal);

    Frontier::InterfaceSequence Composition;
    Frontier::InterfaceViewConfiguration View;
    View.EyeX = 0.0f; View.EyeY = -1.70f; View.EyeZ = 1.30f;
    View.ForwardX = 0.0f; View.ForwardY = 1.0f; View.ForwardZ = 0.0f;
    Composition.AssignView(View);
    Composition.Advance(Structure, 0.0);

    std::printf("  panel at y=1.50, housing 0.80 x 0.50 m, button at local (+0.18, +0.10)\n\n");

    //──────────────────────────────────────────────────────────────────────────
    // ① Centre of the housing.
    //──────────────────────────────────────────────────────────────────────────
    {
        const Frontier::PointerContact Hit =
            Frontier::InterfacePointerProjection::Project(Structure, Composition, AimAt(0.0f, 1.30f));
        CheckTrue ("a ray at the centre hits the housing", Hit.Valid && Hit.Ordinal == HousingOrdinal);
        CheckNear ("centre hit is at local origin X", Hit.LocalX, 0.0f, 1.0e-4f);
        CheckNear ("centre hit is at local origin Y", Hit.LocalY, 0.0f, 1.0e-4f);
        CheckNear ("distance is the panel depth",     Hit.Distance, 3.20f, 1.0e-3f);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ② Sign convention — the classic mirrored-panel bug.
    //──────────────────────────────────────────────────────────────────────────
    // The panel faces −Y. Aiming to the RIGHT in world +X must read as +X in the panel's own frame, and aiming
    //    HIGHER in world +Z must read as +Y. Getting either backwards mirrors every control on the panel.
    {
        const Frontier::PointerContact Right =
            Frontier::InterfacePointerProjection::Project(Structure, Composition, AimAt(0.20f, 1.30f));
        CheckTrue("aiming world +X reads as local +X (not mirrored)", Right.Valid && Right.LocalX > 0.15f);

        const Frontier::PointerContact Above =
            Frontier::InterfacePointerProjection::Project(Structure, Composition, AimAt(0.0f, 1.45f));
        CheckTrue("aiming world +Z reads as local +Y (not flipped)", Above.Valid && Above.LocalY > 0.10f);

        const Frontier::PointerContact Left =
            Frontier::InterfacePointerProjection::Project(Structure, Composition, AimAt(-0.20f, 1.30f));
        CheckTrue("aiming world -X reads as local -X", Left.Valid && Left.LocalX < -0.15f);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ③ The nearer figure wins — a raised button must beat the housing behind it.
    //──────────────────────────────────────────────────────────────────────────
    {
        const Frontier::PointerContact Hit =
            Frontier::InterfacePointerProjection::Project(Structure, Composition, AimAt(0.18f, 1.40f));
        CheckTrue("a ray at the button picks the button, not the housing",
                  Hit.Valid && Hit.Ordinal == ButtonOrdinal);
        CheckNear("the button hit is centred on the button", Hit.LocalX, 0.0f, 0.02f);
        CheckNear("fraction at the button centre is zero",   Hit.FractionX, 0.0f, 0.2f);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ④ Fractions reach ±1 at the edges.
    //──────────────────────────────────────────────────────────────────────────
    {
        const Frontier::PointerContact Edge =
            Frontier::InterfacePointerProjection::Project(Structure, Composition, AimAt(0.39f, 1.30f));
        CheckTrue ("a ray near the right edge still hits", Edge.Valid && Edge.Ordinal == HousingOrdinal);
        CheckNear ("fraction approaches +1 at the edge", Edge.FractionX, 0.975f, 0.02f);

        const Frontier::PointerContact Beyond =
            Frontier::InterfacePointerProjection::Project(Structure, Composition, AimAt(0.45f, 1.30f));
        CheckTrue("a ray past the edge misses entirely", !Beyond.Valid);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑤ PointerTarget is honoured.
    //──────────────────────────────────────────────────────────────────────────
    {
        // Straight at the lamp: it must fall through to the housing behind it rather than pick the lamp.
        const Frontier::PointerContact Hit =
            Frontier::InterfacePointerProjection::Project(Structure, Composition, AimAt(-0.20f, 1.40f));
        CheckTrue("a non-target figure is never picked", Hit.Valid && Hit.Ordinal != LampOrdinal);
        CheckTrue("the ray falls through to the housing", Hit.Valid && Hit.Ordinal == HousingOrdinal);
        (void)LampOrdinal;
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑥ Rounded corners are respected.
    //──────────────────────────────────────────────────────────────────────────
    {
        Frontier::InterfaceStructure Rounded;
        Frontier::InterfaceFigure Card;
        Card.HalfWidth     = 0.20f;
        Card.HalfHeight    = 0.20f;
        Card.CornerRadius  = 0.10f;
        Card.PointerTarget = true;
        Card.Placement.Origin    = Frontier::PlaneOrigin{ 0.0f, 1.50f, 1.30f };
        Card.Placement.RotationX = 1.57079633f;
        (void)Rounded.Construct(Card);

        Frontier::InterfaceSequence RoundedComposition;
        RoundedComposition.AssignView(View);
        RoundedComposition.Advance(Rounded, 0.0);

        // Dead centre of a corner cell: inside the square extent, outside the corner arc.
        const Frontier::PointerContact Corner =
            Frontier::InterfacePointerProjection::Project(Rounded, RoundedComposition, AimAt(0.195f, 1.495f));
        CheckTrue("a ray outside the corner arc misses (radius honoured)", !Corner.Valid);

        const Frontier::PointerContact Middle =
            Frontier::InterfacePointerProjection::Project(Rounded, RoundedComposition, AimAt(0.0f, 1.30f));
        CheckTrue("the same card is still hit through its middle", Middle.Valid);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑦ Degenerate rays report nothing rather than a phantom hit.
    //──────────────────────────────────────────────────────────────────────────
    {
        Frontier::PointerRay Backwards = AimAt(0.0f, 1.30f);
        Backwards.DirectionY = -1.0f;                                  // pointing away from the panel
        CheckTrue("a ray pointing away reports no hit",
                  !Frontier::InterfacePointerProjection::Project(Structure, Composition, Backwards).Valid);

        Frontier::PointerRay Parallel = AimAt(0.0f, 1.30f);
        Parallel.DirectionX = 1.0f; Parallel.DirectionY = 0.0f;        // grazing the plane
        CheckTrue("a ray parallel to the panel reports no hit",
                  !Frontier::InterfacePointerProjection::Project(Structure, Composition, Parallel).Valid);

        Frontier::PointerRay Short = AimAt(0.0f, 1.30f);
        Short.Reach = 1.0f;                                            // panel is 3.2 m away
        CheckTrue("a ray that stops short reports no hit",
                  !Frontier::InterfacePointerProjection::Project(Structure, Composition, Short).Valid);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑧ Viewport ray construction agrees with the render convention.
    //──────────────────────────────────────────────────────────────────────────
    {
        const float Eye[3]     = { 0.0f, -1.70f, 1.30f };
        const float Forward[3] = { 0.0f, 1.0f, 0.0f };
        const float Right[3]   = { 1.0f, 0.0f, 0.0f };
        const float Up[3]      = { 0.0f, 0.0f, 1.0f };
        const float TanHalf    = std::tan(0.5f * 55.0f * 3.14159265f / 180.0f);

        const Frontier::PointerRay Centre =
            Frontier::InterfacePointerProjection::ConstructViewportRay(640.0f, 360.0f, 1280u, 720u,
                                                                       Eye, Forward, Right, Up, TanHalf, 16.0f / 9.0f);
        CheckNear("a centre pixel looks straight ahead", Centre.DirectionY, 1.0f, 1.0e-3f);

        // Cursor ABOVE centre (smaller Y in framebuffer coordinates) must look UP in the world.
        const Frontier::PointerRay Upper =
            Frontier::InterfacePointerProjection::ConstructViewportRay(640.0f, 200.0f, 1280u, 720u,
                                                                       Eye, Forward, Right, Up, TanHalf, 16.0f / 9.0f);
        CheckTrue("a pixel above centre looks upward (Y flip correct)", Upper.DirectionZ > 0.05f);

        // Cursor RIGHT of centre must look right.
        const Frontier::PointerRay Rightward =
            Frontier::InterfacePointerProjection::ConstructViewportRay(1000.0f, 360.0f, 1280u, 720u,
                                                                       Eye, Forward, Right, Up, TanHalf, 16.0f / 9.0f);
        CheckTrue("a pixel right of centre looks rightward", Rightward.DirectionX > 0.05f);

        // End to end: a pixel right of centre must land on the panel at a positive local X. Pixel 680 is chosen
        //    because it lands at x ≈ +0.19 m, comfortably inside the 0.40 m half width — pixel 1000 projects to
        //    x ≈ +1.67 m and misses the panel entirely, which is correct behaviour and a useless assertion.
        const Frontier::PointerRay Aimed =
            Frontier::InterfacePointerProjection::ConstructViewportRay(680.0f, 360.0f, 1280u, 720u,
                                                                       Eye, Forward, Right, Up, TanHalf, 16.0f / 9.0f);
        const Frontier::PointerContact Hit =
            Frontier::InterfacePointerProjection::Project(Structure, Composition, Aimed);
        CheckTrue("a viewport ray lands on the panel at positive local X", Hit.Valid && Hit.LocalX > 0.0f);
        CheckNear("and lands where the projection predicts", Hit.LocalX, 0.187f, 0.01f);

        // A pixel far off to the side must MISS, not clamp to the panel edge.
        CheckTrue("a pixel aimed past the panel misses it",
                  !Frontier::InterfacePointerProjection::Project(Structure, Composition, Rightward).Valid);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑨ End to end on the REAL trial panel: a click changes a real value.
    //──────────────────────────────────────────────────────────────────────────
    // Everything above is geometry. This is the part that matters to a user: aiming at the progress trough and
    //    clicking must move the fill, and clicking nothing must not.
    {
        Frontier::InterfaceStructure Panel;
        Frontier::MotionIntegrator   Motion;
        Frontier::ProjectZero::InterfaceTrialSequence Trial;

        Frontier::PlanePlacement Placement;
        Placement.Origin    = Frontier::PlaneOrigin{ 0.0f, 1.55f, 1.32f };
        Placement.RotationX = 1.57079633f - 0.21f;
        Placement.Scale     = 2.2f;
        Trial.AssignPanelPlacement(Placement);
        Trial.Construct(Panel, Motion);

        Frontier::InterfaceSequence PanelComposition;
        Frontier::InterfaceViewConfiguration PanelView;
        PanelView.EyeX = 0.0f; PanelView.EyeY = -1.70f; PanelView.EyeZ = 1.45f;
        PanelView.ForwardY = 1.0f;
        PanelComposition.AssignView(PanelView);
        PanelComposition.Advance(Panel, 0.0);

        // Aim at the RIGHT-HAND END of the progress trough, which should drive the fill toward 1.0. Sweeping
        //    blindly and accepting any change was the first version of this check and it was weak: the first hit
        //    it found was the trough's far-left edge, which sets the fill to 0.025 — a real response, indis-
        //    tinguishable from no response. Asserting a SPECIFIC value proves the fraction is mapped correctly,
        //    not merely that something moved.
        //
        //    The trough is found by scanning for the widest pointer target rather than by hard-coding an ordinal,
        //    so retuning the layout does not silently retarget this at a button.
        uint32_t Trough = Frontier::InterfaceStructure::Detached;
        float    Widest = 0.0f;
        for (uint32_t Ordinal = 0u; Ordinal < Panel.QueryCount(); ++Ordinal)
        {
            const Frontier::InterfaceFigure& Figure = Panel.Query(Ordinal);
            if (Figure.PointerTarget && Figure.HalfWidth > Widest) { Widest = Figure.HalfWidth; Trough = Ordinal; }
        }
        CheckTrue("the progress trough is a pointer target", Trough != Frontier::InterfaceStructure::Detached);

        // Walk rays across the panel and keep the one that lands furthest right ON the trough.
        Frontier::PointerContact Best;
        for (int Step = -60; Step <= 60; ++Step)
        {
            for (int Height = -30; Height <= 30; ++Height)
            {
                Frontier::PointerRay Ray;
                Ray.OriginX = 0.01f * static_cast<float>(Step);
                Ray.OriginY = -1.70f;
                Ray.OriginZ = 1.45f + 0.01f * static_cast<float>(Height);
                Ray.DirectionY = 1.0f;
                const Frontier::PointerContact Contact =
                    Frontier::InterfacePointerProjection::Project(Panel, PanelComposition, Ray);
                if (Contact.Valid && Contact.Ordinal == Trough && (!Best.Valid || Contact.FractionX > Best.FractionX))
                    Best = Contact;
            }
        }
        CheckTrue("a ray reaches the right-hand end of the trough", Best.Valid && Best.FractionX > 0.5f);

        Trial.ApplyPointer(Panel, Best, true);
        for (int Settle = 0; Settle < 240; ++Settle) Trial.AdvanceTrial(Panel, Motion, 1.0 / 60.0, true);

        const double Expected = (Best.FractionX + 1.0) * 0.5;
        std::printf("        clicked trough at fraction %+.2f -> fill %.3f (expected %.3f)\n",
                    Best.FractionX, Trial.QueryFillValue(), Expected);
        CheckNear("the click drives the fill to the clicked position",
                  static_cast<float>(Trial.QueryFillValue()), static_cast<float>(Expected), 0.05f);
        CheckTrue("the pointer takes over from the scripted loop",      Trial.IsPointerDriven());

        // And a miss must clear the hover rather than latch it.
        Frontier::PointerContact Nothing;
        Trial.ApplyPointer(Panel, Nothing, false);
        CheckTrue("a pointer off the panel clears the hover",
                  Trial.QueryHoveredOrdinal() == Frontier::InterfaceStructure::Detached);
    }

    std::printf(Failures ? "\n>>> %d FAILURE(S)\n\n" : "\n>>> ALL PASS (0 failures)\n\n", Failures);
    return Failures ? 1 : 0;
}
