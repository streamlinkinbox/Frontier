/*============================================================================================================================================
                                                    PARTICLEREFERENCEGEOMETRY.H
============================================================================================================================================*/
// 🧩 Faithful C++20 reference geometry provider mirroring Experimental/ParticleEditor Final across all 93 presets

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticlePresetRegistry.h"
#include "Engine/Workspaces/Particle/ParticleViewportRasterization.h"

#include <string_view>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                             PARTICLEREFERENCEGEOMETRYSTORAGE
//------------------------------------------------------------------------------------------------------------------------

struct ParticleReferenceGeometryStorage
{
    std::vector<ParticleGpuSegmentPrimitive> AlphaSegments              {};
    std::vector<ParticleGpuSegmentPrimitive> AdditiveSegments           {};
    std::vector<ParticleGpuSurfaceVertex>    OpaqueSurfaceVertices      {};
    std::vector<ParticleGpuSurfaceVertex>    TranslucentSurfaceVertices {};
    std::vector<ParticleGpuSurfaceVertex>    AdditiveSurfaceVertices    {};

    bool                  RenderLiveBillboards     = true;
    bool                  RenderLiveOrientedSolids = false;
    bool                  RenderLiveStrands        = false;
    ParticleGpuSilhouette Silhouette               = ParticleGpuSilhouette::SoftDisc;
    ParticleGpuBlendMode  BlendMode                = ParticleGpuBlendMode::Alpha;
    ParticleSolidProfile  SolidProfile             = ParticleSolidProfile::Cube;
    float                 SizeScale                = 1.0f;
    float                 EmissiveBoost            = 1.15f;
    float                 RibbonWidthMeters        = 0.03f;
    ParticleCameraRig     Camera                   {};

    void ClearGeometry() noexcept
    {
        AlphaSegments.clear();
        AdditiveSegments.clear();
        OpaqueSurfaceVertices.clear();
        TranslucentSurfaceVertices.clear();
        AdditiveSurfaceVertices.clear();
    }

    [[nodiscard]] ParticleReferenceGeometryBatch ViewBatch() const noexcept
    {
        return ParticleReferenceGeometryBatch{
            AlphaSegments,
            AdditiveSegments,
            OpaqueSurfaceVertices,
            TranslucentSurfaceVertices,
            AdditiveSurfaceVertices,
        };
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC PROVIDER
//------------------------------------------------------------------------------------------------------------------------

// Populate GPU-ready 3D segment arcs, silk/botanical strands, studio-lit 3D props/surfaces & matched camera framing
// for any of the 93 active Particle Studio presets, faithfully mirroring Experimental/ParticleEditor Final.
void BuildParticleReferenceGeometry(
    std::string_view               PresetSlug,
    const ParticleEffectDescriptor& Descriptor,
    float                          TimeSeconds,
    ParticleReferenceGeometryStorage& OutStorage) noexcept;

} // namespace Frontier::Workspaces
