/*==========================================================================================================================================
                                                     PANETHICKNESSVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Check finite pane closure, analytic volume, invalid construction and six retained vehicle poses.
#include "Engine/Render/PaneThickness.h"
#include "LightComputeFormat.h"
#include <iostream>
#include <limits>
#include <filesystem>
using namespace Frontier;

SurfaceGeometry Rectangle(float Z = 0,float X = 0)
{
    SurfaceGeometry S;
    for (const auto P : {std::array<float,3>{X-1,-1,Z},{X+1,-1,Z},{X+1,1,Z},{X-1,1,Z}})
    {
        SurfaceCorner C; C.Position = P; C.Normal = {0,0,1}; S.Corners.push_back(C);
    }
    S.TriangleCorners = {0,1,2,0,2,3}; return S;
}
void Append(SurfaceGeometry &A,const SurfaceGeometry &B)
{
    const auto Offset = uint32_t(A.Corners.size());
    A.Corners.insert(A.Corners.end(),B.Corners.begin(),B.Corners.end());
    for (auto I : B.TriangleCorners) A.TriangleCorners.push_back(Offset+I);
}
SurfaceGeometry VehicleWindows(const LightComputeArchive &Scene)
{
    SurfaceGeometry Glass;
    for (const auto &F : Scene.Surfaces)
        if (std::abs(F.AlbedoRoughness[0]-.026f) < 1e-5 && std::abs(F.AlbedoRoughness[3]-.02f) < 1e-5)
            for (const auto &P : {F.A,F.B,F.C})
            {
                SurfaceCorner C; C.Position = {P[0],P[1],P[2]}; C.Normal = {0,0,1};
                Glass.TriangleCorners.push_back(uint32_t(Glass.Corners.size())); Glass.Corners.push_back(C);
            }
    return Glass;
}
int main(int Count,char **Arguments)
{
    try
    {
        uint32_t Checks = 0, Rejections = 0;
        auto Check = [&](bool Accepted)
        {
            if (!Accepted) throw std::runtime_error("Pane closure assertion failed.");
            ++Checks;
        };
        auto Reject = [&](const SurfaceGeometry &S,double Thickness = .006)
        {
            bool Rejected = false;
            try { (void)ThickenPane(S,Thickness); } catch (const std::exception &) { Rejected = true; }
            Check(Rejected); ++Rejections;
        };
        double VolumeError = 0;
        const auto Flat = Rectangle();
        for (double Thickness : {.001,.006,.02})
        {
            const auto T = ThickenPane(Flat,Thickness);
            Check(T.PaneCount == 1 && T.BoundaryEdges == 4 && T.Surface.TriangleCorners.size() == 36);
            VolumeError = std::max(VolumeError,std::abs(T.SignedVolume-4*Thickness));
            Check(std::abs(T.SignedVolume-4*Thickness) < 1e-8);
        }
        auto Reversed = Flat;
        for (size_t I = 0; I < 6; I += 3) std::swap(Reversed.TriangleCorners[I],Reversed.TriangleCorners[I+1]);
        Check(std::abs(ThickenPane(Reversed,.006).SignedVolume-ThickenPane(Flat,.006).SignedVolume) < 1e-12);
        auto Separate = Flat; Append(Separate,Rectangle(0,3)); Check(ThickenPane(Separate,.006).PaneCount == 2);
        auto Curve = Flat; Curve.Corners[3].Position[2] = .15f; Check(ThickenPane(Curve,.006).SignedVolume > 0);
        Reject(Flat,0); Reject(Flat,-.006); Reject(Flat,.06); Reject(Flat,std::numeric_limits<double>::quiet_NaN());
        auto Wrong = Flat; std::swap(Wrong.TriangleCorners[3],Wrong.TriangleCorners[4]); Reject(Wrong);
        auto Duplicate = Flat; Append(Duplicate,Flat); Reject(Duplicate);
        auto Crossing = Flat; Append(Crossing,Rectangle(.002f,.3f)); Reject(Crossing);
        auto Coplanar = Flat; Append(Coplanar,Rectangle(0,.3f)); Reject(Coplanar);
        auto Degenerate = Flat; Degenerate.Corners[1].Position = Degenerate.Corners[0].Position; Reject(Degenerate);
        auto Nonfinite = Flat; Nonfinite.Corners[0].Position[0] = std::numeric_limits<float>::infinity(); Reject(Nonfinite);
        auto Missing = Flat; Missing.TriangleCorners[0] = 999; Reject(Missing);
        auto Folded = Flat; Folded.Corners[3].Position = {2,0,.1f}; Reject(Folded);
        auto Tiny = Flat; Tiny.Corners[1].Position = {-.999f,-1,0}; Reject(Tiny);
        Reject(ThickenPane(Flat,.006).Surface);
        auto Far = Flat; for (auto &C : Far.Corners) C.Position[2] += 1e7f; Reject(Far);
        auto Fork = Flat; auto Touching = Rectangle(0,2);
        for (auto &C : Touching.Corners) C.Position[1] += 2;
        Append(Fork,Touching); Reject(Fork);
        for (uint32_t Capped : {0u,1u,128u})
            for (uint32_t Tir : {0u,65534u,65535u,65536u,262016u})
            {
                const float Packed = float(Capped*65536u+std::min(65535u,Tir));
                Check(uint32_t(Packed)/65536 == Capped && uint32_t(Packed)%65536 == std::min(65535u,Tir));
            }
        Check(Count == 2);
        const auto Vehicle = ThickenPane(VehicleWindows(LightComputeArchive::Load(Arguments[1])),.006);
        Check(Vehicle.PaneCount == 3 && Vehicle.BoundaryEdges == 68 && Vehicle.Surface.TriangleCorners.size()/3 == 284);
        for (uint32_t Pose = 1; Pose < 6; ++Pose)
        {
            const auto Path = std::filesystem::path(Arguments[1]).parent_path()/("VehicleMotion"+std::to_string(Pose)+".bin");
            const auto Closed = ThickenPane(VehicleWindows(LightComputeArchive::Load(Path.string())),.006);
            Check(Closed.PaneCount == 3 && Closed.BoundaryEdges == 68 && Closed.Surface.TriangleCorners.size()/3 == 284);
            Check(std::abs(Closed.SignedVolume-Vehicle.SignedVolume) < 1e-7);
        }
        std::cout << "PANE_THICKNESS checks=" << Checks << " malformed_rejected=" << Rejections
                  << " host_diagnostic_packing_cases=15 planar_volume_error_max=" << VolumeError
                  << " vehicle_poses=6 vehicle_panes=3 vehicle_triangles=284 volume_m3=" << Vehicle.SignedVolume << '\n';
    }
    catch (const std::exception &Error) { std::cerr << Error.what() << '\n'; return 1; }
}
