/*==========================================================================================================================================
                                                      GEOMETRYPROCESSINGVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 CPU checks for retained GeometryProcessing: .sdfspace bake round-trip, vehicle SDF union, SAH BVH ray queries

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Geometry/GeometryProcessing/BoundingVolumeAtlas.h"
#include "Engine/Geometry/GeometryProcessing/ScreenSpaceClusterRefinement.h"
#include "Engine/Geometry/GeometryProcessing/VolumeDistanceArchive.h"
#include "Engine/Geometry/PrimitiveGeometryGenerator.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

//------------------------------------------------------------------------------------------------------------------------
//                                                   INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

using namespace Frontier;

// 📝 Exits non-zero on the first failed contract so CTest reports the exact line, expression, and expectation.
void AssertCondition(bool Condition, const char* Expression, const char* Detail, int Line)
{
    if (!Condition)
    {
        std::cerr << "[GeometryProcessingValidation] FAILED at line " << Line << ": " << Detail
                  << " (" << Expression << ")\n";
        std::exit(1);
    }
}

#define VERIFY_GEOMETRY(Condition, Detail) AssertCondition((Condition), #Condition, (Detail), __LINE__)

} // namespace


//------------------------------------------------------------------------------------------------------------------------
//                                                          MAIN
//------------------------------------------------------------------------------------------------------------------------

// 💡 These checks previously lived in the removed ReSTIR exhibit; they cover only geometry modules, so no renderer,
//    Vulkan device, or shader archive is required.
int main(int ArgumentCount, char** Arguments)
{
    const std::filesystem::path RepositoryRoot  = (ArgumentCount > 1) ? Arguments[1] : ".";
    const std::filesystem::path OutputDirectory = (ArgumentCount > 2) ? Arguments[2] : "build/geometry-processing-exhibits";
    std::filesystem::create_directories(OutputDirectory);
    std::string Diagnostic;

    // ① Headless .sdfspace bake and Float16 round-trip on a closed unit sphere (zero UI dependency).
    std::cout << "[1/3] Verifying headless VolumeDistanceField -> .sdfspace Float16 round-trip...\n";
    {
        FacetField SphereFacets{};
        AppendProceduralGeodesicSphere(SphereFacets, 1.0, 2u);

        HeadlessSdfBakeOptions BakeOptions{};
        BakeOptions.Dimensions   = {32u, 32u, 32u};
        BakeOptions.PaddingRatio = 0.25f;
        BakeOptions.SignedField  = true;

        const VolumeDistanceField BakedField = BakeFacetFieldDistanceVolume(SphereFacets, BakeOptions);
        VERIFY_GEOMETRY(BakedField.ClosedManifold && BakedField.MinimumDistance < -0.60f && BakedField.MaximumDistance > 0.35f,
                        "Headless bake of a closed sphere must yield negative interior and positive exterior distances");

        const std::filesystem::path ArchivePath = OutputDirectory / "UnitSphereRoundTrip.sdfspace";
        VERIFY_GEOMETRY(SaveVolumeDistanceSpace(BakedField, ArchivePath, &Diagnostic),
                        ("SaveVolumeDistanceSpace failed: " + Diagnostic).c_str());

        VolumeDistanceField ReloadedField{};
        VERIFY_GEOMETRY(LoadVolumeDistanceSpace(ArchivePath.string(), ReloadedField, &Diagnostic),
                        ("LoadVolumeDistanceSpace failed: " + Diagnostic).c_str());

        const float BakedCentre     = SampleVolumeDistanceMeters(BakedField, 0.0f, 0.0f, 0.0f);     // [m] - Sphere centre before save
        const float ReloadedCentre  = SampleVolumeDistanceMeters(ReloadedField, 0.0f, 0.0f, 0.0f);  // [m] - Sphere centre after reload
        const float SurfaceDistance = SampleVolumeDistanceMeters(ReloadedField, 1.0f, 0.0f, 0.0f);  // [m] - On the unit surface
        const float OutsideDistance = SampleVolumeDistanceMeters(ReloadedField, 1.35f, 0.0f, 0.0f); // [m] - 0.35 m outside
        std::cout << "      Round-trip SDF: Centre=" << ReloadedCentre << " m, Surface=" << SurfaceDistance
                  << " m, Outside=" << OutsideDistance << " m\n";
        VERIFY_GEOMETRY(ReloadedCentre < -0.55f &&
                            std::abs(ReloadedCentre - BakedCentre) < 2.0e-3f &&
                            std::abs(SurfaceDistance) < 0.08f &&
                            OutsideDistance > 0.25f,
                        ".sdfspace Float16 round-trip must preserve metric signed distances inside, on, and outside the surface");
    }

    // ② ControlVehicle proving-ground scene and the pre-baked .sdfspace world union.
    std::cout << "[2/3] Assembling the ControlVehicle scene and composing the pre-baked .sdfspace world SDF...\n";
    FacetField              SceneFacets{};
    ControlVehiclePlacement Placement{};
    Placement.ArchiveDirectory = (RepositoryRoot / "EngineContent/EnvironmentArchives/ControlVehicle").string();
    if (!std::filesystem::is_directory(Placement.ArchiveDirectory))
    {
        Placement.ArchiveDirectory = "EnvironmentArchives/ControlVehicle";
    }
    VERIFY_GEOMETRY(AssembleControlVehicle(SceneFacets, Placement, &Diagnostic),
                    ("AssembleControlVehicle failed: " + Diagnostic).c_str());

    {
        PrimitivePlacement FloorPlacement{};
        FloorPlacement.TranslationMeters = {0.0, 0.0, 0.0};
        FloorPlacement.ClassificationId  = 0u;
        FloorPlacement.UvScale           = 0.35;
        AppendProceduralGroundGrid(SceneFacets, 14.0, 14.0, 16u, 16u, FloorPlacement);

        PrimitivePlacement ArchPlacement{};
        ArchPlacement.TranslationMeters = {0.0, 0.0, -5.2};
        ArchPlacement.ClassificationId  = 1u;
        AppendProceduralArchTunnel(SceneFacets, 4.2, 0.45, 3.6, 20u, ArchPlacement);
    }

    VolumeDistanceField WorldField{};
    VERIFY_GEOMETRY(ComposeControlVehicleDistanceVolume(WorldField, Placement, {68u, 38u, 92u}, true, &Diagnostic),
                    ("ComposeControlVehicleDistanceVolume failed: " + Diagnostic).c_str());
    const float NearChassisDistance = SampleVolumeDistanceMeters(WorldField, 0.0f, 0.55f, 1.35f); // [m] - Inside the body shell
    const float AboveRoofDistance   = SampleVolumeDistanceMeters(WorldField, 0.0f, 2.65f, 0.0f);  // [m] - Open air above the roof
    std::cout << "      Composed world SDF: NearChassis=" << NearChassisDistance
              << " m, AboveRoof=" << AboveRoofDistance << " m\n";
    VERIFY_GEOMETRY(NearChassisDistance < 0.08f && AboveRoofDistance > 0.55f,
                    "Composed ControlVehicle world SDF must distinguish the chassis volume from open air above the roof");

    // ③ 16-bin SAH software BoundingVolumeAtlas build plus CPU reference closest-hit and shadow-transmittance queries.
    std::cout << "[3/3] Building the 16-bin SAH software BoundingVolumeAtlas and tracing reference rays...\n";
    ScreenSpaceClusterRefinement MaterialLookup{};
    MaterialLookup.RegisterControlVehicleMaterials();
    BoundingVolumeAtlas VolumeAtlas{};
    VERIFY_GEOMETRY(VolumeAtlas.BuildFromFacetField(SceneFacets, &MaterialLookup),
                    "BoundingVolumeAtlas::BuildFromFacetField failed");
    std::cout << "      Built SAH BoundingVolumeAtlas: " << VolumeAtlas.CellCount() << " cells over "
              << VolumeAtlas.TriangleCount() << " triangles\n";

    const SoftwareRayHit       RoofHit       = VolumeAtlas.TraceClosestRay({0.0f, 3.0f, -0.4f}, {0.0f, -1.0f, 0.0f});
    const std::array<float, 3> ChassisShadow = VolumeAtlas.TraceShadowTransmittance({0.0f, 0.05f, 0.0f}, {0.0f, 1.0f, 0.0f}, 5.0f);
    const std::array<float, 3> OpenSkyShadow = VolumeAtlas.TraceShadowTransmittance({5.5f, 0.05f, 5.5f}, {0.0f, 1.0f, 0.0f}, 5.0f);
    std::cout << "      RoofHit=" << (RoofHit.Hit ? 1 : 0) << " at " << RoofHit.DistanceMeters
              << " m, ChassisShadowG=" << ChassisShadow[1] << ", OpenSkyShadowG=" << OpenSkyShadow[1] << "\n";
    VERIFY_GEOMETRY(RoofHit.Hit && RoofHit.DistanceMeters < 2.5f && ChassisShadow[1] < 0.05f,
                    "Software SAH BoundingVolumeAtlas must intersect the vehicle body and occlude sky rays beneath the chassis");
    VERIFY_GEOMETRY(OpenSkyShadow[0] > 0.99f && OpenSkyShadow[1] > 0.99f && OpenSkyShadow[2] > 0.99f,
                    "Unobstructed floor point beside the vehicle must keep full sky transmittance");

    std::cout << "[GeometryProcessingValidation] ALL .SDFSPACE AND SOFTWARE SAH-BVH CHECKS PASSED.\n";
    return 0;
}
