//============================================================================================================================================
//                                                     RESTIRINTEGRATOR.CPP
//============================================================================================================================================
// 🧩 Accumulates ReSTIR DI+GI radiance by numerically integrating light transport paths on the GPU compute pipeline.

#include "ReSTIRIntegrator.h"
#include <algorithm>
#include <string>
#include <cmath>

namespace Frontier {

//============================================================================================================================================
//                                                     LIFECYCLE
//============================================================================================================================================

ReSTIRIntegrator::ReSTIRIntegrator(ReSTIRIntegratorConfiguration InitialConfiguration) noexcept
    : ActiveConfiguration(InitialConfiguration)
    , AccumulationIndex(0u)
    , HistoryOrigin{ 0.0f, 0.0f, 0.0f }
    , HistoryForward{ 0.0f, 0.0f, 0.0f }
    , HistoryWidth(0u)
    , HistoryHeight(0u)
{
}

//============================================================================================================================================
//                                                OBSERVE CAMERA
//============================================================================================================================================

void ReSTIRIntegrator::ObserveCamera(const ProjectZero::FlyThroughSolver& Camera,
                                     uint32_t ViewportWidth, uint32_t ViewportHeight) noexcept
{
    const Vector3& Origin  = Camera.QuerySpatialLocation();
    const Vector3& Forward = Camera.QueryForwardVector();

    constexpr float PositionTolerance  = 1e-5f;   // [m]
    constexpr float DirectionTolerance = 1e-6f;   // [-]

    const Vector3 OriginDelta  = Origin  - HistoryOrigin;
    const Vector3 ForwardDelta = Forward - HistoryForward;

    const bool Moved   = OriginDelta.LengthSquared()  > PositionTolerance  * PositionTolerance;
    const bool Turned  = ForwardDelta.LengthSquared() > DirectionTolerance * DirectionTolerance;
    const bool Resized = ViewportWidth != HistoryWidth || ViewportHeight != HistoryHeight;

    if (Moved || Turned || Resized)
    {
        HistoryOrigin  = Origin;
        HistoryForward = Forward;
        HistoryWidth   = ViewportWidth;
        HistoryHeight  = ViewportHeight;
        ResetAccumulation();
    }
}

//============================================================================================================================================
//                                                  BUILD DISPATCH
//============================================================================================================================================

DispatchConfiguration ReSTIRIntegrator::BuildDispatch(
    const ProjectZero::FlyThroughSolver& Camera,
    uint32_t                             ViewportWidth,
    uint32_t                             ViewportHeight,
    uint32_t                             AlphaMaskedMaterialCount,
    uint32_t                             LuminaireTriangleCount) const noexcept
{
    const Vector3& Origin  = Camera.QuerySpatialLocation();
    const Vector3& Forward = Camera.QueryForwardVector();
    const Vector3& Right   = Camera.QueryRightVector();
    const Vector3& Up      = Camera.QueryUpwardVector();

    const float TanHalf = std::tan(Camera.QueryFieldOfViewRadians() * 0.5f);

    DispatchConfiguration Dispatch{};
    Dispatch.CameraOriginX         = Origin.x;
    Dispatch.CameraOriginY         = Origin.y;
    Dispatch.CameraOriginZ         = Origin.z;
    Dispatch.FieldOfViewTanHalf    = TanHalf;
    Dispatch.CameraForwardX        = Forward.x;
    Dispatch.CameraForwardY        = Forward.y;
    Dispatch.CameraForwardZ        = Forward.z;
    Dispatch.AspectRatio           = Camera.QueryAspectRatio();
    Dispatch.CameraRightX          = Right.x;
    Dispatch.CameraRightY          = Right.y;
    Dispatch.CameraRightZ          = Right.z;
    Dispatch.Exposure              = ActiveConfiguration.Exposure;
    Dispatch.CameraUpX             = Up.x;
    Dispatch.CameraUpY             = Up.y;
    Dispatch.CameraUpZ             = Up.z;
    Dispatch.AmbientStrength       = ActiveConfiguration.AmbientStrength;
    Dispatch.ViewportWidth         = ViewportWidth;
    Dispatch.ViewportHeight        = ViewportHeight;
    Dispatch.AccumulationIndex     = AccumulationIndex;
    Dispatch.ExtraCandidateCount      = ActiveConfiguration.ExtraCandidateCount;
    Dispatch.CandidatesPerPixel    = ActiveConfiguration.CandidatesPerPixel;
    Dispatch.AlphaMaskedMaterialCount = AlphaMaskedMaterialCount;   // R4b: 0 keeps the any-hit shadow path
    Dispatch.LuminaireTriangleCount = LuminaireTriangleCount;
    Dispatch.FeatureFlags          = (ActiveConfiguration.GlobalIllumination ? DispatchFeatureGlobalIllumination : 0u)
                                   | (ActiveConfiguration.AntiAliasing       ? DispatchFeatureAntiAliasing       : 0u)
                                   | (ActiveConfiguration.AmbientFloor       ? DispatchFeatureAmbientFloor       : 0u)
                                   | (ActiveConfiguration.TemporalReuse      ? DispatchFeatureTemporalReuse      : 0u)
                                   | (ActiveConfiguration.SpatialReuse       ? DispatchFeatureSpatialReuse       : 0u)
                                   | (ActiveConfiguration.AliasPick          ? DispatchFeatureAliasPick          : 0u);

    return Dispatch;
}

//============================================================================================================================================
//                                               SCENE RECORD BUILDERS
//============================================================================================================================================

uint32_t ReSTIRIntegrator::CountLuminaireTriangles(const ProjectZero::RayTracingSolver& Scene) noexcept
{
    const auto& Triangles = Scene.QueryTriangles();
    const auto& Materials = Scene.QueryMaterials();

    uint32_t Count = 0u;
    for (const auto& Triangle : Triangles)
    {
        if (Triangle.MaterialIndex < Materials.size())
        {
            const auto& Material = Materials[Triangle.MaterialIndex];
            const float EmissiveMagnitude = Material.EmissiveRadiance.x
                                          + Material.EmissiveRadiance.y
                                          + Material.EmissiveRadiance.z;
            if (EmissiveMagnitude > 0.0f) ++Count;
        }
    }
    return Count;
}

std::vector<TriangleIndex> ReSTIRIntegrator::BuildTriangleIndex(
    const ProjectZero::RayTracingSolver& Scene) noexcept
{
    const auto& Triangles = Scene.QueryTriangles();

    std::vector<TriangleIndex> Records;
    Records.reserve(Triangles.size());

    for (const auto& Triangle : Triangles)
    {
        TriangleIndex Record{};
        Record.VertexAlphaX  = Triangle.VertexAlpha.x;
        Record.VertexAlphaY  = Triangle.VertexAlpha.y;
        Record.VertexAlphaZ  = Triangle.VertexAlpha.z;
        Record.MaterialSlot  = *reinterpret_cast<const float*>(&Triangle.MaterialIndex);
        Record.VertexBetaX   = Triangle.VertexBeta.x;
        Record.VertexBetaY   = Triangle.VertexBeta.y;
        Record.VertexBetaZ   = Triangle.VertexBeta.z;
        Record.VertexGammaX  = Triangle.VertexGamma.x;
        Record.VertexGammaY  = Triangle.VertexGamma.y;
        Record.VertexGammaZ  = Triangle.VertexGamma.z;
        // R4a: no per-face normal or UVs in the analytical Cornell soup (flat-shaded, untextured)
        Records.push_back(Record);
    }
    return Records;
}

std::vector<MaterialDescriptor> ReSTIRIntegrator::BuildMaterialDescriptors(
    const ProjectZero::RayTracingSolver& Scene) noexcept
{
    const auto& Materials = Scene.QueryMaterials();

    std::vector<MaterialDescriptor> Records;
    Records.reserve(Materials.size());

    for (const auto& Material : Materials)
    {
        MaterialDescriptor D;
        D.Name = "material_" + std::to_string(Material.MaterialIdentifier);
        D.Slabs.emplace_back();
        MaterialSlabDescriptor& S = D.Slabs.back();
        S.BaseColor[0] = Material.AlbedoColor.x; S.BaseColor[1] = Material.AlbedoColor.y; S.BaseColor[2] = Material.AlbedoColor.z;
        S.SpecularRoughness = Material.RoughnessValue;
        S.BaseMetalness     = Material.MetallicValue;
        S.SpecularWeight    = 0.0f;   // R4b pin (approved): the analytical Cornell box is Lambertian — no dielectric lobe, so R3/R4a images stay the reference
        const float E[3] = { Material.EmissiveRadiance.x, Material.EmissiveRadiance.y, Material.EmissiveRadiance.z };
        const float Peak = std::max({ E[0], E[1], E[2], 0.0f });
        if (Peak > 0.0f) { S.EmissionLuminance = Peak; S.EmissionColor[0] = E[0] / Peak; S.EmissionColor[1] = E[1] / Peak; S.EmissionColor[2] = E[2] / Peak; }
        Records.push_back(std::move(D));
    }
    return Records;
}

} // namespace Frontier
