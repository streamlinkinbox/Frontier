//============================================================================================================================================
//                                                           LOBBYCAMERAMOTION.JS
//============================================================================================================================================
// 🧩 Pure camera mathematics for the Lobby entrance: cinematic easing, frame-rate-independent lag (an exponential
//    chase of the desired pose), angle-shortest-path interpolation and the studio containment volume that keeps
//    the eye below the suspended softbox (underside 5.95 m), off its housing and inside the cyclorama (r 20 m).

export function EaseCinematic(Fraction)
{
    const T = Math.max(0.0, Math.min(1.0, Fraction));
    return T * T * T * (T * (T * 6.0 - 15.0) + 10.0);   // smootherstep: zero velocity at both ends
}

export function InterpolateAngle(From, To, Fraction)
{
    const Turn = 2.0 * Math.PI;
    let Difference = (To - From) % Turn;
    if (Difference > Math.PI) Difference -= Turn;
    if (Difference < -Math.PI) Difference += Turn;
    return From + Difference * Fraction;
}

export function InterpolateOrbitPose(From, To, Fraction)
{
    return {
        Yaw:      InterpolateAngle(From.Yaw, To.Yaw, Fraction),
        Pitch:    From.Pitch + (To.Pitch - From.Pitch) * Fraction,
        Distance: From.Distance + (To.Distance - From.Distance) * Fraction
    };
}

// 📝 The fraction of the remaining gap a lagged camera closes this frame: 1 − e^(−Δτ/τ).
export function ResolveLagBlend(Seconds, TimeConstant)
{
    return 1.0 - Math.exp(-Math.max(0.0, Seconds) / Math.max(TimeConstant, 1.0e-3));
}

export function BlendVector(Current, Desired, Blend)
{
    return Current.map((Value, Axis) => Value + (Desired[Axis] - Value) * Blend);
}

// 📝 The containment volume: under the softbox underside (4.92 m, housing included), above the floor, and inside
//    the cyclorama wall (radius 12 m) with margin, so the eye can never leave the lit studio or clip the fixture.
export function ClampStudioEye(Eye)
{
    const Result = Eye.slice();
    Result[2] = Math.max(0.35, Math.min(5.25, Result[2]));
    const Radius = Math.hypot(Result[0], Result[1]);
    if (Radius > 18.4)
    {
        Result[0] *= 18.4 / Radius;
        Result[1] *= 18.4 / Radius;
    }
    return Result;
}
