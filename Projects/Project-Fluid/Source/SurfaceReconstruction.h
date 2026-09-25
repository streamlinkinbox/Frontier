#pragma once
#include "PbfFluid.h"
#include <array>
#include <vector>

namespace Frontier::ProjectFluid {
struct SurfaceKernel {
    Vec3 Centre;
    Vec3 AxisA;
    Vec3 AxisB;
    Vec3 AxisC;
    float VolumeWeight{};
};

// Weighted covariance/PCA kernel reconstruction adapted from Yu & Turk and
// the authoritative Flux reconstruction.ts. Physics positions are untouched.
class SurfaceReconstruction final {
public:
    void Update(const std::vector<Vec3>& positions);
    [[nodiscard]] const std::vector<SurfaceKernel>& Kernels() const noexcept { return Kernels_; }
private:
    static void Diagonalize(std::array<float,9>& covariance,std::array<float,9>& rotation) noexcept;
    std::vector<SurfaceKernel> Kernels_;
};
}
