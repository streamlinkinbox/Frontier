/*==========================================================================================================================================
                                                     PROVINGGROUNDSCOURSE.CPP
==========================================================================================================================================*/
// 🧩 Exact 1:1 C++ geometry, 'FCT1' binary facet decoder, 82-slot linear Rec.709 material table, ControlVehicle assembly
//    (chassis, glazing, Slate cockpit, wing/rear mirrors, roof fixture, 4 wheel sockets), and Proving Grounds level layout.

#include "Engine/Rendering/ProvingGroundsCourse.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace Frontier
{
namespace
{

constexpr float Pi = 3.14159265358979323846f;

using Vec3 = std::array<float, 3>;

[[nodiscard]] Vec3 Add(Vec3 Lhs, Vec3 Rhs) noexcept
{
    return { Lhs[0] + Rhs[0], Lhs[1] + Rhs[1], Lhs[2] + Rhs[2] };
}

[[nodiscard]] Vec3 Sub(Vec3 Lhs, Vec3 Rhs) noexcept
{
    return { Lhs[0] - Rhs[0], Lhs[1] - Rhs[1], Lhs[2] - Rhs[2] };
}

[[nodiscard]] Vec3 Scale(Vec3 Value, float Factor) noexcept
{
    return { Value[0] * Factor, Value[1] * Factor, Value[2] * Factor };
}

[[nodiscard]] float Dot(Vec3 Lhs, Vec3 Rhs) noexcept
{
    return Lhs[0] * Rhs[0] + Lhs[1] * Rhs[1] + Lhs[2] * Rhs[2];
}

[[nodiscard]] Vec3 Cross(Vec3 Lhs, Vec3 Rhs) noexcept
{
    return {
        Lhs[1] * Rhs[2] - Lhs[2] * Rhs[1],
        Lhs[2] * Rhs[0] - Lhs[0] * Rhs[2],
        Lhs[0] * Rhs[1] - Lhs[1] * Rhs[0]
    };
}

[[nodiscard]] Vec3 Normalize(Vec3 Value) noexcept
{
    const float Len = std::sqrt(std::max(Dot(Value, Value), 1.0e-16f));
    return { Value[0] / Len, Value[1] / Len, Value[2] / Len };
}

[[nodiscard]] Vec3 RotateZ(Vec3 Value, float Yaw) noexcept
{
    const float CosYaw = std::cos(Yaw);
    const float SinYaw = std::sin(Yaw);
    return {
        Value[0] * CosYaw - Value[1] * SinYaw,
        Value[0] * SinYaw + Value[1] * CosYaw,
        Value[2]
    };
}

void EmitFlatTriangle(std::vector<CourseTriangle>& Out, Vec3 A, Vec3 B, Vec3 C, uint32_t Material)
{
    const Vec3 Normal = Normalize(Cross(Sub(B, A), Sub(C, A)));
    Out.push_back(CourseTriangle{ A, B, C, Normal, Normal, Normal, Material });
}

void EmitOrientedTriangle(std::vector<CourseTriangle>& Out, Vec3 A, Vec3 B, Vec3 C, uint32_t Material, Vec3 Expected)
{
    const Vec3 Raw = Cross(Sub(B, A), Sub(C, A));
    if (Dot(Raw, Expected) < 0.0f)
    {
        EmitFlatTriangle(Out, A, C, B, Material);
    }
    else
    {
        EmitFlatTriangle(Out, A, B, C, Material);
    }
}

void EmitQuad(std::vector<CourseTriangle>& Out, Vec3 A, Vec3 B, Vec3 C, Vec3 D, uint32_t Material)
{
    EmitFlatTriangle(Out, A, B, C, Material);
    EmitFlatTriangle(Out, A, C, D, Material);
}

// 📝 Local to bottom centre (Z in [0, ExtentZ]), matching CourseStructure.js EmitBoxFacets.
void AppendBoxBottom(std::vector<CourseTriangle>& Out,
                     float ExtentX, float ExtentY, float ExtentZ,
                     uint32_t Material,
                     Vec3 Position = { 0.0f, 0.0f, 0.0f },
                     float Yaw = 0.0f,
                     const std::array<uint32_t, 6>* FaceMaterials = nullptr)
{
    const float X = 0.5f * ExtentX;
    const float Y = 0.5f * ExtentY;
    const float Z = ExtentZ;
    const std::array<uint32_t, 6> Faces = FaceMaterials != nullptr
        ? *FaceMaterials
        : std::array<uint32_t, 6>{ Material, Material, Material, Material, Material, Material };

    const auto Place = [&](Vec3 Local) -> Vec3 {
        return Add(Position, Yaw == 0.0f ? Local : RotateZ(Local, Yaw));
    };

    EmitQuad(Out, Place({ -X, -Y, Z }), Place({  X, -Y, Z }), Place({  X,  Y, Z }), Place({ -X,  Y, Z }), Faces[0]);
    EmitQuad(Out, Place({ -X,  Y, 0 }), Place({  X,  Y, 0 }), Place({  X, -Y, 0 }), Place({ -X, -Y, 0 }), Faces[1]);
    EmitQuad(Out, Place({  X, -Y, 0 }), Place({  X,  Y, 0 }), Place({  X,  Y, Z }), Place({  X, -Y, Z }), Faces[2]);
    EmitQuad(Out, Place({ -X,  Y, 0 }), Place({ -X, -Y, 0 }), Place({ -X, -Y, Z }), Place({ -X,  Y, Z }), Faces[3]);
    EmitQuad(Out, Place({  X,  Y, 0 }), Place({ -X,  Y, 0 }), Place({ -X,  Y, Z }), Place({  X,  Y, Z }), Faces[4]);
    EmitQuad(Out, Place({ -X, -Y, 0 }), Place({  X, -Y, 0 }), Place({  X, -Y, Z }), Place({ -X, -Y, Z }), Faces[5]);
}

void AppendPanelBottom(std::vector<CourseTriangle>& Out,
                       float ExtentX, float ExtentY, float ExtentZ,
                       uint32_t OuterMaterial, uint32_t InnerMaterial,
                       Vec3 Position, float Yaw)
{
    const std::array<uint32_t, 6> Faces = {
        OuterMaterial, OuterMaterial, OuterMaterial, OuterMaterial, OuterMaterial, InnerMaterial
    };
    AppendBoxBottom(Out, ExtentX, ExtentY, ExtentZ, OuterMaterial, Position, Yaw, &Faces);
}

void AppendCornerBox(std::vector<CourseTriangle>& Out, Vec3 MinCorner, Vec3 MaxCorner, uint32_t Material)
{
    const float X0 = MinCorner[0], Y0 = MinCorner[1], Z0 = MinCorner[2];
    const float X1 = MaxCorner[0], Y1 = MaxCorner[1], Z1 = MaxCorner[2];
    EmitQuad(Out, { X0, Y0, Z1 }, { X1, Y0, Z1 }, { X1, Y1, Z1 }, { X0, Y1, Z1 }, Material);
    EmitQuad(Out, { X0, Y1, Z0 }, { X1, Y1, Z0 }, { X1, Y0, Z0 }, { X0, Y0, Z0 }, Material);
    EmitQuad(Out, { X1, Y0, Z0 }, { X1, Y1, Z0 }, { X1, Y1, Z1 }, { X1, Y0, Z1 }, Material);
    EmitQuad(Out, { X0, Y1, Z0 }, { X0, Y0, Z0 }, { X0, Y0, Z1 }, { X0, Y1, Z1 }, Material);
    EmitQuad(Out, { X1, Y1, Z0 }, { X0, Y1, Z0 }, { X0, Y1, Z1 }, { X1, Y1, Z1 }, Material);
    EmitQuad(Out, { X0, Y0, Z0 }, { X1, Y0, Z0 }, { X1, Y0, Z1 }, { X0, Y0, Z1 }, Material);
}

// 📝 Convex (Y, Z) profile extruded along X over Length centred on X = 0, matching EmitExtrusionFacets.
void AppendExtrusion(std::vector<CourseTriangle>&                Out,
                     const std::vector<std::array<float, 2>>&    Profile,
                     float                                       Length,
                     uint32_t                                    Material,
                     Vec3                                        Position,
                     float                                       Yaw)
{
    const float HalfX = 0.5f * Length;
    const auto Place = [&](Vec3 Local) -> Vec3 {
        return Add(Position, Yaw == 0.0f ? Local : RotateZ(Local, Yaw));
    };

    const size_t Count = Profile.size();
    for (size_t Index = 0; Index < Count; ++Index)
    {
        const auto [Y0, Z0] = Profile[Index];
        const auto [Y1, Z1] = Profile[(Index + 1u) % Count];
        EmitQuad(Out,
                 Place({ -HalfX, Y0, Z0 }),
                 Place({ -HalfX, Y1, Z1 }),
                 Place({  HalfX, Y1, Z1 }),
                 Place({  HalfX, Y0, Z0 }),
                 Material);
    }
    for (size_t Index = 1; Index + 1u < Count; ++Index)
    {
        EmitFlatTriangle(Out,
                         Place({ HalfX, Profile[0][0], Profile[0][1] }),
                         Place({ HalfX, Profile[Index][0], Profile[Index][1] }),
                         Place({ HalfX, Profile[Index + 1u][0], Profile[Index + 1u][1] }),
                         Material);
        EmitFlatTriangle(Out,
                         Place({ -HalfX, Profile[0][0], Profile[0][1] }),
                         Place({ -HalfX, Profile[Index + 1u][0], Profile[Index + 1u][1] }),
                         Place({ -HalfX, Profile[Index][0], Profile[Index][1] }),
                         Material);
    }
}

void AppendPrismBottom(std::vector<CourseTriangle>& Out,
                       float Radius, float Height, int Sides,
                       uint32_t Material, Vec3 Position, float Yaw = 0.0f)
{
    const auto Place = [&](Vec3 Local) -> Vec3 {
        return Add(Position, Yaw == 0.0f ? Local : RotateZ(Local, Yaw));
    };
    for (int Side = 0; Side < Sides; ++Side)
    {
        const float A0 = 2.0f * Pi * static_cast<float>(Side) / static_cast<float>(Sides);
        const float A1 = 2.0f * Pi * static_cast<float>(Side + 1) / static_cast<float>(Sides);
        const Vec3  P0 = { Radius * std::cos(A0), Radius * std::sin(A0), 0.0f };
        const Vec3  P1 = { Radius * std::cos(A1), Radius * std::sin(A1), 0.0f };
        EmitQuad(Out,
                 Place({ P0[0], P0[1], 0.0f }),
                 Place({ P1[0], P1[1], 0.0f }),
                 Place({ P1[0], P1[1], Height }),
                 Place({ P0[0], P0[1], Height }),
                 Material);
        EmitFlatTriangle(Out, Place({ 0.0f, 0.0f, Height }), Place({ P0[0], P0[1], Height }), Place({ P1[0], P1[1], Height }), Material);
        EmitFlatTriangle(Out, Place({ 0.0f, 0.0f, 0.0f }), Place({ P1[0], P1[1], 0.0f }), Place({ P0[0], P0[1], 0.0f }), Material);
    }
}

void AppendCylinderBetween(std::vector<CourseTriangle>& Out,
                           Vec3 From, Vec3 To, float Radius, uint32_t Material, int Sides = 14)
{
    const Vec3 Axis   = Normalize(Sub(To, From));
    const Vec3 ReferenceAxis = std::fabs(Axis[2]) < 0.9f ? Vec3{ 0.0f, 0.0f, 1.0f } : Vec3{ 1.0f, 0.0f, 0.0f };
    const Vec3 U             = Normalize(Cross(Axis, ReferenceAxis));
    const Vec3 V      = Cross(Axis, U);
    for (int Index = 0; Index < Sides; ++Index)
    {
        const float T0 = 2.0f * Pi * static_cast<float>(Index) / static_cast<float>(Sides);
        const float T1 = 2.0f * Pi * static_cast<float>(Index + 1) / static_cast<float>(Sides);
        const Vec3  R0 = Add(Scale(U, std::cos(T0)), Scale(V, std::sin(T0)));
        const Vec3  R1 = Add(Scale(U, std::cos(T1)), Scale(V, std::sin(T1)));
        const Vec3  A  = Add(From, Scale(R0, Radius));
        const Vec3  B  = Add(From, Scale(R1, Radius));
        const Vec3  C  = Add(To, Scale(R1, Radius));
        const Vec3  D  = Add(To, Scale(R0, Radius));
        Out.push_back(CourseTriangle{ A, B, C, R0, R1, R1, Material });
        Out.push_back(CourseTriangle{ A, C, D, R0, R1, R0, Material });
        EmitOrientedTriangle(Out, From, B, A, Material, Scale(Axis, -1.0f));
        EmitOrientedTriangle(Out, To, D, C, Material, Axis);
    }
}

// 📝 EN-13422 750 mm traffic cone with 2 retroreflective white collars, matching CourseStructure.js EmitConeFacets.
void AppendTrafficCone(std::vector<CourseTriangle>& Out, Vec3 Position)
{
    constexpr float BaseHalf   = 0.19f;
    constexpr float BaseHeight = 0.036f;
    constexpr float BottomRad  = 0.145f;
    constexpr float TopRad     = 0.036f;
    constexpr float BodyTop    = 0.700f;
    constexpr float TipTop     = 0.716f;
    constexpr int   Sides      = 14;

    AppendBoxBottom(Out, 2.0f * BaseHalf, 2.0f * BaseHalf, BaseHeight,
                    static_cast<uint32_t>(CourseMaterialId::Tyre), Position, 0.0f);

    struct Band
    {
        float    Z0;
        float    Z1;
        uint32_t Material;
    };
    constexpr std::array<Band, 5> Bands = {{
        { BaseHeight, 0.250f, static_cast<uint32_t>(CourseMaterialId::ConeOrange) },
        { 0.250f,     0.350f, static_cast<uint32_t>(CourseMaterialId::ConeCollar) },
        { 0.350f,     0.445f, static_cast<uint32_t>(CourseMaterialId::ConeOrange) },
        { 0.445f,     0.595f, static_cast<uint32_t>(CourseMaterialId::ConeCollar) },
        { 0.595f,     BodyTop, static_cast<uint32_t>(CourseMaterialId::ConeOrange) }
    }};

    const auto RadiusAt = [&](float Z) -> float {
        const float T = std::clamp((Z - BaseHeight) / (BodyTop - BaseHeight), 0.0f, 1.0f);
        return BottomRad + (TopRad - BottomRad) * T;
    };
    const float Slope = (BottomRad - TopRad) / (BodyTop - BaseHeight);

    for (int Side = 0; Side < Sides; ++Side)
    {
        const float A0 = 2.0f * Pi * static_cast<float>(Side) / static_cast<float>(Sides);
        const float A1 = 2.0f * Pi * static_cast<float>(Side + 1) / static_cast<float>(Sides);
        const Vec3  N0 = Normalize({ std::cos(A0), std::sin(A0), Slope });
        const Vec3  N1 = Normalize({ std::cos(A1), std::sin(A1), Slope });

        for (const Band& Layer : Bands)
        {
            const float R0  = RadiusAt(Layer.Z0);
            const float R1  = RadiusAt(Layer.Z1);
            const Vec3  P00 = Add(Position, { R0 * std::cos(A0), R0 * std::sin(A0), Layer.Z0 });
            const Vec3  P10 = Add(Position, { R0 * std::cos(A1), R0 * std::sin(A1), Layer.Z0 });
            const Vec3  P01 = Add(Position, { R1 * std::cos(A0), R1 * std::sin(A0), Layer.Z1 });
            const Vec3  P11 = Add(Position, { R1 * std::cos(A1), R1 * std::sin(A1), Layer.Z1 });
            Out.push_back(CourseTriangle{ P00, P10, P11, N0, N1, N1, Layer.Material });
            Out.push_back(CourseTriangle{ P00, P11, P01, N0, N1, N0, Layer.Material });
        }

        const Vec3 Top0 = Add(Position, { TopRad * std::cos(A0), TopRad * std::sin(A0), BodyTop });
        const Vec3 Top1 = Add(Position, { TopRad * std::cos(A1), TopRad * std::sin(A1), BodyTop });
        const Vec3 Tip  = Add(Position, { 0.0f, 0.0f, TipTop });
        Out.push_back(CourseTriangle{ Top0, Top1, Tip, N0, N1, { 0.0f, 0.0f, 1.0f },
                                      static_cast<uint32_t>(CourseMaterialId::ConeOrange) });
    }
}

void AppendTyreStackWall(std::vector<CourseTriangle>& Out,
                         int Count, float Spacing, float Radius, float Height,
                         Vec3 Position, float Yaw)
{
    for (int Stack = 0; Stack < Count; ++Stack)
    {
        const float OffsetX = (static_cast<float>(Stack) - 0.5f * static_cast<float>(Count - 1)) * Spacing;
        const Vec3  Base    = Add(Position, RotateZ({ OffsetX, 0.0f, 0.0f }, Yaw));
        AppendPrismBottom(Out, Radius, 0.75f * Height, 12, static_cast<uint32_t>(CourseMaterialId::Tyre), Base, Yaw);
        AppendPrismBottom(Out, Radius, 0.25f * Height, 12, static_cast<uint32_t>(CourseMaterialId::PaintWhite),
                          Add(Base, { 0.0f, 0.0f, 0.75f * Height }), Yaw);
    }
}

void AppendOrientedBox(std::vector<CourseTriangle>& Out,
                       Vec3 Centre, Vec3 HalfExtents,
                       const std::array<Vec3, 3>& Axes,
                       uint32_t Material)
{
    const auto Combine = [&](float Wx, float Wy, float Wz) -> Vec3 {
        return Add(Centre, Add(Add(Scale(Axes[0], Wx), Scale(Axes[1], Wy)), Scale(Axes[2], Wz)));
    };
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        for (float Sign : { -1.0f, 1.0f })
        {
            const int U = (Axis + 1) % 3;
            const int V = (Axis + 2) % 3;
            const auto Corner = [&](float Su, float Sv) -> Vec3 {
                float Weights[3] = { 0.0f, 0.0f, 0.0f };
                Weights[Axis]    = Sign * HalfExtents[static_cast<size_t>(Axis)];
                Weights[U]       = Su * HalfExtents[static_cast<size_t>(U)];
                Weights[V]       = Sv * HalfExtents[static_cast<size_t>(V)];
                return Combine(Weights[0], Weights[1], Weights[2]);
            };
            const Vec3 Outward = Scale(Axes[static_cast<size_t>(Axis)], Sign);
            const Vec3 P0 = Corner(-1.0f, -1.0f);
            const Vec3 P1 = Corner( 1.0f, -1.0f);
            const Vec3 P2 = Corner( 1.0f,  1.0f);
            const Vec3 P3 = Corner(-1.0f,  1.0f);
            EmitOrientedTriangle(Out, P0, P1, P2, Material, Outward);
            EmitOrientedTriangle(Out, P0, P2, P3, Material, Outward);
        }
    }
}

// 📝 Slate 3c987f3 cockpit + steering wheel + hexagonal cluster + centre tablet + 3 mirrors (CabinStructure.js).
void AppendControlVehicleCabin(std::vector<CourseTriangle>& Out)
{
    const float Rebase = ControlVehicleConstants::CoMAuthoredZ();
    const auto  BodyPt = [&](float X, float Y, float Z) -> Vec3 { return { X, Y, Z - Rebase }; };

    const auto CabinBox = [&](Vec3 Centre, Vec3 Size, CourseMaterialId Mat) {
        AppendBoxBottom(Out, Size[0], Size[1], Size[2], static_cast<uint32_t>(Mat),
                        { Centre[0], Centre[1], Centre[2] - Rebase }, 0.0f);
    };

    // Dashboard, centre tunnel, seat cushions, seat backs, floorpan (SlateCabinLayout.js)
    CabinBox({  1.24f,  0.00f,  0.52f }, { 0.20f, 1.98f, 0.17f }, CourseMaterialId::CabinTrim);
    CabinBox({  0.30f,  0.00f,  0.05f }, { 1.28f, 0.28f, 0.30f }, CourseMaterialId::CabinTrim);
    CabinBox({ -0.50f,  0.50f,  0.03f }, { 0.65f, 0.49f, 0.15f }, CourseMaterialId::CabinSeat);
    CabinBox({ -0.50f, -0.50f,  0.03f }, { 0.65f, 0.49f, 0.15f }, CourseMaterialId::CabinSeat);
    CabinBox({ -0.83f,  0.50f,  0.16f }, { 0.14f, 0.52f, 0.69f }, CourseMaterialId::CabinSeat);
    CabinBox({ -0.83f, -0.50f,  0.16f }, { 0.14f, 0.52f, 0.69f }, CourseMaterialId::CabinSeat);
    CabinBox({  0.00f,  0.00f, -0.05f }, { 2.10f, 1.90f, 0.07f }, CourseMaterialId::CabinTrim);

    // Steering wheel toroidal rim + spokes + hexagonal cluster (SlateCabinFittings.js)
    const auto PosePoint = [&](Vec3 Centre, float Tilt, Vec3 Local) -> Vec3 {
        const float SinT = std::sin(Tilt);
        const float CosT = std::cos(Tilt);
        // Columns: right=(0, -1, 0), up=(SinT, 0, CosT), normal=(-CosT, 0, SinT)
        return {
            Centre[0] + SinT * Local[1] - CosT * Local[2],
            Centre[1] - Local[0],
            Centre[2] - Rebase + CosT * Local[1] + SinT * Local[2]
        };
    };

    constexpr Vec3  WheelCentre = { 0.68f, 0.42f, 0.82f };
    constexpr float WheelTilt   = 0.22f;
    constexpr float WheelRadius = 0.19f;
    constexpr float TubeRadius  = 0.018f;
    constexpr int   Sectors     = 36;
    constexpr int   TubeSides   = 8;

    const auto TorusPt = [&](int I, int J) -> Vec3 {
        const float A = 2.0f * Pi * static_cast<float>(I) / static_cast<float>(Sectors);
        const float B = 2.0f * Pi * static_cast<float>(J) / static_cast<float>(TubeSides);
        return PosePoint(WheelCentre, WheelTilt, {
            (WheelRadius + TubeRadius * std::cos(B)) * std::cos(A),
            (WheelRadius + TubeRadius * std::cos(B)) * std::sin(A),
            TubeRadius * std::sin(B)
        });
    };
    for (int I = 0; I < Sectors; ++I)
    {
        for (int J = 0; J < TubeSides; ++J)
        {
            EmitQuad(Out, TorusPt(I, J), TorusPt(I + 1, J), TorusPt(I + 1, J + 1), TorusPt(I, J + 1),
                     static_cast<uint32_t>(CourseMaterialId::SteeringWheel));
        }
    }
    EmitQuad(Out,
             PosePoint(WheelCentre, WheelTilt, { -0.17f, -0.018f, 0.0f }),
             PosePoint(WheelCentre, WheelTilt, {  0.17f, -0.018f, 0.0f }),
             PosePoint(WheelCentre, WheelTilt, {  0.17f,  0.018f, 0.0f }),
             PosePoint(WheelCentre, WheelTilt, { -0.17f,  0.018f, 0.0f }),
             static_cast<uint32_t>(CourseMaterialId::SteeringWheel));

    constexpr std::array<std::array<float, 2>, 6> Hex = {{
        { -0.62f, -1.0f }, { 0.62f, -1.0f }, { 1.0f, 0.0f },
        {  0.62f,  1.0f }, { -0.62f, 1.0f }, { -1.0f, 0.0f }
    }};
    for (size_t I = 0; I < Hex.size(); ++I)
    {
        const auto A = Hex[I];
        const auto B = Hex[(I + 1u) % Hex.size()];
        EmitFlatTriangle(Out,
                         PosePoint(WheelCentre, WheelTilt, { 0.0f, 0.0f, 0.023f }),
                         PosePoint(WheelCentre, WheelTilt, { A[0] * 0.135f, A[1] * 0.0755f, 0.023f }),
                         PosePoint(WheelCentre, WheelTilt, { B[0] * 0.135f, B[1] * 0.0755f, 0.023f }),
                         static_cast<uint32_t>(CourseMaterialId::DisplayGlass));
    }

    // Centre HMI Tablet (0.30 x 0.1875 tilted 0.42 rad)
    constexpr Vec3  TabletCentre = { 0.98f, 0.0f, 0.62f };
    constexpr float TabletTilt   = 0.42f;
    constexpr float TabHalfW     = 0.15f;
    constexpr float TabHalfH     = 0.09375f;
    EmitQuad(Out,
             PosePoint(TabletCentre, TabletTilt, { -TabHalfW - 0.012f, -TabHalfH - 0.012f, 0.008f }),
             PosePoint(TabletCentre, TabletTilt, {  TabHalfW + 0.012f, -TabHalfH - 0.012f, 0.008f }),
             PosePoint(TabletCentre, TabletTilt, {  TabHalfW + 0.012f,  TabHalfH + 0.012f, 0.008f }),
             PosePoint(TabletCentre, TabletTilt, { -TabHalfW - 0.012f,  TabHalfH + 0.012f, 0.008f }),
             static_cast<uint32_t>(CourseMaterialId::TabletHousing));
    EmitQuad(Out,
             PosePoint(TabletCentre, TabletTilt, { -TabHalfW, -TabHalfH, 0.014f }),
             PosePoint(TabletCentre, TabletTilt, {  TabHalfW, -TabHalfH, 0.014f }),
             PosePoint(TabletCentre, TabletTilt, {  TabHalfW,  TabHalfH, 0.014f }),
             PosePoint(TabletCentre, TabletTilt, { -TabHalfW,  TabHalfH, 0.014f }),
             static_cast<uint32_t>(CourseMaterialId::DisplayGlass));

    // Left/Right wing mirrors + Rear-view mirror (CabinStructure.js)
    const Vec3 DriverEye = BodyPt(-0.20f, 0.42f, 1.12f);
    struct MirrorSpec
    {
        bool  RearHeader;
        Vec3  CentreModel;
        Vec3  Reflected;
        float Width;
        float Height;
    };
    const std::array<MirrorSpec, 3> Mirrors = {{
        { false, { 1.02f,  1.33f, 0.86f }, { -1.0f,  0.06f, -0.03f  }, 0.19f, 0.105f },
        { false, { 1.02f, -1.33f, 0.86f }, { -1.0f, -0.06f, -0.03f  }, 0.19f, 0.105f },
        { true,  { 0.62f,  0.00f, 1.20f }, { -1.0f,  0.00f, -0.035f }, 0.26f, 0.075f }
    }};
    for (const MirrorSpec& Mirror : Mirrors)
    {
        const Vec3  Centre   = BodyPt(Mirror.CentreModel[0], Mirror.CentreModel[1], Mirror.CentreModel[2]);
        const Vec3  Incoming = Normalize(Sub(Centre, DriverEye));
        const Vec3  Normal   = Normalize(Sub(Normalize(Mirror.Reflected), Incoming));
        const Vec3  Right    = Normalize(Cross({ 0.0f, 0.0f, 1.0f }, Normal));
        const Vec3  Up       = Cross(Normal, Right);
        const float Depth    = Mirror.RearHeader ? 0.018f : 0.05f;
        const Vec3  Housing  = Add(Centre, Scale(Normal, -Depth - 0.0015f));
        AppendOrientedBox(Out, Housing,
                          { Mirror.Width * 0.5f + 0.012f, Mirror.Height * 0.5f + 0.012f, Depth },
                          { Right, Up, Normal },
                          static_cast<uint32_t>(Mirror.RearHeader ? CourseMaterialId::MirrorTrim : CourseMaterialId::BodyCoat));

        const auto GlassCorner = [&](float Sx, float Sy) -> Vec3 {
            return Add(Add(Centre, Scale(Right, Sx * Mirror.Width * 0.5f)), Scale(Up, Sy * Mirror.Height * 0.5f));
        };
        const Vec3 G0 = GlassCorner(-1.0f, -1.0f);
        const Vec3 G1 = GlassCorner( 1.0f, -1.0f);
        const Vec3 G2 = GlassCorner( 1.0f,  1.0f);
        const Vec3 G3 = GlassCorner(-1.0f,  1.0f);
        EmitOrientedTriangle(Out, G0, G1, G2, static_cast<uint32_t>(CourseMaterialId::MirrorGlass), Normal);
        EmitOrientedTriangle(Out, G0, G2, G3, static_cast<uint32_t>(CourseMaterialId::MirrorGlass), Normal);

        if (Mirror.RearHeader)
        {
            AppendCylinderBetween(Out, Add(Centre, Scale(Normal, -0.03f)), Add(Centre, { 0.03f, 0.0f, 0.11f }),
                                  0.012f, static_cast<uint32_t>(CourseMaterialId::MirrorTrim), 10);
        }
        else
        {
            const float Side = Centre[1] >= 0.0f ? 1.0f : -1.0f;
            AppendOrientedBox(Out, Add(Centre, { 0.035f, -Side * 0.12f, -0.05f }),
                              { 0.06f, 0.07f, 0.022f },
                              { Vec3{ 1.0f, 0.0f, 0.0f }, Vec3{ 0.0f, 1.0f, 0.0f }, Vec3{ 0.0f, 0.0f, 1.0f } },
                              static_cast<uint32_t>(CourseMaterialId::BodyCoat));
        }
    }
}

void AppendRoofFixture(std::vector<CourseTriangle>& Out, bool RallyPod)
{
    const float Rebase = ControlVehicleConstants::CoMAuthoredZ();
    const auto  Box = [&](Vec3 Centre, Vec3 Size, CourseMaterialId Mat) {
        AppendBoxBottom(Out, Size[0], Size[1], Size[2], static_cast<uint32_t>(Mat),
                        { Centre[0], Centre[1], Centre[2] - Rebase }, 0.0f);
    };
    if (!RallyPod)
    {
        Box({ 0.25f,  0.44f, 1.35f }, { 0.10f, 0.04f, 0.05f }, CourseMaterialId::SteelDark);
        Box({ 0.25f, -0.44f, 1.35f }, { 0.10f, 0.04f, 0.05f }, CourseMaterialId::SteelDark);
        Box({ 0.25f,  0.00f, 1.38f }, { 0.12f, 0.94f, 0.07f }, CourseMaterialId::VehiclePlastic);
        Box({ 0.31f,  0.00f, 1.38f }, { 0.02f, 0.88f, 0.05f }, CourseMaterialId::Headlight);
    }
    else
    {
        for (float Y : { 0.32f, -0.32f })
        {
            Box({ 0.25f, Y, 1.35f }, { 0.12f, 0.08f, 0.05f }, CourseMaterialId::SteelDark);
            Box({ 0.25f, Y, 1.40f }, { 0.16f, 0.20f, 0.16f }, CourseMaterialId::VehiclePlastic);
            Box({ 0.33f, Y, 1.40f }, { 0.03f, 0.16f, 0.14f }, CourseMaterialId::Headlight);
        }
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  COURSE MATERIAL TABLE
//------------------------------------------------------------------------------------------------------------------------

std::array<CourseMaterialSpecification, 82> ConstructCourseMaterials(std::array<float, 3> BodyPaintOverride) noexcept
{
    std::array<CourseMaterialSpecification, 82> Table{};

    const auto Set = [&](CourseMaterialId Id, Vec3 Albedo, float Metal, float Rough,
                         Vec3 Emission = { 0.0f, 0.0f, 0.0f },
                         float CoatWeight = 0.0f, float CoatRough = 0.08f, float Transmission = 0.0f) {
        Table[static_cast<size_t>(Id)] = CourseMaterialSpecification{
            Albedo, Metal, Rough, Emission, CoatWeight, CoatRough, Transmission
        };
    };

    Set(CourseMaterialId::Default,           { 0.50f, 0.50f, 0.50f }, 0.00f, 0.60f);
    Set(CourseMaterialId::VehicleBodyLegacy, { 0.55f, 0.12f, 0.10f }, 0.10f, 0.30f);
    Set(CourseMaterialId::AsphaltLegacy,     { 0.055f, 0.056f, 0.060f }, 0.00f, 0.82f);
    Set(CourseMaterialId::ConcreteLegacy,    { 0.38f, 0.37f, 0.35f }, 0.00f, 0.72f);
    Set(CourseMaterialId::MarkingLegacy,     { 0.74f, 0.74f, 0.72f }, 0.00f, 0.55f);
    Set(CourseMaterialId::EmitterLegacy,     { 0.90f, 0.90f, 0.90f }, 0.00f, 0.20f, { 12.0f, 11.0f, 9.5f });

    // ControlVehicle exact linear Rec.709 palette (MaterialSpecification.js)
    const Vec3 DefaultBodyCoat = { 0.680005f * 0.78f, 0.800458f * 0.78f, 0.0f };
    const Vec3 BodyCoatAlbedo  = BodyPaintOverride[0] >= 0.0f ? BodyPaintOverride : DefaultBodyCoat;
    Set(CourseMaterialId::BodyCoat,       BodyCoatAlbedo,          0.00f, 0.0775f, { 0.0f, 0.0f, 0.0f }, 1.0f, 0.03f, 0.0f);
    Set(CourseMaterialId::VehicleGlass,   { 0.026f, 0.028f, 0.030f }, 0.00f, 0.02f,  { 0.0f, 0.0f, 0.0f }, 0.0f, 0.08f, 1.0f);
    Set(CourseMaterialId::VehiclePlastic, { 0.055f, 0.055f, 0.058f }, 0.00f, 0.55f);
    Set(CourseMaterialId::Tyre,           { 0.040f, 0.041f, 0.044f }, 0.00f, 0.78f);
    Set(CourseMaterialId::Hub,            { 0.060f, 0.220f, 0.620f }, 1.00f, 0.22f,  { 0.0f, 0.03327756f, 0.12f });
    Set(CourseMaterialId::Brake,          { 0.340f, 0.350f, 0.370f }, 0.90f, 0.36f);
    Set(CourseMaterialId::VehicleTrim,    { 0.180f, 0.190f, 0.210f }, 0.85f, 0.28f);
    Set(CourseMaterialId::Headlight,      { 0.900f, 0.920f, 0.960f }, 0.00f, 0.12f,  { 48.7f, 48.7f, 48.7f });
    Set(CourseMaterialId::Taillight,      { 0.550f, 0.020f, 0.020f }, 0.00f, 0.16f,  { 6.5f, 0.0f, 0.107f });
    Set(CourseMaterialId::Amber,          { 0.700f, 0.280f, 0.020f }, 0.00f, 0.18f,  { 6.5f, 1.13f, 0.0f });

    // Proving Grounds surfaces & structures
    Set(CourseMaterialId::Asphalt,        { 0.055f, 0.056f, 0.060f }, 0.00f, 0.82f);
    Set(CourseMaterialId::AsphaltPatch,   { 0.042f, 0.043f, 0.047f }, 0.00f, 0.88f);
    Set(CourseMaterialId::PaintWhite,     { 0.740f, 0.740f, 0.720f }, 0.00f, 0.55f);
    Set(CourseMaterialId::PaintYellow,    { 0.680f, 0.500f, 0.070f }, 0.00f, 0.55f);
    Set(CourseMaterialId::KerbRed,        { 0.520f, 0.065f, 0.055f }, 0.00f, 0.62f);
    Set(CourseMaterialId::KerbWhite,      { 0.720f, 0.720f, 0.700f }, 0.00f, 0.62f);
    Set(CourseMaterialId::Grass,          { 0.058f, 0.105f, 0.042f }, 0.00f, 0.92f);
    Set(CourseMaterialId::Jersey,         { 0.420f, 0.410f, 0.390f }, 0.00f, 0.76f);
    Set(CourseMaterialId::ConeOrange,     { 0.720f, 0.160f, 0.020f }, 0.00f, 0.46f);
    Set(CourseMaterialId::ConeCollar,     { 0.840f, 0.840f, 0.860f }, 0.00f, 0.28f);
    Set(CourseMaterialId::HangarWall,     { 0.340f, 0.350f, 0.370f }, 0.25f, 0.58f);
    Set(CourseMaterialId::HangarRoof,     { 0.240f, 0.250f, 0.270f }, 0.45f, 0.52f);
    Set(CourseMaterialId::HangarCrimson,  { 0.460f, 0.055f, 0.060f }, 0.00f, 0.64f);
    Set(CourseMaterialId::HangarChalk,    { 0.680f, 0.670f, 0.640f }, 0.00f, 0.68f);
    Set(CourseMaterialId::LampStrip,      { 0.900f, 0.930f, 0.980f }, 0.00f, 0.15f,  { 16.0f, 18.0f, 22.0f });
    Set(CourseMaterialId::TunnelWall,     { 0.300f, 0.300f, 0.310f }, 0.00f, 0.74f);
    Set(CourseMaterialId::TunnelCobalt,   { 0.055f, 0.130f, 0.420f }, 0.00f, 0.62f);
    Set(CourseMaterialId::LampWarm,       { 0.950f, 0.800f, 0.550f }, 0.00f, 0.18f,  { 18.0f, 12.5f, 6.2f });
    Set(CourseMaterialId::TowerConcrete,  { 0.460f, 0.450f, 0.430f }, 0.00f, 0.70f);
    Set(CourseMaterialId::TowerGlass,     { 0.045f, 0.085f, 0.110f }, 0.00f, 0.06f,  { 0.0f, 0.0f, 0.0f }, 0.0f, 0.08f, 0.75f);
    Set(CourseMaterialId::Pole,           { 0.220f, 0.230f, 0.250f }, 0.75f, 0.42f);
    Set(CourseMaterialId::PoleHead,       { 0.920f, 0.860f, 0.720f }, 0.00f, 0.18f,  { 14.0f, 12.0f, 8.8f });
    Set(CourseMaterialId::TrafficHead,    { 0.050f, 0.050f, 0.055f }, 0.10f, 0.52f);
    Set(CourseMaterialId::SignalRed,      { 0.650f, 0.030f, 0.025f }, 0.00f, 0.16f,  { 14.0f, 0.45f, 0.30f });
    Set(CourseMaterialId::SignalAmber,    { 0.700f, 0.340f, 0.020f }, 0.00f, 0.16f,  { 12.0f, 5.20f, 0.25f });
    Set(CourseMaterialId::SignalGreen,    { 0.040f, 0.580f, 0.160f }, 0.00f, 0.16f,  { 0.55f, 13.5f, 2.60f });
    Set(CourseMaterialId::TrafficPost,    { 0.180f, 0.190f, 0.200f }, 0.70f, 0.44f);
    Set(CourseMaterialId::RampDeck,       { 0.180f, 0.200f, 0.230f }, 0.35f, 0.58f);
    Set(CourseMaterialId::RampSide,       { 0.620f, 0.440f, 0.055f }, 0.10f, 0.50f);
    Set(CourseMaterialId::CrateWood,      { 0.360f, 0.230f, 0.110f }, 0.00f, 0.76f);
    Set(CourseMaterialId::CrateBand,      { 0.140f, 0.140f, 0.150f }, 0.65f, 0.48f);
    Set(CourseMaterialId::PaneGlass,      { 0.060f, 0.120f, 0.140f }, 0.00f, 0.04f,  { 0.0f, 0.0f, 0.0f }, 0.0f, 0.08f, 0.88f);
    Set(CourseMaterialId::PaneFrame,      { 0.160f, 0.170f, 0.180f }, 0.75f, 0.38f);
    Set(CourseMaterialId::PillarConcrete, { 0.400f, 0.390f, 0.370f }, 0.00f, 0.76f);
    Set(CourseMaterialId::TemperedGlass,  { 0.050f, 0.110f, 0.100f }, 0.00f, 0.05f,  { 0.0f, 0.0f, 0.0f }, 0.0f, 0.08f, 0.86f);
    Set(CourseMaterialId::BarrierShell,   { 0.680f, 0.150f, 0.035f }, 0.00f, 0.48f);
    Set(CourseMaterialId::WallStone,      { 0.330f, 0.310f, 0.280f }, 0.00f, 0.82f);
    Set(CourseMaterialId::BlockConcrete,  { 0.430f, 0.420f, 0.400f }, 0.00f, 0.75f);
    Set(CourseMaterialId::TyreStack,      { 0.045f, 0.045f, 0.048f }, 0.00f, 0.80f);
    Set(CourseMaterialId::CabinetRed,     { 0.500f, 0.050f, 0.045f }, 0.35f, 0.34f);
    Set(CourseMaterialId::SteelDark,      { 0.120f, 0.125f, 0.135f }, 0.82f, 0.38f);
    Set(CourseMaterialId::StressCoral,    { 0.660f, 0.190f, 0.150f }, 0.00f, 0.46f);
    Set(CourseMaterialId::StressAmber,    { 0.680f, 0.440f, 0.090f }, 0.00f, 0.46f);
    Set(CourseMaterialId::StressTeal,     { 0.100f, 0.440f, 0.420f }, 0.00f, 0.46f);
    Set(CourseMaterialId::StressSlate,    { 0.240f, 0.280f, 0.350f }, 0.20f, 0.46f);
    Set(CourseMaterialId::CabinTrim,      { 0.048f, 0.050f, 0.055f }, 0.00f, 0.62f);
    Set(CourseMaterialId::CabinSeat,      { 0.070f, 0.072f, 0.078f }, 0.00f, 0.70f);
    Set(CourseMaterialId::SteeringWheel,  { 0.042f, 0.044f, 0.048f }, 0.10f, 0.48f);
    Set(CourseMaterialId::TabletHousing,  { 0.080f, 0.085f, 0.092f }, 0.75f, 0.30f);
    Set(CourseMaterialId::MirrorGlass,    { 0.920f, 0.940f, 0.960f }, 1.00f, 0.02f);
    Set(CourseMaterialId::MirrorTrim,     { 0.055f, 0.056f, 0.060f }, 0.10f, 0.50f);
    Set(CourseMaterialId::DisplayGlass,   { 0.060f, 0.140f, 0.220f }, 0.00f, 0.08f,  { 0.35f, 0.82f, 1.15f });
    Set(CourseMaterialId::LampPost,       { 0.150f, 0.155f, 0.165f }, 0.78f, 0.40f);
    Set(CourseMaterialId::LampLantern,    { 0.960f, 0.820f, 0.560f }, 0.00f, 0.16f,  { 18.0f, 13.5f, 7.5f });
    Set(CourseMaterialId::LampLanternCool,{ 0.840f, 0.920f, 1.000f }, 0.00f, 0.16f,  { 13.0f, 16.5f, 21.0f });
    Set(CourseMaterialId::RimBase,        { 0.120f, 0.126f, 0.138f }, 0.88f, 0.28f);

    return Table;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  FACET ARCHIVE DECODER
//------------------------------------------------------------------------------------------------------------------------

bool DecodeFacetArchiveFile(const std::filesystem::path& FilePath,
                            bool                         ForceFlatNormals,
                            DecodedFacetArchive&         ArchiveDelivery)
{
    ArchiveDelivery = {};
    std::ifstream Input(FilePath, std::ios::binary | std::ios::ate);
    if (!Input.is_open())
    {
        return false;
    }
    const std::streamsize ByteSize = Input.tellg();
    if (ByteSize < 48)
    {
        return false;
    }
    Input.seekg(0, std::ios::beg);
    std::vector<uint8_t> Buffer(static_cast<size_t>(ByteSize));
    if (!Input.read(reinterpret_cast<char*>(Buffer.data()), ByteSize))
    {
        return false;
    }

    if (std::memcmp(Buffer.data(), "FCT1", 4u) != 0)
    {
        return false;
    }

    uint32_t VertexCount = 0u;
    uint32_t FacetCount  = 0u;
    uint32_t Flags       = 0u;
    std::memcpy(&VertexCount, Buffer.data() + 4u, 4u);
    std::memcpy(&FacetCount,  Buffer.data() + 8u, 4u);
    std::memcpy(&Flags,       Buffer.data() + 12u, 4u);
    std::memcpy(ArchiveDelivery.BoundsMin.data(), Buffer.data() + 16u, 12u);
    std::memcpy(ArchiveDelivery.BoundsMax.data(), Buffer.data() + 28u, 12u);

    const bool   HasCornerNormals = (Flags & 1u) != 0u;
    const size_t PositionsBytes   = static_cast<size_t>(VertexCount) * 12u;
    const size_t IndicesBytes     = static_cast<size_t>(FacetCount) * 12u;
    const size_t FamiliesBytes    = static_cast<size_t>(FacetCount) * 4u;
    const size_t NormalsBytes     = HasCornerNormals ? static_cast<size_t>(FacetCount) * 36u : 0u;
    if (48u + PositionsBytes + IndicesBytes + FamiliesBytes + NormalsBytes > Buffer.size())
    {
        return false;
    }

    const float*    Positions = reinterpret_cast<const float*>(Buffer.data() + 48u);
    const uint32_t* Indices   = reinterpret_cast<const uint32_t*>(Buffer.data() + 48u + PositionsBytes);
    const uint32_t* Families  = reinterpret_cast<const uint32_t*>(Buffer.data() + 48u + PositionsBytes + IndicesBytes);
    const float*    Normals   = HasCornerNormals
        ? reinterpret_cast<const float*>(Buffer.data() + 48u + PositionsBytes + IndicesBytes + FamiliesBytes)
        : nullptr;

    ArchiveDelivery.VertexCount = VertexCount;
    ArchiveDelivery.FacetCount  = FacetCount;
    ArchiveDelivery.Smooth      = HasCornerNormals && !ForceFlatNormals;
    ArchiveDelivery.Triangles.reserve(FacetCount);

    for (uint32_t Facet = 0u; Facet < FacetCount; ++Facet)
    {
        const uint32_t I0 = Indices[Facet * 3u + 0u];
        const uint32_t I1 = Indices[Facet * 3u + 1u];
        const uint32_t I2 = Indices[Facet * 3u + 2u];
        if (I0 >= VertexCount || I1 >= VertexCount || I2 >= VertexCount)
        {
            continue;
        }
        const Vec3 A = { Positions[I0 * 3u + 0u], Positions[I0 * 3u + 1u], Positions[I0 * 3u + 2u] };
        const Vec3 B = { Positions[I1 * 3u + 0u], Positions[I1 * 3u + 1u], Positions[I1 * 3u + 2u] };
        const Vec3 C = { Positions[I2 * 3u + 0u], Positions[I2 * 3u + 1u], Positions[I2 * 3u + 2u] };

        CourseTriangle Tri{};
        Tri.A        = A;
        Tri.B        = B;
        Tri.C        = C;
        Tri.Material = std::min<uint32_t>(Families[Facet], 81u);

        if (Normals != nullptr && !ForceFlatNormals)
        {
            const float* Base = Normals + static_cast<size_t>(Facet) * 9u;
            Tri.NormalA = Normalize({ Base[0], Base[1], Base[2] });
            Tri.NormalB = Normalize({ Base[3], Base[4], Base[5] });
            Tri.NormalC = Normalize({ Base[6], Base[7], Base[8] });
        }
        else
        {
            const Vec3 Flat = Normalize(Cross(Sub(B, A), Sub(C, A)));
            Tri.NormalA = Flat;
            Tri.NormalB = Flat;
            Tri.NormalC = Flat;
        }
        ArchiveDelivery.Triangles.push_back(Tri);
    }

    return !ArchiveDelivery.Triangles.empty();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CONTROLVEHICLE ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

ControlVehicleAssembly ConstructControlVehicleAssembly(const std::filesystem::path& VehicleArchiveDirectory)
{
    ControlVehicleAssembly Assembly{};

    DecodedFacetArchive BodyArchive{};
    DecodedFacetArchive RimArchive{};
    DecodedFacetArchive TyreArchive{};
    const bool BodyOk = DecodeFacetArchiveFile(VehicleArchiveDirectory / "ControlVehicleBody.facet", true, BodyArchive);
    const bool RimOk  = DecodeFacetArchiveFile(VehicleArchiveDirectory / "ControlVehicleRim.facet",  true, RimArchive);
    const bool TyreOk = DecodeFacetArchiveFile(VehicleArchiveDirectory / "ControlVehicleTyre.facet", false, TyreArchive);

    const float Rebase = ControlVehicleConstants::CoMAuthoredZ();
    if (BodyOk)
    {
        Assembly.LoadedFromArchives = RimOk && TyreOk;
        Assembly.AuthoredBodyFacets = BodyArchive.FacetCount;
        Assembly.BodyOpaque.reserve(BodyArchive.Triangles.size() + 1400u);
        Assembly.BodyGlazing.reserve(96u);
        Assembly.SideHullSamples.reserve(512u);

        uint32_t SampleStride = 0u;
        for (CourseTriangle Tri : BodyArchive.Triangles)
        {
            Tri.A[2] -= Rebase;
            Tri.B[2] -= Rebase;
            Tri.C[2] -= Rebase;
            if ((SampleStride++ % 4u) == 0u)
            {
                Assembly.SideHullSamples.push_back({ Tri.A[0], Tri.A[2] });
            }
            if (Tri.Material == static_cast<uint32_t>(CourseMaterialId::VehicleGlass))
            {
                Assembly.BodyGlazing.push_back(Tri);
            }
            else
            {
                Assembly.BodyOpaque.push_back(Tri);
            }
        }
    }

    // Interior cockpit, wheel-mounted hexagonal cluster, centre HMI tablet, wing & rear mirrors
    AppendControlVehicleCabin(Assembly.BodyOpaque);
    AppendRoofFixture(Assembly.RoofSlimLed, false);
    AppendRoofFixture(Assembly.RoofRallyPod, true);

    // Wheel assembly: Tyre (smooth normals) + Rim (flat normals, RimScale = 0.34460 / 0.344818, Tyre -> RimBase remap)
    if (RimOk && TyreOk)
    {
        Assembly.AuthoredRimFacets  = RimArchive.FacetCount;
        Assembly.AuthoredTyreFacets = TyreArchive.FacetCount;

        const float RimScale = ControlVehicleConstants::TyreRimRadius / ControlVehicleConstants::AuthoredRimRadius;
        Assembly.WheelLeft.reserve(TyreArchive.Triangles.size() + RimArchive.Triangles.size());
        Assembly.WheelRight.reserve(TyreArchive.Triangles.size() + RimArchive.Triangles.size());

        const auto PushWheelTri = [&](CourseTriangle LeftTri) {
            Assembly.WheelLeft.push_back(LeftTri);

            // Right-hand wheel mirrors Y and swaps corners [B, C] so outward CCW winding holds (FacetStructure.js)
            CourseTriangle RightTri = LeftTri;
            RightTri.A[1]       = -LeftTri.A[1];
            RightTri.B          = { LeftTri.C[0], -LeftTri.C[1], LeftTri.C[2] };
            RightTri.C          = { LeftTri.B[0], -LeftTri.B[1], LeftTri.B[2] };
            RightTri.NormalA[1] = -LeftTri.NormalA[1];
            RightTri.NormalB    = { LeftTri.NormalC[0], -LeftTri.NormalC[1], LeftTri.NormalC[2] };
            RightTri.NormalC    = { LeftTri.NormalB[0], -LeftTri.NormalB[1], LeftTri.NormalB[2] };
            Assembly.WheelRight.push_back(RightTri);
        };

        for (const CourseTriangle& Tri : TyreArchive.Triangles)
        {
            PushWheelTri(Tri);
        }
        for (CourseTriangle Tri : RimArchive.Triangles)
        {
            Tri.A = Scale(Tri.A, RimScale);
            Tri.B = Scale(Tri.B, RimScale);
            Tri.C = Scale(Tri.C, RimScale);
            if (Tri.Material == static_cast<uint32_t>(CourseMaterialId::Tyre))
            {
                Tri.Material = static_cast<uint32_t>(CourseMaterialId::RimBase);
            }
            PushWheelTri(Tri);
        }
    }

    return Assembly;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PROVING GROUNDS SCENE
//------------------------------------------------------------------------------------------------------------------------

float QueryProvingGroundsCourseHeight(float WorldX, float WorldY) noexcept
{
    // Jump ramp deck: X in [22, 28], Y in [-2.6, 2.6], linear rise to 1.35 m
    if (WorldX >= 22.0f && WorldX <= 28.0f && std::fabs(WorldY) <= 2.6f)
    {
        return 1.35f * (WorldX - 22.0f) / 6.0f;
    }
    // Kerb strips: X in [-52, 20], Y in [±5.65, ±6.65], height 0.12 m
    if (WorldX >= -52.0f && WorldX <= 20.0f && std::fabs(std::fabs(WorldY) - 6.15f) <= 0.5f)
    {
        return 0.12f;
    }
    return 0.0f;
}

ProvingGroundsAssembly ConstructProvingGroundsAssembly()
{
    ProvingGroundsAssembly Out{};
    Out.StaticWorld.reserve(10500u);

    constexpr float PaintLift = 0.004f;

    // 1. Grass surround (480 m square at Z = -0.02 m) + Paved Asphalt Pad [-60..156] x [-60..60] (4 m cells, 12% patches)
    EmitQuad(Out.StaticWorld,
             { -240.0f, -240.0f, -0.02f },
             {  240.0f, -240.0f, -0.02f },
             {  240.0f,  240.0f, -0.02f },
             { -240.0f,  240.0f, -0.02f },
             static_cast<uint32_t>(CourseMaterialId::Grass));

    for (int Ix = 0; Ix < 54; ++Ix)
    {
        const float X0 = -60.0f + static_cast<float>(Ix) * 4.0f;
        const float X1 = X0 + 4.0f;
        for (int Iy = 0; Iy < 30; ++Iy)
        {
            const float Y0 = -60.0f + static_cast<float>(Iy) * 4.0f;
            const float Y1 = Y0 + 4.0f;
            const uint32_t Hash = (static_cast<uint32_t>(Ix) * 73856093u) ^ (static_cast<uint32_t>(Iy) * 19349663u);
            const CourseMaterialId Mat = (Hash % 100u) < 12u ? CourseMaterialId::AsphaltPatch : CourseMaterialId::Asphalt;
            EmitQuad(Out.StaticWorld, { X0, Y0, 0.0f }, { X1, Y0, 0.0f }, { X1, Y1, 0.0f }, { X0, Y1, 0.0f },
                     static_cast<uint32_t>(Mat));
        }
    }

    // 2. Painted lane lines, start-grid checkerboard, skidpad rings, burnout box (LevelStructure.js)
    const auto PaintRect = [&](float Cx, float Cy, float LenX, float WidY, CourseMaterialId Mat, float Yaw = 0.0f) {
        const float Hx = 0.5f * LenX;
        const float Hy = 0.5f * WidY;
        const auto Pt = [&](float X, float Y) -> Vec3 {
            const Vec3 R = Yaw == 0.0f ? Vec3{ X, Y, PaintLift } : RotateZ({ X, Y, PaintLift }, Yaw);
            return { Cx + R[0], Cy + R[1], PaintLift };
        };
        EmitQuad(Out.StaticWorld, Pt(-Hx, -Hy), Pt(Hx, -Hy), Pt(Hx, Hy), Pt(-Hx, Hy), static_cast<uint32_t>(Mat));
    };

    // Main straight edge lines & dashed yellow centre line
    PaintRect(46.0f,  5.6f, 200.0f, 0.18f, CourseMaterialId::PaintWhite);
    PaintRect(46.0f, -5.6f, 200.0f, 0.18f, CourseMaterialId::PaintWhite);
    for (float X = -50.0f; X <= 142.0f; X += 6.0f)
    {
        PaintRect(X, 0.0f, 2.8f, 0.14f, CourseMaterialId::PaintYellow);
    }

    // Start-line checkerboard at X = -34.0 m (directly ahead of SpawnPoint [-40, 0])
    for (int Row = 0; Row < 2; ++Row)
    {
        for (int Col = 0; Col < 16; ++Col)
        {
            if (((Row + Col) & 1) == 0)
            {
                const float X = -34.15f + static_cast<float>(Row) * 0.60f;
                const float Y = -4.50f + static_cast<float>(Col) * 0.60f;
                PaintRect(X, Y, 0.58f, 0.58f, CourseMaterialId::PaintWhite);
            }
        }
    }
    // Launch grid box around ControlVehicle at [-40, 0]
    PaintRect(-37.2f,  1.35f, 0.90f, 0.14f, CourseMaterialId::PaintWhite);
    PaintRect(-37.2f, -1.35f, 0.90f, 0.14f, CourseMaterialId::PaintWhite);
    PaintRect(-36.8f,  0.00f, 0.14f, 2.84f, CourseMaterialId::PaintWhite);

    // Skidpad circles at [-30, 30] (R = 16 m white, R = 8 m yellow)
    for (int Seg = 0; Seg < 48; ++Seg)
    {
        const float Angle = 2.0f * Pi * (static_cast<float>(Seg) + 0.5f) / 48.0f;
        PaintRect(-30.0f + 16.0f * std::cos(Angle), 30.0f + 16.0f * std::sin(Angle),
                  2.15f, 0.18f, CourseMaterialId::PaintWhite, Angle + 0.5f * Pi);
        PaintRect(-30.0f + 8.0f * std::cos(Angle), 30.0f + 8.0f * std::sin(Angle),
                  1.08f, 0.14f, CourseMaterialId::PaintYellow, Angle + 0.5f * Pi);
    }

    // Burnout box at [-30, -28]
    PaintRect(-30.0f, -23.0f, 18.0f, 0.18f, CourseMaterialId::PaintYellow);
    PaintRect(-30.0f, -33.0f, 18.0f, 0.18f, CourseMaterialId::PaintYellow);
    PaintRect(-39.0f, -28.0f, 0.18f, 10.0f, CourseMaterialId::PaintYellow);
    PaintRect(-21.0f, -28.0f, 0.18f, 10.0f, CourseMaterialId::PaintYellow);

    // 3. Alternating Red & White Kerb Strips along X in [-52, 20], Y = ±6.15 m
    const std::vector<std::array<float, 2>> KerbProfileLeft  = {{ { -0.50f, 0.0f }, { 0.50f, 0.0f }, { 0.50f, 0.12f }, { -0.30f, 0.12f } }};
    const std::vector<std::array<float, 2>> KerbProfileRight = {{ { -0.50f, 0.0f }, { 0.50f, 0.0f }, { 0.30f, 0.12f }, { -0.50f, 0.12f } }};
    for (int Seg = 0; Seg < 48; ++Seg)
    {
        const float Cx = -52.0f + (static_cast<float>(Seg) + 0.5f) * 1.5f;
        const CourseMaterialId Mat = (Seg & 1) == 0 ? CourseMaterialId::KerbRed : CourseMaterialId::KerbWhite;
        AppendExtrusion(Out.StaticWorld, KerbProfileLeft,  1.5f, static_cast<uint32_t>(Mat), { Cx,  6.15f, 0.0f }, 0.0f);
        AppendExtrusion(Out.StaticWorld, KerbProfileRight, 1.5f, static_cast<uint32_t>(Mat), { Cx, -6.15f, 0.0f }, 0.0f);
    }

    // 4. Jump Ramp at X in [22, 28], Y in [-2.6, 2.6], Rise = 1.35 m
    EmitQuad(Out.StaticWorld,
             { 22.0f, -2.6f, 0.0f }, { 28.0f, -2.6f, 1.35f }, { 28.0f,  2.6f, 1.35f }, { 22.0f,  2.6f, 0.0f },
             static_cast<uint32_t>(CourseMaterialId::RampDeck));
    EmitQuad(Out.StaticWorld,
             { 28.0f, -2.6f, 0.0f }, { 28.0f,  2.6f, 0.0f  }, { 28.0f,  2.6f, 1.35f }, { 28.0f, -2.6f, 1.35f },
             static_cast<uint32_t>(CourseMaterialId::RampSide));
    EmitFlatTriangle(Out.StaticWorld, { 22.0f,  2.6f, 0.0f }, { 28.0f,  2.6f, 1.35f }, { 28.0f,  2.6f, 0.0f },
                     static_cast<uint32_t>(CourseMaterialId::RampSide));
    EmitFlatTriangle(Out.StaticWorld, { 22.0f, -2.6f, 0.0f }, { 28.0f, -2.6f, 0.0f  }, { 28.0f, -2.6f, 1.35f },
                     static_cast<uint32_t>(CourseMaterialId::RampSide));

    // 5. Perimeter & Smash Alley Concrete Jersey Barriers (JerseyProfile)
    const std::vector<std::array<float, 2>> JerseyProfile = {{
        { -0.32f, 0.0f }, { 0.32f, 0.0f }, { 0.22f, 0.25f }, { 0.12f, 0.86f }, { -0.12f, 0.86f }, { -0.22f, 0.25f }
    }};
    for (float X = -54.0f; X <= 150.0f; X += 12.0f)
    {
        AppendExtrusion(Out.StaticWorld, JerseyProfile, 11.6f, static_cast<uint32_t>(CourseMaterialId::Jersey), { X,  59.2f, 0.0f }, 0.0f);
        AppendExtrusion(Out.StaticWorld, JerseyProfile, 11.6f, static_cast<uint32_t>(CourseMaterialId::Jersey), { X, -59.2f, 0.0f }, 0.0f);
    }
    for (float Y = -54.0f; Y <= 54.0f; Y += 12.0f)
    {
        AppendExtrusion(Out.StaticWorld, JerseyProfile, 11.6f, static_cast<uint32_t>(CourseMaterialId::Jersey), { -59.2f, Y, 0.0f }, 0.5f * Pi);
        AppendExtrusion(Out.StaticWorld, JerseyProfile, 11.6f, static_cast<uint32_t>(CourseMaterialId::Jersey), { 155.2f, Y, 0.0f }, 0.5f * Pi);
    }
    // Smash Alley lane dividers
    for (float X : { 6.0f, 16.0f, 26.0f, 36.0f })
    {
        AppendExtrusion(Out.StaticWorld, JerseyProfile, 8.6f, static_cast<uint32_t>(CourseMaterialId::Jersey), { X, -20.5f, 0.0f }, 0.0f);
        AppendExtrusion(Out.StaticWorld, JerseyProfile, 8.6f, static_cast<uint32_t>(CourseMaterialId::Jersey), { X, -36.5f, 0.0f }, 0.0f);
    }

    // 6. Hangar / Garage (X = 20..48, Y = 31..46, Z = 8 m) with crimson/chalk inner walls, slatted roof, cool lamp strips, red cabinets
    AppendPanelBottom(Out.StaticWorld, 28.4f, 0.45f, 8.0f,
                      static_cast<uint32_t>(CourseMaterialId::HangarWall),
                      static_cast<uint32_t>(CourseMaterialId::HangarCrimson),
                      { 34.0f, 46.0f, 0.0f }, 0.0f);
    AppendPanelBottom(Out.StaticWorld, 15.2f, 0.45f, 8.0f,
                      static_cast<uint32_t>(CourseMaterialId::HangarWall),
                      static_cast<uint32_t>(CourseMaterialId::HangarChalk),
                      { 48.0f, 38.6f, 0.0f }, -0.5f * Pi);
    AppendBoxBottom(Out.StaticWorld, 0.45f, 3.8f, 8.0f, static_cast<uint32_t>(CourseMaterialId::HangarWall), { 20.0f, 44.3f, 0.0f }, 0.0f);
    AppendBoxBottom(Out.StaticWorld, 0.45f, 3.8f, 8.0f, static_cast<uint32_t>(CourseMaterialId::HangarWall), { 20.0f, 32.9f, 0.0f }, 0.0f);
    for (int Slat = 0; Slat < 7; ++Slat)
    {
        const float SlatX = 21.8f + static_cast<float>(Slat) * 4.0f;
        AppendCornerBox(Out.StaticWorld, { SlatX - 1.55f, 31.0f, 7.85f }, { SlatX + 1.55f, 46.2f, 8.20f },
                        static_cast<uint32_t>(CourseMaterialId::HangarRoof));
    }
    for (float StripX : { 25.0f, 34.0f, 43.0f })
    {
        AppendCornerBox(Out.StaticWorld, { StripX - 1.2f, 34.5f, 7.68f }, { StripX + 1.2f, 42.5f, 7.84f },
                        static_cast<uint32_t>(CourseMaterialId::LampStrip));
    }
    AppendBoxBottom(Out.StaticWorld, 2.2f, 0.7f, 1.4f, static_cast<uint32_t>(CourseMaterialId::CabinetRed), { 45.2f, 44.8f, 0.0f }, 0.0f);
    AppendBoxBottom(Out.StaticWorld, 2.2f, 0.7f, 1.4f, static_cast<uint32_t>(CourseMaterialId::CabinetRed), { 41.8f, 44.8f, 0.0f }, 0.0f);

    // 7. South Tunnel (X = -26..2, Y = -48, cobalt/chalk walls & warm ceiling lamps)
    AppendPanelBottom(Out.StaticWorld, 28.0f, 0.55f, 5.2f,
                      static_cast<uint32_t>(CourseMaterialId::TunnelWall),
                      static_cast<uint32_t>(CourseMaterialId::TunnelCobalt),
                      { -12.0f, -43.8f, 0.0f }, Pi);
    AppendPanelBottom(Out.StaticWorld, 28.0f, 0.55f, 5.2f,
                      static_cast<uint32_t>(CourseMaterialId::TunnelWall),
                      static_cast<uint32_t>(CourseMaterialId::HangarChalk),
                      { -12.0f, -52.2f, 0.0f }, 0.0f);
    AppendCornerBox(Out.StaticWorld, { -26.0f, -52.5f, 5.2f }, { 2.0f, -43.5f, 5.65f },
                    static_cast<uint32_t>(CourseMaterialId::TunnelWall));
    for (float LampX : { -22.0f, -15.0f, -8.0f, -1.0f })
    {
        AppendCornerBox(Out.StaticWorld, { LampX - 0.9f, -48.6f, 5.05f }, { LampX + 0.9f, -47.4f, 5.20f },
                        static_cast<uint32_t>(CourseMaterialId::LampWarm));
    }

    // 8. Control Tower at [-54, -11] (concrete shaft + glazed observation deck + roof antenna)
    AppendBoxBottom(Out.StaticWorld, 4.4f, 4.4f, 9.0f, static_cast<uint32_t>(CourseMaterialId::TowerConcrete), { -54.0f, -11.0f, 0.0f }, 0.0f);
    AppendCornerBox(Out.StaticWorld, { -56.8f, -13.8f, 9.0f  }, { -51.2f, -8.2f, 9.35f  }, static_cast<uint32_t>(CourseMaterialId::TowerConcrete));
    AppendCornerBox(Out.StaticWorld, { -56.5f, -13.5f, 9.35f }, { -51.5f, -8.5f, 11.85f }, static_cast<uint32_t>(CourseMaterialId::TowerGlass));
    AppendCornerBox(Out.StaticWorld, { -56.9f, -13.9f, 11.85f }, { -51.1f, -8.1f, 12.25f }, static_cast<uint32_t>(CourseMaterialId::TowerConcrete));
    AppendBoxBottom(Out.StaticWorld, 0.14f, 0.14f, 3.6f, static_cast<uint32_t>(CourseMaterialId::Pole), { -54.0f, -11.0f, 12.25f }, 0.0f);

    // 9. Six Traffic Lights (including [-36, ±8.5] flanking the Start Grid directly ahead of ControlVehicle!)
    struct TrafficLightSpec
    {
        float X;
        float Y;
        float Yaw;
    };
    constexpr std::array<TrafficLightSpec, 6> Signals = {{
        { -36.0f,   8.5f,  Pi        },
        { -36.0f,  -8.5f,  Pi        },
        { -10.0f,  14.0f, -0.5f * Pi },
        { -26.0f, -42.5f,  Pi        },
        {  19.0f,  26.0f, -0.5f * Pi },
        { -48.0f,  -6.0f,  0.0f      }
    }};
    for (const TrafficLightSpec& Sig : Signals)
    {
        AppendBoxBottom(Out.StaticWorld, 0.18f, 0.18f, 4.4f, static_cast<uint32_t>(CourseMaterialId::TrafficPost),
                        { Sig.X, Sig.Y, 0.0f }, Sig.Yaw);
        AppendBoxBottom(Out.StaticWorld, 0.36f, 0.44f, 1.15f, static_cast<uint32_t>(CourseMaterialId::TrafficHead),
                        { Sig.X, Sig.Y, 3.35f }, Sig.Yaw);
        const Vec3 Front = RotateZ({ 0.20f, 0.0f, 0.0f }, Sig.Yaw);
        AppendBoxBottom(Out.StaticWorld, 0.06f, 0.24f, 0.24f, static_cast<uint32_t>(CourseMaterialId::SignalRed),
                        { Sig.X + Front[0], Sig.Y + Front[1], 4.16f }, Sig.Yaw);
        AppendBoxBottom(Out.StaticWorld, 0.06f, 0.24f, 0.24f, static_cast<uint32_t>(CourseMaterialId::SignalAmber),
                        { Sig.X + Front[0], Sig.Y + Front[1], 3.80f }, Sig.Yaw);
        AppendBoxBottom(Out.StaticWorld, 0.06f, 0.24f, 0.24f, static_cast<uint32_t>(CourseMaterialId::SignalGreen),
                        { Sig.X + Front[0], Sig.Y + Front[1], 3.44f }, Sig.Yaw);
    }

    // 10. Roadway Street Lamp Poles (10.8 m tall + cantilevered arm + PoleHead + IES batwing StreetLamp light)
    struct PoleSpec
    {
        float X;
        float Y;
        float ArmSignY;
    };
    std::vector<PoleSpec> Poles;
    for (float X : { -46.0f, -18.0f, 10.0f, 38.0f, 66.0f, 94.0f, 122.0f })
    {
        Poles.push_back({ X,  10.2f, -1.0f });
        Poles.push_back({ X, -10.2f,  1.0f });
    }
    Poles.push_back({ -48.0f, 30.0f,  1.0f });
    Poles.push_back({ -12.0f, 30.0f, -1.0f });
    Poles.push_back({ -44.0f, -28.0f, 1.0f });
    Poles.push_back({  16.0f,  28.0f, 1.0f });

    for (const PoleSpec& Pole : Poles)
    {
        AppendBoxBottom(Out.StaticWorld, 0.22f, 0.22f, 10.8f, static_cast<uint32_t>(CourseMaterialId::Pole),
                        { Pole.X, Pole.Y, 0.0f }, 0.0f);
        const float ReachY = Pole.Y + Pole.ArmSignY * 1.6f;
        AppendCornerBox(Out.StaticWorld,
                        { Pole.X - 0.10f, std::min(Pole.Y, ReachY), 10.62f },
                        { Pole.X + 0.10f, std::max(Pole.Y, ReachY), 10.82f },
                        static_cast<uint32_t>(CourseMaterialId::Pole));
        AppendCornerBox(Out.StaticWorld,
                        { Pole.X - 0.42f, ReachY - 0.26f, 10.48f },
                        { Pole.X + 0.42f, ReachY + 0.26f, 10.64f },
                        static_cast<uint32_t>(CourseMaterialId::PoleHead));

        Out.StaticLights.push_back(ProvingGroundsLight{
            ClusteredLightKind::StreetLamp,
            { Pole.X, ReachY, 10.55f },
            { 0.0f, 0.0f, -1.0f },
            { 1.0f, 0.88f, 0.65f },
            45.0f,
            28.0f,
            0.70f,
            0.20f
        });
    }

    // 11. Ten Point-Light Lamp Posts along Launch Straight, Hangar Apron, Burnout Box & Smash Alley (LightingSystem.js)
    constexpr std::array<std::array<float, 3>, 10> PointLamps = {{
        { -38.0f,   7.78f, 5.35f },
        { -38.0f,  -7.78f, 5.35f },
        { -22.0f,   7.78f, 5.35f },
        { -22.0f,  -7.78f, 5.35f },
        {  -6.0f,   7.78f, 5.35f },
        {  -6.0f,  -7.78f, 5.35f },
        {  19.42f, 28.50f, 5.35f },
        {  48.58f, 28.50f, 5.35f },
        { -38.0f, -18.42f, 5.35f },
        {  12.0f, -18.42f, 5.35f }
    }};
    for (size_t Index = 0; Index < PointLamps.size(); ++Index)
    {
        const auto [Lx, Ly, Lz] = PointLamps[Index];
        const bool IsCool       = (Index & 1u) != 0u;
        AppendBoxBottom(Out.StaticWorld, 0.16f, 0.16f, Lz - 0.35f, static_cast<uint32_t>(CourseMaterialId::LampPost),
                        { Lx, Ly, 0.0f }, 0.0f);
        AppendBoxBottom(Out.StaticWorld, 0.36f, 0.36f, 0.44f,
                        static_cast<uint32_t>(IsCool ? CourseMaterialId::LampLanternCool : CourseMaterialId::LampLantern),
                        { Lx, Ly, Lz - 0.35f }, 0.0f);

        Out.StaticLights.push_back(ProvingGroundsLight{
            ClusteredLightKind::Omni,
            { Lx, Ly, Lz - 0.35f },
            { 0.0f, 0.0f, -1.0f },
            IsCool ? Vec3{ 0.80f, 0.90f, 1.00f } : Vec3{ 1.00f, 0.78f, 0.48f },
            42.0f,
            24.0f,
            -1.0f,
            -1.0f
        });
    }

    // 12. Tyre Barrier Chicane Stacks
    AppendTyreStackWall(Out.StaticWorld, 5, 0.68f, 0.33f, 0.96f, { 29.5f,  4.2f, 0.0f }, 0.0f);
    AppendTyreStackWall(Out.StaticWorld, 5, 0.68f, 0.33f, 0.96f, { 29.5f, -4.2f, 0.0f }, 0.0f);

    // 13. EN-13422 Traffic Cones (Start gates, Ramp gates, Skidpad ring, Slalom, Moose test — LevelStructure.js)
    const auto PlaceCone = [&](float X, float Y) {
        AppendTrafficCone(Out.StaticWorld, { X, Y, QueryProvingGroundsCourseHeight(X, Y) });
        ++Out.ConeCount;
    };
    PlaceCone(-34.15f,  6.7f);
    PlaceCone(-34.15f, -6.7f);
    PlaceCone( 16.0f,   4.5f);
    PlaceCone( 16.0f,  -4.5f);
    for (int I = 0; I < 16; ++I)
    {
        const float Angle = 2.0f * Pi * static_cast<float>(I) / 16.0f;
        PlaceCone(-30.0f + 17.5f * std::cos(Angle), 30.0f + 17.5f * std::sin(Angle));
        PlaceCone(-30.0f +  6.5f * std::cos(Angle), 30.0f +  6.5f * std::sin(Angle));
    }
    for (int Gate = 0; Gate < 8; ++Gate)
    {
        PlaceCone(38.0f + static_cast<float>(Gate) * 12.0f, (Gate & 1) == 0 ? 1.8f : -1.8f);
    }
    for (float X : { 54.0f, 58.0f, 62.0f, 70.0f, 74.0f, 78.0f, 86.0f, 90.0f, 94.0f })
    {
        PlaceCone(X, 15.5f);
        PlaceCone(X, 19.0f);
    }

    // 14. Smash Alley Breakable Props (13 props matching ResolveBreakables in LevelStructure.js)
    const auto AddBreakable = [&](const char* Name, const char* Label, Vec3 Pos, Vec3 Size, CourseMaterialId Mat, float Yaw = 0.0f) {
        BreakablePropEntry Prop{};
        Prop.Name          = Name;
        Prop.MaterialLabel = Label;
        Prop.Position      = Pos;
        Prop.Yaw           = Yaw;
        Prop.Intact        = true;
        AppendBoxBottom(Prop.Triangles, Size[0], Size[1], Size[2], static_cast<uint32_t>(Mat), Pos, Yaw);
        Out.Breakables.push_back(std::move(Prop));
    };
    AddBreakable("Wooden crate 1",    "Pine wood",       {  8.0f, -26.0f, 0.0f }, { 1.1f, 1.1f, 1.1f }, CourseMaterialId::CrateWood,      0.12f);
    AddBreakable("Wooden crate 2",    "Pine wood",       {  8.2f, -27.3f, 0.0f }, { 1.1f, 1.1f, 1.1f }, CourseMaterialId::CrateWood,     -0.08f);
    AddBreakable("Wooden crate 3",    "Pine wood",       {  8.1f, -26.6f, 1.1f }, { 1.0f, 1.0f, 1.0f }, CourseMaterialId::CrateWood,      0.05f);
    AddBreakable("Glass pane A",      "Annealed glass",  { 15.0f, -28.5f, 0.0f }, { 0.12f, 3.2f, 2.4f }, CourseMaterialId::PaneGlass,     0.0f);
    AddBreakable("Concrete pillar A", "Cast concrete",   { 21.0f, -25.5f, 0.0f }, { 0.65f, 0.65f, 2.8f }, CourseMaterialId::PillarConcrete, 0.0f);
    AddBreakable("Concrete pillar B", "Cast concrete",   { 21.0f, -31.5f, 0.0f }, { 0.65f, 0.65f, 2.8f }, CourseMaterialId::PillarConcrete, 0.0f);
    AddBreakable("Tempered pane",     "Tempered glass",  { 26.5f, -28.5f, 0.0f }, { 0.10f, 3.4f, 2.4f }, CourseMaterialId::TemperedGlass, 0.0f);
    AddBreakable("ABS barrier A",     "ABS shell",       { 31.5f, -26.8f, 0.0f }, { 0.70f, 1.8f, 0.95f }, CourseMaterialId::BarrierShell, 0.0f);
    AddBreakable("ABS barrier B",     "ABS shell",       { 31.5f, -30.2f, 0.0f }, { 0.70f, 1.8f, 0.95f }, CourseMaterialId::BarrierShell, 0.0f);
    AddBreakable("Stone wall",        "Limestone",       { 36.5f, -28.5f, 0.0f }, { 0.55f, 4.2f, 1.65f }, CourseMaterialId::WallStone,    0.0f);
    AddBreakable("Concrete block",    "Reinforced",      { 41.5f, -28.5f, 0.0f }, { 1.40f, 1.8f, 1.25f }, CourseMaterialId::BlockConcrete, 0.0f);
    AddBreakable("Apron crate A",     "Pine wood",       { 17.5f,  34.5f, 0.0f }, { 1.05f, 1.05f, 1.05f }, CourseMaterialId::CrateWood,   0.22f);
    AddBreakable("Apron crate B",     "Pine wood",       { 17.8f,  36.0f, 0.0f }, { 1.05f, 1.05f, 1.05f }, CourseMaterialId::CrateWood,  -0.18f);

    // 15. Arena Teleport Zones (LevelStructure.js ArenaZones)
    Out.Zones = {
        { "Start grid",      { -40.0f,   0.0f },  0.0f      },
        { "Jump ramp",       {   8.0f,   0.0f },  0.0f      },
        { "Skidpad",         { -46.0f,  30.0f },  0.5f * Pi },
        { "Slalom",          {  30.0f,   0.0f },  0.0f      },
        { "Moose test",      {  44.0f,  17.2f },  0.0f      },
        { "Hangar apron",    {  14.0f,  38.5f },  0.0f      },
        { "South tunnel",    { -34.0f, -48.0f },  0.0f      },
        { "Smash alley",     {   0.0f, -28.5f },  0.0f      },
        { "Burnout box",     { -36.0f, -28.0f },  0.0f      }
    };

    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CAMERA RIGS
//------------------------------------------------------------------------------------------------------------------------

ResolvedCameraFrame ResolveProvingGroundsCamera(EverestCameraMode    Mode,
                                                std::array<float, 3> VehiclePosition,
                                                float                VehicleYawRad,
                                                float                OrbitYawRad,
                                                float                OrbitPitchRad,
                                                float                OrbitDistanceM,
                                                float                AspectRatio) noexcept
{
    const float Rebase = ControlVehicleConstants::CoMAuthoredZ();
    const auto  BodyToWorld = [&](Vec3 Local) -> Vec3 {
        return Add(VehiclePosition, RotateZ(Local, VehicleYawRad));
    };
    const Vec3 Forward = { std::cos(VehicleYawRad), std::sin(VehicleYawRad), 0.0f };

    ResolvedCameraFrame Out{};
    Out.Up = { 0.0f, 0.0f, 1.0f };

    switch (Mode)
    {
        case EverestCameraMode::Chase:
        {
            // ChaseCameraSolver.js: FollowDistance=7.0, FollowHeight=2.6, LookAtHeight=0.8, RestFieldOfView=60.0.
            // Chase uses a subtle -14 deg three-quarter follow behind the rear bumper so the vehicle silhouette,
            // ruby taillight bar, checkered start grid, skidpad cones, hangar, and 212 m straight all read together.
            const float Yaw        = VehicleYawRad + Pi - 0.24f;
            const float Distance   = 7.0f;
            const float Height     = 2.55f;
            Out.Eye = {
                VehiclePosition[0] + std::cos(Yaw) * Distance,
                VehiclePosition[1] + std::sin(Yaw) * Distance,
                std::max(0.35f, VehiclePosition[2] + Height)
            };
            Out.Target = {
                VehiclePosition[0] + Forward[0] * 2.20f,
                VehiclePosition[1] + Forward[1] * 2.20f,
                VehiclePosition[2] + 0.82f
            };
            Out.VerticalFovDeg = 60.0f;
            break;
        }
        case EverestCameraMode::Orbit:
        {
            // VehicleOrbitCamera.js: Target = Position + [0,0,0.35], default Yaw = PI + 0.4, Pitch = 0.30, Distance = 8.0
            const float Yaw      = VehicleYawRad + OrbitYawRad;
            const float Pitch    = std::clamp(OrbitPitchRad, 0.04f, 1.38f);
            const float Distance = std::clamp(OrbitDistanceM > 0.1f ? OrbitDistanceM : 8.0f, 3.2f, 24.0f);
            Out.Target = { VehiclePosition[0], VehiclePosition[1], VehiclePosition[2] + 0.35f };
            Out.Eye = {
                Out.Target[0] + Distance * std::cos(Pitch) * std::cos(Yaw),
                Out.Target[1] + Distance * std::cos(Pitch) * std::sin(Yaw),
                std::max(0.28f, Out.Target[2] + Distance * std::sin(Pitch))
            };
            Out.VerticalFovDeg = 60.0f;
            break;
        }
        case EverestCameraMode::Cockpit:
        {
            const Vec3 Eye = BodyToWorld({ -0.20f, 0.42f, 1.12f - Rebase });
            Out.Eye        = Eye;
            Out.Target     = Add(Eye, Add(Scale(Forward, 8.0f), { 0.0f, 0.0f, -8.0f * std::tan(0.14f) }));
            Out.VerticalFovDeg = 70.0f;
            Out.Interior       = true;
            break;
        }
        case EverestCameraMode::HmiTablet:
        {
            const Vec3  TabCentre = BodyToWorld({ 0.98f, 0.0f, 0.62f - Rebase });
            const Vec3  TabNormal = RotateZ({ -std::cos(0.42f), 0.0f, std::sin(0.42f) }, VehicleYawRad);
            const float Dist      = 0.5f * std::max(0.1875f, 0.30f / std::max(0.4f, AspectRatio))
                / std::tan(40.0f * Pi / 360.0f) * 1.18f;
            Out.Eye            = Add(TabCentre, Scale(TabNormal, Dist));
            Out.Target         = TabCentre;
            Out.VerticalFovDeg = 40.0f;
            Out.Interior       = true;
            break;
        }
        case EverestCameraMode::Hood:
        {
            const Vec3 Eye = BodyToWorld({ 1.62f, 0.0f, 0.70f - Rebase });
            Out.Eye        = Eye;
            Out.Target     = Add(Eye, Add(Scale(Forward, 12.0f), { 0.0f, 0.0f, -0.60f }));
            Out.VerticalFovDeg = 62.0f;
            break;
        }
        case EverestCameraMode::Free:
        {
            // FreeCameraSolver.js overlooking Start Grid, ControlVehicle, Kerbs, Ramp & Hangar
            Out.Eye            = { -50.5f, -11.5f, 5.8f };
            Out.Target         = { -32.0f,   2.0f, 1.1f };
            Out.VerticalFovDeg = 60.0f;
            break;
        }
        case EverestCameraMode::TyreFl:
        {
            Out.Eye            = BodyToWorld({ 2.95f, 2.05f, -0.12f });
            Out.Target         = BodyToWorld({ 1.7274f, 1.0475f, -0.38f });
            Out.VerticalFovDeg = 38.0f;
            break;
        }
        case EverestCameraMode::TyreFr:
        {
            Out.Eye            = BodyToWorld({ 2.95f, -2.05f, -0.12f });
            Out.Target         = BodyToWorld({ 1.7274f, -1.0475f, -0.38f });
            Out.VerticalFovDeg = 38.0f;
            break;
        }
        case EverestCameraMode::TyreRr:
        {
            Out.Eye            = BodyToWorld({ -0.55f, -2.15f, -0.15f });
            Out.Target         = BodyToWorld({ -1.6686f, -1.0475f, -0.38f });
            Out.VerticalFovDeg = 38.0f;
            break;
        }
        case EverestCameraMode::TyreRl:
        {
            Out.Eye            = BodyToWorld({ -0.55f, 2.15f, -0.15f });
            Out.Target         = BodyToWorld({ -1.6686f, 1.0475f, -0.38f });
            Out.VerticalFovDeg = 38.0f;
            break;
        }
    }

    return Out;
}

} // namespace Frontier
