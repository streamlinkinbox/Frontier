#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace Frontier::ProjectFluid {

struct Vec3 {
    float x{}, y{}, z{};
    bool operator==(const Vec3&) const noexcept = default;
    Vec3& operator+=(Vec3 b) noexcept { x += b.x; y += b.y; z += b.z; return *this; }
    Vec3& operator-=(Vec3 b) noexcept { x -= b.x; y -= b.y; z -= b.z; return *this; }
};
Vec3 operator+(Vec3 a, Vec3 b) noexcept;
Vec3 operator-(Vec3 a, Vec3 b) noexcept;
Vec3 operator*(Vec3 a, float s) noexcept;
Vec3 operator/(Vec3 a, float s) noexcept;
float Dot(Vec3 a, Vec3 b) noexcept;
float Length(Vec3 a) noexcept;
Vec3 Normalized(Vec3 a) noexcept;

struct FluidMaterial {
    std::string_view Name;
    Vec3 Colour;
    Vec3 Absorption;
    float Opacity;
    float Roughness;
    float Ior;
    float Viscosity;
    float SurfaceTension;
    float Wetting;
    float ShearThinning;
    float TemperatureC;
};

enum class Material : std::uint32_t { Water, Milk, Honey, Chocolate };
enum class Experiment : std::uint32_t { Basin, WettingDrop, SuspendedDrop };

struct SolverDiagnostics {
    float MeanCompression{};
    float PeakCompression{};
    float MeanShear{};
    float MeanApparentViscosity{};
    std::uint32_t PressureIterations{};
    std::uint32_t ViscosityIterations{};
    float ViscosityRelativeResidual{};
    std::uint32_t SphereContacts{};
    std::uint32_t WallContacts{};
};

class PbfFluid final {
public:
    void SetReferenceNeighbourSearch(bool enabled) noexcept { ReferenceSearch_=enabled; }
    static constexpr std::uint32_t MaxParticles = 2800;
    static constexpr float SmoothingRadius = 0.31f;
    static constexpr float RestDensity = 265.0f;

    PbfFluid();
    void Reset(Experiment experiment = Experiment::Basin);
    void Step(float fixedDeltaSeconds);
    void Pour(float fixedDeltaSeconds, float rate = 1.0f);
    void Stir(float strength = 2.5f, float centreX = 0.0f, float centreZ = 0.0f);
    void SetMaterial(Material material) noexcept;
    void SetObstacle(bool enabled) noexcept { ObstacleEnabled_ = enabled; }

    [[nodiscard]] const std::vector<Vec3>& Positions() const noexcept { return Positions_; }
    [[nodiscard]] const std::vector<Vec3>& Velocities() const noexcept { return Velocities_; }
    [[nodiscard]] const SolverDiagnostics& Diagnostics() const noexcept { return Diagnostics_; }
    [[nodiscard]] const FluidMaterial& ActiveMaterial() const noexcept;
    [[nodiscard]] Material ActiveMaterialKey() const noexcept { return Material_; }
    [[nodiscard]] Experiment ActiveExperiment() const noexcept { return Experiment_; }
    [[nodiscard]] float Time() const noexcept { return Time_; }
    [[nodiscard]] bool ObstacleEnabled() const noexcept { return ObstacleEnabled_; }

    static constexpr Vec3 BoundsMin() noexcept { return {-1.95f, 0.19f, -1.25f}; }
    static constexpr Vec3 BoundsMax() noexcept { return { 1.95f, 3.70f,  1.25f}; }
    static constexpr Vec3 ObstacleCentre() noexcept { return {0.65f, 0.65f, 0.0f}; }
    static constexpr float ObstacleRadius() noexcept { return 0.36f; }

private:
    bool ReferenceSearch_=false;
    void Add(Vec3 position, Vec3 velocity = {});
    void Collide(Vec3& position);
    void BuildNeighbours();
    void RebuildBoundarySamples();
    void BuildBoundaryNeighbours();
    void ComputeDensity(bool computeLambda);
    void SolvePressure();
    void ApplySurfaceTension(float dt);
    void ApplyViscosity(float dt);
    float Poly6(float distanceSquared) const noexcept;

    std::vector<Vec3> Positions_;
    std::vector<Vec3> Velocities_;
    std::vector<Vec3> Previous_;
    std::vector<Vec3> Corrections_;
    std::vector<Vec3> BoundaryGradients_;
    std::vector<float> Density_;
    std::vector<float> Lambda_;
    std::vector<float> ApparentViscosity_;
    std::vector<std::vector<std::uint16_t>> Neighbours_;
    std::vector<Vec3> BoundaryPositions_;
    std::vector<float> BoundaryPseudoMasses_;
    std::vector<std::vector<std::uint16_t>> BoundaryNeighbours_;
    SolverDiagnostics Diagnostics_{};
    Material Material_{Material::Water};
    Experiment Experiment_{Experiment::Basin};
    bool ObstacleEnabled_{true};
    float Gravity_{9.81f};
    float Time_{};
    float Emission_{};
};

} // namespace Frontier::ProjectFluid
