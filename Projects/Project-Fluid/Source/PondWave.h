#pragma once
#include "PbfFluid.h"
#include <cstdint>
#include <vector>

namespace Frontier::ProjectFluid {
struct PondObstacle { float x{},z{},radius{}; };

// Exact native adaptation of Ripple's damped 2D surface-wave solver.
class PondWave final {
public:
    // Dimensions in [0.01, 10000] metres; each axis >=3 samples; at most 160000 cells.
    PondWave(std::uint32_t rows=96,float length=8.0f,float width=12.0f);
    void Reset();
    void Step(float dt);
    void Advance(float seconds,std::uint32_t maxSteps=32);
    void Disturb(float x,float z,float strength=.65f,float radius=.3f);
    bool RefineIfNeeded();
    [[nodiscard]] float Sample(float x,float z)const;
    [[nodiscard]] Vec3 Normal(float x,float z)const;
    [[nodiscard]] bool IsWet(float x,float z,float margin=0)const;
    [[nodiscard]] float StableDt()const;
    [[nodiscard]] float Energy()const;
    [[nodiscard]] std::uint32_t Rows()const noexcept{return Rows_;}
    [[nodiscard]] std::uint32_t Columns()const noexcept{return Columns_;}
    [[nodiscard]] float Width()const noexcept{return Width_;}
    [[nodiscard]] float Length()const noexcept{return Length_;}
    [[nodiscard]] float Dx()const noexcept{return Dx_;}
    [[nodiscard]] float Dz()const noexcept{return Dz_;}
    [[nodiscard]] float Time()const noexcept{return Time_;}
    [[nodiscard]] std::uint32_t Refinements()const noexcept{return Refinements_;}
    [[nodiscard]] std::uint32_t ClampHits()const noexcept{return ClampHits_;}
    [[nodiscard]] const std::vector<float>& Heights()const noexcept{return Height_;}
    [[nodiscard]] const std::vector<PondObstacle>& Obstacles()const noexcept{return Obstacles_;}
private:
    void Allocate(std::uint32_t rows);
    void Acceleration();
    float SampleField(const std::vector<float>& field,float x,float z,std::uint32_t rows)const;
    float MaxSlope()const;
    std::uint32_t BaseRows_{},MaxRows_{},Rows_{},Columns_{};
    float Length_{},Width_{},Dx_{},Dz_{},Speed_{1.4f},Damping_{.45f},Time_{},Accumulator_{},DroppedTime_{};
    std::uint32_t Refinements_{},ClampHits_{};
    std::vector<float> Height_,Velocity_,Acceleration_;
    std::vector<std::uint8_t> Solid_;
    std::vector<PondObstacle> Obstacles_{{-2.4f,-1.65f,.65f},{2.7f,1.65f,.46f}};
};
}
