/*==========================================================================================================================================
                                      CLUSTER REFINEMENT & PREVIEW RASTER VALIDATION
==========================================================================================================================================*/
// End-to-end validation of:
//  1. 9 watertight PrimitiveGeometryGenerator shapes + binary FCT1 ControlVehicle{Body,Rim,Tyre}.facet assembly
//  2. Locked-border Garland-Heckbert QEM ScreenSpaceClusterRefinement DAG, curved-glass IOR boost, & Karis group-atomic cut
//  3. Two-phase opaque-only Hi-Z culling (cabin interior behind curved Class-7 glass survives)
//  4. 3-tier virtualized materials (FullStrata 64B / FilteredToksvig 24B / CompactProxy 16B) & 64 KB RAM/VRAM page telemetry
//  5. ThemeCardPalette 1:1 ThemeStructure binding (zero hardcoded green)
//  6. Editor/Development ONLY (#if FRONTIER_EDITOR) Vulkan 1.3 PreviewRaster + ALL 5 modular PreviewShadowModes + Shipping exclusion proof

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/GeometricRaster/PreviewRaster.h"
#include "Engine/GeometricRaster/ThemeCardPalette.h"
#include "Engine/Geometry/GeometryProcessing/ScreenSpaceClusterRefinement.h"
#include "Engine/Geometry/GeometryProcessing/VolumeDistanceArchive.h"
#include "Engine/Geometry/PrimitiveGeometryGenerator.h"

#include <stb_image_write.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace Frontier
{
bool VerifyPreviewRasterExcludedInShipping() noexcept;
}

namespace
{

using namespace Frontier;

void RequireGate(bool Condition, const char* Expr, const char* Detail, int Line)
{
    if (!Condition)
    {
        std::cerr << "[ClusterDiagnosticValidation] FAILED at line " << Line << ": " << Detail
                  << " (" << Expr << ")\n";
        std::exit(1);
    }
}

#define REQUIRE_CD(Cond, Msg) RequireGate((Cond), #Cond, (Msg), __LINE__)

} // namespace

int main(int Argc, char** Argv)
{
    const std::filesystem::path RepoRoot  = (Argc > 1) ? Argv[1] : ".";
    const std::filesystem::path ShaderDir = (Argc > 2) ? Argv[2] : "build/ShaderArchives/Environment";
    const std::filesystem::path OutputDir = (Argc > 3) ? Argv[3] : "build/cluster-diagnostic-exhibits";

    bool MirrorShowcase = false;
    for (int Idx = 1; Idx < Argc; ++Idx)
    {
        if (std::strcmp(Argv[Idx], "--write-showcase") == 0)
        {
            MirrorShowcase = true;
        }
    }

    // 1. Shipping Exclusion Proof
    std::cout << "[1/6] Verifying Shipping Build Exclusion (FRONTIER_EDITOR == 0 TU)...\n";
    REQUIRE_CD(VerifyPreviewRasterExcludedInShipping(),
               "Shipping profile translation unit must compile with FRONTIER_EDITOR == 0");

    // 2. PrimitiveGeometryGenerator 3D Shapes + Binary FCT1 ControlVehicle Assembly
    std::cout << "[2/6] Verifying 9 Watertight PrimitiveGeometryGenerator Builders & ControlVehicle Assembly...\n";
    {
        FacetField BoxF{}, SphF{}, GeoF{}, CylF{}, CapF{}, TorF{}, ConF{}, TunF{}, GrdF{};
        AppendProceduralBox(BoxF, {0.5, 0.5, 0.5});
        AppendProceduralUvSphere(SphF, 1.0, 24u, 16u);
        AppendProceduralGeodesicSphere(GeoF, 1.0, 2u);
        AppendProceduralCylinder(CylF, 0.6, 1.8, 24u);
        AppendProceduralCapsule(CapF, 0.45, 1.2, 20u, 8u);
        AppendProceduralTorus(TorF, 1.2, 0.35, 24u, 16u);
        AppendProceduralCone(ConF, 0.8, 1.6, 24u);
        AppendProceduralArchTunnel(TunF, 2.2, 0.4, 4.0, 16u);
        AppendProceduralGroundGrid(GrdF, 5.0, 5.0, 8u, 8u);

        REQUIRE_CD(BoxF.Classification.size() == 12u && SphF.Classification.size() > 400u && GeoF.Classification.size() == 320u &&
                       CylF.Classification.size() == 96u && CapF.Classification.size() > 400u && TorF.Classification.size() == 768u &&
                       ConF.Classification.size() == 48u && TunF.Classification.size() > 100u && GrdF.Classification.size() == 128u,
                   "All 9 procedural 3D primitive builders must emit valid triangulated facet fields");
    }

    std::string             Diag;
    FacetField              VehicleScene{};
    ControlVehiclePlacement Placement{};
    Placement.ArchiveDirectory = (RepoRoot / "EngineContent/EnvironmentArchives/ControlVehicle").string();
    if (!std::filesystem::is_directory(Placement.ArchiveDirectory))
    {
        Placement.ArchiveDirectory = "EnvironmentArchives/ControlVehicle";
    }
    REQUIRE_CD(AssembleControlVehicle(VehicleScene, Placement, &Diag),
               ("AssembleControlVehicle failed: " + Diag).c_str());
    const uint32_t VehicleTriCount = static_cast<uint32_t>(VehicleScene.Classification.size());
    std::cout << "      ControlVehicle assembled: " << VehicleTriCount << " triangles\n";
    REQUIRE_CD(VehicleTriCount > 14000u,
               "ControlVehicle must assemble body + 4x rims + 4x tyres + interior cabin (>14,000 triangles)");

    // Add proving-grounds asphalt plinth beneath tyres (ClassificationId = 0)
    PrimitivePlacement PlinthPlace{};
    PlinthPlace.TranslationMeters = {0.0, 0.0, 0.0};
    PlinthPlace.ClassificationId  = 0u;
    PlinthPlace.UvScale           = 0.35;
    AppendProceduralGroundGrid(VehicleScene, 9.5, 9.5, 12u, 12u, PlinthPlace);

    // 3. Build Locked-Border QEM ScreenSpaceClusterRefinement + Verify Curved-Glass IOR Boost, Cut, Two-Phase Hi-Z, & 3-Tier Materials
    std::cout << "[3/6] Building QEM ScreenSpaceClusterRefinement DAG, 64 KB Pages, & Virtualized Material Tiers...\n";
    ScreenSpaceClusterRefinement Refinement{};
    Refinement.RegisterControlVehicleMaterials();
    TierBuildSpecification BuildSpec{};
    BuildSpec.TriangleLimit      = 96u;
    BuildSpec.CoarseningFraction = 0.52;
    BuildSpec.MaximumTiers       = 5u;
    REQUIRE_CD(Refinement.BuildFromFacetField(VehicleScene, BuildSpec),
               "ScreenSpaceClusterRefinement::BuildFromFacetField failed");

    std::cout << "      Built " << Refinement.TierSequence().Tiers.size() << " LOD tiers, "
              << Refinement.ClusterRecords().size() << " total clusters across "
              << Refinement.StreamingPages().size() << " x 64 KB pages\n";
    REQUIRE_CD(Refinement.TierSequence().Tiers.size() >= 3u && Refinement.StreamingPages().size() >= 2u,
               "ScreenSpaceClusterRefinement must construct multi-tier QEM DAG packed into 64 KB pages");

    float MaxGlassBoost = 1.0f;
    for (const auto& Rec : Refinement.ClusterRecords())
    {
        if (Rec.TransmissiveGlass)
        {
            MaxGlassBoost = std::max(MaxGlassBoost, Rec.OpticalDeviationBoost);
        }
    }
    std::cout << "      MaxCurvedGlassOpticalBoost=" << MaxGlassBoost << "x\n";
    REQUIRE_CD(MaxGlassBoost > 1.65f,
               "Curved automotive laminated glazing (Class 7/12, IOR 1.52/1.58) must receive refractive optical error boost");

    ClusterCameraView NearCam{};
    NearCam.CameraOrigin     = {3.20f, 1.48f, 4.20f};
    NearCam.CameraTarget     = {-0.10f, 0.72f, 0.05f};
    NearCam.ViewportWidthPx  = 800u;
    NearCam.ViewportHeightPx = 450u;

    ClusterCameraView MidCam  = NearCam;
    MidCam.CameraOrigin       = {12.0f, 5.2f, 16.0f};
    ClusterCameraView FarCam  = NearCam;
    FarCam.CameraOrigin       = {68.0f, 26.0f, 88.0f};

    const ClusterRuntimeFrameReport NearCut = Refinement.ExecuteCutAndTwoPhaseHiZ(NearCam);
    const ClusterRuntimeFrameReport MidCut  = Refinement.ExecuteCutAndTwoPhaseHiZ(MidCam);
    const ClusterRuntimeFrameReport FarCut  = Refinement.ExecuteCutAndTwoPhaseHiZ(FarCam);

    std::cout << "      DAG Cut Active Triangles: Near(4.2m)=" << NearCut.ActiveTrianglesAfterCull
              << " -> Mid(20m)=" << MidCut.ActiveTrianglesAfterCull
              << " -> Far(110m)=" << FarCut.ActiveTrianglesAfterCull
              << " | OpaqueBehindGlassSurvived=" << NearCut.OpaqueBehindGlassSurvived << "\n";

    REQUIRE_CD(NearCut.ActiveTrianglesAfterCull > FarCut.ActiveTrianglesAfterCull &&
                   FarCut.ActiveTrianglesAfterCull > 0u,
               "QEM Cluster-DAG cut must reduce active triangles at far camera distances");
    REQUIRE_CD(NearCut.OpaqueBehindGlassSurvived > 0u,
               "Two-Phase Opaque-Only Hi-Z must NOT occlude interior cabin seats behind transmissive Class-7 glass");
    REQUIRE_CD(NearCut.MaterialTierClusterCounts[0] > 0u &&
                   (FarCut.MaterialTierClusterCounts[1] + FarCut.MaterialTierClusterCounts[2]) > 0u,
               "3-tier material virtualization must select FullStrata (64B) near and Toksvig/CompactProxy far");

    const ClusterMemoryTelemetry UnconstrainedMem = Refinement.QueryMemoryTelemetry(NearCut, 64u * 1024u * 1024u);
    Refinement.EnforcePageResidencyBudget(128u * 1024u, FarCam, 2u);
    const ClusterMemoryTelemetry TightMem         = Refinement.QueryMemoryTelemetry(NearCut, 128u * 1024u);
    REQUIRE_CD(UnconstrainedMem.TotalHostRamBytes > 128u * 1024u &&
                   TightMem.PinnedRootPages >= 1u &&
                   TightMem.ResidentVramBytes <= UnconstrainedMem.TotalPagedVramBytes,
               "ClusterMemoryTelemetry must track RAM/VRAM 64 KB page bytes while keeping root tier pinned");
    Refinement.EnforcePageResidencyBudget(64u * 1024u * 1024u, NearCam, 3u);

    // 4. Verify ThemeCardPalette 1:1 ThemeStructure Binding (Zero Hardcoded Green)
    std::cout << "[4/6] Verifying ThemeCardPalette 1:1 ThemeStructure Binding (Zero Hardcoded Green)...\n";
    {
        ThemeStructure         Theme{};
        const ThemeCardPalette DefaultCard = ResolveThemeCardPalette(Theme);
        REQUIRE_CD(DefaultCard.WorkspaceBackground.Red == 0.0f &&
                       DefaultCard.WorkspaceBackground.Green == 0.0f &&
                       DefaultCard.AccentPrimary.Blue > DefaultCard.AccentPrimary.Green &&
                       DefaultCard.FontFamilyAssetStem == "GeneralSans",
                   "Default ThemeCardPalette must bind to OLED black + Blue (#3B82F6) accent + GeneralSans with zero green tint");

        Theme.ConfigureTheme(ThemeCategory::Sepia);
        Theme.ConfigureAccent(AccentCategory::Amber);
        Theme.ConfigureFontFamily(FontFamilyCategory::JetBrainsMono);
        const ThemeCardPalette SepiaCard = ResolveThemeCardPalette(Theme);
        REQUIRE_CD(SepiaCard.WorkspaceBackground.Red > 0.85f &&
                       SepiaCard.ForegroundPrimary.Red < 0.45f &&
                       SepiaCard.AccentPrimary.Red > 0.90f &&
                       SepiaCard.FontFamilyAssetStem == "JetBrainsMono",
                   "Reconfiguring ThemeStructure must dynamically update card background, foreground, accent, and font");
    }

    // 5. Execute Editor/Development Vulkan 1.3 PreviewRaster + Attach Pre-Baked .sdfspace Distance Volume
    std::cout << "[5/6] Initializing Vulkan 1.3 PreviewRaster & Composing Pre-Baked .sdfspace Vehicle SDF...\n";
    VulkanComputeExchange Exchange{};
    REQUIRE_CD(Exchange.Initialize("", &Diag), ("VulkanComputeExchange::Initialize failed: " + Diag).c_str());

    PreviewRaster Raster{};
    REQUIRE_CD(Raster.Initialize(Exchange, ShaderDir.string(), &Diag),
               ("PreviewRaster::Initialize failed: " + Diag).c_str());

    VolumeDistanceField VehicleSdf{};
    REQUIRE_CD(ComposeControlVehicleDistanceVolume(VehicleSdf, Placement, {64u, 36u, 88u}, false, &Diag),
               ("ComposeControlVehicleDistanceVolume failed: " + Diag).c_str());
    REQUIRE_CD(Raster.AttachDistanceVolume(Exchange, &VehicleSdf),
               "PreviewRaster::AttachDistanceVolume failed");

    PreviewAtmosphereBackdrop Sky{};
    Sky.SolarDirectionUnit = {-0.54f, 0.62f, 0.57f};
    Sky.ZenithSkyRgb       = {0.11f, 0.25f, 0.58f};
    Sky.HorizonSkyRgb      = {0.54f, 0.66f, 0.82f};
    Sky.ShadowMode         = PreviewShadowMode::OrthoClusterDepth;
    Sky.ExposureScale      = 1.08f;

    std::filesystem::create_directories(OutputDir);
    const std::filesystem::path ShowcaseDir = RepoRoot / "Exhibits/ClusterDiagnostic/Images";
    if (MirrorShowcase)
    {
        std::filesystem::create_directories(ShowcaseDir);
    }

    const auto RenderAndSave = [&](PreviewViewMode Mode, PreviewShadowMode Shadow, const char* FileName, bool MirrorToShowcase, uint64_t* OutSumLum = nullptr) {
        PreviewAtmosphereBackdrop LocalSky = Sky;
        LocalSky.ShadowMode                = Shadow;
        PreviewFrameExecution Exec{};
        REQUIRE_CD(Raster.RenderView(Exchange, Refinement, NearCam, Mode, LocalSky, &Exec),
                   "PreviewRaster::RenderView failed");
        std::vector<uint8_t> SrgbBytes;
        REQUIRE_CD(Raster.ReadbackTonemappedSrgb8(Exchange, LocalSky.ExposureScale, SrgbBytes),
                   "PreviewRaster::ReadbackTonemappedSrgb8 failed");

        uint64_t SumLum        = 0u;
        uint32_t NonZeroPixels = 0u;
        for (size_t Off = 0u; Off < SrgbBytes.size(); Off += 4u)
        {
            const uint32_t L = SrgbBytes[Off + 0u] + SrgbBytes[Off + 1u] + SrgbBytes[Off + 2u];
            SumLum += L;
            if (L > 18u) ++NonZeroPixels;
        }
        REQUIRE_CD(NonZeroPixels > (NearCam.ViewportWidthPx * NearCam.ViewportHeightPx) / 2u && SumLum > 1000000u,
                   "Rendered PreviewRaster view must produce valid lit/inspection pixels");
        if (OutSumLum) *OutSumLum = SumLum;

        const auto OutFile = OutputDir / FileName;
        REQUIRE_CD(stbi_write_png(OutFile.string().c_str(),
                                  static_cast<int>(NearCam.ViewportWidthPx),
                                  static_cast<int>(NearCam.ViewportHeightPx),
                                  4,
                                  SrgbBytes.data(),
                                  static_cast<int>(NearCam.ViewportWidthPx * 4u)) != 0,
                   "Failed writing PNG exhibit");
        if (MirrorShowcase && MirrorToShowcase)
        {
            (void)stbi_write_png((ShowcaseDir / FileName).string().c_str(),
                                 static_cast<int>(NearCam.ViewportWidthPx),
                                 static_cast<int>(NearCam.ViewportHeightPx),
                                 4,
                                 SrgbBytes.data(),
                                 static_cast<int>(NearCam.ViewportWidthPx * 4u));
        }
        std::cout << "      Saved " << FileName << " (ShadowMode=" << static_cast<uint32_t>(Exec.ActiveShadowMode)
                  << ", ActiveClusters=" << Exec.UploadedClusterCount
                  << ", ActiveTris=" << Exec.UploadedTriangleCount << ")\n";
    };

    // 6. Verify ALL 5 Modular PreviewShadowModes + Core Inspection Views
    std::cout << "[6/6] Rendering Modular PreviewShadowModes (None / Contact / OrthoDepth / SdfCone / Hybrid) & Inspection Views...\n";
    uint64_t LumNoShadow     = 0u;
    uint64_t LumContact      = 0u;
    uint64_t LumOrthoDepth   = 0u;
    uint64_t LumSdfCone      = 0u;
    uint64_t LumHybrid       = 0u;

    RenderAndSave(PreviewViewMode::ShadedPreview, PreviewShadowMode::None,                   "PreviewShadow0_None.png", false, &LumNoShadow);
    RenderAndSave(PreviewViewMode::ShadedPreview, PreviewShadowMode::ContactEllipsoid,       "PreviewShadow1_ContactEllipsoid.png", false, &LumContact);
    RenderAndSave(PreviewViewMode::ShadedPreview, PreviewShadowMode::OrthoClusterDepth,      "ControlVehicleShadedPreview.png", true, &LumOrthoDepth);
    RenderAndSave(PreviewViewMode::ShadedPreview, PreviewShadowMode::SignedDistanceCone,     "ControlVehicleSdfConePreviewShadow.png", true, &LumSdfCone);
    RenderAndSave(PreviewViewMode::ShadedPreview, PreviewShadowMode::HybridOrthoAndDistance, "ControlVehicleHybridPreviewShadow.png", true, &LumHybrid);

    REQUIRE_CD(LumNoShadow > LumOrthoDepth && LumNoShadow > LumSdfCone && LumNoShadow > LumHybrid && LumOrthoDepth != LumSdfCone,
               "All modular directional PreviewShadowModes (OrthoClusterDepth, SignedDistanceCone, HybridOrthoAndDistance) must cast measurable directional shadows relative to PreviewShadowMode::None");

    RenderAndSave(PreviewViewMode::ClusterIdentity, PreviewShadowMode::None, "ControlVehicleClusterIdentity.png", true);
    RenderAndSave(PreviewViewMode::ShadedWireframe, PreviewShadowMode::None, "ControlVehicleRefinementAndWireframe.png", true);
    RenderAndSave(PreviewViewMode::GlassLayerPeel,  PreviewShadowMode::None, "ControlVehicleGlassLayerPeel.png", true);

    Raster.Shutdown(Exchange);
    Exchange.Shutdown();

    std::cout << "[ClusterDiagnosticValidation] ALL REFINEMENT-DAG, MODULAR PREVIEW SHADOWS, THEME CARD & SHIPPING EXCLUSION CHECKS PASSED.\n";
    return 0;
}
