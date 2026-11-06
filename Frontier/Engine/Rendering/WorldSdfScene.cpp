/*==========================================================================================================================================
                                                            WORLDSDFSCENE.CPP
==========================================================================================================================================*/
// 🧩 3D Signed-Distance World & Vulkan 1.1 GPU Renderer for Project Everest / Frontier Editor. Zero OpenGL dependencies.

#include "Frontier/Engine/Rendering/WorldSdfScene.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier
{

namespace
{

constexpr float PiVal   = 3.14159265358979323846f;
constexpr float Deg2Rad = PiVal / 180.0f;

struct alignas(16) WorldUniformBlock
{
    float ViewProj[16];
    float ModelMat[16];
    float CamEyeAndFov[4];        // xyz=eye, w=tanHalfFov
    float CamForwardAndAspect[4]; // xyz=forward, w=aspect
    float CamRightAndCourse[4];   // xyz=right, w=provingGrounds
    float CamUpAndObstacles[4];   // xyz=up, w=obstaclesEnabled
    float SunDirAndIntensity[4];  // xyz=sunDir, w=sunIntensity
    float EnvParams[4];           // x=ambient, y=exposure, z=nearPlane, w=farPlane
    float MeshTintAndOpacity[4];  // xyz=paintRgb, w=faceOpacity
    float MeshScanParams[4];      // x=viewMode, y=scanFrac, z=boundsMinX, w=boundsMaxX
    float MeshThermalParams[4];   // x=thermalGlow, y=fireLightCount
    float FireLightPosRad[8][4];  // xyz=worldPos, w=radius
    float FireLightColor[8][4];   // xyz=linearRadiance
};
static_assert(sizeof(WorldUniformBlock) == 33u * 16u, "WorldUniformBlock std140 size mismatch");

std::array<float, 3> NormalizeVec(const std::array<float, 3>& V) noexcept
{
    const float L = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);
    if (L < 1.0e-6f)
        return { 1.0f, 0.0f, 0.0f };
    return { V[0] / L, V[1] / L, V[2] / L };
}

std::array<float, 3> CrossVec(const std::array<float, 3>& A, const std::array<float, 3>& B) noexcept
{
    return {
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0],
    };
}

float DotVec(const std::array<float, 3>& A, const std::array<float, 3>& B) noexcept
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

float SdBox3(float Px, float Py, float Pz, float Bx, float By, float Bz) noexcept
{
    const float Qx = std::fabs(Px) - Bx;
    const float Qy = std::fabs(Py) - By;
    const float Qz = std::fabs(Pz) - Bz;
    const float Ox = std::max(Qx, 0.0f);
    const float Oy = std::max(Qy, 0.0f);
    const float Oz = std::max(Qz, 0.0f);
    return std::sqrt(Ox * Ox + Oy * Oy + Oz * Oz) + std::min(std::max(Qx, std::max(Qy, Qz)), 0.0f);
}

float SdCylinderZ(float Px, float Py, float Pz, float Radius, float HalfH) noexcept
{
    const float Dx = std::sqrt(Px * Px + Py * Py) - Radius;
    const float Dy = std::fabs(Pz) - HalfH;
    const float Ox = std::max(Dx, 0.0f);
    const float Oy = std::max(Dy, 0.0f);
    return std::min(std::max(Dx, Dy), 0.0f) + std::sqrt(Ox * Ox + Oy * Oy);
}

void BuildVulkanViewProjMatrix(const WorldCameraView& Cam, float NearP, float FarP, float OutColMajor[16]) noexcept
{
    const float TanHalf = std::tan(Cam.FovRadians * 0.5f);
    const float Sx      = 1.0f / (Cam.Aspect * TanHalf);
    const float Sy      = -1.0f / TanHalf; // Vulkan Y-down clip space!
    const float A       = FarP / (FarP - NearP);
    const float B       = -(FarP * NearP) / (FarP - NearP);

    const float Tx = -DotVec(Cam.Eye, Cam.Right);
    const float Ty = -DotVec(Cam.Eye, Cam.Up);
    const float Tz = -DotVec(Cam.Eye, Cam.Forward);

    // Column-major 4x4: clip = ( x_cam*Sx, y_cam*Sy, z_cam*A + B, z_cam )
    OutColMajor[0]  = Cam.Right[0] * Sx;   OutColMajor[1]  = Cam.Up[0] * Sy;   OutColMajor[2]  = Cam.Forward[0] * A;   OutColMajor[3]  = Cam.Forward[0];
    OutColMajor[4]  = Cam.Right[1] * Sx;   OutColMajor[5]  = Cam.Up[1] * Sy;   OutColMajor[6]  = Cam.Forward[1] * A;   OutColMajor[7]  = Cam.Forward[1];
    OutColMajor[8]  = Cam.Right[2] * Sx;   OutColMajor[9]  = Cam.Up[2] * Sy;   OutColMajor[10] = Cam.Forward[2] * A;   OutColMajor[11] = Cam.Forward[2];
    OutColMajor[12] = Tx * Sx;             OutColMajor[13] = Ty * Sy;          OutColMajor[14] = Tz * A + B;           OutColMajor[15] = Tz;
}

void BuildTransformColumnMajor(const std::array<float, 3>& Translation, float YawRad, float SteerRad, float SpinRad, float Out[16]) noexcept
{
    const float TotalYaw = YawRad + SteerRad;
    const float Cy = std::cos(TotalYaw), Sy = std::sin(TotalYaw);
    const float Cp = std::cos(SpinRad),  Sp = std::sin(SpinRad);

    // Rotate around local Y (axle spin), then around world Z (vehicle yaw + steer)
    // Local point (x, y, z):
    //   after spin around Y: x' = x*Cp + z*Sp, y' = y, z' = -x*Sp + z*Cp
    //   after yaw around Z:  X  = x'*Cy - y'*Sy, Y = x'*Sy + y'*Cy, Z = z'
    Out[0]  =  Cp * Cy;  Out[1]  =  Cp * Sy;  Out[2]  = -Sp;  Out[3]  = 0.0f;
    Out[4]  = -Sy;       Out[5]  =  Cy;       Out[6]  =  0.0f;Out[7]  = 0.0f;
    Out[8]  =  Sp * Cy;  Out[9]  =  Sp * Sy;  Out[10] =  Cp;  Out[11] = 0.0f;
    Out[12] = Translation[0];
    Out[13] = Translation[1];
    Out[14] = Translation[2];
    Out[15] = 1.0f;
}

} // namespace

//----------------------------------------------------------------------------------------------------------------------
//                                        CAMERA COORDINATE TRANSFORMATIONS
//----------------------------------------------------------------------------------------------------------------------

WorldCameraView BuildWorldCameraFromPresentation(const PresentationCamera& Orbit, float Aspect) noexcept
{
    WorldCameraView View{};
    View.Aspect     = std::max(0.2f, Aspect);
    View.FovRadians = 52.0f * Deg2Rad;

    const float Yaw   = Orbit.YawDegrees * Deg2Rad;
    const float Pitch = Orbit.PitchDegrees * Deg2Rad;
    const float Dist  = std::max(1.5f, Orbit.DistanceMetres);

    View.Eye = {
        Orbit.FocusMetres[0] - Dist * std::cos(Pitch) * std::cos(Yaw),
        Orbit.FocusMetres[1] - Dist * std::cos(Pitch) * std::sin(Yaw),
        Orbit.FocusMetres[2] + Dist * std::sin(Pitch),
    };
    View.Forward = NormalizeVec({
        Orbit.FocusMetres[0] - View.Eye[0],
        Orbit.FocusMetres[1] - View.Eye[1],
        Orbit.FocusMetres[2] - View.Eye[2],
    });
    View.Right = NormalizeVec(CrossVec(View.Forward, { 0.0f, 0.0f, 1.0f }));
    View.Up    = NormalizeVec(CrossVec(View.Right, View.Forward));
    return View;
}

PyroCameraFrame TransformWorldCameraToPyroDomain(const WorldCameraView& WorldCam,
                                                 const std::array<float, 3>& DomainOriginWorld) noexcept
{
    // Frontier Z-up (X fwd, Y left, Z up, ground Z=0) -> Pyro Y-up (x=Wx-Ox, y=Wz-Oz-0.6, z=Wy-Oy)
    PyroCameraFrame Pyro{};
    Pyro.Position = {
        WorldCam.Eye[0] - DomainOriginWorld[0],
        WorldCam.Eye[2] - DomainOriginWorld[2] - 0.60f,
        WorldCam.Eye[1] - DomainOriginWorld[1],
    };
    Pyro.Forward    = { WorldCam.Forward[0], WorldCam.Forward[2], WorldCam.Forward[1] };
    Pyro.Right      = { WorldCam.Right[0],   WorldCam.Right[2],   WorldCam.Right[1]   };
    Pyro.Up         = { WorldCam.Up[0],      WorldCam.Up[2],      WorldCam.Up[1]      };
    Pyro.FovRadians = WorldCam.FovRadians;
    Pyro.Aspect     = WorldCam.Aspect;
    return Pyro;
}

//----------------------------------------------------------------------------------------------------------------------
//                                   ANALYTIC 3D SDF COURSE & VEHICLE WAKE
//----------------------------------------------------------------------------------------------------------------------

float WorldSdfScene::ResolveCourseHeight(float WorldX, float WorldY, bool ProvingGrounds) noexcept
{
    if (!ProvingGrounds)
        return (WorldX * WorldX + WorldY * WorldY <= 3.6f * 3.6f) ? 0.04f : 0.0f;

    float Height = 0.0f;
    if (WorldX >= -52.0f && WorldX <= 44.0f && std::fabs(WorldY) >= 6.0f && std::fabs(WorldY) <= 6.3f)
        Height = std::max(Height, 0.12f);
    if (WorldX >= 22.0f && WorldX <= 28.0f && std::fabs(WorldY) <= 3.2f)
        Height = std::max(Height, ((WorldX - 22.0f) / 6.0f) * 1.35f);
    return Height;
}

void WorldSdfScene::EvaluateSdfAndVelocity(const std::array<float, 3>& WorldPoint, bool ProvingGrounds,
                                           bool ObstaclesEnabled, const std::vector<WorldVehicleRig>& Vehicles,
                                           float& OutSignedDistance, std::array<float, 3>& OutSolidVelocityWorld) const noexcept
{
    const float Px = WorldPoint[0];
    const float Py = WorldPoint[1];
    const float Pz = WorldPoint[2];

    float Dist = Pz; // Ground plane Z = 0
    OutSolidVelocityWorld = { 0.0f, 0.0f, 0.0f };

    if (ProvingGrounds)
    {
        Dist = std::min(Dist, SdBox3(Px + 4.0f, std::fabs(Py) - 6.15f, Pz - 0.06f, 48.0f, 0.15f, 0.06f));
        if ( std::fabs(Px - 25.0f) < 4.5f && std::fabs(Py) < 4.2f)
        {
            const float RampTop = std::clamp((Px - 22.0f) / 6.0f, 0.0f, 1.0f) * 1.35f;
            const float RampD   = std::max(std::max(std::fabs(Px - 25.0f) - 3.0f, std::fabs(Py) - 3.2f),
                                           std::max(-Pz, Pz - RampTop));
            Dist = std::min(Dist, RampD);
        }

        if (ObstaclesEnabled)
        {
            const float Dx = Px - 6.2f, Dy = Py + 2.35f, Dz = Pz - 0.82f;
            Dist = std::min(Dist, std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz) - 0.68f);
            Dist = std::min(Dist, SdCylinderZ(Px - 9.5f, std::fabs(Py) - 3.15f, Pz - 1.35f, 0.48f, 1.35f));

            const float Leg    = SdBox3(Px - 15.2f, std::fabs(Py) - 4.4f, Pz - 1.9f, 0.42f, 0.42f, 1.9f);
            const float Lintel = SdBox3(Px - 15.2f, Py, Pz - 3.95f, 0.48f, 4.85f, 0.36f);
            Dist = std::min(Dist, std::min(Leg, Lintel));
        }
    }

    // Evaluate moving ControlVehicle 3D oriented bounding hull + aerodynamic wake velocity!
    for (const WorldVehicleRig& Car : Vehicles)
    {
        const float YawRad = Car.Visual.YawDegrees * Deg2Rad;
        const float Cy     = std::cos(YawRad);
        const float Sy     = std::sin(YawRad);
        const float Rx     = Px - Car.Visual.PositionMetres[0];
        const float Ry     = Py - Car.Visual.PositionMetres[1];
        const float Rz     = Pz - (Car.Visual.PositionMetres[2] + 0.34f);

        const float LocalX =  Rx * Cy + Ry * Sy;
        const float LocalY = -Rx * Sy + Ry * Cy;
        const float CarHull= SdBox3(LocalX, LocalY, Rz, 2.32f, 1.02f, 0.56f) - 0.12f;

        if (CarHull < Dist)
        {
            Dist = CarHull;
            OutSolidVelocityWorld = {
                Car.VelocityMps[0] - Car.YawRateRad * Ry,
                Car.VelocityMps[1] + Car.YawRateRad * Rx,
                Car.VelocityMps[2],
            };
        }
        else if (CarHull < 0.85f)
        {
            OutSolidVelocityWorld = {
                Car.VelocityMps[0] - Car.YawRateRad * Ry,
                Car.VelocityMps[1] + Car.YawRateRad * Rx,
                Car.VelocityMps[2],
            };
        }
    }

    OutSignedDistance = Dist;
}

void WorldSdfScene::BuildDomainSdfAtlas(const std::array<float, 3>& DomainOriginWorld, const PyroBoundsBox& Bounds,
                                        int32_t LatticeRes, int32_t TilesX, int32_t TilesY,
                                        bool ProvingGrounds, bool ObstaclesEnabled,
                                        const std::vector<WorldVehicleRig>& Vehicles,
                                        std::vector<float>& OutAtlasRgba) const
{
    const int32_t AtlasW = LatticeRes * TilesX;
    const int32_t AtlasH = LatticeRes * TilesY;
    OutAtlasRgba.assign(static_cast<size_t>(AtlasW) * static_cast<size_t>(AtlasH) * 4u, 0.0f);

    const float SpanX = Bounds.BoxMax[0] - Bounds.BoxMin[0];
    const float SpanY = Bounds.BoxMax[1] - Bounds.BoxMin[1];
    const float SpanZ = Bounds.BoxMax[2] - Bounds.BoxMin[2];

    for (int32_t Vz = 0; Vz < LatticeRes; ++Vz)
    {
        const int32_t TileX = Vz % TilesX;
        const int32_t TileY = Vz / TilesX;
        const float PyroZ   = Bounds.BoxMin[2] + ((static_cast<float>(Vz) + 0.5f) / static_cast<float>(LatticeRes)) * SpanZ;
        const float WorldY  = DomainOriginWorld[1] + PyroZ;

        for (int32_t Vy = 0; Vy < LatticeRes; ++Vy)
        {
            const float PyroY  = Bounds.BoxMin[1] + ((static_cast<float>(Vy) + 0.5f) / static_cast<float>(LatticeRes)) * SpanY;
            const float WorldZ = DomainOriginWorld[2] + PyroY + 0.60f;
            const int32_t Py   = TileY * LatticeRes + Vy;

            for (int32_t Vx = 0; Vx < LatticeRes; ++Vx)
            {
                const float PyroX  = Bounds.BoxMin[0] + ((static_cast<float>(Vx) + 0.5f) / static_cast<float>(LatticeRes)) * SpanX;
                const float WorldX = DomainOriginWorld[0] + PyroX;
                const int32_t Px   = TileX * LatticeRes + Vx;

                float SignedDist = 10.0f;
                std::array<float, 3> VelW{};
                EvaluateSdfAndVelocity({ WorldX, WorldY, WorldZ }, ProvingGrounds, ObstaclesEnabled,
                                       Vehicles, SignedDist, VelW);

                const size_t Offset = (static_cast<size_t>(Py) * static_cast<size_t>(AtlasW) + static_cast<size_t>(Px)) * 4u;
                // Convert Frontier Z-up velocity (Vx, Vy, Vz) -> Pyro Y-up velocity (Vx, Vz, Vy)
                OutAtlasRgba[Offset + 0u] = VelW[0];
                OutAtlasRgba[Offset + 1u] = VelW[2];
                OutAtlasRgba[Offset + 2u] = VelW[1];
                OutAtlasRgba[Offset + 3u] = SignedDist;
            }
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------
//                                   VULKAN INITIALIZATION & .FACET UPLOAD
//----------------------------------------------------------------------------------------------------------------------

bool WorldSdfScene::UploadFacetMesh(VulkanContext& Ctx, const FacetArchiveData& Archive, float ZRebase, float YSign,
                                    VulkanFacetMesh& OutMesh, std::string& OutDiagnostic)
{
    DestroyFacetMesh(Ctx, OutMesh);
    if (!Archive.IsValid() || Archive.Facets.size() < 3u || Archive.Positions.size() < 3u)
    {
        OutDiagnostic = "Invalid FacetArchiveData.";
        return false;
    }

    // 9 floats per vertex: Position(3), Normal(3), Barycentric(3)
    const size_t TriCount = Archive.Facets.size() / 3u;
    const bool HasCornerN = (Archive.CornerNormals.size() >= TriCount * 9u);
    std::vector<float> Packed;
    Packed.reserve(TriCount * 3u * 9u);
    float MinX =  1.0e6f;
    float MaxX = -1.0e6f;

    static constexpr float BaryCoords[3][3] = {
        { 1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f },
    };

    for (size_t FIdx = 0u; FIdx < TriCount; ++FIdx)
    {
        const uint32_t I0 = Archive.Facets[FIdx * 3u + 0u];
        const uint32_t I1 = Archive.Facets[FIdx * 3u + 1u];
        const uint32_t I2 = Archive.Facets[FIdx * 3u + 2u];
        if (I0 >= Archive.VertexCount || I1 >= Archive.VertexCount || I2 >= Archive.VertexCount)
            continue;

        const uint32_t Idxs[3] = { I0, (YSign < 0.0f) ? I2 : I1, (YSign < 0.0f) ? I1 : I2 };
        const int CornerOrd[3] = { 0,  (YSign < 0.0f) ? 2  : 1,  (YSign < 0.0f) ? 1  : 2  };

        std::array<float, 3> Pos[3]{};
        for (int C = 0; C < 3; ++C)
        {
            const size_t POff = static_cast<size_t>(Idxs[C]) * 3u;
            Pos[C] = {
                Archive.Positions[POff + 0u],
                Archive.Positions[POff + 1u] * YSign,
                Archive.Positions[POff + 2u] + ZRebase,
            };
        }

        const std::array<float, 3> E1    = { Pos[1][0] - Pos[0][0], Pos[1][1] - Pos[0][1], Pos[1][2] - Pos[0][2] };
        const std::array<float, 3> E2    = { Pos[2][0] - Pos[0][0], Pos[2][1] - Pos[0][1], Pos[2][2] - Pos[0][2] };
        const std::array<float, 3> FaceN = NormalizeVec(CrossVec(E1, E2));

        for (int VIdx = 0; VIdx < 3; ++VIdx)
        {
            std::array<float, 3> Norm = FaceN;
            if (HasCornerN)
            {
                const size_t NOff = FIdx * 9u + static_cast<size_t>(CornerOrd[VIdx]) * 3u;
                Norm = NormalizeVec({
                    Archive.CornerNormals[NOff + 0u],
                    Archive.CornerNormals[NOff + 1u] * YSign,
                    Archive.CornerNormals[NOff + 2u],
                });
            }

            MinX = std::min(MinX, Pos[VIdx][0]);
            MaxX = std::max(MaxX, Pos[VIdx][0]);
            Packed.push_back(Pos[VIdx][0]);
            Packed.push_back(Pos[VIdx][1]);
            Packed.push_back(Pos[VIdx][2]);
            Packed.push_back(Norm[0]);
            Packed.push_back(Norm[1]);
            Packed.push_back(Norm[2]);
            Packed.push_back(BaryCoords[VIdx][0]);
            Packed.push_back(BaryCoords[VIdx][1]);
            Packed.push_back(BaryCoords[VIdx][2]);
        }
    }

    if (Packed.empty())
        return false;

    if (!Ctx.CreateHostBuffer(Packed.size() * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              OutMesh.VertexBuffer, OutDiagnostic))
    {
        return false;
    }
    std::memcpy(OutMesh.VertexBuffer.Mapped, Packed.data(), Packed.size() * sizeof(float));
    OutMesh.VertexCount = static_cast<uint32_t>(Packed.size() / 9u);
    OutMesh.BoundsMinX  = MinX;
    OutMesh.BoundsMaxX  = MaxX;
    return true;
}

void WorldSdfScene::DestroyFacetMesh(VulkanContext& Ctx, VulkanFacetMesh& Mesh) noexcept
{
    Ctx.DestroyBuffer(Mesh.VertexBuffer);
    Mesh = VulkanFacetMesh{};
}

bool WorldSdfScene::Initialize(VulkanContext& Ctx, const FacetArchiveData* Body, const FacetArchiveData* Tyre,
                               const FacetArchiveData* Rim, std::string& OutDiagnostic)
{
    Shutdown(Ctx);

    // 1. 3D SDF World Raymarch Pipeline (writes RGBA8 + RGBA16F linear tOpaque + hardware D32_SFLOAT)
    {
        VulkanPipelineSpec Spec{};
        Spec.VertexShader     = VulkanShaders::FullScreenQuadVert;
        Spec.FragmentShader   = VulkanShaders::WorldSdfRaymarchFrag;
        Spec.CompatibleTarget = Ctx.QueryTargetWorldMrtClear();
        Spec.ColorAttachments = 2u;
        Spec.Blend            = VulkanBlendFormula::OpaqueOverwrite;
        Spec.DepthTest        = true;
        Spec.DepthWrite       = true;
        PipeWorldSdfRaymarch  = Ctx.CreateGraphicsPipeline(Spec, OutDiagnostic);
    }

    // 2. ControlVehicle .facet Mesh Pipelines (Opaque + Ghost/Wireframe/Scan)
    {
        VulkanPipelineSpec Spec{};
        Spec.VertexShader     = VulkanShaders::WorldVehicleVert;
        Spec.FragmentShader   = VulkanShaders::WorldVehicleFrag;
        Spec.CompatibleTarget = Ctx.QueryTargetWorldMrtClear();
        Spec.ColorAttachments = 2u;
        Spec.VertexFormat     = VulkanVertexLayout::WorldFacet9;
        Spec.Blend            = VulkanBlendFormula::OpaqueOverwrite;
        Spec.DepthTest        = true;
        Spec.DepthWrite       = true;
        PipeVehicleOpaque     = Ctx.CreateGraphicsPipeline(Spec, OutDiagnostic);

        Spec.Blend            = VulkanBlendFormula::StandardAlpha;
        Spec.DepthWrite       = false;
        PipeVehicleGhost      = Ctx.CreateGraphicsPipeline(Spec, OutDiagnostic);
    }

    if (!PipeWorldSdfRaymarch || !PipeVehicleOpaque || !PipeVehicleGhost)
    {
        Shutdown(Ctx);
        return false;
    }

    if (Body && Body->IsValid())
    {
        if (!UploadFacetMesh(Ctx, *Body, 0.11285f, 1.0f, BodyMesh, OutDiagnostic))
            return false;
    }
    if (Tyre && Tyre->IsValid())
    {
        if (!UploadFacetMesh(Ctx, *Tyre, 0.0f,  1.0f, TyreMeshLeft,  OutDiagnostic)) return false;
        if (!UploadFacetMesh(Ctx, *Tyre, 0.0f, -1.0f, TyreMeshRight, OutDiagnostic)) return false;
    }
    if (Rim && Rim->IsValid())
    {
        if (!UploadFacetMesh(Ctx, *Rim, 0.0f,  1.0f, RimMeshLeft,  OutDiagnostic)) return false;
        if (!UploadFacetMesh(Ctx, *Rim, 0.0f, -1.0f, RimMeshRight, OutDiagnostic)) return false;
    }

    Initialized = true;
    return true;
}

void WorldSdfScene::Shutdown(VulkanContext& Ctx) noexcept
{
    Ctx.WaitIdle();
    Ctx.DestroyFramebuffer(SceneColorLoadFbo);
    Ctx.DestroyFramebuffer(WorldMrtFbo);
    Ctx.DestroyImageTarget(HardwareDepthImage);
    Ctx.DestroyImageTarget(SceneDepthImage);
    Ctx.DestroyImageTarget(SceneColorImage);

    DestroyFacetMesh(Ctx, RimMeshRight);
    DestroyFacetMesh(Ctx, RimMeshLeft);
    DestroyFacetMesh(Ctx, TyreMeshRight);
    DestroyFacetMesh(Ctx, TyreMeshLeft);
    DestroyFacetMesh(Ctx, BodyMesh);

    Ctx.DestroyPipeline(PipeVehicleGhost);
    Ctx.DestroyPipeline(PipeVehicleOpaque);
    Ctx.DestroyPipeline(PipeWorldSdfRaymarch);
    ActiveCmd   = VK_NULL_HANDLE;
    Initialized = false;
}

bool WorldSdfScene::EnsureSceneTargets(VulkanContext& Ctx, int Width, int Height)
{
    const int ClampedW = std::clamp(Width,  64, 3840);
    const int ClampedH = std::clamp(Height, 64, 2160);
    if (SceneColorImage.Image != VK_NULL_HANDLE &&
        SceneColorImage.Width == ClampedW && SceneColorImage.Height == ClampedH)
    {
        return true;
    }

    Ctx.WaitIdle();
    Ctx.DestroyFramebuffer(SceneColorLoadFbo);
    Ctx.DestroyFramebuffer(WorldMrtFbo);
    Ctx.DestroyImageTarget(HardwareDepthImage);
    Ctx.DestroyImageTarget(SceneDepthImage);
    Ctx.DestroyImageTarget(SceneColorImage);

    std::string Diag;
    if (!Ctx.CreateImageTarget(ClampedW, ClampedH, VK_FORMAT_R8G8B8A8_UNORM,
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, true, SceneColorImage, Diag))
        return false;
    if (!Ctx.CreateImageTarget(ClampedW, ClampedH, VK_FORMAT_R16G16B16A16_SFLOAT,
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, false, SceneDepthImage, Diag))
        return false;
    if (!Ctx.CreateImageTarget(ClampedW, ClampedH, VK_FORMAT_D32_SFLOAT,
                               0u, false, HardwareDepthImage, Diag))
        return false;

    const VulkanImageTarget* MrtAtts[3]  = { &SceneColorImage, &SceneDepthImage, &HardwareDepthImage };
    const VulkanImageTarget* ColorOnly[1]= { &SceneColorImage };
    WorldMrtFbo       = Ctx.CreateFramebuffer(Ctx.QueryTargetWorldMrtClear(),     MrtAtts,   3u, ClampedW, ClampedH);
    SceneColorLoadFbo = Ctx.CreateFramebuffer(Ctx.QueryTargetSingleRgba8Load(),   ColorOnly, 1u, ClampedW, ClampedH);
    return WorldMrtFbo != VK_NULL_HANDLE && SceneColorLoadFbo != VK_NULL_HANDLE;
}

//----------------------------------------------------------------------------------------------------------------------
//                                   OPAQUE WORLD + VOLUMETRIC COMPOSITING
//----------------------------------------------------------------------------------------------------------------------

bool WorldSdfScene::BeginWorldFrame(VulkanContext& Ctx, int Width, int Height, bool ProvingGrounds,
                                    bool ObstaclesEnabled, const WorldCameraView& Cam,
                                    const WorldEnvironmentLight& Env,
                                    const std::vector<WorldVehicleRig>& Vehicles,
                                    const std::vector<DynamicFireLight>& FireLights)
{
    if (!Initialized || !EnsureSceneTargets(Ctx, Width, Height))
        return false;

    WorldUniformBlock Ubo{};
    BuildVulkanViewProjMatrix(Cam, 0.12f, 240.0f, Ubo.ViewProj);

    Ubo.CamEyeAndFov[0]        = Cam.Eye[0];
    Ubo.CamEyeAndFov[1]        = Cam.Eye[1];
    Ubo.CamEyeAndFov[2]        = Cam.Eye[2];
    Ubo.CamEyeAndFov[3]        = std::tan(Cam.FovRadians * 0.5f);
    Ubo.CamForwardAndAspect[0] = Cam.Forward[0];
    Ubo.CamForwardAndAspect[1] = Cam.Forward[1];
    Ubo.CamForwardAndAspect[2] = Cam.Forward[2];
    Ubo.CamForwardAndAspect[3] = Cam.Aspect;
    Ubo.CamRightAndCourse[0]   = Cam.Right[0];
    Ubo.CamRightAndCourse[1]   = Cam.Right[1];
    Ubo.CamRightAndCourse[2]   = Cam.Right[2];
    Ubo.CamRightAndCourse[3]   = ProvingGrounds ? 1.0f : 0.0f;
    Ubo.CamUpAndObstacles[0]   = Cam.Up[0];
    Ubo.CamUpAndObstacles[1]   = Cam.Up[1];
    Ubo.CamUpAndObstacles[2]   = Cam.Up[2];
    Ubo.CamUpAndObstacles[3]   = ObstaclesEnabled ? 1.0f : 0.0f;

    const float Az = Env.SunAzimuthDeg * Deg2Rad;
    const float El = Env.SunElevationDeg * Deg2Rad;
    const std::array<float, 3> Sun = NormalizeVec({
        std::cos(El) * std::cos(Az),
        std::cos(El) * std::sin(Az),
        std::sin(El),
    });
    Ubo.SunDirAndIntensity[0]  = Sun[0];
    Ubo.SunDirAndIntensity[1]  = Sun[1];
    Ubo.SunDirAndIntensity[2]  = Sun[2];
    Ubo.SunDirAndIntensity[3]  = Env.SunIntensity;

    Ubo.EnvParams[0] = Env.AmbientIntensity;
    Ubo.EnvParams[1] = Env.Exposure;
    Ubo.EnvParams[2] = 0.12f;
    Ubo.EnvParams[3] = 240.0f;

    const int LightCount       = std::min<int>(8, static_cast<int>(FireLights.size()));
    Ubo.MeshThermalParams[1]   = static_cast<float>(LightCount);
    for (int Idx = 0; Idx < LightCount; ++Idx)
    {
        Ubo.FireLightPosRad[Idx][0] = FireLights[static_cast<size_t>(Idx)].PositionWorld[0];
        Ubo.FireLightPosRad[Idx][1] = FireLights[static_cast<size_t>(Idx)].PositionWorld[1];
        Ubo.FireLightPosRad[Idx][2] = FireLights[static_cast<size_t>(Idx)].PositionWorld[2];
        Ubo.FireLightPosRad[Idx][3] = FireLights[static_cast<size_t>(Idx)].RadiusMetres;
        Ubo.FireLightColor[Idx][0]  = FireLights[static_cast<size_t>(Idx)].RadianceLinear[0];
        Ubo.FireLightColor[Idx][1]  = FireLights[static_cast<size_t>(Idx)].RadianceLinear[1];
        Ubo.FireLightColor[Idx][2]  = FireLights[static_cast<size_t>(Idx)].RadianceLinear[2];
    }

    ActiveCmd = Ctx.BeginFrameCommands();
    Ctx.TransitionImage(ActiveCmd, SceneColorImage,    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    Ctx.TransitionImage(ActiveCmd, SceneDepthImage,    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    Ctx.TransitionImage(ActiveCmd, HardwareDepthImage, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    const std::array<float, 4> ClearBg = { 0.04f, 0.05f, 0.06f, 1.0f };
    Ctx.BeginTarget(ActiveCmd, Ctx.QueryTargetWorldMrtClear(), WorldMrtFbo,
                    SceneColorImage.Width, SceneColorImage.Height, &ClearBg, true);

    // 1. Fullscreen 3D SDF Course / Studio Raymarch
    Ctx.BindPipelineAndDescriptors(ActiveCmd, PipeWorldSdfRaymarch, &Ubo, sizeof(Ubo));
    Ctx.Vk().CmdDraw(ActiveCmd, 3u, 1u, 0u, 0u);

    // 2. Hardware-rasterized ControlVehicle .facet Body + 4x Steered/Spinning Wheels & Rims
    auto DrawPart = [&](const VulkanFacetMesh& Part, const float Model[16],
                        const std::array<float, 3>& Tint, const VehicleInspection& Insp, float Thermal)
    {
        if (Part.VertexCount == 0u || Part.VertexBuffer.Buffer == VK_NULL_HANDLE)
            return;
        std::memcpy(Ubo.ModelMat, Model, sizeof(float) * 16u);
        Ubo.MeshTintAndOpacity[0] = Tint[0];
        Ubo.MeshTintAndOpacity[1] = Tint[1];
        Ubo.MeshTintAndOpacity[2] = Tint[2];
        Ubo.MeshTintAndOpacity[3] = Insp.ResolveFaceOpacity();
        Ubo.MeshScanParams[0]     = static_cast<float>(Insp.View);
        Ubo.MeshScanParams[1]     = Insp.ResolveScanFraction();
        Ubo.MeshScanParams[2]     = Part.BoundsMinX;
        Ubo.MeshScanParams[3]     = Part.BoundsMaxX;
        Ubo.MeshThermalParams[0]  = Thermal;

        VkPipeline ChosenPipe = (Insp.View == InspectionView::Solid) ? PipeVehicleOpaque : PipeVehicleGhost;
        Ctx.BindPipelineAndDescriptors(ActiveCmd, ChosenPipe, &Ubo, sizeof(Ubo));
        const VkDeviceSize ZeroOff = 0u;
        Ctx.Vk().CmdBindVertexBuffers(ActiveCmd, 0u, 1u, &Part.VertexBuffer.Buffer, &ZeroOff);
        Ctx.Vk().CmdDraw(ActiveCmd, Part.VertexCount, 1u, 0u, 0u);
    };

    static constexpr float WheelMountX[4] = {  1.7274f,  1.7274f, -1.6686f, -1.6686f };
    static constexpr float WheelMountY[4] = {  1.0475f, -1.0475f,  1.0475f, -1.0475f };
    static constexpr float WheelMountZ    = -0.02145f;

    for (const WorldVehicleRig& Car : Vehicles)
    {
        const float YawRad = Car.Visual.YawDegrees * Deg2Rad;
        const float Cy     = std::cos(YawRad);
        const float Sy     = std::sin(YawRad);

        float BodyModel[16]{};
        BuildTransformColumnMajor(Car.Visual.PositionMetres, YawRad, 0.0f, 0.0f, BodyModel);
        DrawPart(BodyMesh, BodyModel, Car.Visual.Paint, Car.Visual.Inspection, 0.0f);

        for (size_t WIdx = 0u; WIdx < 4u; ++WIdx)
        {
            const float Lx = WheelMountX[WIdx];
            const float Ly = WheelMountY[WIdx];
            const std::array<float, 3> HubWorld = {
                Car.Visual.PositionMetres[0] + Lx * Cy - Ly * Sy,
                Car.Visual.PositionMetres[1] + Lx * Sy + Ly * Cy,
                Car.Visual.PositionMetres[2] + WheelMountZ,
            };

            float WheelModel[16]{};
            BuildTransformColumnMajor(HubWorld, YawRad, Car.WheelSteerRad[WIdx], Car.WheelSpinRad[WIdx], WheelModel);
            const bool LeftSide = (Ly > 0.0f);
            DrawPart(LeftSide ? TyreMeshLeft : TyreMeshRight, WheelModel,
                     { 0.045f, 0.046f, 0.048f }, Car.Visual.Inspection, Car.WheelHeatRatio[WIdx]);
            DrawPart(LeftSide ? RimMeshLeft : RimMeshRight, WheelModel,
                     { 0.56f, 0.58f, 0.62f }, Car.Visual.Inspection, 0.0f);
        }
    }

    Ctx.EndTarget(ActiveCmd);

    // Transition SceneDepthImage -> SHADER_READ_ONLY_OPTIMAL so PyroVolumeSolver domains clip against tOpaque!
    Ctx.TransitionImage(ActiveCmd, SceneDepthImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
}

void WorldSdfScene::CompositeDomainIntoWorld(VulkanContext& Ctx, PyroVolumeSolver& Solver, const GasSettings& Settings,
                                             const std::array<float, 3>& DomainOriginWorld, const WorldCameraView& Cam)
{
    if (!Initialized || ActiveCmd == VK_NULL_HANDLE)
        return;

    Solver.PrepareAtlasesForWorldComposite(Ctx, ActiveCmd);
    Ctx.TransitionImage(ActiveCmd, SceneColorImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const PyroCameraFrame PyroCam = TransformWorldCameraToPyroDomain(Cam, DomainOriginWorld);
    Ctx.BeginTarget(ActiveCmd, Ctx.QueryTargetSingleRgba8Load(), SceneColorLoadFbo,
                    SceneColorImage.Width, SceneColorImage.Height, nullptr, false);
    Solver.RecordCompositeIntoWorldTarget(Ctx, ActiveCmd, Settings, PyroCam,
                                          SceneColorImage.Width, SceneColorImage.Height, SceneDepthImage);
    Ctx.EndTarget(ActiveCmd);
}

void WorldSdfScene::EndWorldFrame(VulkanContext& Ctx)
{
    if (!Initialized || ActiveCmd == VK_NULL_HANDLE)
        return;

    Ctx.TransitionImage(ActiveCmd, SceneColorImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Ctx.SubmitAndWaitFrameCommands();
    ActiveCmd = VK_NULL_HANDLE;
}

bool WorldSdfScene::ReadWorldRgba(VulkanContext& Ctx, std::vector<uint8_t>& OutTopDownRgba, int& OutWidth, int& OutHeight)
{
    if (SceneColorImage.Image == VK_NULL_HANDLE)
        return false;
    OutWidth  = SceneColorImage.Width;
    OutHeight = SceneColorImage.Height;
    return Ctx.DownloadRgba8Image(SceneColorImage, OutTopDownRgba);
}

} // namespace Frontier
