//============================================================================================================================================
//                                                               TYRESEQUENCE.H
//============================================================================================================================================
// 📦 Shared native XPBD snapshot projection: renderer vertices and the closed lighting envelope use the same nodes.

#pragma once

#include "../../../Engine/PhysicalDynamics/Vehicle/XPBDSoftTyre.h"
#include "../../../Engine/GeometricRaster/DeformationSpace.h"

namespace Frontier::Drive
{

class TyreSequence
{
public:
    void Capture(const Vehicle::XPBDSoftTyre& Tyre, Vehicle::Vec3 Hub, Vehicle::Quat HubRotation)
    {
        Parameters = Tyre.Params();
        Position = Hub;
        Rotation = Tyre.MaterialFrame(HubRotation);
        Nodes = Tyre.Nodes();
    }

    [[nodiscard]] Vector3 Project(Vector3 Local, bool Deform = true) const
    {
        const float Radius = std::hypot(Local.x, Local.z);
        const float Weight = std::clamp((Radius - Parameters.RimRadius) /
                                       (Parameters.Radius - Parameters.RimRadius), 0.0f, 1.0f);
        Vehicle::Vec3 World = Position + Rotation.Rotate({Local.x, Local.y, Local.z});
        if (!Deform || Weight <= 0.0f || Nodes.empty()) return {World.x, World.y, World.z};
        constexpr float TwoPi = 6.28318530718f;
        float Angle = std::atan2(Local.z, Local.x);
        if (Angle < 0.0f) Angle += TwoPi;
        const float Around = Angle * static_cast<float>(Parameters.SegmentCount) / TwoPi;
        const float Across = std::clamp(0.5f + Local.y / Parameters.Width, 0.0f, 1.0f) * (Parameters.RingCount - 1u);
        const uint32_t Segment = static_cast<uint32_t>(Around), Ring = static_cast<uint32_t>(Across);
        const float AroundFraction = Around - Segment, AcrossFraction = Across - Ring;
        auto Displacement = [&](uint32_t Row, uint32_t Column)
        {
            const auto& Node = Nodes[std::min(Row, Parameters.RingCount - 1u) * Parameters.SegmentCount +
                                     Column % Parameters.SegmentCount];
            return Node.Position - (Position + Rotation.Rotate(Node.TreadLocal));
        };
        const auto First = Displacement(Ring, Segment) * (1.0f - AroundFraction) +
                           Displacement(Ring, Segment + 1u) * AroundFraction;
        const auto Second = Displacement(Ring + 1u, Segment) * (1.0f - AroundFraction) +
                            Displacement(Ring + 1u, Segment + 1u) * AroundFraction;
        World += (First * (1.0f - AcrossFraction) + Second * AcrossFraction) * Weight;
        return {World.x, World.y, World.z};
    }

    // Object/material-frame field: remove the hub's rigid transform AFTER applying solved world displacements.
    // The outer surface matches DriveWheelMesh. Its inner barrel closes the two beads, leaving the axle opening.
    void Envelope(std::vector<Vector3>& Vertices, std::vector<DeformationSpace::Face>& Faces, bool Rest) const
    {
        Vertices.clear(); Faces.clear();
        const uint32_t Rings = Parameters.RingCount, Segments = Parameters.SegmentCount;
        const Vehicle::Quat Inverse{-Rotation.x, -Rotation.y, -Rotation.z, Rotation.w};
        auto Append = [&](Vector3 Local)
        {
            if (Rest) Vertices.push_back(Local);
            else
            {
                const Vector3 World = Project(Local);
                const auto Point = Inverse.Rotate(Vehicle::Vec3{World.x, World.y, World.z} - Position);
                Vertices.push_back({Point.x, Point.y, Point.z});
            }
        };
        // Authored wheel rings run +Y to -Y; XPBD nodes run -Y to +Y. Reverse the ring index, not world positions.
        for (uint32_t Ring = 0u; Ring < Rings; ++Ring)
            for (uint32_t Segment = 0u; Segment < Segments; ++Segment)
            {
                const auto& Node = Nodes[(Rings - 1u - Ring) * Segments + Segment];
                Append({Node.TreadLocal.x, Node.TreadLocal.y, Node.TreadLocal.z});
            }
        for (uint32_t Side = 0u; Side < 2u; ++Side)
            for (uint32_t Segment = 0u; Segment < Segments; ++Segment)
            {
                const float Angle = 6.28318530718f * Segment / Segments;
                // DriveWheelMesh's bead half-width, not the solver's internal pressure-volume anchor width.
                Append({Parameters.RimRadius * std::cos(Angle), (Side == 0u ? 1.0f : -1.0f) * Parameters.Width * 0.5f * 0.74f,
                        Parameters.RimRadius * std::sin(Angle)});
            }
        auto Quad = [&](uint32_t A, uint32_t B, uint32_t C, uint32_t D)
        {
            Faces.push_back({A, B, C}); Faces.push_back({A, C, D});
        };
        for (uint32_t Ring = 0u; Ring + 1u < Rings; ++Ring)
            for (uint32_t Segment = 0u; Segment < Segments; ++Segment)
            {
                const uint32_t Next = (Segment + 1u) % Segments;
                Quad(Ring * Segments + Segment, (Ring + 1u) * Segments + Segment,
                     (Ring + 1u) * Segments + Next, Ring * Segments + Next);
            }
        const uint32_t Left = Rings * Segments, Right = Left + Segments;
        for (uint32_t Segment = 0u; Segment < Segments; ++Segment)
        {
            const uint32_t Next = (Segment + 1u) % Segments, Shoulder = (Rings - 1u) * Segments;
            Quad(Left + Segment, Segment, Next, Left + Next);
            Quad(Shoulder + Segment, Right + Segment, Right + Next, Shoulder + Next);
            Quad(Left + Segment, Left + Next, Right + Next, Right + Segment);
        }
        for (auto& Face : Faces) std::swap(Face[1], Face[2]);
    }

private:
    Vehicle::SoftTyreParameters Parameters;
    Vehicle::Vec3 Position;
    Vehicle::Quat Rotation;
    std::vector<Vehicle::SoftTyreNode> Nodes;
};

} // namespace Frontier::Drive
