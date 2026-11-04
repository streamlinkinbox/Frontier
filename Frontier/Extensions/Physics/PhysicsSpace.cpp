/*==========================================================================================================================================
                                                              PHYSICSSPACE.CPP
==========================================================================================================================================*/
// 🧩 The PhysicsSpace over its PhysicsAdapter: the descriptors it was given, the simulated time, and every call passed through.

#include "Frontier/Extensions/Physics/PhysicsSpace.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

PhysicsSpace::PhysicsSpace(const PhysicsSpaceSettings& Settings) : Adapter(ConstructJoltAdapter(Settings))
{
}

PhysicsSpace::~PhysicsSpace() = default;

uint32_t PhysicsSpace::AddBody(const PhysicsBodyDescriptor& Descriptor)
{
    const uint32_t Index = Adapter->AddBody(Descriptor);
    if (Index == PhysicsNoBody) return PhysicsNoBody;
    Descriptors.push_back(Descriptor);
    // 📝 The descriptor keeps its shape figures, not the hull points or triangles (the solver holds those); the wireframe store
    //    keeps them so the collision overlay can draw the actual collider, not a stand-in.
    WireframeShape Wire;
    Wire.Shape            = Descriptor.Shape;
    Wire.HalfExtentMetres = Descriptor.HalfExtentMetres;
    Wire.Points           = Descriptor.Points;
    Wire.Triangles        = Descriptor.Triangles;
    WireframeShapes.push_back(std::move(Wire));
    Descriptors.back().Points.clear();
    Descriptors.back().Points.shrink_to_fit();
    Descriptors.back().Triangles.clear();
    Descriptors.back().Triangles.shrink_to_fit();
    return Index;
}

void PhysicsSpace::Prepare()
{
    Adapter->Prepare();
}

void PhysicsSpace::Step(double DeltaSeconds, uint32_t CollisionSteps)
{
    if (!(DeltaSeconds > 0.0)) return;
    Adapter->Step(DeltaSeconds, CollisionSteps);
    SimulatedSeconds += DeltaSeconds;
}

PhysicsBodyReading PhysicsSpace::ReadBody(uint32_t Index) const
{
    return Adapter->ReadBody(Index);
}

void PhysicsSpace::PlaceBody(uint32_t Index, const PhysicsBodyReading& Pose)
{
    Adapter->PlaceBody(Index, Pose);
}

void PhysicsSpace::ApplyForce(uint32_t Index, const std::array<double, 3>& Force, const std::array<double, 3>& Point)
{
    Adapter->ApplyForce(Index, Force, Point);
}

void PhysicsSpace::ApplyTorque(uint32_t Index, const std::array<double, 3>& Torque)
{
    Adapter->ApplyTorque(Index, Torque);
}

bool PhysicsSpace::CastRay(const PhysicsRay& Ray, PhysicsRayHit& Hit) const
{
    return Adapter->CastRay(Ray, Hit);
}

void PhysicsSpace::AssignMass(uint32_t Index, double MassKilograms, const std::array<double, 3>& InertiaKgm2)
{
    Adapter->AssignMass(Index, MassKilograms, InertiaKgm2);
}

void PhysicsSpace::Dispatch(uint32_t Count, const std::function<void(uint32_t)>& Task)
{
    Adapter->Dispatch(Count, Task);
}

size_t PhysicsSpace::QueryAwakeCount() const
{
    return Adapter->QueryAwakeCount();
}

uint64_t PhysicsSpace::QueryContactsAdded() const noexcept
{
    return Adapter->QueryContactsAdded();
}

uint32_t PhysicsSpace::QueryWorkers() const noexcept
{
    return Adapter->QueryWorkers();
}

const char* PhysicsSpace::QuerySolverName() const noexcept
{
    return Adapter->QuerySolverName();
}

std::vector<PhysicsWireSegment> PhysicsSpace::ResolveWireframe() const
{
    std::vector<PhysicsWireSegment> Segments;
    for (uint32_t Index = 0; Index < static_cast<uint32_t>(Descriptors.size()); ++Index)
    {
        const PhysicsBodyDescriptor& Descriptor = Descriptors[Index];
        const WireframeShape&        Shape      = WireframeShapes[Index];
        const PhysicsBodyReading     Pose       = Adapter->ReadBody(Index);
        const bool                   Moving     = Descriptor.Motion == PhysicsMotion::Moving;
        auto Place = [&](const std::array<double, 3>& Local)
        {
            const std::array<double, 3> Turned = RotatePhysicsVector(Pose.Orientation, Local);
            return std::array<double, 3>{ Pose.PositionMetres[0] + Turned[0], Pose.PositionMetres[1] + Turned[1],
                                          Pose.PositionMetres[2] + Turned[2] };
        };
        auto Edge = [&](const std::array<double, 3>& A, const std::array<double, 3>& B)
        {
            Segments.push_back({ Place(A), Place(B), Index, Moving });
        };
        if (Shape.Shape == PhysicsShape::Box)
        {
            const double X = Shape.HalfExtentMetres[0], Y = Shape.HalfExtentMetres[1], Z = Shape.HalfExtentMetres[2];
            const std::array<std::array<double, 3>, 8> Corner = { { { -X, -Y, -Z }, { X, -Y, -Z }, { X, Y, -Z }, { -X, Y, -Z },
                                                                    { -X, -Y, Z }, { X, -Y, Z }, { X, Y, Z }, { -X, Y, Z } } };
            constexpr int EdgeIndex[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
            for (const auto& Pair : EdgeIndex) Edge(Corner[Pair[0]], Corner[Pair[1]]);
        }
        else if (Shape.Shape == PhysicsShape::Sphere)
        {
            const double Radius = Shape.HalfExtentMetres[0];
            constexpr int Steps = 24;
            for (int Plane = 0; Plane < 3; ++Plane)
                for (int Step = 0; Step < Steps; ++Step)
                {
                    const double A0 = 2.0 * 3.14159265358979323846 * Step / Steps, A1 = 2.0 * 3.14159265358979323846 * (Step + 1) / Steps;
                    auto Circle = [&](double Angle)
                    {
                        const double C = Radius * std::cos(Angle), S = Radius * std::sin(Angle);
                        if (Plane == 0) return std::array<double, 3>{ C, S, 0.0 };
                        if (Plane == 1) return std::array<double, 3>{ C, 0.0, S };
                        return std::array<double, 3>{ 0.0, C, S };
                    };
                    Edge(Circle(A0), Circle(A1));
                }
        }
        else if (Shape.Shape == PhysicsShape::Triangles)
        {
            for (size_t Triangle = 0; Triangle + 2 < Shape.Triangles.size(); Triangle += 3)
            {
                const auto& P0 = Shape.Points[Shape.Triangles[Triangle]];
                const auto& P1 = Shape.Points[Shape.Triangles[Triangle + 1]];
                const auto& P2 = Shape.Points[Shape.Triangles[Triangle + 2]];
                const std::array<double, 3> A{ P0[0], P0[1], P0[2] }, B{ P1[0], P1[1], P1[2] }, C{ P2[0], P2[1], P2[2] };
                Edge(A, B); Edge(B, C); Edge(C, A);
            }
        }
        else if (Shape.Shape == PhysicsShape::Hull && !Shape.Points.empty())
        {
            // 📝 The hull's local bounds outline: a faithful enough debug cage without re-deriving the solver's hull edges.
            std::array<double, 3> Minimum{ 1.0e30, 1.0e30, 1.0e30 }, Maximum{ -1.0e30, -1.0e30, -1.0e30 };
            for (const auto& Point : Shape.Points)
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    Minimum[Axis] = std::min(Minimum[Axis], static_cast<double>(Point[Axis]));
                    Maximum[Axis] = std::max(Maximum[Axis], static_cast<double>(Point[Axis]));
                }
            const std::array<std::array<double, 3>, 8> Corner = { { { Minimum[0], Minimum[1], Minimum[2] }, { Maximum[0], Minimum[1], Minimum[2] },
                                                                    { Maximum[0], Maximum[1], Minimum[2] }, { Minimum[0], Maximum[1], Minimum[2] },
                                                                    { Minimum[0], Minimum[1], Maximum[2] }, { Maximum[0], Minimum[1], Maximum[2] },
                                                                    { Maximum[0], Maximum[1], Maximum[2] }, { Minimum[0], Maximum[1], Maximum[2] } } };
            constexpr int EdgeIndex[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
            for (const auto& Pair : EdgeIndex) Edge(Corner[Pair[0]], Corner[Pair[1]]);
        }
    }
    return Segments;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ORIENTATION
//------------------------------------------------------------------------------------------------------------------------

std::array<double, 4> ResolvePhysicsOrientation(const std::array<double, 3>& OrientationDegrees) noexcept
{
    // 📝 q = qz(γ) · qy(β) · qx(α), half angles.
    constexpr double Radians = 3.14159265358979323846 / 360.0;
    const double     Cx = std::cos(OrientationDegrees[0] * Radians), Sx = std::sin(OrientationDegrees[0] * Radians);
    const double     Cy = std::cos(OrientationDegrees[1] * Radians), Sy = std::sin(OrientationDegrees[1] * Radians);
    const double     Cz = std::cos(OrientationDegrees[2] * Radians), Sz = std::sin(OrientationDegrees[2] * Radians);
    return { Sx * Cy * Cz - Cx * Sy * Sz, Cx * Sy * Cz + Sx * Cy * Sz, Cx * Cy * Sz - Sx * Sy * Cz, Cx * Cy * Cz + Sx * Sy * Sz };
}

std::array<double, 3> ResolvePhysicsEulerDegrees(const std::array<double, 4>& Q) noexcept
{
    // 📝 From the rotation's columns: β = asin(−R₂₀), α = atan2(R₂₁, R₂₂), γ = atan2(R₁₀, R₀₀).
    constexpr double Degrees = 180.0 / 3.14159265358979323846;
    const double     X = Q[0], Y = Q[1], Z = Q[2], W = Q[3];
    const double     R00 = 1.0 - 2.0 * (Y * Y + Z * Z), R10 = 2.0 * (X * Y + Z * W), R20 = 2.0 * (X * Z - Y * W);
    const double     R21 = 2.0 * (Y * Z + X * W), R22 = 1.0 - 2.0 * (X * X + Y * Y);
    return { std::atan2(R21, R22) * Degrees, std::asin(std::clamp(-R20, -1.0, 1.0)) * Degrees, std::atan2(R10, R00) * Degrees };
}

std::array<double, 3> RotatePhysicsVector(const std::array<double, 4>& Q, const std::array<double, 3>& V) noexcept
{
    // 📝 v' = v + 2 w (q × v) + 2 q × (q × v)
    const double Tx = 2.0 * (Q[1] * V[2] - Q[2] * V[1]), Ty = 2.0 * (Q[2] * V[0] - Q[0] * V[2]), Tz = 2.0 * (Q[0] * V[1] - Q[1] * V[0]);
    return { V[0] + Q[3] * Tx + (Q[1] * Tz - Q[2] * Ty), V[1] + Q[3] * Ty + (Q[2] * Tx - Q[0] * Tz), V[2] + Q[3] * Tz + (Q[0] * Ty - Q[1] * Tx) };
}

} // namespace Frontier
