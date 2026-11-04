/*==========================================================================================================================================
                                                        EVERESTVIEWPORTNAVIGATION.CPP
==========================================================================================================================================*/
// 🧩 Selected-target orbit/chase, fixed-axis perspective views and wide-range space flight over the existing free-camera integrator.

#include "EverestWorkspace.h"
#include <algorithm>
#include <cmath>

namespace Everest
{
namespace
{
constexpr double      Pi = 3.14159265358979323846;
std::array<double, 3> Subtract(const std::array<double, 3> &First, const std::array<double, 3> &Last)
{
    return {First[0] - Last[0], First[1] - Last[1], First[2] - Last[2]};
}
void LookAt(SceneViewportFlight &Flight, const std::array<double, 3> &Target)
{
    const auto   Direction = Subtract(Target, Flight.Position);
    const double Length    = std::hypot(Direction[0], Direction[1], Direction[2]);
    if (Length < .00001) return;
    Flight.Heading       = std::atan2(Direction[1], Direction[0]) * 180 / Pi;
    Flight.Elevation     = std::asin(std::clamp(Direction[2] / Length, -1.0, 1.0)) * 180 / Pi;
    Flight.PivotDistance = Length;
}
bool AxisView(SceneCameraMode Mode)
{
    return Mode >= SceneCameraMode::Top && Mode <= SceneCameraMode::Right;
}
} // namespace

void ConfigureSceneCamera(SceneViewportFlight      &Flight,
                          SceneViewportNavigation  &Navigation,
                          SceneCameraMode           Mode,
                          const SceneCameraSubject &Subject,
                          bool                      FrameSelection)
{
    if (Navigation.Mode == SceneCameraMode::Space && Mode != SceneCameraMode::Space && Navigation.ScenePoseReady)
        Flight = Navigation.ScenePose;
    if (Mode == SceneCameraMode::Space && Navigation.Mode != SceneCameraMode::Space)
    {
        Navigation.ScenePose       = Flight;
        Navigation.ScenePoseReady  = true;
        Flight.Position[2]         = 150000;
        Flight.SpeedSetting        = 4;
        Flight.Elevation           = 15;
        Flight.VerticalFieldOfView = 70 * Pi / 180;
    }
    Navigation.Mode = Mode;
    Navigation.Up   = {0, 0, 1};
    Flight.Velocity = {0, 0, 0};
    if (Subject.Celestial && Subject.Available && !AxisView(Mode))
    {
        Flight.Heading   = Subject.Azimuth;
        Flight.Elevation = Subject.Elevation;
        if (FrameSelection) Flight.VerticalFieldOfView = std::clamp(Subject.Diameter * 2.4, 3.0, 110.0) * Pi / 180;
        Navigation.Anchor = Flight.ResolveTarget();
        return;
    }
    Navigation.Anchor     = Subject.Available ? Subject.Position : Flight.ResolveTarget();
    const double Distance = std::max(std::max(1.0, Subject.Celestial ? 50.0 : Subject.Radius) * 3.2, 2.0);
    if (FrameSelection || AxisView(Mode))
    {
        auto Forward = Flight.ResolveForward();
        switch (Mode)
        {
        case SceneCameraMode::Top:
            Forward       = {0, 0, -1};
            Navigation.Up = {0, 1, 0};
            break;
        case SceneCameraMode::Bottom:
            Forward       = {0, 0, 1};
            Navigation.Up = {0, 1, 0};
            break;
        case SceneCameraMode::Front:
            Forward = {1, 0, 0};
            break;
        case SceneCameraMode::Back:
            Forward = {-1, 0, 0};
            break;
        case SceneCameraMode::Left:
            Forward = {0, 1, 0};
            break;
        case SceneCameraMode::Right:
            Forward = {0, -1, 0};
            break;
        default:
            break;
        }
        for (size_t Axis = 0; Axis < 3; ++Axis)
            Flight.Position[Axis] = Navigation.Anchor[Axis] - Forward[Axis] * Distance;
    }
    LookAt(Flight, Navigation.Anchor);
    if (Mode == SceneCameraMode::Free) Flight.Elevation = std::clamp(Flight.Elevation, -89.0, 89.0);
    Navigation.ChaseOffset = Subtract(Flight.Position, Navigation.Anchor);
}

void IntegrateSceneCamera(SceneViewportFlight        &Flight,
                          SceneViewportNavigation    &Navigation,
                          const SceneViewportPointer &Pointer,
                          const SceneCameraSubject   &Subject,
                          double                      DeltaSeconds)
{
    if (Pointer.RecentrePress)
    {
        ConfigureSceneCamera(Flight, Navigation, Navigation.Mode, Subject, true);
        return;
    }
    if (Navigation.Mode == SceneCameraMode::Free || Navigation.Mode == SceneCameraMode::Space)
    {
        const auto Before = Flight.Position;
        IntegrateSceneViewportFlight(Flight, Pointer, DeltaSeconds);
        if (Navigation.Mode == SceneCameraMode::Space)
            for (size_t Axis = 0; Axis < 3; ++Axis)
                Flight.Position[Axis] = Before[Axis] + (Flight.Position[Axis] - Before[Axis]) * 1000;
        return;
    }
    if (Navigation.Mode == SceneCameraMode::Chase && Subject.Available)
    {
        if (Subject.Celestial)
        {
            Flight.Heading   = Subject.Azimuth;
            Flight.Elevation = Subject.Elevation;
        }
        else
        {
            Navigation.Anchor = Subject.Position;
            for (size_t Axis = 0; Axis < 3; ++Axis)
                Flight.Position[Axis] = Navigation.Anchor[Axis] + Navigation.ChaseOffset[Axis];
            LookAt(Flight, Navigation.Anchor);
        }
    }
    if (!(Pointer.Hovered || Pointer.Active)) return;
    const bool Pan = Pointer.MiddleDown && (Pointer.ShiftDown || AxisView(Navigation.Mode));
    const bool Orbit =
        Pointer.Active && !Pan && !AxisView(Navigation.Mode) && (Pointer.MiddleDown || Pointer.LeftDown || Pointer.RightDown);
    double Distance = std::max(.1, Flight.PivotDistance);
    if (Pan)
    {
        const auto            Forward = Flight.ResolveForward();
        const auto            Up      = Navigation.Up;
        std::array<double, 3> Right   = {Forward[1] * Up[2] - Forward[2] * Up[1], Forward[2] * Up[0] - Forward[0] * Up[2],
                                         Forward[0] * Up[1] - Forward[1] * Up[0]};
        const double          Length  = std::max(.000001, std::hypot(Right[0], Right[1], Right[2]));
        for (double &Axis : Right)
            Axis /= Length;
        const double Scale = Distance * .0015;
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            const double Shift = -Right[Axis] * Pointer.MotionPixels[0] * Scale + Navigation.Up[Axis] * Pointer.MotionPixels[1] * Scale;
            Flight.Position[Axis] += Shift;
            Navigation.Anchor[Axis] += Shift;
        }
    }
    if (Orbit)
    {
        if (Pointer.ControlDown)
            Distance *= std::exp(Pointer.MotionPixels[1] * .01);
        else
        {
            Flight.Heading += Pointer.MotionPixels[0] * .25;
            Flight.Elevation = std::clamp(Flight.Elevation - Pointer.MotionPixels[1] * .25, -89.5, 89.5);
        }
    }
    Distance             = std::clamp(Distance * std::exp(-Pointer.WheelNotches * .14), .25, 1000000.0);
    Flight.PivotDistance = Distance;
    if (Orbit || Pointer.WheelNotches != 0)
    {
        const auto Forward = Flight.ResolveForward();
        for (size_t Axis = 0; Axis < 3; ++Axis)
            Flight.Position[Axis] = Navigation.Anchor[Axis] - Forward[Axis] * Distance;
        Navigation.ChaseOffset = Subtract(Flight.Position, Navigation.Anchor);
    }
}

} // namespace Everest
