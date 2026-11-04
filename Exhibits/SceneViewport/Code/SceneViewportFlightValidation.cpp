/*==========================================================================================================================================
                                                  SCENEVIEWPORTFLIGHTVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 The Scene Viewport's Unreal-style camera (IntegrateSceneViewportFlight) against the renderer's own view basis.

#include "EverestWorkspace.h"
#include "Engine/Rendering/GlobalIllumination/TransformProjection.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{

using Vector3 = std::array<double, 3>;

constexpr double TickSeconds = 1.0 / 60.0;   // [s] interface tick of the checks

int FailureCount = 0;
int CheckCount   = 0;

void Verify(bool Condition, const std::string& Description, const std::string& Measured)
{
    ++CheckCount;
    if (!Condition) ++FailureCount;
    std::printf("  [%s] %s — %s\n", Condition ? "pass" : "FAIL", Description.c_str(), Measured.c_str());
}

std::string Describe(const char* Format, double First, double Second = 0.0, double Third = 0.0)
{
    char Text[256];
    std::snprintf(Text, sizeof(Text), Format, First, Second, Third);
    return Text;
}

Vector3 Subtract(const Vector3& Left, const Vector3& Right) { return {Left[0] - Right[0], Left[1] - Right[1], Left[2] - Right[2]}; }
double  Dot(const Vector3& Left, const Vector3& Right) { return Left[0] * Right[0] + Left[1] * Right[1] + Left[2] * Right[2]; }
double  Length(const Vector3& Value) { return std::sqrt(Dot(Value, Value)); }

// 📝 The renderer's screen axes for a camera: rows of ConstructViewTransform (column-major, element [Column * 4 + Row]).
struct ScreenAxes
{
    Vector3 Rightward{};   // [-] view +x: screen right
    Vector3 Upward{};      // [-] view +y: screen up
    Vector3 Forward{};     // [-] −(view +z): into the screen
};

ScreenAxes ResolveScreenAxes(const Everest::SceneViewportFlight& Flight)
{
    const Frontier::GlobalIllumination::Transform View = Frontier::GlobalIllumination::ConstructViewTransform(Flight.Position, Flight.ResolveTarget());
    return {{View[0], View[4], View[8]}, {View[1], View[5], View[9]}, {-View[2], -View[6], -View[10]}};
}

// 📝 Holds the right button over the viewport with the given flight keys for a duration.
void Fly(Everest::SceneViewportFlight& Flight, std::initializer_list<int> Keys, double Seconds, bool Shift = false)
{
    Everest::SceneViewportPointer Pointer;
    Pointer.Hovered   = true;
    Pointer.Active    = true;
    Pointer.RightDown = true;
    Pointer.ShiftDown = Shift;
    for (const int Key : Keys) Pointer.FlightKeys[static_cast<size_t>(Key)] = true;
    for (double Elapsed = 0.0; Elapsed < Seconds - 1.0e-9; Elapsed += TickSeconds) Everest::IntegrateSceneViewportFlight(Flight, Pointer, TickSeconds);
}

void Idle(Everest::SceneViewportFlight& Flight, double Seconds)
{
    const Everest::SceneViewportPointer Pointer;
    for (double Elapsed = 0.0; Elapsed < Seconds - 1.0e-9; Elapsed += TickSeconds) Everest::IntegrateSceneViewportFlight(Flight, Pointer, TickSeconds);
}

void Drag(Everest::SceneViewportFlight& Flight, bool Left, bool Right, bool Middle, bool Alt, double MotionX, double MotionY)
{
    Everest::SceneViewportPointer Pointer;
    Pointer.Hovered      = true;
    Pointer.Active       = true;
    Pointer.LeftDown     = Left;
    Pointer.RightDown    = Right;
    Pointer.MiddleDown   = Middle;
    Pointer.AltDown      = Alt;
    Pointer.MotionPixels = {MotionX, MotionY};
    Everest::IntegrateSceneViewportFlight(Flight, Pointer, TickSeconds);
}

void Wheel(Everest::SceneViewportFlight& Flight, double Notches, bool RightDown)
{
    Everest::SceneViewportPointer Pointer;
    Pointer.Hovered      = true;
    Pointer.Active       = RightDown;
    Pointer.RightDown    = RightDown;
    Pointer.WheelNotches = Notches;
    Everest::IntegrateSceneViewportFlight(Flight, Pointer, TickSeconds);
}

enum FlightKey : int { Forward = 0, Back, Left, Right, Up, Down, LocalUp, LocalDown, Widen, Narrow };

} // namespace

int main()
{
    std::printf("SceneViewportFlightValidation — Unreal-style viewport camera\n");

    // ① the default view is the former orbit view
    {
        const Everest::SceneViewportFlight Flight;
        const Vector3 Target = Flight.ResolveTarget();
        Verify(Length(Subtract(Target, {-10.0, 6.0, 3.0})) < 1.0e-3, "default view aims at the former orbit focus (−10, 6, 3)",
               Describe("target (%.4f, %.4f, %.4f)", Target[0], Target[1], Target[2]));
    }

    // ② D moves to the screen's right and A to its left, at every heading and elevation (the reported inversion)
    {
        int Correct = 0, Total = 0;
        double WorstRight = 1.0e9;
        for (const double Heading : {0.0, 37.57, 90.0, 180.0, -120.0})
        {
            for (const double Elevation : {-60.0, -5.23, 0.0, 45.0})
            {
                for (const int Key : {static_cast<int>(Right), static_cast<int>(Left)})
                {
                    Everest::SceneViewportFlight Flight;
                    Flight.Heading   = Heading;
                    Flight.Elevation = Elevation;
                    const ScreenAxes Axes   = ResolveScreenAxes(Flight);
                    const Vector3    Before = Flight.Position;
                    Fly(Flight, {Key}, 0.5);
                    const Vector3 Moved      = Subtract(Flight.Position, Before);
                    const double  Rightwards = Dot(Moved, Axes.Rightward) * (Key == Right ? 1.0 : -1.0);
                    const double  Tolerance  = 1.0e-6 * Length(Moved);   // [m] the renderer stores its basis at float precision
                    const bool    Lateral    = Rightwards > 0.0 && std::fabs(Dot(Moved, Axes.Forward)) < Tolerance && std::fabs(Dot(Moved, Axes.Upward)) < Tolerance;
                    Correct += Lateral ? 1 : 0;
                    ++Total;
                    WorstRight = std::min(WorstRight, Rightwards);
                }
            }
        }
        Verify(Correct == Total, "D moves along screen right and A along screen left (renderer's view +x), purely sideways",
               Describe("%.0f of %.0f headings × elevations × keys; least sideways travel %.3f m in 0.5 s", Correct, Total, WorstRight));
    }

    // ③ W/S along the view, E/Q along world z, R/F along the screen's up
    {
        Everest::SceneViewportFlight Flight;
        Flight.Elevation       = 30.0;
        const ScreenAxes Axes  = ResolveScreenAxes(Flight);
        Vector3          Start = Flight.Position;
        Fly(Flight, {Forward}, 0.5);
        const Vector3 ForwardMove = Subtract(Flight.Position, Start);
        Verify(Dot(ForwardMove, Axes.Forward) > 0.0 && Length(ForwardMove) - Dot(ForwardMove, Axes.Forward) < 1.0e-6 * Length(ForwardMove), "W moves into the screen, pitch included",
               Describe("%.3f m forward, rise %.3f m at 30° elevation", Dot(ForwardMove, Axes.Forward), ForwardMove[2]));
        Idle(Flight, 2.0);
        Start = Flight.Position;
        Fly(Flight, {Up}, 0.5);
        const Vector3 UpMove = Subtract(Flight.Position, Start);
        Verify(UpMove[2] > 0.0 && std::fabs(UpMove[0]) + std::fabs(UpMove[1]) < 1.0e-9, "E rises along world z", Describe("Δz %.3f m", UpMove[2]));
        Idle(Flight, 2.0);
        Start = Flight.Position;
        Fly(Flight, {LocalUp}, 0.5);
        const Vector3 LocalMove = Subtract(Flight.Position, Start);
        Verify(Dot(LocalMove, Axes.Upward) > 0.0 && Length(LocalMove) - Dot(LocalMove, Axes.Upward) < 1.0e-6 * Length(LocalMove), "R rises along the screen's up",
               Describe("%.3f m along screen up", Dot(LocalMove, Axes.Upward)));
    }

    // ④ Unreal's flight speeds: terminal 20 m/s at setting 4, ×2 with Shift, the eight-step table at 1 and 8
    {
        Everest::SceneViewportFlight Flight;
        Fly(Flight, {Forward}, 2.0);
        const double Default = Length(Flight.Velocity);
        Everest::SceneViewportFlight Boosted;
        Fly(Boosted, {Forward}, 2.0, true);
        const double Shifted = Length(Boosted.Velocity);
        Everest::SceneViewportFlight Fastest;
        Fastest.SpeedSetting = 8;
        Fly(Fastest, {Forward}, 2.0);
        Everest::SceneViewportFlight Slowest;
        Slowest.SpeedSetting = 1;
        Fly(Slowest, {Forward}, 2.0);
        Verify(std::fabs(Default - 20.0) < 0.01 && std::fabs(Shifted - 40.0) < 0.02, "terminal speed 20 m/s at setting 4, 40 m/s with Shift",
               Describe("%.3f m/s, %.3f m/s", Default, Shifted));
        Verify(std::fabs(Length(Fastest.Velocity) - 640.0) < 0.5 && std::fabs(Length(Slowest.Velocity) - 0.625) < 0.001,
               "settings 8 and 1 fly at 32× and 1/32× (Unreal's table)", Describe("%.2f m/s, %.4f m/s", Length(Fastest.Velocity), Length(Slowest.Velocity)));
        Everest::SceneViewportFlight Release = Flight;
        Idle(Release, 0.5);
        Verify(Length(Release.Velocity) < 0.01 * Default, "released flight settles within 0.5 s", Describe("%.4f m/s left", Length(Release.Velocity)));
        Everest::SceneViewportFlight Unpressed;
        Everest::SceneViewportPointer Hovering;
        Hovering.Hovered       = true;
        Hovering.FlightKeys[0] = true;
        for (int Tick = 0; Tick < 30; ++Tick) Everest::IntegrateSceneViewportFlight(Unpressed, Hovering, TickSeconds);
        Verify(Length(Subtract(Unpressed.Position, Everest::SceneViewportFlight{}.Position)) == 0.0, "W without the right button held does not move",
               Describe("%.6f m", Length(Subtract(Unpressed.Position, Everest::SceneViewportFlight{}.Position))));
    }

    // ⑤ the wheel: camera speed while the right button is held, a 0.96 m dolly step otherwise
    {
        Everest::SceneViewportFlight Flight;
        Wheel(Flight, 3.0, true);
        const int Raised = Flight.SpeedSetting;
        Wheel(Flight, 10.0, true);
        const int Capped = Flight.SpeedSetting;
        Wheel(Flight, -20.0, true);
        Verify(Raised == 7 && Capped == 8 && Flight.SpeedSetting == 1, "right button + wheel steps the speed setting 1–8",
               Describe("4 + 3 → %.0f, + 10 → %.0f, − 20 → %.0f", Raised, Capped, Flight.SpeedSetting));
        Everest::SceneViewportFlight Dolly;
        const Vector3 Forward = Dolly.ResolveForward();
        const Vector3 Before  = Dolly.Position;
        Wheel(Dolly, 1.0, false);
        const double Step = Dot(Subtract(Dolly.Position, Before), Forward);
        Verify(std::fabs(Step - 0.96) < 1.0e-9 && std::fabs(Dolly.PivotDistance - (65.88 - 0.96)) < 1.0e-9, "one wheel notch dollies 0.96 m toward the pivot",
               Describe("%.4f m, pivot %.2f m", Step, Dolly.PivotDistance));
    }

    // ⑥ drags: right turns in place, left moves over the ground and turns, middle pans without turning
    {
        Everest::SceneViewportFlight Flight;
        const ScreenAxes Before = ResolveScreenAxes(Flight);
        const Vector3    Origin = Flight.Position;
        Drag(Flight, false, true, false, false, 100.0, 50.0);
        const ScreenAxes After = ResolveScreenAxes(Flight);
        Verify(Dot(After.Forward, Before.Rightward) > 0.0 && Dot(After.Forward, Before.Upward) < 0.0 && Length(Subtract(Flight.Position, Origin)) == 0.0,
               "right drag right/down turns the view right and down, in place",
               Describe("heading %.2f° (−20°), elevation %.2f° (−10°)", Flight.Heading - 37.57, Flight.Elevation + 5.23));
        Everest::SceneViewportFlight Walk;
        const Vector3 WalkStart = Walk.Position;
        Drag(Walk, true, false, false, false, 0.0, -100.0);
        const Vector3 Walked = Subtract(Walk.Position, WalkStart);
        Verify(std::fabs(Length(Walked) - 1.0) < 1.0e-9 && Walked[2] == 0.0 && Dot(Walked, Walk.ResolveForward()) > 0.0,
               "left drag up moves forward over the ground, 1 cm per pixel at setting 4", Describe("%.4f m, Δz %.4f m", Length(Walked), Walked[2]));
        Everest::SceneViewportFlight Pan;
        const ScreenAxes PanAxes  = ResolveScreenAxes(Pan);
        const Vector3    PanStart = Pan.Position;
        Drag(Pan, false, false, true, false, 100.0, -100.0);
        const Vector3 Panned = Subtract(Pan.Position, PanStart);
        Verify(std::fabs(Dot(Panned, PanAxes.Rightward) - 1.0) < 1.0e-6 && std::fabs(Panned[2] - 1.0) < 1.0e-9 && Pan.Heading == 37.57,
               "middle drag right/up moves the camera right and up with the pointer, without turning",
               Describe("%.4f m right, %.4f m up", Dot(Panned, PanAxes.Rightward), Panned[2]));
    }

    // ⑦ Alt: orbit, dolly and track around the pivot
    {
        Everest::SceneViewportFlight Flight;
        const Vector3 Pivot = Flight.ResolveTarget();
        Drag(Flight, true, false, false, true, 150.0, -40.0);
        Verify(Length(Subtract(Flight.ResolveTarget(), Pivot)) < 1.0e-9 && std::fabs(Length(Subtract(Flight.Position, Pivot)) - 65.88) < 1.0e-9,
               "Alt + left drag orbits the pivot at constant distance", Describe("pivot drift %.2e m", Length(Subtract(Flight.ResolveTarget(), Pivot))));
        Drag(Flight, false, true, false, true, 60.0, 40.0);
        Verify(Length(Subtract(Flight.ResolveTarget(), Pivot)) < 1.0e-9 && Flight.PivotDistance < 65.88, "Alt + right drag right/down dollies toward the pivot",
               Describe("distance %.2f m", Flight.PivotDistance));
        const Vector3 TrackStart = Flight.Position;
        Drag(Flight, false, false, true, true, 80.0, 0.0);
        const Vector3 Tracked = Subtract(Flight.Position, TrackStart);
        Verify(Length(Subtract(Subtract(Flight.ResolveTarget(), Pivot), Tracked)) < 1.0e-9 && Length(Tracked) > 0.0, "Alt + middle drag tracks the camera and its pivot together",
               Describe("%.3f m", Length(Tracked)));
    }

    // ⑧ Z/C zoom while flying, recoiling once the right button is released; F frames the default view and keeps the speed
    {
        Everest::SceneViewportFlight Flight;
        const double Rest = Flight.ResolveFieldOfView();
        Fly(Flight, {Widen}, 0.3);
        const double Widened = Flight.ResolveFieldOfView();
        Idle(Flight, 1.0);
        Verify(Widened > Rest + 0.1 && std::fabs(Flight.ResolveFieldOfView() - Rest) < 1.0e-4, "Z widens the view while flying and recoils after release",
               Describe("%.2f° → %.2f° → %.4f°", Rest * 57.29577951308232, Widened * 57.29577951308232, Flight.ResolveFieldOfView() * 57.29577951308232));
        Flight.SpeedSetting = 6;
        Fly(Flight, {Forward}, 1.0);
        Idle(Flight, 1.0);
        Everest::SceneViewportPointer Recentre;
        Recentre.Hovered       = true;
        Recentre.RecentrePress = true;
        Everest::IntegrateSceneViewportFlight(Flight, Recentre, TickSeconds);
        Verify(Length(Subtract(Flight.ResolveTarget(), {-10.0, 6.0, 3.0})) < 1.0e-3 && Flight.SpeedSetting == 6, "F frames the default view and keeps the speed setting",
               Describe("setting %.0f", Flight.SpeedSetting));
    }

    std::printf("SceneViewportFlightValidation: %d checks, %d failing\n", CheckCount, FailureCount);
    return FailureCount == 0 ? 0 : 1;
}
