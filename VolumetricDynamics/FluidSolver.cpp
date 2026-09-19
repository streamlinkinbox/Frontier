//============================================================================================================================================
// 📦 Frontier/VolumetricDynamics/FluidSolver.cpp — 3D Eulerian Navier-Stokes Fluid Solver Implementation
//============================================================================================================================================

#include "FluidSolver.h"
#include <algorithm>
#include <cmath>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

FluidSolver::FluidSolver(FluidConfiguration InitialConfig) noexcept
    : Config(InitialConfig)
    , CellCount(static_cast<size_t>(InitialConfig.ResolutionX * InitialConfig.ResolutionY * InitialConfig.ResolutionZ))
    , Density(CellCount, 0.0f)
    , DensityScratch(CellCount, 0.0f)
    , Temperature(CellCount, InitialConfig.AmbientTemperature)
    , TemperatureScratch(CellCount, InitialConfig.AmbientTemperature)
    , Velocity(CellCount, Vector3{ 0.0f, 0.0f, 0.0f })
    , VelocityScratch(CellCount, Vector3{ 0.0f, 0.0f, 0.0f })
    , Divergence(CellCount, 0.0f)
    , Pressure(CellCount, 0.0f)
    , PressureScratch(CellCount, 0.0f)
{
}

size_t FluidSolver::CellIndex(uint32_t x, uint32_t y, uint32_t z) const noexcept
{
    return static_cast<size_t>(z * (Config.ResolutionX * Config.ResolutionY) + y * Config.ResolutionX + x);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                INJECTION OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void FluidSolver::InjectDensity(uint32_t x, uint32_t y, uint32_t z, float Amount) noexcept
{
    if (x < Config.ResolutionX && y < Config.ResolutionY && z < Config.ResolutionZ)
    {
        Density[CellIndex(x, y, z)] += Amount;
    }
}

void FluidSolver::InjectTemperature(uint32_t x, uint32_t y, uint32_t z, float Kelvins) noexcept
{
    if (x < Config.ResolutionX && y < Config.ResolutionY && z < Config.ResolutionZ)
    {
        Temperature[CellIndex(x, y, z)] = Kelvins;
    }
}

void FluidSolver::InjectMomentum(uint32_t x, uint32_t y, uint32_t z, const Vector3& Impulse) noexcept
{
    if (x < Config.ResolutionX && y < Config.ResolutionY && z < Config.ResolutionZ)
    {
        Velocity[CellIndex(x, y, z)] += Impulse;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SIMULATION INTEGRATION
//------------------------------------------------------------------------------------------------------------------------

void FluidSolver::AdvanceSimulation(float Δτ, const LevelSetSpace* SolidBoundaries) noexcept
{
    if (Δτ <= 0.0f)
    {
        return;
    }

    ApplyBuoyancy(Δτ);
    AdvectFields(Δτ);
    ProjectPressure(SolidBoundaries);
}

void FluidSolver::ApplyBuoyancy(float Δτ) noexcept
{
    for (uint32_t z = 0; z < Config.ResolutionZ; ++z)
    {
        for (uint32_t y = 0; y < Config.ResolutionY; ++y)
        {
            for (uint32_t x = 0; x < Config.ResolutionX; ++x)
            {
                size_t idx = CellIndex(x, y, z);
                float Dens = Density[idx];
                float Temp = Temperature[idx];

                // Buoyancy force: f = (-alpha * rho + beta * (T - T_amb)) * g
                float BuoyantForce = -Config.BuoyancyAlpha * Dens + Config.BuoyancyBeta * (Temp - Config.AmbientTemperature);
                Velocity[idx].y += BuoyantForce * 9.81f * Δτ;
            }
        }
    }
}

void FluidSolver::AdvectFields(float Δτ) noexcept
{
    float invCell = 1.0f / Config.CellWidth;

    for (uint32_t z = 1; z < Config.ResolutionZ - 1; ++z)
    {
        for (uint32_t y = 1; y < Config.ResolutionY - 1; ++y)
        {
            for (uint32_t x = 1; x < Config.ResolutionX - 1; ++x)
            {
                size_t idx = CellIndex(x, y, z);
                Vector3 vel = Velocity[idx];

                // Backtrace position: x_prev = x - v * Δτ
                float px = static_cast<float>(x) - vel.x * Δτ * invCell;
                float py = static_cast<float>(y) - vel.y * Δτ * invCell;
                float pz = static_cast<float>(z) - vel.z * Δτ * invCell;

                int32_t sx = std::clamp(static_cast<int32_t>(px), 0, static_cast<int32_t>(Config.ResolutionX - 1));
                int32_t sy = std::clamp(static_cast<int32_t>(py), 0, static_cast<int32_t>(Config.ResolutionY - 1));
                int32_t sz = std::clamp(static_cast<int32_t>(pz), 0, static_cast<int32_t>(Config.ResolutionZ - 1));

                size_t sidx = CellIndex(sx, sy, sz);
                DensityScratch[idx] = Density[sidx];
                TemperatureScratch[idx] = Temperature[sidx];
                VelocityScratch[idx] = Velocity[sidx];
            }
        }
    }

    Density = DensityScratch;
    Temperature = TemperatureScratch;
    Velocity = VelocityScratch;
}

void FluidSolver::ProjectPressure(const LevelSetSpace* SolidBoundaries) noexcept
{
    float invCell2 = 1.0f / (2.0f * Config.CellWidth);

    // 1. Compute velocity divergence: ∇ · u
    for (uint32_t z = 1; z < Config.ResolutionZ - 1; ++z)
    {
        for (uint32_t y = 1; y < Config.ResolutionY - 1; ++y)
        {
            for (uint32_t x = 1; x < Config.ResolutionX - 1; ++x)
            {
                size_t idx = CellIndex(x, y, z);
                float du_dx = (Velocity[CellIndex(x + 1, y, z)].x - Velocity[CellIndex(x - 1, y, z)].x) * invCell2;
                float dv_dy = (Velocity[CellIndex(x, y + 1, z)].y - Velocity[CellIndex(x, y - 1, z)].y) * invCell2;
                float dw_dz = (Velocity[CellIndex(x, y, z + 1)].z - Velocity[CellIndex(x, y, z - 1)].z) * invCell2;

                Divergence[idx] = du_dx + dv_dy + dw_dz;
                Pressure[idx] = 0.0f;
            }
        }
    }

    // 2. Jacobi Poisson pressure iterations: ∇²p = ∇ · u
    for (uint32_t iter = 0; iter < Config.PressureIterations; ++iter)
    {
        for (uint32_t z = 1; z < Config.ResolutionZ - 1; ++z)
        {
            for (uint32_t y = 1; y < Config.ResolutionY - 1; ++y)
            {
                for (uint32_t x = 1; x < Config.ResolutionX - 1; ++x)
                {
                    size_t idx = CellIndex(x, y, z);
                    float p_sum = Pressure[CellIndex(x + 1, y, z)] + Pressure[CellIndex(x - 1, y, z)]
                                + Pressure[CellIndex(x, y + 1, z)] + Pressure[CellIndex(x, y - 1, z)]
                                + Pressure[CellIndex(x, y, z + 1)] + Pressure[CellIndex(x, y, z - 1)];

                    PressureScratch[idx] = (p_sum - Divergence[idx] * (Config.CellWidth * Config.CellWidth)) / 6.0f;
                }
            }
        }
        Pressure = PressureScratch;
    }

    // 3. Project velocity: u -= ∇p
    for (uint32_t z = 1; z < Config.ResolutionZ - 1; ++z)
    {
        for (uint32_t y = 1; y < Config.ResolutionY - 1; ++y)
        {
            for (uint32_t x = 1; x < Config.ResolutionX - 1; ++x)
            {
                size_t idx = CellIndex(x, y, z);
                float dp_dx = (Pressure[CellIndex(x + 1, y, z)] - Pressure[CellIndex(x - 1, y, z)]) * invCell2;
                float dp_dy = (Pressure[CellIndex(x, y + 1, z)] - Pressure[CellIndex(x, y - 1, z)]) * invCell2;
                float dp_dz = (Pressure[CellIndex(x, y, z + 1)] - Pressure[CellIndex(x, y - 1, z)]) * invCell2;

                Velocity[idx].x -= dp_dx;
                Velocity[idx].y -= dp_dy;
                Velocity[idx].z -= dp_dz;

                // LevelSetSpace solid boundary masking: u · n = 0
                if (SolidBoundaries)
                {
                    Vector3 CellPos{
                        static_cast<float>(x) * Config.CellWidth,
                        static_cast<float>(y) * Config.CellWidth,
                        static_cast<float>(z) * Config.CellWidth
                    };

                    if (SolidBoundaries->IsInsideSolid(CellPos))
                    {
                        Vector3 SolidNormal = SolidBoundaries->SampleGradient(CellPos);
                        float NormalVelocity = OrientationClassifier::DotProduct(Velocity[idx], SolidNormal);
                        if (NormalVelocity < 0.0f)
                        {
                            Velocity[idx] -= SolidNormal * NormalVelocity;
                        }
                    }
                }
            }
        }
    }
}

float FluidSolver::QueryDensity(uint32_t x, uint32_t y, uint32_t z) const noexcept
{
    if (x < Config.ResolutionX && y < Config.ResolutionY && z < Config.ResolutionZ)
    {
        return Density[CellIndex(x, y, z)];
    }
    return 0.0f;
}

float FluidSolver::QueryTemperature(uint32_t x, uint32_t y, uint32_t z) const noexcept
{
    if (x < Config.ResolutionX && y < Config.ResolutionY && z < Config.ResolutionZ)
    {
        return Temperature[CellIndex(x, y, z)];
    }
    return Config.AmbientTemperature;
}

Vector3 FluidSolver::QueryVelocity(uint32_t x, uint32_t y, uint32_t z) const noexcept
{
    if (x < Config.ResolutionX && y < Config.ResolutionY && z < Config.ResolutionZ)
    {
        return Velocity[CellIndex(x, y, z)];
    }
    return Vector3{ 0.0f, 0.0f, 0.0f };
}

} // namespace Frontier
