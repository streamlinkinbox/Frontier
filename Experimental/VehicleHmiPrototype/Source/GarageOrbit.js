//============================================================================================================================================
//                                                             GARAGEORBIT.JS
//============================================================================================================================================
// 🧩 The configurator's camera, ported from interactive-vehicle-hmi-configurator.zip (Scene.tsx): a 34° perspective camera that only orbits
//    its vehicle — no pan — through three.js OrbitControls with damping 0.06, polar angle 0.35 … π/2.05, distance 6 … 16 m, looking at a point
//    1.18 m above the ground the vehicle stands on (target [0, 0.55, 0] over the truck's ground at −0.62), starting at the offset
//    [6.8, 2.35, 7.6] from it (camera [6.8, 2.9, 7.6]): the front-three-quarter view from the vehicle's right.
//
//    Angles are measured in the vehicle's frame: azimuth from its forward axis toward its right, elevation above the horizon.

import { AddVector, ScaleVector, NormalizeVector, CrossVector } from "./TransformProjection.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                    SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

export const GarageOrbitSpecification = Object.freeze({
    FieldOfView:      34.0,                  // [°]   <PerspectiveCamera fov=34>
    TargetHeight:     1.18,                  // [m]   above the ground: target 0.55 over the truck's ground at −0.62
    StartOffset:      Object.freeze([6.8, 2.35, 7.6]),   // [m] forward, up, right of the target
    MinimumDistance:  6.0,                   // [m]   minDistance
    MaximumDistance:  16.0,                  // [m]   maxDistance
    MinimumPolar:     0.35,                  // [rad] minPolarAngle, from straight up
    MaximumPolar:     Math.PI / 2.05,        // [rad] maxPolarAngle
    Damping:          0.06,                  // [-]   dampingFactor, per frame at 60 Hz
    FrameRate:        60.0,                  // [Hz]  what a damping factor is per
    RotateExtent:     400.0,                 // [px]  OrbitControls divides a drag by the element's clientHeight: the stage is 400 tall
    ZoomBase:         0.95,                  // [-]   getZoomScale: 0.95^(zoomSpeed · |deltaY · 0.01|)
    SpinSpeed:        0.8                    // [-]   autoRotateSpeed: 2π/60 rad per second per unit
});

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE RUNTIME
//------------------------------------------------------------------------------------------------------------------------

export class GarageOrbit
{
    constructor()
    {
        this.Reset();
        this.Spin = false;
    }

    Reset()
    {
        const [Forward, Up, Right] = GarageOrbitSpecification.StartOffset;
        this.Azimuth   = Math.atan2(Right, Forward);
        this.Elevation = Math.asin(Up / Math.hypot(Forward, Up, Right));
        this.Distance  = Math.hypot(Forward, Up, Right);
        this.PendingAzimuth   = 0.0;
        this.PendingElevation = 0.0;
    }

    // 📝 OrbitControls.handleMouseMoveRotate: a drag of (X, Y) pixels turns the view by 2π · pixels / element height. Dragging right moves the
    //    camera toward the vehicle's right; dragging down raises it. The turn is stored, not applied: damping spends it over the next frames.
    Rotate(PixelsAcross, PixelsDown)
    {
        const Turn = 2.0 * Math.PI / GarageOrbitSpecification.RotateExtent;
        this.PendingAzimuth   += PixelsAcross * Turn;
        this.PendingElevation += PixelsDown * Turn;
    }

    // 📝 OrbitControls.handleMouseWheel: deltaY < 0 dollies in by 0.95^|deltaY · 0.01|, deltaY > 0 out by its inverse. Applied at once, not damped.
    Zoom(DeltaY)
    {
        const Scale = Math.pow(GarageOrbitSpecification.ZoomBase, Math.abs(DeltaY * 0.01));
        this.Distance *= DeltaY < 0.0 ? Scale : 1.0 / Scale;
        this.ClampDistance();
    }

    ClampDistance()
    {
        const Specification = GarageOrbitSpecification;
        this.Distance = Math.max(Specification.MinimumDistance, Math.min(Specification.MaximumDistance, this.Distance));
    }

    // 📝 OrbitControls.update(deltaTime): auto-rotation adds to the pending turn; each update spends `damping` of what is pending (per
    //    frame at 60 Hz, here per second so the rate does not depend on the frame rate) and keeps the rest.
    Advance(Seconds)
    {
        const Specification = GarageOrbitSpecification;
        if (this.Spin)
        {
            this.PendingAzimuth += 2.0 * Math.PI / 60.0 * Specification.SpinSpeed * Seconds;
        }
        const Spend = 1.0 - Math.pow(1.0 - Specification.Damping, Seconds * Specification.FrameRate);
        this.Azimuth   += this.PendingAzimuth * Spend;
        this.Elevation += this.PendingElevation * Spend;
        this.PendingAzimuth   *= 1.0 - Spend;
        this.PendingElevation *= 1.0 - Spend;
        // 📝 The polar clamp: elevation = π/2 − polar.
        this.Elevation = Math.max(Math.PI / 2.0 - Specification.MaximumPolar, Math.min(Math.PI / 2.0 - Specification.MinimumPolar, this.Elevation));
        this.ClampDistance();
    }

    // 📝 The camera for a vehicle standing at Frame.Origin (the ground under its middle) with Frame.Forward, Frame.Right, Frame.Up.
    ResolveCamera(Frame, Aspect, Near = 0.1)
    {
        const Specification = GarageOrbitSpecification;
        const Target = AddVector(Frame.Origin, ScaleVector(Frame.Up, Specification.TargetHeight));
        const Reach  = this.Distance * Math.cos(this.Elevation);
        let Position = AddVector(Target, ScaleVector(Frame.Forward, Reach * Math.cos(this.Azimuth)));
        Position = AddVector(Position, ScaleVector(Frame.Right, Reach * Math.sin(this.Azimuth)));
        Position = AddVector(Position, ScaleVector(Frame.Up, this.Distance * Math.sin(this.Elevation)));
        return { Position, Target, Up: Frame.Up.slice(), VerticalFieldOfView: Specification.FieldOfView * Math.PI / 180.0, Aspect, Near };
    }
}

// 📝 A vehicle's frame from its body transform (column-major, local x forward, y left, z up): forward flattened onto the ground, up the world's,
//    right across them; origin the ground point under the centre of mass, CentreOfMassHeight below it.
export function ResolveVehicleFrame(BodyTransform, CentreOfMassHeight)
{
    let Forward = NormalizeVector([BodyTransform[0], BodyTransform[1], 0.0]);
    if (!(Math.hypot(Forward[0], Forward[1]) > 0.5))
    {
        Forward = [1.0, 0.0, 0.0];
    }
    const Up = [0.0, 0.0, 1.0];
    const Right = CrossVector(Forward, Up);   // forward × up: toward the vehicle's right
    return { Origin: [BodyTransform[12], BodyTransform[13], BodyTransform[14] - CentreOfMassHeight], Forward, Right, Up };
}
