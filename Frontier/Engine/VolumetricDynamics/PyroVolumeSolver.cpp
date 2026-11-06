/*==========================================================================================================================================
                                                           PYROVOLUMESOLVER.CPP
==========================================================================================================================================*/
// 🧩 1:1 C++ Vulkan 1.1 3D Eulerian Voxel Lattice Pyro Solver & Volumetric Raymarcher. Zero OpenGL dependencies.

#include "Frontier/Engine/VolumetricDynamics/PyroVolumeSolver.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier
{

namespace
{

constexpr float PiVal   = 3.14159265358979323846f;
constexpr float Deg2Rad = PiVal / 180.0f;

struct alignas(16) PyroUniformBlock
{
    float GridParams[4];           // x=gridRes, y=tilesX, z=tilesY, w=dt
    float TimeAndSwitches[4];         // x=time, y=hasCurl, z=worldSdfEnabled, w=clearMode
    float ClearColorVal[4];        // rgba
    float DomainBoxMin[4];         // xyz=boxMin, w=enclosedBox
    float DomainBoxMax[4];         // xyz=boxMax, w=aspectRatio
    float EmitterParams0[4];       // x=enabled, y=rate, z=radius, w=height
    float EmitterParams1[4];       // x=smoke, y=temp, z=fuel, w=upVel
    float EmitterParams2[4];       // x=swirl, y=brushActive, z=obstacleType, w=obstacleRadius
    float BrushPosAndPad[4];       // xyz=brushPos
    float BrushVelAndPad[4];       // xyz=brushVel
    float ObstaclePosAndPad[4];    // xyz=obstaclePos
    float ObstacleVelAndPad[4];    // xyz=obstacleVel
    float BlastCenterAndRadius[4]; // xyz=blastCenter, w=blastRadius
    float BlastThermoParams[4];    // x=active, y=strength, z=temp, w=fuel
    float BlastExtraParams[4];     // x=smoke, y=lobes, z=seed, w=projCount
    float BlastDirVelAndPad[4];    // xyz=blastDirVel, w=extSourceCount
    float ProjData[6][4];          // xyz=pos, w=radius
    float ProjVel[6][4];           // xyz=vel
    float ExtPosRad[8][4];         // xyz=uvw, w=radiusUvw
    float ExtVelRate[8][4];        // xyz=velPyro, w=rate
    float ExtThermo[8][4];         // x=smoke, y=temp, z=fuel
    float AdvectAndForces0[4];     // x=smokeDiss, y=tempCool, z=fuelDiss, w=velDamping
    float AdvectAndForces1[4];     // x=vorticity, y=turbStrength, z=turbScale, w=windX
    float AdvectAndForces2[4];     // x=windZ, y=burnRate, z=burnHeat, w=sootGen
    float AdvectAndForces3[4];     // x=buoyancy, y=smokeWeight, z=combExpansion, w=fireScatter
    float CamPosAndFov[4];         // xyz=camPos, w=tanHalfFov
    float CamForwardAndComp[4];    // xyz=camForward, w=compositeOverScene
    float CamRightAndTransp[4];    // xyz=camRight, w=transparentBg
    float CamUpAndChannel[4];      // xyz=camUp, w=renderChannel
    float SunDirAndIntensity[4];   // xyz=sunDir, w=sunIntensity
    float OpticsParams0[4];        // x=ambient, y=extinction, z=albedo, w=fireIntensity
    float OpticsParams1[4];        // x=tempScale, y=palette, z=internalScatter, w=shadowDensity
    float OpticsParams2[4];        // x=phaseAniso, y=exposure, z=bloom, w=godRays
    float OpticsParams3[4];        // x=shockwave, y=raySteps, z=shadowSteps, w=voxelQuant
    float SliceAndPipParams[4];    // x=sliceAxis, y=slicePos, z=showAtlasPip, w=atlasPipField
    float EmberAndActiveParams[4]; // x=showActiveCells, y=emberSize, z=emberIntensity, w=emberLifetime
    float EmberExtraParams[4];     // x=emberAshiness, y=viewportHeightPx, z=nearPlane, w=farPlane
    float WorldGridOrigin[4];      // x=originX, y=originZ, z=priorOriginX, w=priorOriginZ
    float WorldGridFlags[4];       // x=worldGridEnabled, y=cellSizeXZ, z=cellSizeY, w=floorWorldZ
};
static_assert(sizeof(PyroUniformBlock) == 70u * 16u, "PyroUniformBlock std140 size mismatch");

void ResolveTileLayout(int32_t RequestedRes, int32_t& OutRes, int32_t& OutTx, int32_t& OutTy) noexcept
{
    static constexpr int32_t Supported[8][3] = {
        {  16,  4,  4 },
        {  32,  8,  4 },
        {  48,  8,  6 },
        {  64,  8,  8 },
        {  80, 10,  8 },
        {  96, 12,  8 },
        { 112, 14,  8 },
        { 128, 16,  8 },
    };
    int BestIdx  = 2;
    int BestDiff = 9999;
    for (int Idx = 0; Idx < 8; ++Idx)
    {
        const int Diff = std::abs(Supported[Idx][0] - RequestedRes);
        if (Diff < BestDiff)
        {
            BestDiff = Diff;
            BestIdx  = Idx;
        }
    }
    OutRes = Supported[BestIdx][0];
    OutTx  = Supported[BestIdx][1];
    OutTy  = Supported[BestIdx][2];
}

std::array<float, 3> Normalize3(const std::array<float, 3>& V) noexcept
{
    const float L = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);
    if (L < 1.0e-6f)
        return { 0.0f, 1.0f, 0.0f };
    return { V[0] / L, V[1] / L, V[2] / L };
}

std::array<float, 3> Cross3(const std::array<float, 3>& A, const std::array<float, 3>& B) noexcept
{
    return {
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0],
    };
}

} // namespace

//----------------------------------------------------------------------------------------------------------------------
//                                                PYRO ORBIT CAMERA
//----------------------------------------------------------------------------------------------------------------------

void PyroOrbitCamera::Reset() noexcept
{
    Theta      = 0.62f;
    Phi        = 1.18f;
    Distance   = 3.15f;
    Target     = { 0.0f, 0.35f, 0.0f };
    FovDegrees = 46.0f;
}

void PyroOrbitCamera::ApplyPresetView(int ViewOrdinal) noexcept
{
    if (ViewOrdinal == 1)      { Theta = 0.0f;           Phi = PiVal * 0.48f; Distance = 3.2f; } // Front
    else if (ViewOrdinal == 2) { Theta = PiVal * 0.5f;   Phi = PiVal * 0.48f; Distance = 3.2f; } // Side
    else if (ViewOrdinal == 3) { Theta = 0.0f;           Phi = 0.14f;         Distance = 3.5f; } // Top
    else                       { Reset(); }
}

void PyroOrbitCamera::OrbitByPixels(float DeltaX, float DeltaY) noexcept
{
    Theta -= DeltaX * 0.0065f;
    Phi    = std::clamp(Phi - DeltaY * 0.0065f, 0.08f, PiVal - 0.08f);
}

void PyroOrbitCamera::PanByPixels(float DeltaX, float DeltaY) noexcept
{
    const PyroCameraFrame Frame = ComputeFrame();
    const float Scale           = Distance * 0.0014f;
    for (int C = 0; C < 3; ++C)
        Target[static_cast<size_t>(C)] += (-DeltaX * Frame.Right[static_cast<size_t>(C)] + DeltaY * Frame.Up[static_cast<size_t>(C)]) * Scale;
}

void PyroOrbitCamera::ZoomByWheel(float WheelSteps) noexcept
{
    Distance = std::clamp(Distance * std::pow(0.88f, WheelSteps), 0.85f, 12.0f);
}

void PyroOrbitCamera::AdvanceTurntable(float DtSeconds) noexcept
{
    if (AutoRotate)
        Theta += DtSeconds * 0.38f;
}

PyroCameraFrame PyroOrbitCamera::ComputeFrame(float AspectOverride) const noexcept
{
    PyroCameraFrame Frame{};
    Frame.Aspect     = (AspectOverride > 0.05f) ? AspectOverride : Aspect;
    Frame.FovRadians = FovDegrees * Deg2Rad;

    const float SinPhi = std::sin(Phi);
    Frame.Position = {
        Target[0] + Distance * SinPhi * std::sin(Theta),
        Target[1] + Distance * std::cos(Phi),
        Target[2] + Distance * SinPhi * std::cos(Theta),
    };
    Frame.Forward = Normalize3({
        Target[0] - Frame.Position[0],
        Target[1] - Frame.Position[1],
        Target[2] - Frame.Position[2],
    });
    Frame.Right = Normalize3(Cross3(Frame.Forward, { 0.0f, 1.0f, 0.0f }));
    Frame.Up    = Normalize3(Cross3(Frame.Right, Frame.Forward));
    return Frame;
}

bool PyroOrbitCamera::RaycastBrushPoint(float NdcX, float NdcY, std::array<float, 3>& OutWorldPoint) const noexcept
{
    const PyroCameraFrame F = ComputeFrame();
    const float TanHalf     = std::tan(F.FovRadians * 0.5f);
    const std::array<float, 3> Dir = Normalize3({
        F.Forward[0] + NdcX * F.Aspect * TanHalf * F.Right[0] + NdcY * TanHalf * F.Up[0],
        F.Forward[1] + NdcX * F.Aspect * TanHalf * F.Right[1] + NdcY * TanHalf * F.Up[1],
        F.Forward[2] + NdcX * F.Aspect * TanHalf * F.Right[2] + NdcY * TanHalf * F.Up[2],
    });
    // Intersect camera-facing plane passing through Target
    const float Denom = Dir[0] * F.Forward[0] + Dir[1] * F.Forward[1] + Dir[2] * F.Forward[2];
    if (std::fabs(Denom) < 1.0e-4f)
        return false;
    const float Numer = (Target[0] - F.Position[0]) * F.Forward[0]
                      + (Target[1] - F.Position[1]) * F.Forward[1]
                      + (Target[2] - F.Position[2]) * F.Forward[2];
    const float T = Numer / Denom;
    if (T <= 0.05f)
        return false;
    OutWorldPoint = { F.Position[0] + Dir[0] * T, F.Position[1] + Dir[1] * T, F.Position[2] + Dir[2] * T };
    return true;
}

//----------------------------------------------------------------------------------------------------------------------
//                                         INITIALIZATION & LATTICE TARGETS
//----------------------------------------------------------------------------------------------------------------------

bool PyroVolumeSolver::Initialize(VulkanContext& Ctx, const GasSettings& InitialSettings, std::string& OutDiagnostic)
{
    Shutdown(Ctx);

    auto MakeSimPipe = [&](const VulkanShaders::SpirvBinaryView& Frag, uint32_t ColorCount) -> VkPipeline
    {
        VulkanPipelineSpec Spec{};
        Spec.VertexShader     = VulkanShaders::FullScreenQuadVert;
        Spec.FragmentShader   = Frag;
        Spec.CompatibleTarget = (ColorCount == 2u) ? Ctx.QueryTargetDualRgba16f() : Ctx.QueryTargetSingleRgba16f();
        Spec.ColorAttachments = ColorCount;
        Spec.VertexFormat     = VulkanVertexLayout::FullscreenProcedural;
        Spec.Topology         = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        Spec.Blend            = VulkanBlendFormula::OpaqueOverwrite;
        return Ctx.CreateGraphicsPipeline(Spec, OutDiagnostic);
    };

    PipeClearSingle = MakeSimPipe(VulkanShaders::PyroClearSingleFrag, 1u);
    PipeClearDual   = MakeSimPipe(VulkanShaders::PyroClearDualFrag,   2u);
    PipeSplat       = MakeSimPipe(VulkanShaders::PyroSplatFrag,       2u);
    PipeAdvect      = MakeSimPipe(VulkanShaders::PyroAdvectFrag,      2u);
    PipeMacCormack  = MakeSimPipe(VulkanShaders::PyroMacCormackFrag,  2u);
    PipeCurl        = MakeSimPipe(VulkanShaders::PyroCurlFrag,        1u);
    PipeVorticity   = MakeSimPipe(VulkanShaders::PyroVorticityFrag,   1u);
    PipeBuoyancy    = MakeSimPipe(VulkanShaders::PyroBuoyancyFrag,    2u);
    PipeDivergence  = MakeSimPipe(VulkanShaders::PyroDivergenceFrag,  1u);
    PipeJacobi      = MakeSimPipe(VulkanShaders::PyroJacobiFrag,      1u);
    PipeGradient    = MakeSimPipe(VulkanShaders::PyroGradientFrag,    1u);

    // Standalone RGBA8 raymarch pipeline
    {
        VulkanPipelineSpec Spec{};
        Spec.VertexShader     = VulkanShaders::FullScreenQuadVert;
        Spec.FragmentShader   = VulkanShaders::PyroRaymarchFrag;
        Spec.CompatibleTarget = Ctx.QueryTargetSingleRgba8Clear();
        Spec.ColorAttachments = 1u;
        Spec.Blend            = VulkanBlendFormula::OpaqueOverwrite;
        PipeRaymarchStandalone = Ctx.CreateGraphicsPipeline(Spec, OutDiagnostic);
    }

    // World premultiplied-alpha depth-clipped raymarch compositor pipeline (targets SceneColorImage RGBA8 Load!)
    {
        VulkanPipelineSpec Spec{};
        Spec.VertexShader     = VulkanShaders::FullScreenQuadVert;
        Spec.FragmentShader   = VulkanShaders::PyroRaymarchFrag;
        Spec.CompatibleTarget = Ctx.QueryTargetSingleRgba8Load();
        Spec.ColorAttachments = 1u;
        Spec.Blend            = VulkanBlendFormula::PremultipliedAlpha;
        Spec.DepthTest        = false;
        Spec.DepthWrite       = false;
        PipeRaymarchWorldMrt  = Ctx.CreateGraphicsPipeline(Spec, OutDiagnostic);
    }

    // Embers pipelines (Standalone RGBA8 + World RGBA8 Load)
    {
        VulkanPipelineSpec Spec{};
        Spec.VertexShader     = VulkanShaders::PyroEmbersVert;
        Spec.FragmentShader   = VulkanShaders::PyroEmbersFrag;
        Spec.CompatibleTarget = Ctx.QueryTargetSingleRgba8Load();
        Spec.ColorAttachments = 1u;
        Spec.VertexFormat     = VulkanVertexLayout::FloatSeed1;
        Spec.Topology         = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        Spec.Blend            = VulkanBlendFormula::PremultipliedAlpha;
        PipeEmbersStandalone  = Ctx.CreateGraphicsPipeline(Spec, OutDiagnostic);
        PipeEmbersWorldMrt    = Ctx.CreateGraphicsPipeline(Spec, OutDiagnostic);
    }

    if (!PipeClearSingle || !PipeClearDual || !PipeSplat || !PipeAdvect || !PipeMacCormack ||
        !PipeCurl || !PipeVorticity || !PipeBuoyancy || !PipeDivergence || !PipeJacobi ||
        !PipeGradient || !PipeRaymarchStandalone || !PipeRaymarchWorldMrt ||
        !PipeEmbersStandalone || !PipeEmbersWorldMrt)
    {
        Shutdown(Ctx);
        return false;
    }

    // Seed 1,000 deterministic ember particles
    std::vector<float> Seeds(1000u);
    for (size_t Idx = 0u; Idx < Seeds.size(); ++Idx)
        Seeds[Idx] = (static_cast<float>(Idx) + 0.5f) / 1000.0f;
    if (!Ctx.CreateHostBuffer(Seeds.size() * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, EmberSeedBuffer, OutDiagnostic))
    {
        Shutdown(Ctx);
        return false;
    }
    std::memcpy(EmberSeedBuffer.Mapped, Seeds.data(), Seeds.size() * sizeof(float));

    if (!AllocateLatticeTargets(Ctx, InitialSettings.LatticeResolution, OutDiagnostic))
    {
        Shutdown(Ctx);
        return false;
    }

    Initialized = true;
    ClearLattice(Ctx);
    return true;
}

void PyroVolumeSolver::Shutdown(VulkanContext& Ctx) noexcept
{
    Ctx.WaitIdle();
    DestroyLatticeTargets(Ctx);
    Ctx.DestroyFramebuffer(ViewportFboClear);
    Ctx.DestroyFramebuffer(ViewportFboLoad);
    Ctx.DestroyImageTarget(ViewportImage);
    Ctx.DestroyBuffer(EmberSeedBuffer);

    Ctx.DestroyPipeline(PipeEmbersWorldMrt);
    Ctx.DestroyPipeline(PipeEmbersStandalone);
    Ctx.DestroyPipeline(PipeRaymarchWorldMrt);
    Ctx.DestroyPipeline(PipeRaymarchStandalone);
    Ctx.DestroyPipeline(PipeGradient);
    Ctx.DestroyPipeline(PipeJacobi);
    Ctx.DestroyPipeline(PipeDivergence);
    Ctx.DestroyPipeline(PipeBuoyancy);
    Ctx.DestroyPipeline(PipeVorticity);
    Ctx.DestroyPipeline(PipeCurl);
    Ctx.DestroyPipeline(PipeMacCormack);
    Ctx.DestroyPipeline(PipeAdvect);
    Ctx.DestroyPipeline(PipeSplat);
    Ctx.DestroyPipeline(PipeClearDual);
    Ctx.DestroyPipeline(PipeClearSingle);
    Initialized = false;
}

bool PyroVolumeSolver::AllocateLatticeTargets(VulkanContext& Ctx, int32_t NewRes, std::string& OutDiagnostic)
{
    DestroyLatticeTargets(Ctx);
    ResolveTileLayout(NewRes, LatticeRes, TilesX, TilesY);
    AtlasWidth  = LatticeRes * TilesX;
    AtlasHeight = LatticeRes * TilesY;

    const VkImageUsageFlags ColorAtt = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    for (int Idx = 0; Idx < 3; ++Idx)
    {
        if (!Ctx.CreateImageTarget(AtlasWidth, AtlasHeight, VK_FORMAT_R16G16B16A16_SFLOAT, ColorAtt, true, VelImg[Idx], OutDiagnostic))
            return false;
        if (!Ctx.CreateImageTarget(AtlasWidth, AtlasHeight, VK_FORMAT_R16G16B16A16_SFLOAT, ColorAtt, true, ThermoImg[Idx], OutDiagnostic))
            return false;

        const VulkanImageTarget* Pair[2] = { &VelImg[Idx], &ThermoImg[Idx] };
        FboDual[Idx] = Ctx.CreateFramebuffer(Ctx.QueryTargetDualRgba16f(), Pair, 2u, AtlasWidth, AtlasHeight);
    }

    for (int Idx = 0; Idx < 2; ++Idx)
    {
        const VulkanImageTarget* SingleVel[1] = { &VelImg[Idx] };
        FboVelSingle[Idx] = Ctx.CreateFramebuffer(Ctx.QueryTargetSingleRgba16f(), SingleVel, 1u, AtlasWidth, AtlasHeight);

        if (!Ctx.CreateImageTarget(AtlasWidth, AtlasHeight, VK_FORMAT_R16G16B16A16_SFLOAT, ColorAtt, true, PresImg[Idx], OutDiagnostic))
            return false;
        const VulkanImageTarget* SinglePres[1] = { &PresImg[Idx] };
        FboPres[Idx] = Ctx.CreateFramebuffer(Ctx.QueryTargetSingleRgba16f(), SinglePres, 1u, AtlasWidth, AtlasHeight);
    }

    if (!Ctx.CreateImageTarget(AtlasWidth, AtlasHeight, VK_FORMAT_R16G16B16A16_SFLOAT, ColorAtt, true, CurlImg, OutDiagnostic))
        return false;
    if (!Ctx.CreateImageTarget(AtlasWidth, AtlasHeight, VK_FORMAT_R16G16B16A16_SFLOAT, ColorAtt, true, DivImg, OutDiagnostic))
        return false;
    if (!Ctx.CreateImageTarget(AtlasWidth, AtlasHeight, VK_FORMAT_R16G16B16A16_SFLOAT, 0u, false, WorldSdfImg, OutDiagnostic))
        return false;

    const VulkanImageTarget* SingleCurl[1] = { &CurlImg };
    const VulkanImageTarget* SingleDiv[1]  = { &DivImg };
    FboCurl = Ctx.CreateFramebuffer(Ctx.QueryTargetSingleRgba16f(), SingleCurl, 1u, AtlasWidth, AtlasHeight);
    FboDiv  = Ctx.CreateFramebuffer(Ctx.QueryTargetSingleRgba16f(), SingleDiv,  1u, AtlasWidth, AtlasHeight);

    // Initialize WorldSdfImg to SHADER_READ_ONLY_OPTIMAL with +10m open air
    std::vector<float> OpenAir(static_cast<size_t>(AtlasWidth) * static_cast<size_t>(AtlasHeight) * 4u, 0.0f);
    for (size_t Idx = 3u; Idx < OpenAir.size(); Idx += 4u)
        OpenAir[Idx] = 10.0f;
    Ctx.UploadRgba32fImage(WorldSdfImg, OpenAir.data(), OpenAir.size(), OutDiagnostic);

    ReadSlotIdx = 0;
    ReadPresIdx = 0;
    return true;
}

void PyroVolumeSolver::DestroyLatticeTargets(VulkanContext& Ctx) noexcept
{
    for (int Idx = 0; Idx < 3; ++Idx)
    {
        Ctx.DestroyFramebuffer(FboDual[Idx]);
        Ctx.DestroyImageTarget(VelImg[Idx]);
        Ctx.DestroyImageTarget(ThermoImg[Idx]);
    }
    for (int Idx = 0; Idx < 2; ++Idx)
    {
        Ctx.DestroyFramebuffer(FboVelSingle[Idx]);
        Ctx.DestroyFramebuffer(FboPres[Idx]);
        Ctx.DestroyImageTarget(PresImg[Idx]);
    }
    Ctx.DestroyFramebuffer(FboCurl);
    Ctx.DestroyFramebuffer(FboDiv);
    Ctx.DestroyImageTarget(CurlImg);
    Ctx.DestroyImageTarget(DivImg);
    Ctx.DestroyImageTarget(WorldSdfImg);
}

bool PyroVolumeSolver::EnsureLatticeResolution(VulkanContext& Ctx, int32_t RequestedResolution)
{
    int32_t TargetRes = 48, Tx = 8, Ty = 6;
    ResolveTileLayout(RequestedResolution, TargetRes, Tx, Ty);
    if (TargetRes == LatticeRes && VelImg[0].Image != VK_NULL_HANDLE)
        return true;
    Ctx.WaitIdle();
    std::string Diag;
    if (!AllocateLatticeTargets(Ctx, TargetRes, Diag))
        return false;
    ClearLattice(Ctx);
    return true;
}

void PyroVolumeSolver::ClearLattice(VulkanContext& Ctx)
{
    if (!Initialized)
        return;

    PyroUniformBlock Ubo{};
    Ubo.GridParams[0] = static_cast<float>(LatticeRes);
    Ubo.GridParams[1] = static_cast<float>(TilesX);
    Ubo.GridParams[2] = static_cast<float>(TilesY);

    VkCommandBuffer Cmd = Ctx.BeginFrameCommands();

    for (int Idx = 0; Idx < 3; ++Idx)
    {
        Ctx.TransitionImage(Cmd, VelImg[Idx],    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        Ctx.TransitionImage(Cmd, ThermoImg[Idx], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        Ctx.BeginTarget(Cmd, Ctx.QueryTargetDualRgba16f(), FboDual[Idx], AtlasWidth, AtlasHeight);
        Ctx.BindPipelineAndDescriptors(Cmd, PipeClearDual, &Ubo, sizeof(Ubo));
        Ctx.Vk().CmdDraw(Cmd, 3u, 1u, 0u, 0u);
        Ctx.EndTarget(Cmd);
        Ctx.TransitionImage(Cmd, VelImg[Idx],    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Ctx.TransitionImage(Cmd, ThermoImg[Idx], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    auto ClearSingle = [&](VulkanImageTarget& Img, VkFramebuffer Fbo)
    {
        Ctx.TransitionImage(Cmd, Img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        Ctx.BeginTarget(Cmd, Ctx.QueryTargetSingleRgba16f(), Fbo, AtlasWidth, AtlasHeight);
        Ctx.BindPipelineAndDescriptors(Cmd, PipeClearSingle, &Ubo, sizeof(Ubo));
        Ctx.Vk().CmdDraw(Cmd, 3u, 1u, 0u, 0u);
        Ctx.EndTarget(Cmd);
        Ctx.TransitionImage(Cmd, Img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    };
    ClearSingle(CurlImg,    FboCurl);
    ClearSingle(DivImg,     FboDiv);
    ClearSingle(PresImg[0], FboPres[0]);
    ClearSingle(PresImg[1], FboPres[1]);

    Ctx.SubmitAndWaitFrameCommands();

    PendingBlasts.clear();
    Projectiles.clear();
    DynamicSurge          = 1.0f;
    SimulationTime        = 0.0f;
    StepCounter           = 0u;
    ReadSlotIdx           = 0;
    ReadPresIdx           = 0;
    WorldGridHasFocus     = false;
    WorldGridPriorOriginX = WorldGridOriginX;
    WorldGridPriorOriginZ = WorldGridOriginZ;
}

void PyroVolumeSolver::UploadWorldSdfAtlas(VulkanContext& Ctx, const float* RgbaTexels, size_t FloatCount)
{
    if (!Initialized || !RgbaTexels || FloatCount < static_cast<size_t>(AtlasWidth * AtlasHeight * 4))
        return;
    std::string Diag;
    if (Ctx.UploadRgba32fImage(WorldSdfImg, RgbaTexels, static_cast<size_t>(AtlasWidth * AtlasHeight * 4), Diag))
        WorldSdfEnabled = true;
}

void PyroVolumeSolver::SetWorldGridMode(bool Enabled) noexcept
{
    if (WorldGridEnabled != Enabled)
    {
        WorldGridEnabled  = Enabled;
        WorldGridHasFocus = false;
    }
}

void PyroVolumeSolver::UpdateWorldGridFocus(float FocusWorldX, float FocusWorldY, const GasSettings& Settings) noexcept
{
    if (!WorldGridEnabled)
        return;

    const int32_t N        = std::max<int32_t>(16, LatticeRes > 0 ? LatticeRes : Settings.LatticeResolution);
    const float   SpanXZ   = std::max(4.0f, Settings.BoundsWidth);
    const float   CellXZ   = SpanXZ / static_cast<float>(N);
    const int32_t FocusX   = static_cast<int32_t>(std::floor(FocusWorldX / CellXZ));
    const int32_t FocusZ   = static_cast<int32_t>(std::floor(FocusWorldY / CellXZ));

    if (!WorldGridHasFocus)
    {
        WorldGridOriginX      = FocusX - N / 2;
        WorldGridOriginZ      = FocusZ - N / 2;
        WorldGridPriorOriginX = WorldGridOriginX;
        WorldGridPriorOriginZ = WorldGridOriginZ;
        WorldGridHasFocus     = true;
        return;
    }

    // Recentre window by integer world cells only when focus exits the inner 50% deadzone (PlumeSpace.js recentreWindow)
    const int32_t Margin  = std::max<int32_t>(2, N / 4);
    const int32_t CenterX = WorldGridOriginX + N / 2;
    const int32_t CenterZ = WorldGridOriginZ + N / 2;
    if (FocusX < CenterX - Margin)
        WorldGridOriginX += (FocusX - (CenterX - Margin));
    else if (FocusX > CenterX + Margin)
        WorldGridOriginX += (FocusX - (CenterX + Margin));

    if (FocusZ < CenterZ - Margin)
        WorldGridOriginZ += (FocusZ - (CenterZ - Margin));
    else if (FocusZ > CenterZ + Margin)
        WorldGridOriginZ += (FocusZ - (CenterZ + Margin));
}

void PyroVolumeSolver::AssignExternalSources(const PyroExternalSource* Sources, int Count) noexcept
{
    ExternalSourceCount = std::clamp(Count, 0, 8);
    for (int Idx = 0; Idx < ExternalSourceCount; ++Idx)
        ExternalSources[static_cast<size_t>(Idx)] = Sources[Idx];
}

void PyroVolumeSolver::AssignBrush(const std::array<float, 3>& PosUvw, const std::array<float, 3>& VelPyro, bool Active) noexcept
{
    BrushPosUvw  = PosUvw;
    BrushVelPyro = VelPyro;
    BrushActive  = Active;
}

PyroBoundsBox PyroVolumeSolver::ComputeBoundsBox(const GasSettings& Settings) const noexcept
{
    PyroBoundsBox Box{};
    if (WorldGridEnabled)
    {
        const int32_t N    = std::max<int32_t>(16, LatticeRes > 0 ? LatticeRes : Settings.LatticeResolution);
        Box.SurgeMultiplier= 1.0f;
        Box.EffectiveWidth = std::max(4.0f, Settings.BoundsWidth);
        Box.EffectiveHeight= std::max(2.0f, Settings.BoundsHeight);
        const float CellXZ = Box.EffectiveWidth / static_cast<float>(N);
        const float MinX   = static_cast<float>(WorldGridOriginX) * CellXZ;
        const float MinZ   = static_cast<float>(WorldGridOriginZ) * CellXZ;
        Box.BoxMin         = { MinX, -0.60f, MinZ };
        Box.BoxMax         = { MinX + Box.EffectiveWidth, -0.60f + Box.EffectiveHeight, MinZ + Box.EffectiveWidth };
        Box.DomainScale    = { Box.EffectiveWidth, Box.EffectiveHeight, Box.EffectiveWidth };
        return Box;
    }

    const float Mult    = Settings.DynamicBounds ? std::clamp(DynamicSurge, 1.0f, std::max(1.0f, Settings.DynamicBoundsMax)) : 1.0f;
    Box.SurgeMultiplier = Mult;
    Box.EffectiveWidth  = std::max(0.4f, Settings.BoundsWidth * Mult);
    Box.EffectiveHeight = std::max(0.5f, Settings.BoundsHeight * Mult);
    Box.BoxMin          = { -Box.EffectiveWidth * 0.5f, -0.60f, -Box.EffectiveWidth * 0.5f };
    Box.BoxMax          = {  Box.EffectiveWidth * 0.5f, -0.60f + Box.EffectiveHeight, Box.EffectiveWidth * 0.5f };
    Box.DomainScale     = { Box.EffectiveWidth, Box.EffectiveHeight, Box.EffectiveWidth };
    return Box;
}

//----------------------------------------------------------------------------------------------------------------------
//                                           EXPLOSIONS & BURST PATTERNS
//----------------------------------------------------------------------------------------------------------------------

void PyroVolumeSolver::TriggerExplosion(const GasSettings& Settings, const PyroExplosionOptions& Options)
{
    PendingBlastRecord Rec{};
    Rec.Center         = Options.CenterUvw;
    Rec.DirectionalVel = Options.DirectionalVel;
    Rec.Radius         = Options.RadiusOverride   > 0.0f ? Options.RadiusOverride   : Settings.BlastRadius;
    Rec.Strength       = Options.StrengthOverride > 0.0f ? Options.StrengthOverride : Settings.BlastStrength;
    Rec.Temperature    = Options.TempOverride     > 0.0f ? Options.TempOverride     : Settings.BlastTemperature;
    Rec.Fuel           = Options.FuelOverride     > 0.0f ? Options.FuelOverride     : Settings.BlastFuel;
    Rec.Smoke          = Options.SmokeOverride    > 0.0f ? Options.SmokeOverride    : Settings.BlastSmoke;
    Rec.Lobes          = Options.LobesOverride    > 0    ? Options.LobesOverride    : Settings.BlastLobes;
    Rec.Seed           = static_cast<float>(BlastSeedCounter++);
    Rec.Delay          = std::max(0.0f, Options.DelaySeconds);
    Rec.SpawnShrapnel  = (Options.ShrapnelOverride >= 0) ? (Options.ShrapnelOverride > 0) : Settings.ShrapnelEnabled;
    PendingBlasts.push_back(Rec);
}

void PyroVolumeSolver::TriggerBurstSequence(const GasSettings& Settings, const char* PatternName,
                                            const std::array<float, 3>* CustomCenter)
{
    const std::array<float, 3> C = CustomCenter ? *CustomCenter : std::array<float, 3>{ 0.5f, 0.20f, 0.5f };
    const char* Pat = PatternName ? PatternName : "single";

    if (std::strcmp(Pat, "salvo") == 0)
    {
        for (int Idx = 0; Idx < 4; ++Idx)
        {
            const float Angle = static_cast<float>(Idx) * (PiVal * 0.5f);
            PyroExplosionOptions Opt{};
            Opt.CenterUvw    = { C[0] + std::cos(Angle) * 0.16f, C[1] + 0.04f * Idx, C[2] + std::sin(Angle) * 0.16f };
            Opt.DelaySeconds = 0.12f * static_cast<float>(Idx);
            TriggerExplosion(Settings, Opt);
        }
    }
    else if (std::strcmp(Pat, "collide") == 0)
    {
        PyroExplosionOptions Left{}, Right{};
        Left.CenterUvw       = { 0.24f, 0.34f, 0.50f };
        Left.DirectionalVel  = {  9.5f, 1.5f,  0.0f  };
        Right.CenterUvw      = { 0.76f, 0.34f, 0.50f };
        Right.DirectionalVel = { -9.5f, 1.5f,  0.0f  };
        TriggerExplosion(Settings, Left);
        TriggerExplosion(Settings, Right);
    }
    else if (std::strcmp(Pat, "mushroom") == 0)
    {
        PyroExplosionOptions Base{}, Stem{};
        Base.CenterUvw        = { C[0], 0.12f, C[2] };
        Base.StrengthOverride = Settings.BlastStrength * 1.25f;
        Stem.CenterUvw        = { C[0], 0.28f, C[2] };
        Stem.DirectionalVel   = { 0.0f, 11.0f, 0.0f };
        Stem.DelaySeconds     = 0.09f;
        TriggerExplosion(Settings, Base);
        TriggerExplosion(Settings, Stem);
    }
    else if (std::strcmp(Pat, "shrapnel") == 0)
    {
        PyroExplosionOptions Air{};
        Air.CenterUvw        = { C[0], 0.32f, C[2] };
        Air.ShrapnelOverride = 1;
        TriggerExplosion(Settings, Air);
    }
    else
    {
        PyroExplosionOptions Single{};
        Single.CenterUvw = C;
        TriggerExplosion(Settings, Single);
    }
}

//----------------------------------------------------------------------------------------------------------------------
//                                   BATCHED VULKAN 3D SIMULATION STEP
//----------------------------------------------------------------------------------------------------------------------

namespace
{

void PopulateCommonPyroUbo(PyroUniformBlock& Ubo, const GasSettings& S, const PyroBoundsBox& Bounds,
                           int32_t Res, int32_t Tx, int32_t Ty, float Dt, float SimTime, bool WorldSdfOn,
                           bool WorldGridOn, int32_t WGOriginX, int32_t WGOriginZ,
                           int32_t WGPriorOriginX, int32_t WGPriorOriginZ) noexcept
{
    Ubo.GridParams[0]   = static_cast<float>(Res);
    Ubo.GridParams[1]   = static_cast<float>(Tx);
    Ubo.GridParams[2]   = static_cast<float>(Ty);
    Ubo.GridParams[3]   = Dt;

    Ubo.WorldGridOrigin[0] = static_cast<float>(WGOriginX);
    Ubo.WorldGridOrigin[1] = static_cast<float>(WGOriginZ);
    Ubo.WorldGridOrigin[2] = static_cast<float>(WGPriorOriginX);
    Ubo.WorldGridOrigin[3] = static_cast<float>(WGPriorOriginZ);
    Ubo.WorldGridFlags[0]  = WorldGridOn ? 1.0f : 0.0f;
    Ubo.WorldGridFlags[1]  = Bounds.EffectiveWidth  / static_cast<float>(std::max<int32_t>(1, Res));
    Ubo.WorldGridFlags[2]  = Bounds.EffectiveHeight / static_cast<float>(std::max<int32_t>(1, Res));
    Ubo.WorldGridFlags[3]  = 0.0f;

    Ubo.TimeAndSwitches[0] = SimTime;
    Ubo.TimeAndSwitches[1] = (S.VorticityConfinement > 0.001f) ? 1.0f : 0.0f;
    Ubo.TimeAndSwitches[2] = WorldSdfOn ? 1.0f : 0.0f;
    Ubo.TimeAndSwitches[3] = 0.0f;

    Ubo.DomainBoxMin[0] = Bounds.BoxMin[0];
    Ubo.DomainBoxMin[1] = Bounds.BoxMin[1];
    Ubo.DomainBoxMin[2] = Bounds.BoxMin[2];
    Ubo.DomainBoxMin[3] = S.EnclosedBox ? 1.0f : 0.0f;

    Ubo.DomainBoxMax[0] = Bounds.BoxMax[0];
    Ubo.DomainBoxMax[1] = Bounds.BoxMax[1];
    Ubo.DomainBoxMax[2] = Bounds.BoxMax[2];
    Ubo.DomainBoxMax[3] = 1.777778f;

    Ubo.EmitterParams0[0] = S.EmitterEnabled ? 1.0f : 0.0f;
    Ubo.EmitterParams0[1] = S.EmitterRate;
    Ubo.EmitterParams0[2] = S.EmitterRadius;
    Ubo.EmitterParams0[3] = S.EmitterHeight;

    Ubo.EmitterParams1[0] = S.EmitterSmoke;
    Ubo.EmitterParams1[1] = S.EmitterTemperature;
    Ubo.EmitterParams1[2] = S.EmitterFuel;
    Ubo.EmitterParams1[3] = S.EmitterUpwardVelocity;

    Ubo.EmitterParams2[0] = S.EmitterSwirl;
    Ubo.EmitterParams2[1] = 0.0f;
    Ubo.EmitterParams2[2] = static_cast<float>(S.ObstacleType);
    Ubo.EmitterParams2[3] = S.ObstacleRadius;

    float ObsX = S.ObstacleX;
    float ObsVelX = 0.0f;
    if (S.ColliderAutoMove && S.ObstacleType != 0)
    {
        ObsX    = 0.5f + 0.24f * std::sin(SimTime * S.ColliderSpeed);
        ObsVelX = 0.24f * S.ColliderSpeed * std::cos(SimTime * S.ColliderSpeed) * Bounds.EffectiveWidth;
    }
    Ubo.ObstaclePosAndPad[0] = ObsX;
    Ubo.ObstaclePosAndPad[1] = S.ObstacleY;
    Ubo.ObstaclePosAndPad[2] = S.ObstacleZ;
    Ubo.ObstacleVelAndPad[0] = ObsVelX;

    // Convert [1/s] dissipation/cooling/damping rates into exact exponential per-step multipliers
    Ubo.AdvectAndForces0[0]   = std::exp(-std::max(0.0f, S.SmokeDissipation) * Dt);
    Ubo.AdvectAndForces0[1]   = std::exp(-std::max(0.0f, S.CoolingRate)      * Dt * 0.35f);
    Ubo.AdvectAndForces0[2]   = std::exp(-0.22f * Dt);
    Ubo.AdvectAndForces0[3]   = std::exp(-std::max(0.0f, S.VelocityDamping)  * Dt);

    Ubo.AdvectAndForces1[0]   = S.VorticityConfinement;
    Ubo.AdvectAndForces1[1]   = S.TurbulenceStrength;
    Ubo.AdvectAndForces1[2]   = S.TurbulenceScale;
    Ubo.AdvectAndForces1[3]   = S.WindX;

    Ubo.AdvectAndForces2[0]   = S.WindZ;
    Ubo.AdvectAndForces2[1]   = S.BurnRate;
    Ubo.AdvectAndForces2[2]   = S.BurnHeat;
    Ubo.AdvectAndForces2[3]   = S.SootGeneration;

    Ubo.AdvectAndForces3[0]   = S.Buoyancy;
    Ubo.AdvectAndForces3[1]   = S.SmokeWeight;
    Ubo.AdvectAndForces3[2]   = S.CombustionExpansion;
    Ubo.AdvectAndForces3[3]   = S.InternalScattering;

    const float Az = S.SunAzimuth * Deg2Rad;
    const float El = S.SunElevation * Deg2Rad;
    const std::array<float, 3> SunDir = Normalize3({
        std::cos(El) * std::sin(Az),
        std::sin(El),
        std::cos(El) * std::cos(Az),
    });
    Ubo.SunDirAndIntensity[0] = SunDir[0];
    Ubo.SunDirAndIntensity[1] = SunDir[1];
    Ubo.SunDirAndIntensity[2] = SunDir[2];
    Ubo.SunDirAndIntensity[3] = S.SunIntensity;

    Ubo.OpticsParams0[0] = S.AmbientIntensity;
    Ubo.OpticsParams0[1] = S.DensityExtinction;
    Ubo.OpticsParams0[2] = S.SmokeAlbedo;
    Ubo.OpticsParams0[3] = S.FireIntensity;

    Ubo.OpticsParams1[0] = S.TemperatureScale;
    Ubo.OpticsParams1[1] = static_cast<float>(S.ColorPalette);
    Ubo.OpticsParams1[2] = S.InternalScattering;
    Ubo.OpticsParams1[3] = S.ShadowDensity;

    Ubo.OpticsParams2[0] = S.PhaseAnisotropy;
    Ubo.OpticsParams2[1] = S.Exposure;
    Ubo.OpticsParams2[2] = S.BloomIntensity;
    Ubo.OpticsParams2[3] = S.GodRaysIntensity;

    Ubo.OpticsParams3[0] = S.ShockwaveStrength;
    Ubo.OpticsParams3[1] = static_cast<float>(S.RaymarchSteps);
    Ubo.OpticsParams3[2] = static_cast<float>(S.ShadowSteps);
    Ubo.OpticsParams3[3] = static_cast<float>(S.VoxelQuantization);

    Ubo.SliceAndPipParams[0] = static_cast<float>(S.SliceAxis);
    Ubo.SliceAndPipParams[1] = S.SlicePosition;
    Ubo.SliceAndPipParams[2] = S.TileSheetOverviewShown ? 1.0f : 0.0f;
    Ubo.SliceAndPipParams[3] = static_cast<float>(S.TileSheetOverviewReading);

    Ubo.EmberAndActiveParams[0] = S.ActiveVoxelCellsShown ? 1.0f : 0.0f;
    Ubo.EmberAndActiveParams[1] = S.EmberSize;
    Ubo.EmberAndActiveParams[2] = S.EmberIntensity;
    Ubo.EmberAndActiveParams[3] = S.EmberLifetime;

    Ubo.EmberExtraParams[0] = S.EmberAshiness;
    Ubo.EmberExtraParams[1] = 650.0f;
    Ubo.EmberExtraParams[2] = 0.12f;
    Ubo.EmberExtraParams[3] = 240.0f;
}

} // namespace

void PyroVolumeSolver::AdvanceSimulation(VulkanContext& Ctx, GasSettings& Settings, float RawDtSeconds)
{
    if (!Initialized)
        return;

    EnsureLatticeResolution(Ctx, Settings.LatticeResolution);

    const float Dt = std::clamp(RawDtSeconds, 1.0f / 240.0f, 1.0f / 24.0f) * std::max(0.0f, Settings.TimeScale);
    if (Dt <= 1.0e-5f)
        return;

    SimulationTime += Dt;
    ++StepCounter;

    // Advance ballistic shrapnel projectiles
    for (size_t Idx = 0u; Idx < Projectiles.size();)
    {
        BallisticProjectile& P = Projectiles[Idx];
        P.Age += Dt;
        if (P.Age >= P.MaxLife)
        {
            Projectiles.erase(Projectiles.begin() + static_cast<std::ptrdiff_t>(Idx));
            continue;
        }
        P.Vel[1] -= 0.45f * Dt;
        for (int C = 0; C < 3; ++C)
            P.Pos[static_cast<size_t>(C)] += P.Vel[static_cast<size_t>(C)] * Dt * 0.25f;
        ++Idx;
    }

    // Pop any due blast record for this frame
    PendingBlastRecord ActiveBlast{};
    bool FireBlastThisStep = false;
    for (size_t Idx = 0u; Idx < PendingBlasts.size(); ++Idx)
    {
        PendingBlasts[Idx].Delay -= Dt;
        if (PendingBlasts[Idx].Delay <= 0.0f && !FireBlastThisStep)
        {
            ActiveBlast       = PendingBlasts[Idx];
            FireBlastThisStep = true;
            if (ActiveBlast.SpawnShrapnel)
            {
                Projectiles.clear();
                for (int SIdx = 0; SIdx < 6; ++SIdx)
                {
                    const float Ang = static_cast<float>(SIdx) * (PiVal * 2.0f / 6.0f) + ActiveBlast.Seed;
                    BallisticProjectile Proj{};
                    Proj.Pos     = ActiveBlast.Center;
                    Proj.Vel     = { std::cos(Ang) * 2.8f, 1.9f + 0.35f * (SIdx % 3), std::sin(Ang) * 2.8f };
                    Proj.MaxLife = 1.4f;
                    Proj.Radius  = 0.048f;
                    Projectiles.push_back(Proj);
                }
            }
            PendingBlasts.erase(PendingBlasts.begin() + static_cast<std::ptrdiff_t>(Idx));
            break;
        }
    }

    // Smoothly surge open-bounds domain volume during energetic blasts / tall plumes
    if (Settings.DynamicBounds)
    {
        const float TargetSurge = FireBlastThisStep
            ? std::min(Settings.DynamicBoundsMax, DynamicSurge + 0.45f)
            : (1.0f + 0.14f * std::clamp(Settings.EmitterRate * 0.35f, 0.0f, 1.0f));
        DynamicSurge += (TargetSurge - DynamicSurge) * std::clamp(Dt * 1.8f, 0.0f, 1.0f);
    }
    else
    {
        DynamicSurge = 1.0f;
    }

    const PyroBoundsBox Bounds = ComputeBoundsBox(Settings);
    PyroUniformBlock Ubo{};
    PopulateCommonPyroUbo(Ubo, Settings, Bounds, LatticeRes, TilesX, TilesY, Dt, SimulationTime, WorldSdfEnabled,
                          WorldGridEnabled, WorldGridOriginX, WorldGridOriginZ,
                          WorldGridPriorOriginX, WorldGridPriorOriginZ);

    if (BrushActive)
    {
        Ubo.EmitterParams2[1] = 1.0f;
        Ubo.BrushPosAndPad[0] = BrushPosUvw[0];
        Ubo.BrushPosAndPad[1] = BrushPosUvw[1];
        Ubo.BrushPosAndPad[2] = BrushPosUvw[2];
        Ubo.BrushVelAndPad[0] = BrushVelPyro[0];
        Ubo.BrushVelAndPad[1] = BrushVelPyro[1];
        Ubo.BrushVelAndPad[2] = BrushVelPyro[2];
    }

    if (FireBlastThisStep)
    {
        Ubo.BlastCenterAndRadius[0] = ActiveBlast.Center[0];
        Ubo.BlastCenterAndRadius[1] = ActiveBlast.Center[1];
        Ubo.BlastCenterAndRadius[2] = ActiveBlast.Center[2];
        Ubo.BlastCenterAndRadius[3] = ActiveBlast.Radius;
        Ubo.BlastThermoParams[0]    = 1.0f;
        Ubo.BlastThermoParams[1]    = ActiveBlast.Strength;
        Ubo.BlastThermoParams[2]    = ActiveBlast.Temperature;
        Ubo.BlastThermoParams[3]    = ActiveBlast.Fuel;
        Ubo.BlastExtraParams[0]     = ActiveBlast.Smoke;
        Ubo.BlastExtraParams[1]     = static_cast<float>(ActiveBlast.Lobes);
        Ubo.BlastExtraParams[2]     = ActiveBlast.Seed;
        Ubo.BlastDirVelAndPad[0]    = ActiveBlast.DirectionalVel[0];
        Ubo.BlastDirVelAndPad[1]    = ActiveBlast.DirectionalVel[1];
        Ubo.BlastDirVelAndPad[2]    = ActiveBlast.DirectionalVel[2];
    }

    const int ProjCount     = std::min<int>(6, static_cast<int>(Projectiles.size()));
    Ubo.BlastExtraParams[3] = static_cast<float>(ProjCount);
    for (int Idx = 0; Idx < ProjCount; ++Idx)
    {
        Ubo.ProjData[Idx][0] = Projectiles[static_cast<size_t>(Idx)].Pos[0];
        Ubo.ProjData[Idx][1] = Projectiles[static_cast<size_t>(Idx)].Pos[1];
        Ubo.ProjData[Idx][2] = Projectiles[static_cast<size_t>(Idx)].Pos[2];
        Ubo.ProjData[Idx][3] = Projectiles[static_cast<size_t>(Idx)].Radius;
        Ubo.ProjVel[Idx][0]  = Projectiles[static_cast<size_t>(Idx)].Vel[0];
        Ubo.ProjVel[Idx][1]  = Projectiles[static_cast<size_t>(Idx)].Vel[1];
        Ubo.ProjVel[Idx][2]  = Projectiles[static_cast<size_t>(Idx)].Vel[2];
    }

    Ubo.BlastDirVelAndPad[3] = static_cast<float>(ExternalSourceCount);
    for (int Idx = 0; Idx < ExternalSourceCount; ++Idx)
    {
        const PyroExternalSource& Src = ExternalSources[static_cast<size_t>(Idx)];
        Ubo.ExtPosRad[Idx][0]  = Src.PositionUvw[0];
        Ubo.ExtPosRad[Idx][1]  = Src.PositionUvw[1];
        Ubo.ExtPosRad[Idx][2]  = Src.PositionUvw[2];
        Ubo.ExtPosRad[Idx][3]  = Src.NormalizedRadius;
        Ubo.ExtVelRate[Idx][0] = Src.VelocityPyro[0];
        Ubo.ExtVelRate[Idx][1] = Src.VelocityPyro[1];
        Ubo.ExtVelRate[Idx][2] = Src.VelocityPyro[2];
        Ubo.ExtVelRate[Idx][3] = std::max(1.0f, Src.SmokeRate * 0.22f);
        Ubo.ExtThermo[Idx][0]  = Src.SmokeRate * 0.25f + Src.SootRate * 0.35f;
        Ubo.ExtThermo[Idx][1]  = Src.Temperature;
        Ubo.ExtThermo[Idx][2]  = Src.FuelRate * 0.35f;
    }

    // Record ALL solver steps into ONE batched Vulkan command buffer!
    VkCommandBuffer Cmd = Ctx.BeginFrameCommands();
    if (WorldSdfEnabled)
        Ctx.TransitionImage(Cmd, WorldSdfImg, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    auto RunDualStep = [&](VkPipeline Pipe, int DstIdx,
                           VulkanImageTarget* T0, VulkanImageTarget* T1,
                           VulkanImageTarget* T2 = nullptr, VulkanImageTarget* T3 = nullptr)
    {
        if (T0) Ctx.TransitionImage(Cmd, *T0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (T1) Ctx.TransitionImage(Cmd, *T1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (T2) Ctx.TransitionImage(Cmd, *T2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (T3) Ctx.TransitionImage(Cmd, *T3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Ctx.TransitionImage(Cmd, VelImg[DstIdx],    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        Ctx.TransitionImage(Cmd, ThermoImg[DstIdx], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        Ctx.BeginTarget(Cmd, Ctx.QueryTargetDualRgba16f(), FboDual[DstIdx], AtlasWidth, AtlasHeight);
        Ctx.BindPipelineAndDescriptors(Cmd, Pipe, &Ubo, sizeof(Ubo), T0, T1, T2, T3);
        Ctx.Vk().CmdDraw(Cmd, 3u, 1u, 0u, 0u);
        Ctx.EndTarget(Cmd);
    };

    auto RunSingleStep = [&](VkPipeline Pipe, VulkanImageTarget& DstImg, VkFramebuffer DstFbo,
                             VulkanImageTarget* T0, VulkanImageTarget* T1 = nullptr, VulkanImageTarget* T2 = nullptr)
    {
        if (T0) Ctx.TransitionImage(Cmd, *T0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (T1) Ctx.TransitionImage(Cmd, *T1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (T2) Ctx.TransitionImage(Cmd, *T2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Ctx.TransitionImage(Cmd, DstImg, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        Ctx.BeginTarget(Cmd, Ctx.QueryTargetSingleRgba16f(), DstFbo, AtlasWidth, AtlasHeight);
        Ctx.BindPipelineAndDescriptors(Cmd, Pipe, &Ubo, sizeof(Ubo), T0, T1, T2);
        Ctx.Vk().CmdDraw(Cmd, 3u, 1u, 0u, 0u);
        Ctx.EndTarget(Cmd);
    };

    // 1. Source Splatting + Internal/World SDF Solid Masking
    int Cur = ReadSlotIdx;
    int Nxt = 1 - Cur;
    RunDualStep(PipeSplat, Nxt, &VelImg[Cur], &ThermoImg[Cur], WorldSdfEnabled ? &WorldSdfImg : nullptr);
    Cur = Nxt;

    // 2. Semi-Lagrangian RK2 Advection (+ optional MacCormack 2nd-order BFECC correction!)
    if (Settings.MacCormackAdvection)
    {
        // Forward advect Cur -> Scratch(2), keeping dissipation = 1.0 during predictor
        const float SavedDiss[4] = {
            Ubo.AdvectAndForces0[0], Ubo.AdvectAndForces0[1], Ubo.AdvectAndForces0[2], Ubo.AdvectAndForces0[3]
        };
        Ubo.AdvectAndForces0[0] = 1.0f;
        Ubo.AdvectAndForces0[1] = 1.0f;
        Ubo.AdvectAndForces0[2] = 1.0f;
        Ubo.AdvectAndForces0[3] = 1.0f;
        RunDualStep(PipeAdvect, 2, &VelImg[Cur], &ThermoImg[Cur]);

        Ubo.AdvectAndForces0[0] = SavedDiss[0];
        Ubo.AdvectAndForces0[1] = SavedDiss[1];
        Ubo.AdvectAndForces0[2] = SavedDiss[2];
        Ubo.AdvectAndForces0[3] = SavedDiss[3];
        Nxt = 1 - Cur;
        RunDualStep(PipeMacCormack, Nxt, &VelImg[Cur], &VelImg[2], &ThermoImg[Cur], &ThermoImg[2]);
        Cur = Nxt;
    }
    else
    {
        Nxt = 1 - Cur;
        RunDualStep(PipeAdvect, Nxt, &VelImg[Cur], &ThermoImg[Cur]);
        Cur = Nxt;
    }

    // 3. 3D Curl & Vorticity Confinement + Curl-Free FBM Turbulence + Wind
    if (Settings.VorticityConfinement > 0.001f)
        RunSingleStep(PipeCurl, CurlImg, FboCurl, &VelImg[Cur]);

    Nxt = 1 - Cur;
    RunSingleStep(PipeVorticity, VelImg[Nxt], FboVelSingle[Nxt], &VelImg[Cur], &CurlImg, &ThermoImg[Cur]);
    // Keep Thermo paired with updated Vel: next DualStep reads VelImg[Nxt] + ThermoImg[Cur] -> writes into Cur!
    const int VelWithForces = Nxt;
    const int ThBeforeComb  = Cur;
    const int PostCombIdx   = Cur; // safe because VelImg[Cur] & ThermoImg[Cur] are not both simultaneous dst/src if we write to 2!
    (void)PostCombIdx;

    // 4. Arrhenius Combustion + Radiative Cooling + Buoyancy Lift (write into slot 2, then ping-pong cleanly!)
    RunDualStep(PipeBuoyancy, 2, &VelImg[VelWithForces], &ThermoImg[ThBeforeComb]);
    // Copy ThermoImg[2] stays paired: let's project VelImg[2] -> VelImg[0] and copy/swap Thermo!
    // Even cleaner: run Divergence from VelImg[2] + ThermoImg[2], Jacobi on PresImg, and Gradient into VelImg[0],
    // while keeping ThermoImg[0] updated via a lightweight dual-slot pairing!
    // Notice: if Step 3 writes into VelImg[2] instead of VelImg[Nxt], then Step 4 reads (VelImg[2], ThermoImg[Cur])
    // and writes BOTH (VelImg[Nxt], ThermoImg[Nxt])! Let's check:
    // Cur is in {0, 1}, Nxt = 1 - Cur is in {0, 1}!
    // Wait: we already wrote VelImg[Nxt] above, so Step 4 wrote (VelImg[2], ThermoImg[2]), and Step 7 (Gradient)
    // writes VelImg[Nxt] while Thermo stays in ThermoImg[2]? Let's keep VelImg[k] & ThermoImg[k] at the SAME index k!
    // Look how clean: copy ThermoImg[2] -> ThermoImg[Nxt] OR just run Gradient into VelImg[Nxt] and swap ThermoImg[2] <-> ThermoImg[Nxt]!
    std::swap(ThermoImg[Nxt], ThermoImg[2]);
    // Rebuild FboDual[Nxt] & FboDual[2] only if swapped? Wait: swapping VulkanImageTarget structs without rebuilding FboDual
    // would desync FboDual! Instead, let's NEVER swap structs: just have FboVelSingle[2] on VelImg[2]!
    // Wait — we didn't even need to swap: look!
    std::swap(ThermoImg[Nxt], ThermoImg[2]); // undo swap!

    // 5. Divergence + Fire Irradiance Source (reads VelImg[2] + ThermoImg[2])
    RunSingleStep(PipeDivergence, DivImg, FboDiv, &VelImg[2], &ThermoImg[2]);

    // 6. Simultaneous 3D Poisson Pressure + Screened-Poisson Multi-Scattering Irradiance Jacobi Sweeps
    int PRead = ReadPresIdx;
    const int Sweeps = std::clamp(Settings.PressureIterations, 4, 96);
    for (int Iter = 0; Iter < Sweeps; ++Iter)
    {
        const int PWrite = 1 - PRead;
        RunSingleStep(PipeJacobi, PresImg[PWrite], FboPres[PWrite], &PresImg[PRead], &DivImg, &VelImg[2]);
        PRead = PWrite;
    }
    ReadPresIdx = PRead;

    // 7. Pressure-Gradient Projection into VelImg[Nxt], and preserve ThermoImg[2] into ThermoImg[Nxt] via 0-dt pass or keep ReadSlotIdx = 2!
    // WAIT A SECOND — ReadSlotIdx CAN BE IN {0, 1, 2}!! We have FboVelSingle for 0,1 — if we also create FboVelSingle for 2,
    // or write Gradient into VelImg[Cur] and copy? Wait: if Step 3 (Vorticity) writes into VelImg[2] (using a single FBO on VelImg[2]),
    // then Step 4 (Buoyancy) reads (VelImg[2], ThermoImg[Cur]) and writes BOTH (VelImg[Nxt], ThermoImg[Nxt])!
    // THEN Step 5 (Divergence) reads (VelImg[Nxt], ThermoImg[Nxt])!
    // THEN Step 7 (Gradient) reads (VelImg[Nxt], PresImg[PRead]) and writes VelImg[Cur] — wait, then Thermo is in Nxt and Vel is in Cur!
    // OR if Step 7 writes VelImg[Nxt] by reading VelImg[2]? Look:
    //   - Step 4 (Buoyancy) writes (VelImg[2], ThermoImg[Nxt])? No, FboDual[Nxt] has (VelImg[Nxt], ThermoImg[Nxt]),
    //     so after Step 4, Thermo is in ThermoImg[Nxt] and Vel is in VelImg[Nxt].
    //   - If Step 7 reads VelImg[Nxt] and writes VelImg[Cur], Thermo is still in ThermoImg[Nxt].
    //   - Why force Vel and Thermo to share the same integer index when `ReadVelIdx` and `ReadThermoIdx` can simply be TWO integers?!
    // BINGO!! Look how clean that is: `VelImg[2]` ALREADY has the post-buoyancy velocity, `ThermoImg[2]` ALREADY has the post-buoyancy thermo,
    // and Step 7 (Gradient) reads `VelImg[2]` + `PresImg[PRead]` and writes `VelImg[Nxt]`!
    // Wait: we just copy `ThermoImg[2]` -> `ThermoImg[Nxt]` with a 1-command `vkCmdCopyImage`, OR even simpler, let's check!
    RunSingleStep(PipeGradient, VelImg[Nxt], FboVelSingle[Nxt], &VelImg[2], &PresImg[ReadPresIdx]);

    // Hardware device-to-device copy ThermoImg[2] -> ThermoImg[Nxt] so slot Nxt holds BOTH projected Vel and post-combustion Thermo!
    Ctx.TransitionImage(Cmd, ThermoImg[2],   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Ctx.TransitionImage(Cmd, ThermoImg[Nxt], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy ImgCopy{};
    ImgCopy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
    ImgCopy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
    ImgCopy.extent         = { static_cast<uint32_t>(AtlasWidth), static_cast<uint32_t>(AtlasHeight), 1u };
    auto CmdCopyImg = reinterpret_cast<PFN_vkCmdCopyImage>(Ctx.Vk().GetDeviceProcAddr(Ctx.QueryDevice(), "vkCmdCopyImage"));
    if (CmdCopyImg)
        CmdCopyImg(Cmd, ThermoImg[2].Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   ThermoImg[Nxt].Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &ImgCopy);

    Ctx.TransitionImage(Cmd, VelImg[Nxt],           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Ctx.TransitionImage(Cmd, ThermoImg[Nxt],        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Ctx.TransitionImage(Cmd, PresImg[ReadPresIdx],  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Ctx.TransitionImage(Cmd, CurlImg,               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    ReadSlotIdx = Nxt;
    Ctx.SubmitAndWaitFrameCommands();
    WorldGridPriorOriginX = WorldGridOriginX;
    WorldGridPriorOriginZ = WorldGridOriginZ;
}

//----------------------------------------------------------------------------------------------------------------------
//                                   STANDALONE & WORLD-COMPOSITED RAYMARCHING
//----------------------------------------------------------------------------------------------------------------------

bool PyroVolumeSolver::EnsureViewportTarget(VulkanContext& Ctx, int Width, int Height)
{
    const int ClampedW = std::clamp(Width,  32, 3840);
    const int ClampedH = std::clamp(Height, 32, 2160);
    if (ViewportImage.Image != VK_NULL_HANDLE && ViewportImage.Width == ClampedW && ViewportImage.Height == ClampedH)
        return true;

    Ctx.WaitIdle();
    Ctx.DestroyFramebuffer(ViewportFboClear);
    Ctx.DestroyFramebuffer(ViewportFboLoad);
    Ctx.DestroyImageTarget(ViewportImage);

    std::string Diag;
    if (!Ctx.CreateImageTarget(ClampedW, ClampedH, VK_FORMAT_R8G8B8A8_UNORM,
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, true, ViewportImage, Diag))
    {
        return false;
    }

    const VulkanImageTarget* Att[1] = { &ViewportImage };
    ViewportFboClear = Ctx.CreateFramebuffer(Ctx.QueryTargetSingleRgba8Clear(), Att, 1u, ClampedW, ClampedH);
    ViewportFboLoad  = Ctx.CreateFramebuffer(Ctx.QueryTargetSingleRgba8Load(),  Att, 1u, ClampedW, ClampedH);
    return ViewportFboClear != VK_NULL_HANDLE && ViewportFboLoad != VK_NULL_HANDLE;
}

bool PyroVolumeSolver::RenderStandaloneViewport(VulkanContext& Ctx, const GasSettings& Settings,
                                                const PyroCameraFrame& Cam, int Width, int Height,
                                                bool TransparentExport)
{
    if (!Initialized || !EnsureViewportTarget(Ctx, Width, Height))
        return false;

    const PyroBoundsBox Bounds = ComputeBoundsBox(Settings);
    PyroUniformBlock Ubo{};
    PopulateCommonPyroUbo(Ubo, Settings, Bounds, LatticeRes, TilesX, TilesY, 1.0f / 60.0f, SimulationTime, WorldSdfEnabled,
                          false, 0, 0, 0, 0);

    Ubo.DomainBoxMax[3]       = Cam.Aspect;
    Ubo.CamPosAndFov[0]       = Cam.Position[0];
    Ubo.CamPosAndFov[1]       = Cam.Position[1];
    Ubo.CamPosAndFov[2]       = Cam.Position[2];
    Ubo.CamPosAndFov[3]       = std::tan(Cam.FovRadians * 0.5f);
    Ubo.CamForwardAndComp[0]  = Cam.Forward[0];
    Ubo.CamForwardAndComp[1]  = Cam.Forward[1];
    Ubo.CamForwardAndComp[2]  = Cam.Forward[2];
    Ubo.CamForwardAndComp[3]  = 0.0f;
    Ubo.CamRightAndTransp[0]  = Cam.Right[0];
    Ubo.CamRightAndTransp[1]  = Cam.Right[1];
    Ubo.CamRightAndTransp[2]  = Cam.Right[2];
    Ubo.CamRightAndTransp[3]  = TransparentExport ? 1.0f : 0.0f;
    Ubo.CamUpAndChannel[0]    = Cam.Up[0];
    Ubo.CamUpAndChannel[1]    = Cam.Up[1];
    Ubo.CamUpAndChannel[2]    = Cam.Up[2];
    Ubo.CamUpAndChannel[3]    = static_cast<float>(Settings.RenderChannel);
    Ubo.EmberExtraParams[1]   = static_cast<float>(ViewportImage.Height);

    VkCommandBuffer Cmd = Ctx.BeginFrameCommands();
    PrepareAtlasesForWorldComposite(Ctx, Cmd);
    Ctx.TransitionImage(Cmd, ViewportImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const std::array<float, 4> ClearCol = { 0.0f, 0.0f, 0.0f, 0.0f };
    Ctx.BeginTarget(Cmd, Ctx.QueryTargetSingleRgba8Clear(), ViewportFboClear,
                    ViewportImage.Width, ViewportImage.Height, &ClearCol, false);
    Ctx.BindPipelineAndDescriptors(Cmd, PipeRaymarchStandalone, &Ubo, sizeof(Ubo),
                                   &ThermoImg[ReadSlotIdx], &VelImg[ReadSlotIdx],
                                   &PresImg[ReadPresIdx], &CurlImg, nullptr);
    Ctx.Vk().CmdDraw(Cmd, 3u, 1u, 0u, 0u);
    Ctx.EndTarget(Cmd);

    if (Settings.EmbersShown && Settings.EmberCount > 0 && Settings.RenderChannel == 0)
    {
        Ctx.BeginTarget(Cmd, Ctx.QueryTargetSingleRgba8Load(), ViewportFboLoad,
                        ViewportImage.Width, ViewportImage.Height, nullptr, false);
        Ctx.BindPipelineAndDescriptors(Cmd, PipeEmbersStandalone, &Ubo, sizeof(Ubo),
                                       &ThermoImg[ReadSlotIdx], &VelImg[ReadSlotIdx]);
        const VkDeviceSize ZeroOff = 0u;
        Ctx.Vk().CmdBindVertexBuffers(Cmd, 0u, 1u, &EmberSeedBuffer.Buffer, &ZeroOff);
        Ctx.Vk().CmdDraw(Cmd, static_cast<uint32_t>(std::clamp(Settings.EmberCount, 1, 1000)), 1u, 0u, 0u);
        Ctx.EndTarget(Cmd);
    }

    Ctx.TransitionImage(Cmd, ViewportImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return Ctx.SubmitAndWaitFrameCommands();
}

void PyroVolumeSolver::PrepareAtlasesForWorldComposite(VulkanContext& Ctx, VkCommandBuffer Cmd)
{
    Ctx.TransitionImage(Cmd, ThermoImg[ReadSlotIdx], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Ctx.TransitionImage(Cmd, VelImg[ReadSlotIdx],    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Ctx.TransitionImage(Cmd, PresImg[ReadPresIdx],   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Ctx.TransitionImage(Cmd, CurlImg,                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void PyroVolumeSolver::RecordCompositeIntoWorldTarget(VulkanContext& Ctx, VkCommandBuffer Cmd,
                                                      const GasSettings& Settings, const PyroCameraFrame& Cam,
                                                      int TargetWidth, int TargetHeight,
                                                      VulkanImageTarget& SceneLinearDepthImage)
{
    if (!Initialized)
        return;

    const PyroBoundsBox Bounds = ComputeBoundsBox(Settings);
    PyroUniformBlock Ubo{};
    PopulateCommonPyroUbo(Ubo, Settings, Bounds, LatticeRes, TilesX, TilesY, 1.0f / 60.0f, SimulationTime, WorldSdfEnabled,
                          WorldGridEnabled, WorldGridOriginX, WorldGridOriginZ,
                          WorldGridOriginX, WorldGridOriginZ);

    Ubo.DomainBoxMax[3]       = Cam.Aspect;
    Ubo.CamPosAndFov[0]       = Cam.Position[0];
    Ubo.CamPosAndFov[1]       = Cam.Position[1];
    Ubo.CamPosAndFov[2]       = Cam.Position[2];
    Ubo.CamPosAndFov[3]       = std::tan(Cam.FovRadians * 0.5f);
    Ubo.CamForwardAndComp[0]  = Cam.Forward[0];
    Ubo.CamForwardAndComp[1]  = Cam.Forward[1];
    Ubo.CamForwardAndComp[2]  = Cam.Forward[2];
    Ubo.CamForwardAndComp[3]  = 1.0f; // compositeOverScene = true!
    Ubo.CamRightAndTransp[0]  = Cam.Right[0];
    Ubo.CamRightAndTransp[1]  = Cam.Right[1];
    Ubo.CamRightAndTransp[2]  = Cam.Right[2];
    Ubo.CamRightAndTransp[3]  = 1.0f;
    Ubo.CamUpAndChannel[0]    = Cam.Up[0];
    Ubo.CamUpAndChannel[1]    = Cam.Up[1];
    Ubo.CamUpAndChannel[2]    = Cam.Up[2];
    Ubo.CamUpAndChannel[3]    = 0.0f;
    Ubo.EmberExtraParams[1]   = static_cast<float>(TargetHeight);

    Ctx.BindPipelineAndDescriptors(Cmd, PipeRaymarchWorldMrt, &Ubo, sizeof(Ubo),
                                   &ThermoImg[ReadSlotIdx], &VelImg[ReadSlotIdx],
                                   &PresImg[ReadPresIdx], &CurlImg, &SceneLinearDepthImage);
    Ctx.Vk().CmdDraw(Cmd, 3u, 1u, 0u, 0u);

    if (Settings.EmbersShown && Settings.EmberCount > 0 && Settings.FireIntensity > 0.1f)
    {
        Ctx.BindPipelineAndDescriptors(Cmd, PipeEmbersWorldMrt, &Ubo, sizeof(Ubo),
                                       &ThermoImg[ReadSlotIdx], &VelImg[ReadSlotIdx]);
        const VkDeviceSize ZeroOff = 0u;
        Ctx.Vk().CmdBindVertexBuffers(Cmd, 0u, 1u, &EmberSeedBuffer.Buffer, &ZeroOff);
        Ctx.Vk().CmdDraw(Cmd, static_cast<uint32_t>(std::clamp(Settings.EmberCount, 1, 1000)), 1u, 0u, 0u);
    }
    (void)TargetWidth;
}

bool PyroVolumeSolver::ReadViewportRgba(VulkanContext& Ctx, std::vector<uint8_t>& OutTopDownRgba,
                                        int& OutWidth, int& OutHeight)
{
    if (ViewportImage.Image == VK_NULL_HANDLE)
        return false;
    OutWidth  = ViewportImage.Width;
    OutHeight = ViewportImage.Height;
    return Ctx.DownloadRgba8Image(ViewportImage, OutTopDownRgba);
}

bool PyroVolumeSolver::BakeFlipbookSheet(VulkanContext& Ctx, GasSettings Settings, const PyroOrbitCamera& Cam,
                                         int Columns, int Rows, int TilePixels, float FrameStepSeconds,
                                         std::vector<uint8_t>& OutTopDownRgba)
{
    if (!Initialized || Columns <= 0 || Rows <= 0 || TilePixels < 16)
        return false;

    const int SheetW = Columns * TilePixels;
    const int SheetH = Rows * TilePixels;
    OutTopDownRgba.assign(static_cast<size_t>(SheetW) * static_cast<size_t>(SheetH) * 4u, 0u);

    Settings.TileSheetOverviewShown = false;
    Settings.BoundingBoxShown       = false;
    const PyroCameraFrame Frame     = Cam.ComputeFrame(1.0f);

    std::vector<uint8_t> TileRgba;
    int Tw = 0, Th = 0;
    for (int Row = 0; Row < Rows; ++Row)
    {
        for (int Col = 0; Col < Columns; ++Col)
        {
            AdvanceSimulation(Ctx, Settings, FrameStepSeconds);
            if (!RenderStandaloneViewport(Ctx, Settings, Frame, TilePixels, TilePixels, true))
                return false;
            if (!ReadViewportRgba(Ctx, TileRgba, Tw, Th))
                return false;

            for (int Y = 0; Y < TilePixels; ++Y)
            {
                const size_t DstOff = (static_cast<size_t>(Row * TilePixels + Y) * static_cast<size_t>(SheetW) +
                                       static_cast<size_t>(Col * TilePixels)) * 4u;
                const size_t SrcOff = static_cast<size_t>(Y) * static_cast<size_t>(TilePixels) * 4u;
                std::memcpy(OutTopDownRgba.data() + DstOff, TileRgba.data() + SrcOff, static_cast<size_t>(TilePixels) * 4u);
            }
        }
    }
    return true;
}

} // namespace Frontier
