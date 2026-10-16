//============================================================================================================================================
//                                                         DRIVERINPUTINTEGRATOR.JS
//============================================================================================================================================
// 🧩 Keyboard samples ramped into the smooth throttle, brake and steer command the vehicle solver consumes.

// 📝 DriverInputIntegrator.h's keyboard path: digital keys ramp in and decay out at the Frontier rates, steering builds
//    toward ±1 while A/D are held and self-centres when released. Layout: W throttle, S brake / reverse, A/D steer,
//    Space handbrake, R respawn — Project-Drive's README controls. Arrow keys mirror WASD.
const InputConfiguration = Object.freeze({
    ThrottleRiseRate: 3.0,   // [1/s]
    ThrottleFallRate: 6.0,   // [1/s]
    BrakeRiseRate:    4.0,   // [1/s]
    BrakeFallRate:    8.0,   // [1/s]
    SteerRate:        3.5,   // [1/s]
    SteerReturnRate:  5.0    // [1/s]
});

function Ramp(Current, Target, Rise, Fall, Δτ)
{
    if (Target > Current)
        return Math.min(Target, Current + Rise * Δτ);
    return Math.max(Target, Current - Fall * Δτ);
}

export class DriverInputIntegrator
{
    constructor()
    {
        this.Throttle = 0.0;
        this.Brake    = 0.0;
        this.Steer    = 0.0;
    }

    Advance(Keys, Δτ)
    {
        const Forward  = Keys.has("KeyW") || Keys.has("ArrowUp");
        const Backward = Keys.has("KeyS") || Keys.has("ArrowDown");
        const Left     = Keys.has("KeyA") || Keys.has("ArrowLeft");
        const Right    = Keys.has("KeyD") || Keys.has("ArrowRight");
        this.Throttle = Ramp(this.Throttle, Forward ? 1.0 : 0.0, InputConfiguration.ThrottleRiseRate, InputConfiguration.ThrottleFallRate, Δτ);
        this.Brake    = Ramp(this.Brake, Backward ? 1.0 : 0.0, InputConfiguration.BrakeRiseRate, InputConfiguration.BrakeFallRate, Δτ);
        const SteerTarget = (Left ? 1.0 : 0.0) - (Right ? 1.0 : 0.0);
        if (Left || Right)
            this.Steer += (SteerTarget - this.Steer) * Math.min(1.0, Δτ * InputConfiguration.SteerRate);
        else
            this.Steer += (0.0 - this.Steer) * Math.min(1.0, Δτ * InputConfiguration.SteerReturnRate);
        return { Throttle: this.Throttle, Brake: this.Brake, Steer: this.Steer, Handbrake: Keys.has("Space") };
    }

    Reset()
    {
        this.Throttle = 0.0;
        this.Brake    = 0.0;
        this.Steer    = 0.0;
    }
}
