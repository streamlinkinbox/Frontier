#include "PondBody.h"
#include <algorithm>
#include <cmath>

namespace Frontier::ProjectFluid {

void PondBody::Reset(float level) {
    X_ = .8f;
    Z_ = .3f;
    Y_ = level;
    Vx_ = Vy_ = Vz_ = 0;
    Angle_ = .6f;
    Radius_ = .48f;
    Travel_ = CoastWakeTime_ = 0;
    HasTarget_ = false;
    Wakes_ = 0;
}

void PondBody::Update(PondWave& pond, float dt, float level) {
    for (float remain = dt; remain > 1e-7f;) {
        const float step = std::min(remain, 1.0f / 120.0f);
        Step(pond, step, level);
        remain -= step;
    }
}

void PondBody::Step(PondWave& pond, float dt, float level) {
    CoastWakeTime_ = HasTarget_ ? 2.0f : std::max(0.0f, CoastWakeTime_ - dt);
    const Vec3 normal = pond.Normal(X_, Z_);
    const float gradientX = -normal.x / std::max(normal.y, 1e-6f);
    const float gradientZ = -normal.z / std::max(normal.y, 1e-6f);

    if (HasTarget_) {
        float accelerationX = (TargetX_ - X_) * 10.0f;
        float accelerationZ = (TargetZ_ - Z_) * 10.0f;
        const float length = std::hypot(accelerationX, accelerationZ);
        const float cap = length > 3.0f ? 3.0f / length : 1.0f;
        const float response = 1.0f - std::exp(-12.0f * dt);
        Vx_ += (accelerationX * cap - Vx_) * response;
        Vz_ += (accelerationZ * cap - Vz_) * response;
    } else {
        Vx_ = (Vx_ - gradientX * 2.0f * dt) * std::exp(-1.35f * dt);
        Vz_ = (Vz_ - gradientZ * 2.0f * dt) * std::exp(-1.35f * dt);
    }

    const float nextX = X_ + Vx_ * dt;
    const float nextZ = Z_ + Vz_ * dt;
    if (pond.IsWet(nextX, nextZ, Radius_)) {
        Travel_ += std::hypot(nextX - X_, nextZ - Z_);
        X_ = nextX;
        Z_ = nextZ;
    } else {
        Vx_ *= -.2f;
        Vz_ *= -.2f;
    }

    const float speed = std::hypot(Vx_, Vz_);
    if (speed > .06f) {
        const float angle = std::atan2(Vx_, Vz_);
        const float difference = std::atan2(std::sin(angle - Angle_), std::cos(angle - Angle_));
        Angle_ += difference * (1.0f - std::exp(-6.0f * dt));
    }

    if (Travel_ > .10f && speed > .08f && (HasTarget_ || CoastWakeTime_ > 0)) {
        const float unitX = Vx_ / speed;
        const float unitZ = Vz_ / speed;
        const float strength = std::min(.7f, speed * .23f);
        pond.Disturb(X_ + unitX * .40f, Z_ + unitZ * .40f, strength, .23f);
        pond.Disturb(X_ - unitX * .35f, Z_ - unitZ * .35f, -strength * .75f, .28f);
        Travel_ = 0;
        ++Wakes_;
    }

    const float targetY = level + pond.Sample(X_, Z_);
    Vy_ += ((targetY - Y_) * 48.0f - Vy_ * 10.0f) * dt;
    Y_ += Vy_ * dt;
}

} // namespace Frontier::ProjectFluid
