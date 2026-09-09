//============================================================================================================================================
//                                                 INTERFACELIGHTPROJECTION.CPP
//============================================================================================================================================

#include "InterfaceLightProjection.h"

#include "../ContentInterchange/MaterialIndex.h"
#include "../GeometricRaster/GeometryStructure.h"
#include "../GeometricRaster/SceneStructure.h"

#include <algorithm>
#include <cmath>

namespace Frontier {

const char* InterfaceFidelityTierName(InterfaceFidelityTier Tier) noexcept
{
    switch (Tier)
    {
        case InterfaceFidelityTier::Off:   return "Off";
        case InterfaceFidelityTier::Low:   return "Low";
        case InterfaceFidelityTier::High:  return "High";
        case InterfaceFidelityTier::Ultra: return "Ultra";
        default:                           return "Unknown";
    }
}

namespace {

constexpr float kByteToUnit = 1.0f / 255.0f;

// sRGB → linear. The tint is authored and stored as display-referred bytes, but radiance adds linearly: summing
//    sRGB values would make a half-lit panel noticeably too bright, and the error is worst in the mid tones where
//    most interface colour lives.
[[nodiscard]] float ToLinear(float Encoded) noexcept
{
    return Encoded <= 0.04045f ? Encoded / 12.92f : std::pow((Encoded + 0.055f) / 1.055f, 2.4f);
}

} // namespace

PanelRadiance InterfaceLightProjection::MeasureRadiance(const InterfaceStructure& /*Structure*/,
                                                        const InterfaceSequence& Composition,
                                                        float PanelArea) noexcept
{
    PanelRadiance Radiance;
    Radiance.PanelArea = std::max(PanelArea, 0.0f);

    const InterfaceInstanceFigure* Instances = Composition.QueryInstances();
    const uint32_t Count = Composition.QueryInstanceCount();
    if (Instances == nullptr || Count == 0u) return Radiance;

    double SumRed = 0.0, SumGreen = 0.0, SumBlue = 0.0, SumArea = 0.0;

    for (uint32_t Index = 0u; Index < Count; ++Index)
    {
        const InterfaceInstanceFigure& Slot = Instances[Index];

        // EmissiveWeight is the ⑦ channel added in P0-4: 0 = pure albedo, 1 = pure emitter. A figure that only
        //    reflects room light contributes nothing to what the panel RADIATES, which is the whole distinction
        //    that field was added to express.
        const float Weight = std::clamp(Slot.EmissiveWeight, 0.0f, 1.0f);
        if (Weight <= 0.0f) continue;

        // Area from the slot's own half extent, so the light is weighted by what is actually drawn. Opacity scales
        //    it: a figure fading out must dim the room as it goes, or a transition would leave light behind.
        const float Area = 4.0f * Slot.HalfWidth * Slot.HalfHeight * std::clamp(Slot.Opacity, 0.0f, 1.0f);
        if (Area <= 0.0f) continue;

        // The resolved tint — palette slot already substituted — so light and image agree by construction.
        const uint32_t Tint = Slot.Tint;
        const float R = ToLinear(static_cast<float>((Tint      ) & 0xFFu) * kByteToUnit);
        const float G = ToLinear(static_cast<float>((Tint >>  8) & 0xFFu) * kByteToUnit);
        const float B = ToLinear(static_cast<float>((Tint >> 16) & 0xFFu) * kByteToUnit);
        const float A = static_cast<float>((Tint >> 24) & 0xFFu) * kByteToUnit;

        const double Contribution = static_cast<double>(Area) * Weight * A;
        SumRed   += R * Contribution;
        SumGreen += G * Contribution;
        SumBlue  += B * Contribution;
        SumArea  += Contribution;
        ++Radiance.Contributors;
    }

    Radiance.LitArea = static_cast<float>(SumArea);
    if (SumArea <= 0.0) return Radiance;

    // Average colour of the lit region, then scaled by how much of the face is lit. A single bright lamp on a dark
    //    panel should tint the room its colour without lighting it as though the whole face glowed.
    //
    //    Coverage is CLAMPED to 1. Figures overlap — a knob sits on its bed, a fill sits in its trough — so the
    //    summed area of the lit figures genuinely exceeds the panel face (measured 122% on the trial panel). That
    //    is not an error in the sum; it is what layered interface geometry looks like. Left unclamped it would
    //    make a busy panel brighter than a solid white one, so the physical bound is applied here.
    const float Coverage = Radiance.Coverage();
    Radiance.Red   = static_cast<float>(SumRed   / SumArea) * Coverage;
    Radiance.Green = static_cast<float>(SumGreen / SumArea) * Coverage;
    Radiance.Blue  = static_cast<float>(SumBlue  / SumArea) * Coverage;
    return Radiance;
}

uint32_t InterfaceLightProjection::ComposeProxy(SceneStructure& Scene, const PanelProxyRequest& Request,
                                                const PanelRadiance& Radiance) noexcept
{
    constexpr uint32_t kNoInstance = 0xFFFFFFFFu;

    if (Request.Tier == InterfaceFidelityTier::Off) return kNoInstance;
    if (!IsTierAvailable(Request.Tier))             return kNoInstance;   // High/Ultra: not built, say so

    // A panel that emits nothing is not registered at all. Adding a black emitter would cost two triangles and a
    //    luminaire entry that can only ever sample to zero — pure waste in the light table.
    const float Peak = std::max({ Radiance.Red, Radiance.Green, Radiance.Blue });
    if (Peak <= 0.0f) return kNoInstance;

    const Vector3 Right{ Request.RightX, Request.RightY, Request.RightZ };
    const Vector3 Up   { Request.UpX,    Request.UpY,    Request.UpZ    };

    // Normal from the half-axes. A degenerate quad would produce a zero-area luminaire, which divides by zero in
    //    the area sampler, so it is refused here rather than left to surface as a NaN in the image.
    Vector3 Normal{ Right.y * Up.z - Right.z * Up.y,
                    Right.z * Up.x - Right.x * Up.z,
                    Right.x * Up.y - Right.y * Up.x };
    const float NormalLength = std::sqrt(Normal.x * Normal.x + Normal.y * Normal.y + Normal.z * Normal.z);
    if (NormalLength <= 1.0e-9f) return kNoInstance;
    Normal = Vector3{ Normal.x / NormalLength, Normal.y / NormalLength, Normal.z / NormalLength };

    // ── The emissive material ────────────────────────────────────────────────────────────────────────────────────
    MaterialDescriptor Descriptor;
    Descriptor.Name = "interface_panel_proxy";
    Descriptor.Slabs.emplace_back();
    MaterialSlabDescriptor& Slab = Descriptor.Slabs[0];

    const float Gain = std::max(Request.Gain, 0.0f);
    Slab.BaseColor[0] = Radiance.Red   / Peak;
    Slab.BaseColor[1] = Radiance.Green / Peak;
    Slab.BaseColor[2] = Radiance.Blue  / Peak;
    Slab.EmissionColor[0] = Slab.BaseColor[0];
    Slab.EmissionColor[1] = Slab.BaseColor[1];
    Slab.EmissionColor[2] = Slab.BaseColor[2];
    Slab.EmissionLuminance = Peak * Gain;
    // A display is a diffuse emitter, not a mirror; a specular lobe on the proxy would put a highlight on a
    //    surface that is supposed to BE the light.
    Slab.SpecularWeight = 0.0f;

    const uint32_t Material = Scene.RegisterMaterial(Descriptor);

    // ── The quad ─────────────────────────────────────────────────────────────────────────────────────────────────
    // Two triangles, in WORLD space, with an identity instance transform. The panel's placement is already baked
    //    into the half-axes the caller passed, so carrying it again in the instance matrix would apply it twice.
    GeometryStructure Mesh;

    const Vector3 Centre{ Request.CentreX, Request.CentreY, Request.CentreZ };
    const auto Corner = [&](float U, float V)
    {
        VertexRecord Vertex{};
        Vertex.SpatialLocation = Vector3{ Centre.x + Right.x * U + Up.x * V,
                                          Centre.y + Right.y * U + Up.y * V,
                                          Centre.z + Right.z * U + Up.z * V };
        Vertex.NormalDirection    = Normal;
        Vertex.TangentDirection   = Vector4{ Right.x, Right.y, Right.z, 1.0f };
        Vertex.TextureCoordinateU = U * 0.5f + 0.5f;
        Vertex.TextureCoordinateV = V * 0.5f + 0.5f;
        return Vertex;
    };

    const VertexRecord Vertices[4] = { Corner(-1.0f, -1.0f), Corner(1.0f, -1.0f),
                                       Corner( 1.0f,  1.0f), Corner(-1.0f, 1.0f) };
    const uint32_t Indices[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
    Mesh.AppendVertices(Vertices, 4u);
    Mesh.AppendIndices(Indices, 6u);

    PolyhedralCluster Cluster{};
    Cluster.BoundingCenter = Centre;
    Cluster.BoundingRadius = std::sqrt(Right.x * Right.x + Right.y * Right.y + Right.z * Right.z)
                           + std::sqrt(Up.x * Up.x + Up.y * Up.y + Up.z * Up.z);
    Cluster.ConeApex       = Centre;
    Cluster.ConeAxis       = Normal;
    Cluster.ConeCutoff     = -1.0f;          // never cone-culled: it is a light, visible from its whole hemisphere
    Cluster.VertexOffset   = 0u;
    Cluster.TriangleOffset = 0u;
    Cluster.TriangleCount  = 2u;
    (void)Mesh.RegisterCluster(Cluster);

    Matrix4x4 Identity;
    for (int Column = 0; Column < 4; ++Column)
        for (int Row = 0; Row < 4; ++Row)
            Identity.Columns[Column][Row] = (Column == Row) ? 1.0f : 0.0f;

    return Scene.RegisterInstance(Mesh, Identity, Material, 0u);
}

} // namespace Frontier
