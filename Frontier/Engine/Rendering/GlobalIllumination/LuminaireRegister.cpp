/*==========================================================================================================================================
                                                         LUMINAIREREGISTER.CPP
==========================================================================================================================================*/
// 🧩 The 12,320-byte clustered luminaire register: street lamps, point lamps, vehicle beams and a transient weather light packed
//    with a 16×16×4 world grid of 64-bit cell masks (LightingSystem.js Update + BuildClusterGrid, ported 1:1).

#include "LuminaireRegister.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier::GlobalIllumination
{

namespace
{

constexpr double Pi = 3.141592653589793;

// 📝 16×16×4 cluster grid over the proving grounds: [-80..180] in X, [-80..80] in Y, [-2..28] in Z.
constexpr Vector3  GridMinimum{-80.0, -80.0, -2.0};
constexpr Vector3  GridExtent{260.0, 160.0, 30.0};
constexpr int32_t  ResolutionX = 16, ResolutionY = 16, ResolutionZ = 4, CellCount = 1024;
constexpr uint32_t HeaderOffset = MaximumLuminaires * LuminaireEntryWords;   // [word] 1024
constexpr uint32_t MaskOffset   = HeaderOffset + 8;                          // [word] 1032

struct LightBounds
{
    double MinX, MinY, MinZ, MaxX, MaxY, MaxZ;
};

// 📝 Tight world AABB of a light's emission volume: sphere (omni), downward hemisphere (street lamp), cone (beams).
LightBounds ResolveLightWorldBounds(const LuminaireProfile& Light) noexcept
{
    const double Px = Light.Position[0], Py = Light.Position[1], Pz = Light.Position[2];
    const double R  = Light.Reach;
    if (Light.Category == LuminaireCategory::Omni) return {Px - R, Py - R, Pz - R, Px + R, Py + R, Pz + R};
    if (Light.Category == LuminaireCategory::StreetLamp) return {Px - R, Py - R, Pz - R, Px + R, Py + R, Pz + 0.5};
    const double OuterCos = std::max(0.0, std::min(0.9999, Light.Params[1]));
    if (OuterCos <= 0.05) return {Px - R, Py - R, Pz - R, Px + R, Py + R, Pz + R};
    const double OuterSin = std::sqrt(std::max(0.0, 1.0 - OuterCos * OuterCos));
    const double Dx = Light.Direction[0], Dy = Light.Direction[1], Dz = Light.Direction[2];
    const double Ux = std::sqrt(std::max(0.0, 1.0 - Dx * Dx));
    const double Uy = std::sqrt(std::max(0.0, 1.0 - Dy * Dy));
    const double Uz = std::sqrt(std::max(0.0, 1.0 - Dz * Dz));
    const double MinDirX = (-Dx >= OuterCos) ? -1.0 : (Dx * OuterCos - Ux * OuterSin);
    const double MaxDirX = (Dx >= OuterCos) ? 1.0 : (Dx * OuterCos + Ux * OuterSin);
    const double MinDirY = (-Dy >= OuterCos) ? -1.0 : (Dy * OuterCos - Uy * OuterSin);
    const double MaxDirY = (Dy >= OuterCos) ? 1.0 : (Dy * OuterCos + Uy * OuterSin);
    const double MinDirZ = (-Dz >= OuterCos) ? -1.0 : (Dz * OuterCos - Uz * OuterSin);
    const double MaxDirZ = (Dz >= OuterCos) ? 1.0 : (Dz * OuterCos + Uz * OuterSin);
    return {Px + R * std::min(0.0, MinDirX), Py + R * std::min(0.0, MinDirY), Pz + R * std::min(0.0, MinDirZ),
            Px + R * std::max(0.0, MaxDirX), Py + R * std::max(0.0, MaxDirY), Pz + R * std::max(0.0, MaxDirZ)};
}

int32_t ClampCell(double Coordinate, int32_t Resolution) noexcept
{
    return static_cast<int32_t>(std::max(0.0, std::min(static_cast<double>(Resolution - 1), std::floor(Coordinate))));
}

} // namespace

const std::vector<LampPlacement>& QueryDefaultPointLamps()
{
    static const std::vector<LampPlacement> Lamps = {
        {-38.0, 7.78, 5.35}, {-38.0, -7.78, 5.35}, {-22.0, 7.78, 5.35}, {-22.0, -7.78, 5.35}, {-6.0, 7.78, 5.35},
        {-6.0, -7.78, 5.35}, {19.42, 28.5, 5.35},  {48.58, 28.5, 5.35},  {-38.0, -18.42, 5.35}, {12.0, -18.42, 5.35}};
    return Lamps;
}

std::vector<uint8_t> LuminaireRegister::Update(const std::optional<Transform>& VehicleTransform, const std::vector<LampPlacement>& Poles,
                                            const std::vector<LampPlacement>& PointLamps, const std::optional<LuminaireProfile>& TransientLight)
{
    Luminaires.clear();

    // ① roadway street lamp poles; the emitter sits 25 cm below the head so shadow rays never self-occlude
    for (const LampPlacement& Pole : Poles)
    {
        if (Luminaires.size() >= MaximumLuminaires - 16) break;
        if (!Pole.Lit) continue;
        LuminaireProfile Lamp;
        Lamp.Category  = LuminaireCategory::StreetLamp;
        Lamp.Position  = {Pole.X, Pole.Y, (Pole.Z != 0.0 ? Pole.Z : 10.8) - 0.25};
        Lamp.Direction = {0.0, 0.0, -1.0};
        Lamp.Colour    = {1.0, 0.88, 0.65};
        Lamp.Intensity = 45.0;
        Lamp.Reach     = 28.0;
        Lamp.Params    = {0.70, 0.20, 1.0, 1.0};
        Luminaires.push_back(Lamp);
    }

    // ② omnidirectional lamps on the lamp-post lanterns, alternating warm and cool
    for (size_t Index = 0; Index < PointLamps.size(); ++Index)
    {
        if (Luminaires.size() >= MaximumLuminaires - 4) break;
        const LampPlacement& Placement = PointLamps[Index];
        const bool           Cool      = (Index & 1u) == 1u;
        if (!Placement.Lit) continue;
        LuminaireProfile Lamp;
        Lamp.Category  = LuminaireCategory::Omni;
        Lamp.Position  = {Placement.X, Placement.Y, (Placement.Z != 0.0 ? Placement.Z : 5.35) - 0.35};
        Lamp.Direction = {0.0, 0.0, -1.0};
        Lamp.Colour    = Cool ? Vector3{0.80, 0.90, 1.0} : Vector3{1.0, 0.78, 0.48};
        Lamp.Intensity = 42.0;
        Lamp.Reach     = 24.0;
        Lamp.Params    = {-1.0, -1.0, 0.0, 1.0};
        Luminaires.push_back(Lamp);
    }

    // ③ automotive beams and ④ the roof fixture, attached to the vehicle body
    if (VehicleTransform)
    {
        const Transform& M    = *VehicleTransform;
        const Vector3    Fwd  = {M[0], M[1], M[2]};
        const Vector3    Left = {M[4], M[5], M[6]};
        const Vector3    Up   = {M[8], M[9], M[10]};
        const Vector3    Pos  = {M[12], M[13], M[14]};
        auto TransformLocal = [&](const Vector3& Offset) {
            return Vector3{Pos[0] + Fwd[0] * Offset[0] + Left[0] * Offset[1] + Up[0] * Offset[2],
                           Pos[1] + Fwd[1] * Offset[0] + Left[1] * Offset[1] + Up[1] * Offset[2],
                           Pos[2] + Fwd[2] * Offset[0] + Left[2] * Offset[1] + Up[2] * Offset[2]};
        };
        auto Beam = [&](LuminaireCategory Category, const Vector3& Position, const Vector3& Colour, double Intensity, double Reach, const Vector4& Params) {
            LuminaireProfile Light;
            Light.Category  = Category;
            Light.Position  = Position;
            Light.Direction = Fwd;
            Light.Colour    = Colour;
            Light.Intensity = Intensity;
            Light.Reach     = Reach;
            Light.Params    = Params;
            Luminaires.push_back(Light);
        };
        const Vector3 HeadlightL = TransformLocal({2.45, 0.65, 0.22});
        const Vector3 HeadlightR = TransformLocal({2.45, -0.65, 0.22});
        if (VehicleBeam == AutomotiveBeam::LowBeam)
        {
            for (const Vector3& Position : {HeadlightL, HeadlightR})
                Beam(LuminaireCategory::LowBeam, Position, {0.92, 0.96, 1.0}, 35.0, 60.0, {0.90, 0.60, 0.0, 1.0});
        }
        else if (VehicleBeam == AutomotiveBeam::HighBeam)
        {
            for (const Vector3& Position : {HeadlightL, HeadlightR})
                Beam(LuminaireCategory::HighBeam, Position, {0.95, 0.98, 1.0}, 75.0, 130.0, {0.98, 0.85, 0.0, 1.0});
        }
        else if (VehicleBeam == AutomotiveBeam::Fog)
        {
            for (const Vector3& Position : {TransformLocal({2.40, 0.70, 0.08}), TransformLocal({2.40, -0.70, 0.08})})
                Beam(LuminaireCategory::Fog, Position, {1.0, 0.90, 0.50}, 40.0, 45.0, {0.85, 0.40, 0.0, 1.0});
        }
        if (RoofLightEnabled)
        {
            if (RoofFixture == RoofFixtureCategory::SlimLed)
            {
                Beam(LuminaireCategory::Floodlight, TransformLocal({0.45, 0.0, 0.98}), {0.90, 0.95, 1.0}, 85.0, 100.0,
                     {std::cos(30 * Pi / 180), std::cos(45 * Pi / 180), 0.0, 1.0});
            }
            else
            {
                for (const Vector3& Position : {TransformLocal({0.45, 0.28, 0.98}), TransformLocal({0.45, -0.28, 0.98})})
                    Beam(LuminaireCategory::Floodlight, Position, {1.0, 0.98, 0.92}, 90.0, 120.0, {std::cos(22 * Pi / 180), std::cos(38 * Pi / 180), 1.0, 1.0});
            }
        }
    }

    // 📝 A bounded transient weather source shares real scene lighting; it never changes exposure.
    if (TransientLight && Luminaires.size() < MaximumLuminaires) Luminaires.push_back(*TransientLight);

    // ⑤ luminaire entries + cluster header + 16×16×4 64-bit cell masks
    std::vector<uint32_t> Words(LuminaireRegisterBytes / 4u, 0u);
    auto Scalar = [&Words](uint32_t Word, double Number) { const float Single = static_cast<float>(StoreSingle(Number)); std::memcpy(&Words[Word], &Single, 4); };
    const uint32_t ActiveCount = static_cast<uint32_t>(std::min<size_t>(Luminaires.size(), MaximumLuminaires));
    for (uint32_t Index = 0; Index < ActiveCount; ++Index)
    {
        const LuminaireProfile& L = Luminaires[Index];
        const uint32_t          W = Index * LuminaireEntryWords;
        for (int Axis = 0; Axis < 3; ++Axis) Scalar(W + Axis, L.Position[Axis]);
        Scalar(W + 3, static_cast<double>(static_cast<uint32_t>(L.Category)));
        for (int Axis = 0; Axis < 3; ++Axis) Scalar(W + 4 + Axis, L.Direction[Axis]);
        Scalar(W + 7, L.Reach);
        for (int Channel = 0; Channel < 3; ++Channel) Scalar(W + 8 + Channel, L.Colour[Channel]);
        Scalar(W + 11, L.Intensity);
        for (int Lane = 0; Lane < 4; ++Lane) Scalar(W + 12 + Lane, L.Params[Lane]);
    }
    Scalar(HeaderOffset + 0, GridMinimum[0]);
    Scalar(HeaderOffset + 1, GridMinimum[1]);
    Scalar(HeaderOffset + 2, GridMinimum[2]);
    Scalar(HeaderOffset + 3, static_cast<double>(ActiveCount));
    Scalar(HeaderOffset + 4, ResolutionX / GridExtent[0]);
    Scalar(HeaderOffset + 5, ResolutionY / GridExtent[1]);
    Scalar(HeaderOffset + 6, ResolutionZ / GridExtent[2]);
    Scalar(HeaderOffset + 7, CullEnabled ? 1.0 : 0.0);

    // 📝 BuildClusterGrid: a light marks every cell its bounds overlap whose nearest point lies within its reach.
    const double CellSizeX = GridExtent[0] / ResolutionX, CellSizeY = GridExtent[1] / ResolutionY, CellSizeZ = GridExtent[2] / ResolutionZ;
    const double InverseCellX = 1.0 / CellSizeX, InverseCellY = 1.0 / CellSizeY, InverseCellZ = 1.0 / CellSizeZ;
    for (uint32_t Index = 0; Index < ActiveCount; ++Index)
    {
        const LuminaireProfile& L = Luminaires[Index];
        if (L.Params[3] < 0.5 || L.Reach <= 0.0) continue;
        const double Px = L.Position[0], Py = L.Position[1], Pz = L.Position[2];
        const double R = L.Reach, RSq = R * R;
        const LightBounds Bounds = ResolveLightWorldBounds(L);
        const int32_t Ix0 = ClampCell((Bounds.MinX - GridMinimum[0]) * InverseCellX, ResolutionX);
        const int32_t Ix1 = ClampCell((Bounds.MaxX - GridMinimum[0]) * InverseCellX, ResolutionX);
        const int32_t Iy0 = ClampCell((Bounds.MinY - GridMinimum[1]) * InverseCellY, ResolutionY);
        const int32_t Iy1 = ClampCell((Bounds.MaxY - GridMinimum[1]) * InverseCellY, ResolutionY);
        const int32_t Iz0 = ClampCell((Bounds.MinZ - GridMinimum[2]) * InverseCellZ, ResolutionZ);
        const int32_t Iz1 = ClampCell((Bounds.MaxZ - GridMinimum[2]) * InverseCellZ, ResolutionZ);
        const uint32_t WordOffset = Index >= 32 ? 1u : 0u;
        const uint32_t Bit        = 1u << (Index & 31u);
        for (int32_t Iz = Iz0; Iz <= Iz1; ++Iz)
        {
            const double CellMinZ = GridMinimum[2] + Iz * CellSizeZ, CellMaxZ = CellMinZ + CellSizeZ;
            const double Dz = Pz < CellMinZ ? (CellMinZ - Pz) : (Pz > CellMaxZ ? (Pz - CellMaxZ) : 0.0);
            const double DzSq = Dz * Dz;
            if (DzSq > RSq) continue;
            for (int32_t Iy = Iy0; Iy <= Iy1; ++Iy)
            {
                const double CellMinY = GridMinimum[1] + Iy * CellSizeY, CellMaxY = CellMinY + CellSizeY;
                const double Dy = Py < CellMinY ? (CellMinY - Py) : (Py > CellMaxY ? (Py - CellMaxY) : 0.0);
                const double DyDzSq = DzSq + Dy * Dy;
                if (DyDzSq > RSq) continue;
                const uint32_t RowBase = MaskOffset + static_cast<uint32_t>(((Iz * ResolutionY + Iy) * ResolutionX) * 2) + WordOffset;
                for (int32_t Ix = Ix0; Ix <= Ix1; ++Ix)
                {
                    const double CellMinX = GridMinimum[0] + Ix * CellSizeX, CellMaxX = CellMinX + CellSizeX;
                    const double Dx = Px < CellMinX ? (CellMinX - Px) : (Px > CellMaxX ? (Px - CellMaxX) : 0.0);
                    if (DyDzSq + Dx * Dx <= RSq) Words[RowBase + static_cast<uint32_t>(Ix) * 2u] |= Bit;
                }
            }
        }
    }
    ActiveClusterCells = 0;
    for (int32_t Cell = 0; Cell < CellCount; ++Cell)
    {
        if ((Words[MaskOffset + Cell * 2] | Words[MaskOffset + Cell * 2 + 1]) != 0u) ++ActiveClusterCells;
    }
    std::vector<uint8_t> Bytes(LuminaireRegisterBytes);
    std::memcpy(Bytes.data(), Words.data(), Bytes.size());
    return Bytes;
}

} // namespace Frontier::GlobalIllumination
