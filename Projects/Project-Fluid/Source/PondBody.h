#pragma once
#include "PondWave.h"

namespace Frontier::ProjectFluid {

// Lightweight kinematic/buoyant coupling from Ripple; not a rigid pressure body.
class PondBody final {
public:
    void Reset(float level = 0);
    void Grab(float x, float z) noexcept { TargetX_ = x; TargetZ_ = z; HasTarget_ = true; }
    void Release() noexcept { HasTarget_ = false; }
    void Update(PondWave& pond, float dt, float level = 0);

    [[nodiscard]] float X() const noexcept { return X_; }
    [[nodiscard]] float Y() const noexcept { return Y_; }
    [[nodiscard]] float Z() const noexcept { return Z_; }
    [[nodiscard]] float Angle() const noexcept { return Angle_; }
    [[nodiscard]] std::uint32_t Wakes() const noexcept { return Wakes_; }

private:
    void Step(PondWave&, float, float);
    float X_{.8f};
    float Y_{};
    float Z_{.3f};
    float Vx_{}, Vy_{}, Vz_{};
    float Angle_{.6f};
    float Radius_{.48f};
    float TargetX_{}, TargetZ_{};
    float Travel_{}, CoastWakeTime_{};
    bool HasTarget_{};
    std::uint32_t Wakes_{};
};

} // namespace Frontier::ProjectFluid
