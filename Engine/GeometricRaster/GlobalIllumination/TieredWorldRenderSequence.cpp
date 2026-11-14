/*==========================================================================================================================================
                                                  TIEREDWORLDRENDERSEQUENCE.CPP
==========================================================================================================================================*/
// 🧩 Complete Ordered GTX Tier 1 + RTX Tier 2 World Render Sequence bound to ViewportProjectionPane (Visibility -> Direct -> GI -> Specular)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] float EncodeLinearToSrgb(float LinearVal) noexcept
{
    const float C = std::clamp(LinearVal, 0.0f, 1.0f);
    return (C <= 0.0031308f) ? (12.92f * C) : (1.055f * std::pow(C, 1.0f / 2.4f) - 0.055f);
}

[[nodiscard]] Mathematics::LinearRGB ApplyAcesFilmic(Mathematics::LinearRGB Hdr) noexcept
{
    const auto Curve = [](float X) noexcept -> float
    {
        const float V = std::max(0.0f, X * 0.85f);
        return std::clamp((V * (2.51f * V + 0.03f)) / (V * (2.43f * V + 0.59f) + 0.14f), 0.0f, 1.0f);
    };
    return { Curve(Hdr.R), Curve(Hdr.G), Curve(Hdr.B) };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            TIERED WORLD RENDER SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

void TieredWorldRenderSequence::PrepareWorldScene(WorldSceneSpecification& InOutScene)
{
    // 1. Compile 32 B BounceSlabEntries for every registered material slot!
    const uint32_t SlotCount = InOutScene.MaterialRegister.QuerySlotCount();
    InOutScene.BounceSummaries.resize(SlotCount);
    for (uint32_t Slot = 0u; Slot < SlotCount; ++Slot)
    {
        InOutScene.BounceSummaries[Slot] = MaterialSpace::CompileBounceSlabEntry(
            InOutScene.MaterialRegister.InspectAuthoredMaterial(static_cast<uint16_t>(Slot)));
    }

    // 2. Build Tier 1 GTX 2-Cascade 3.9 mm Orthographic Contact Shadow Clips
    const Mathematics::Axis3D FocusCenter = (InOutScene.SceneTree.RootBoundsMin + InOutScene.SceneTree.RootBoundsMax) * 0.5f;
    SolarShadowSequence::BuildTwoCascadeContactClips(
        InOutScene.Sun, FocusCenter, InOutScene.SceneTree, FineContactClip, CoarseContactClip);

    // 3. Build & Propagate Multi-Bounce Tier 2 RTX 1/pi Octahedral Irradiance Lattice (with 16x16 Chebyshev Depth!)
    RtxLattice.BuildAndPropagateRtxLattice(
        InOutScene.LatticeConfig,
        InOutScene.SceneTree,
        InOutScene.BounceSummaries,
        InOutScene.Sun,
        InOutScene.SkyRadiance);
}

WorldRenderFrameResult TieredWorldRenderSequence::RenderWorldFrame(
    WorldRenderTier                Tier,
    const PerspectiveCameraFrame&  CurrentCamera,
    const PerspectiveCameraFrame&  PreviousCamera,
    const WorldSceneSpecification& PreparedScene)
{
    WorldRenderFrameResult Result {};
    Result.WidthPx    = std::max(1u, CurrentCamera.ViewportWidthPx);
    Result.HeightPx   = std::max(1u, CurrentCamera.ViewportHeightPx);
    Result.ActiveTier = Tier;
    const std::size_t TotalPixels = static_cast<std::size_t>(Result.WidthPx) * Result.HeightPx;

    Result.CompositeLinearHdr.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Result.IndirectDiffuseHdr.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Result.PrimaryDepthMeters.assign(TotalPixels, 1.0e6f);
    Result.DisplayRgba8.assign(TotalPixels * 4u, 255u);

    //====================================================================================================================
    // PASS 1: SHARED 64-BIT (8 B/PX) PRIMARY VISIBILITY, TWO-PHASE HI-Z CULL & 2-LAYER GLASS PEEL
    //====================================================================================================================
    const DepthPyramidSequence* PrevHiZPtr =
        (PreviousHiZ.QueryMipCount() > 0u &&
         PreviousHiZ.InspectMipLevel(0u).Width == Result.WidthPx &&
         PreviousHiZ.InspectMipLevel(0u).Height == Result.HeightPx)
            ? &PreviousHiZ
            : nullptr;

    Result.VisibilityCull = ClusterCullEncoder::EncodePhaseOneVisibility(
        CurrentCamera, PreparedScene.ClusterInstances, PrevHiZPtr, &PreviousCamera);

    VisibilityBufferTargets VisTargets {};
    VisTargets.AllocateAndClear(Result.WidthPx, Result.HeightPx);
    VisibilityClusterRasterization::RasterizeOpaqueClusters(
        CurrentCamera, PreparedScene.ClusterInstances,
        Result.VisibilityCull.HardwareClusterBatches,
        Result.VisibilityCull.MicroSoftwareClusters,
        VisTargets);

    DepthPyramidSequence CurrentHiZ {};
    CurrentHiZ.BuildFromVisibilityBuffer64(Result.WidthPx, Result.HeightPx, VisTargets.OpaquePrimary64);

    if (!Result.VisibilityCull.PhaseOneDeferredQueue.empty())
    {
        ClusterCullEncoder::ResolvePhaseTwoDisocclusions(
            CurrentCamera, PreparedScene.ClusterInstances, CurrentHiZ, Result.VisibilityCull);
        VisTargets.AllocateAndClear(Result.WidthPx, Result.HeightPx);
        VisibilityClusterRasterization::RasterizeOpaqueClusters(
            CurrentCamera, PreparedScene.ClusterInstances,
            Result.VisibilityCull.HardwareClusterBatches,
            Result.VisibilityCull.MicroSoftwareClusters,
            VisTargets);
        CurrentHiZ.BuildFromVisibilityBuffer64(Result.WidthPx, Result.HeightPx, VisTargets.OpaquePrimary64);
    }

    VisibilityClusterRasterization::RasterizeTwoLayerGlassPeel(
        CurrentCamera, PreparedScene.ClusterInstances, Result.VisibilityCull.TransparentGlassBatches, VisTargets);

    //====================================================================================================================
    // PASS 2: ANALYTIC PRIMARY HIT RECONSTRUCTION & CALIBRATED DIRECT LIGHTING (SOLAR + LTC AREA LIGHTS + CHASSIS AO)
    //====================================================================================================================
    const OrthonormalCameraBasis CurBasis  = ComputeCameraBasis(CurrentCamera);
    const OrthonormalCameraBasis PrevBasis = ComputeCameraBasis(PreviousCamera);

    std::vector<AnalyticResolvedSurfaceSample> PrimarySamples(TotalPixels);
    for (uint32_t PY = 0u; PY < Result.HeightPx; ++PY)
    {
        for (uint32_t PX = 0u; PX < Result.WidthPx; ++PX)
        {
            const std::size_t Off = static_cast<std::size_t>(PY) * Result.WidthPx + PX;
            PrimarySamples[Off] = VisibilityMaterialResolve::ReconstructAnalyticPixelHit(
                PX, PY, VisTargets.OpaquePrimary64[Off], CurBasis, PrevBasis, PreparedScene.ClusterInstances);
        }
    }

    const DirectShadowTier ShadowTier = (Tier == WorldRenderTier::Tier2RtxDeterministic)
        ? DirectShadowTier::RtxDeterministicSolarRay
        : DirectShadowTier::GtxContactClipAndSdfCone;

    Result.DirectLighting = LinearCosineLightIntegration::EvaluateDirectLightingFrame(
        ShadowTier,
        CurrentCamera,
        PreparedScene.ClusterInstances,
        VisTargets,
        PreparedScene.MaterialRegister,
        PreparedScene.SceneTree,
        PreparedScene.Sun,
        PreparedScene.AreaLights,
        &PreparedScene.SceneSdf,
        &FineContactClip,
        &CoarseContactClip);

    //====================================================================================================================
    // PASS 3: GLOBAL ILLUMINATION (RTX 1/PI OCTAHEDRAL CHEBYSHEV LATTICE vs GTX 6-CONE SDF GI) + SPECULAR & GLASS
    //====================================================================================================================
    if (Tier == WorldRenderTier::Tier2RtxDeterministic)
    {
        Result.SpecularAndGlass = SpecularReflectionSequence::EvaluateRtxReflectionsAndGlass(
            CurrentCamera, PreparedScene.ClusterInstances, VisTargets, PrimarySamples,
            PreparedScene.MaterialRegister, PreparedScene.BounceSummaries,
            PreparedScene.SceneTree, RtxLattice, PreparedScene.Sun, PreparedScene.SkyRadiance);
    }
    else
    {
        Result.SpecularAndGlass = SpecularReflectionSequence::EvaluateGtxHiZReflectionsAndGlass(
            CurrentCamera, PreparedScene.ClusterInstances, VisTargets, CurrentHiZ, PrimarySamples,
            Result.DirectLighting.DirectHdrRadiance, PreparedScene.MaterialRegister, PreparedScene.SkyRadiance);
    }

    const uint32_t MatCount = PreparedScene.MaterialRegister.QuerySlotCount();

    for (std::size_t Off = 0u; Off < TotalPixels; ++Off)
    {
        const auto& Hit = PrimarySamples[Off];
        Mathematics::LinearRGB FinalHdr = Result.DirectLighting.DirectHdrRadiance[Off];

        if (Hit.ValidHit)
        {
            const Mathematics::Axis3D CamDelta = Hit.WorldPosition - CurBasis.CameraOrigin;
            Result.PrimaryDepthMeters[Off]     = std::max(CurBasis.NearClip, Mathematics::EvaluateDot(CamDelta, CurBasis.ForwardAxis));
        }

        if (Hit.ValidHit && Hit.MaterialSlot < MatCount)
        {
            const auto& Mat = PreparedScene.MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);

            // Evaluate 1/pi-normalized diffuse indirect radiance (multi-bounce color bleeding!)
            Mathematics::LinearRGB IndirectOverPi {};
            if (Tier == WorldRenderTier::Tier2RtxDeterministic)
            {
                IndirectOverPi = RtxLattice.SampleChebyshevIrradianceOverPi(
                    Hit.WorldPosition, Hit.TangentFrame.NormalZ, Hit.WorldViewDirection);
            }
            else
            {
                IndirectOverPi = RtxLattice.EvaluateGtxSignedDistanceConeGi(
                    Hit.WorldPosition, Hit.TangentFrame.NormalZ, PreparedScene.SceneSdf,
                    PreparedScene.SceneTree, PreparedScene.BounceSummaries, PreparedScene.Sun, PreparedScene.SkyRadiance);
            }

            const float DiffAo     = Result.DirectLighting.CombinedDiffuseAo[Off];
            const float SpecAo     = Result.DirectLighting.LagardeSpecularAo[Off];
            const float DiffFactor = (1.0f - Mat.Specular.Metalness) *
                                     (1.0f - Mat.Transmission.TransmissionWeight) *
                                     Mat.Diffuse.BaseWeight;

            // L_diffuse_indirect = (E(N) / pi) * pi * (rho / pi) = (E(N) / pi) * rho! (Zero pi factor mismatch!)
            const Mathematics::LinearRGB DiffuseGi {
                IndirectOverPi.R * Mat.Diffuse.BaseColor.R * DiffFactor * (0.35f + 0.65f * DiffAo),
                IndirectOverPi.G * Mat.Diffuse.BaseColor.G * DiffFactor * (0.35f + 0.65f * DiffAo),
                IndirectOverPi.B * Mat.Diffuse.BaseColor.B * DiffFactor * (0.35f + 0.65f * DiffAo),
            };
            Result.IndirectDiffuseHdr[Off] = DiffuseGi;

            FinalHdr = FinalHdr + DiffuseGi + Result.SpecularAndGlass.IndirectSpecularHdr[Off] * SpecAo;
        }

        // Composite 2-Layer Abbe Crown Glass Refraction & Fresnel over pixel if peeled glass shell present!
        if (VisTargets.GlassPeelFront64[Off] != 0ull)
        {
            FinalHdr = Result.SpecularAndGlass.RefractedGlassHdr[Off];
        }

        Result.CompositeLinearHdr[Off] = FinalHdr;
    }

    //====================================================================================================================
    // PASS 4: DEPTH-COMPOSITED HDR PARTICLE INTEGRATION + ACES FILMIC SRGB8 RESOLVE
    //====================================================================================================================
    if (PreparedScene.ParticleBank != nullptr)
    {
        Result.CompositedParticles = CompositeParticleBankIntoFrame(
            *PreparedScene.ParticleBank,
            CurrentCamera,
            PreparedScene.ParticleAdditiveBlend,
            PreparedScene.ParticleEmissiveGain,
            PreparedScene.ParticleDepthFadeSpanM,
            Result);
    }
    else
    {
        for (std::size_t Off = 0u; Off < TotalPixels; ++Off)
        {
            const auto Tone = ApplyAcesFilmic(Result.CompositeLinearHdr[Off]);
            Result.DisplayRgba8[Off * 4u + 0u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.R) * 255.0f));
            Result.DisplayRgba8[Off * 4u + 1u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.G) * 255.0f));
            Result.DisplayRgba8[Off * 4u + 2u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.B) * 255.0f));
            Result.DisplayRgba8[Off * 4u + 3u] = 255u;
        }
    }

    PreviousHiZ = std::move(CurrentHiZ);
    return Result;
}

uint32_t TieredWorldRenderSequence::CompositeParticleBankIntoFrame(
    const SimulationSpace::Particles::ParticleChannelBank& Bank,
    const PerspectiveCameraFrame&                          Camera,
    bool                                                   AdditiveBlend,
    float                                                  EmissiveGain,
    float                                                  DepthFadeSpanMeters,
    WorldRenderFrameResult&                                InOutFrame)
{
    using namespace SimulationSpace::Particles;

    const uint32_t Width  = InOutFrame.WidthPx;
    const uint32_t Height = InOutFrame.HeightPx;
    const std::size_t TotalPixels = static_cast<std::size_t>(Width) * Height;
    if (Width == 0u || Height == 0u || InOutFrame.CompositeLinearHdr.size() != TotalPixels)
    {
        return 0u;
    }

    const auto Kin    = Bank.QueryChannel(ChannelLayouts::Kinematics);
    const auto Appear = Bank.QueryChannel(ChannelLayouts::Appearance);
    const auto Life   = Bank.QueryChannel(ChannelLayouts::Lifespan);
    const auto Links  = Bank.QueryChannel(ChannelLayouts::StrandConnectivity);
    const uint32_t ActiveCount = std::min<uint32_t>(Bank.QueryActiveCount(), static_cast<uint32_t>(Kin.size()));

    const OrthonormalCameraBasis Basis = ComputeCameraBasis(Camera);
    const float InvFadeSpan = 1.0f / std::max(0.02f, DepthFadeSpanMeters);
    const float FocalPixels = 0.5f * static_cast<float>(Height) / std::max(1.0e-4f, Basis.TanHalfFovY);

    auto ProjectPoint = [&](Mathematics::Axis3D WorldPt, float& OutSx, float& OutSy, float& OutDepthM) noexcept -> bool
    {
        const Mathematics::Axis3D Delta = WorldPt - Basis.CameraOrigin;
        const float Depth = Mathematics::EvaluateDot(Delta, Basis.ForwardAxis);
        if (Depth <= Basis.NearClip || Depth >= Basis.FarClip)
        {
            return false;
        }
        const float NdcX = Mathematics::EvaluateDot(Delta, Basis.RightAxis) / (Depth * Basis.TanHalfFovX);
        const float NdcY = Mathematics::EvaluateDot(Delta, Basis.UpAxis) / (Depth * Basis.TanHalfFovY);
        OutSx     = (NdcX * 0.5f + 0.5f) * static_cast<float>(Width);
        OutSy     = (0.5f - NdcY * 0.5f) * static_cast<float>(Height);
        OutDepthM = Depth;
        return true;
    };

    auto BlendHdrPixel = [&](int32_t Px, int32_t Py, float ParticleDepthM, const Mathematics::LinearRGB& Radiance, float Alpha) noexcept
    {
        if (Px < 0 || Py < 0 || Px >= static_cast<int32_t>(Width) || Py >= static_cast<int32_t>(Height) || Alpha <= 1.0e-4f)
        {
            return;
        }
        const std::size_t Off = static_cast<std::size_t>(Py) * Width + static_cast<std::size_t>(Px);
        const float OpaqueDepthM = (Off < InOutFrame.PrimaryDepthMeters.size()) ? InOutFrame.PrimaryDepthMeters[Off] : 1.0e6f;
        const float DepthGapM    = OpaqueDepthM - ParticleDepthM;
        if (DepthGapM <= -0.02f)
        {
            return; // Occluded behind opaque world surface!
        }
        const float SoftEdge = std::clamp((DepthGapM + 0.02f) * InvFadeSpan, 0.0f, 1.0f);
        const float EffAlpha = std::clamp(Alpha * SoftEdge, 0.0f, 1.0f);

        Mathematics::LinearRGB& Dst = InOutFrame.CompositeLinearHdr[Off];
        if (AdditiveBlend)
        {
            Dst.R += Radiance.R * EffAlpha;
            Dst.G += Radiance.G * EffAlpha;
            Dst.B += Radiance.B * EffAlpha;
        }
        else
        {
            Dst.R = Dst.R * (1.0f - EffAlpha) + Radiance.R * EffAlpha;
            Dst.G = Dst.G * (1.0f - EffAlpha) + Radiance.G * EffAlpha;
            Dst.B = Dst.B * (1.0f - EffAlpha) + Radiance.B * EffAlpha;
        }
    };

    // Depth-sort visible particles back-to-front so translucent clouds layer cleanly
    struct ProjectedEntry
    {
        uint32_t Slot    = 0u;
        float    ScreenX = 0.0f;
        float    ScreenY = 0.0f;
        float    DepthM  = 0.0f;
    };
    std::vector<ProjectedEntry> VisibleList;
    VisibleList.reserve(ActiveCount);

    for (uint32_t Slot = 0u; Slot < ActiveCount; ++Slot)
    {
        if (!Life.empty() && (Life[Slot].LivenessMask & 1u) == 0u)
        {
            continue;
        }
        float Sx = 0.0f, Sy = 0.0f, DepthM = 0.0f;
        if (ProjectPoint({ Kin[Slot].PosX, Kin[Slot].PosY, Kin[Slot].PosZ }, Sx, Sy, DepthM))
        {
            if (Sx >= -64.0f && Sx <= static_cast<float>(Width) + 64.0f &&
                Sy >= -64.0f && Sy <= static_cast<float>(Height) + 64.0f)
            {
                VisibleList.push_back({ Slot, Sx, Sy, DepthM });
            }
        }
    }

    std::sort(VisibleList.begin(), VisibleList.end(), [](const ProjectedEntry& A, const ProjectedEntry& B) {
        return A.DepthM > B.DepthM;
    });

    // Optional strand/cloth depth-tested ribbons
    if (!Links.empty())
    {
        for (const ProjectedEntry& Entry : VisibleList)
        {
            const uint32_t PrevSlot = Links[Entry.Slot].LinkPrevSlot;
            if (PrevSlot >= ActiveCount)
            {
                continue;
            }
            float PrevSx = 0.0f, PrevSy = 0.0f, PrevDepth = 0.0f;
            if (!ProjectPoint({ Kin[PrevSlot].PosX, Kin[PrevSlot].PosY, Kin[PrevSlot].PosZ }, PrevSx, PrevSy, PrevDepth))
            {
                continue;
            }
            const float Dx    = Entry.ScreenX - PrevSx;
            const float Dy    = Entry.ScreenY - PrevSy;
            const int32_t Steps = std::clamp(static_cast<int32_t>(std::ceil(std::sqrt(Dx * Dx + Dy * Dy))), 1, 96);
            const float ColR  = !Appear.empty() ? Appear[Entry.Slot].ColorR : 0.85f;
            const float ColG  = !Appear.empty() ? Appear[Entry.Slot].ColorG : 0.88f;
            const float ColB  = !Appear.empty() ? Appear[Entry.Slot].ColorB : 0.72f;
            const Mathematics::LinearRGB RibbonRad{ ColR * EmissiveGain, ColG * EmissiveGain, ColB * EmissiveGain };

            for (int32_t Step = 0; Step <= Steps; ++Step)
            {
                const float T = static_cast<float>(Step) / static_cast<float>(Steps);
                const int32_t Px = static_cast<int32_t>(std::round(PrevSx + Dx * T));
                const int32_t Py = static_cast<int32_t>(std::round(PrevSy + Dy * T));
                const float Dp   = PrevDepth + (Entry.DepthM - PrevDepth) * T;
                BlendHdrPixel(Px, Py, Dp, RibbonRad, 0.88f);
                BlendHdrPixel(Px + 1, Py, Dp, RibbonRad, 0.55f);
                BlendHdrPixel(Px, Py + 1, Dp, RibbonRad, 0.55f);
            }
        }
    }

    // Soft-disc + hot-core HDR depth-tested particle splats
    for (const ProjectedEntry& Entry : VisibleList)
    {
        const float RadiusM = !Appear.empty() ? std::max(0.015f, Appear[Entry.Slot].RadiusMeters) : 0.06f;
        const float RadiusPx = std::clamp((RadiusM * FocalPixels) / std::max(0.2f, Entry.DepthM), 1.5f, 36.0f);
        const float ColR = !Appear.empty() ? Appear[Entry.Slot].ColorR : 1.0f;
        const float ColG = !Appear.empty() ? Appear[Entry.Slot].ColorG : 0.85f;
        const float ColB = !Appear.empty() ? Appear[Entry.Slot].ColorB : 0.55f;
        const float ColA = !Appear.empty() ? std::clamp(Appear[Entry.Slot].ColorA, 0.0f, 1.0f) : 1.0f;
        const Mathematics::LinearRGB Rad{ ColR * EmissiveGain, ColG * EmissiveGain, ColB * EmissiveGain };

        const int32_t MinX = std::max(0, static_cast<int32_t>(std::floor(Entry.ScreenX - RadiusPx)));
        const int32_t MaxX = std::min(static_cast<int32_t>(Width) - 1, static_cast<int32_t>(std::ceil(Entry.ScreenX + RadiusPx)));
        const int32_t MinY = std::max(0, static_cast<int32_t>(std::floor(Entry.ScreenY - RadiusPx)));
        const int32_t MaxY = std::min(static_cast<int32_t>(Height) - 1, static_cast<int32_t>(std::ceil(Entry.ScreenY + RadiusPx)));
        const float InvRadSq = 1.0f / std::max(1.0f, RadiusPx * RadiusPx);

        for (int32_t Py = MinY; Py <= MaxY; ++Py)
        {
            const float Dy = (static_cast<float>(Py) + 0.5f) - Entry.ScreenY;
            for (int32_t Px = MinX; Px <= MaxX; ++Px)
            {
                const float Dx = (static_cast<float>(Px) + 0.5f) - Entry.ScreenX;
                const float R2 = (Dx * Dx + Dy * Dy) * InvRadSq;
                if (R2 >= 1.0f)
                {
                    continue;
                }
                const float Falloff = (1.0f - R2) * (1.0f - R2);
                const float HotCore = std::exp(-R2 * 4.5f) * 0.45f;
                const Mathematics::LinearRGB PixelRad{
                    Rad.R * (1.0f + HotCore),
                    Rad.G * (1.0f + HotCore),
                    Rad.B * (1.0f + HotCore),
                };
                BlendHdrPixel(Px, Py, Entry.DepthM, PixelRad, ColA * Falloff);
            }
        }
    }

    // Re-resolve ACES Filmic + sRGB8 DisplayRgba8 after HDR particle compositing
    InOutFrame.DisplayRgba8.resize(TotalPixels * 4u);
    for (std::size_t Off = 0u; Off < TotalPixels; ++Off)
    {
        const auto Tone = ApplyAcesFilmic(InOutFrame.CompositeLinearHdr[Off]);
        InOutFrame.DisplayRgba8[Off * 4u + 0u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.R) * 255.0f));
        InOutFrame.DisplayRgba8[Off * 4u + 1u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.G) * 255.0f));
        InOutFrame.DisplayRgba8[Off * 4u + 2u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.B) * 255.0f));
        InOutFrame.DisplayRgba8[Off * 4u + 3u] = 255u;
    }

    return static_cast<uint32_t>(VisibleList.size());
}

} // namespace Frontier::GeometricRaster
