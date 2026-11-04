/*==========================================================================================================================================
                                                           SHAPEARCHIVEBAKE.CPP
==========================================================================================================================================*/
// 🧩 Bakes the signed distance volume of every engine shape (EngineContent/ShapeArchives/<Name>.toml → <Name>.sdfspace)
//    and reports it against the shape's closed form; "check" bakes again and compares with the stored volumes instead of writing.

#include "Engine/Geometry/ShapeArchive.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{

// 📝 The closed-form signed distance of the ideal primitive (Inigo Quilez's catalogue) the tessellation approximates.
double ResolveIdealDistance(const Frontier::ShapeArchive& Archive, double X, double Y, double Z)
{
    using Frontier::ShapePrimitive;
    const auto& Size = Archive.Size;
    switch (Archive.Primitive)
    {
    case ShapePrimitive::Sphere:
        return std::sqrt(X * X + Y * Y + Z * Z) - 0.5 * Size[0];
    case ShapePrimitive::Cylinder:
    {
        const double Radial = std::sqrt(X * X + Y * Y) - 0.5 * Size[0], Axial = std::abs(Z) - 0.5 * Size[2];
        return std::min(std::max(Radial, Axial), 0.0) + std::hypot(std::max(Radial, 0.0), std::max(Axial, 0.0));
    }
    case ShapePrimitive::Torus:
    {
        const double Tube = 0.5 * Archive.TubeRatio * Size[0], Ring = 0.5 * Size[0] - Tube;
        return std::hypot(std::sqrt(X * X + Y * Y) - Ring, Z) - Tube;
    }
    case ShapePrimitive::Cone:
        return std::nan("");
    default:
    {
        const double QX = std::abs(X) - 0.5 * Size[0], QY = std::abs(Y) - 0.5 * Size[1], QZ = std::abs(Z) - 0.5 * Size[2];
        const double Outside = std::sqrt(std::pow(std::max(QX, 0.0), 2) + std::pow(std::max(QY, 0.0), 2) + std::pow(std::max(QZ, 0.0), 2));
        return Outside + std::min(std::max({QX, QY, QZ}), 0.0);
    }
    }
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount < 2)
    {
        std::fprintf(stderr, "usage: ShapeArchiveBake <EngineContent/ShapeArchives> [check]\n");
        return 2;
    }
    const bool               Check = ArgumentCount > 2 && std::string(Arguments[2]) == "check";
    std::vector<std::string> Diagnostics;
    const auto               Archives = Frontier::ReadShapeArchives(Arguments[1], &Diagnostics);
    for (const auto& Diagnostic : Diagnostics) std::fprintf(stderr, "[ShapeArchiveBake] %s\n", Diagnostic.c_str());
    if (Archives.empty())
    {
        std::fprintf(stderr, "[ShapeArchiveBake] no shape archives in %s\n", Arguments[1]);
        return 1;
    }
    int Failures = static_cast<int>(Diagnostics.size());
    for (const auto& Archive : Archives)
    {
        const Frontier::ShapeDistanceVolume Volume = Frontier::BakeShapeDistance(Archive);
        // 📝 Against the ideal primitive, within the narrow band where the tessellation and not the volume decides.
        double Worst = 0.0;
        size_t Inside = 0;
        for (uint32_t Z = 0; Z < Volume.Resolution[2]; ++Z)
            for (uint32_t Y = 0; Y < Volume.Resolution[1]; ++Y)
                for (uint32_t X = 0; X < Volume.Resolution[0]; ++X)
                {
                    const float Distance = Volume.Distances[(static_cast<size_t>(Z) * Volume.Resolution[1] + Y) * Volume.Resolution[0] + X];
                    Inside += Distance < 0.0f ? 1u : 0u;
                    const double Ideal = ResolveIdealDistance(Archive, Volume.Minimum[0] + (X + 0.5) * Volume.VoxelExtent,
                                                              Volume.Minimum[1] + (Y + 0.5) * Volume.VoxelExtent,
                                                              Volume.Minimum[2] + (Z + 0.5) * Volume.VoxelExtent);
                    if (std::isfinite(Ideal)) Worst = std::max(Worst, std::abs(Ideal - Distance));
                }
        std::string Outcome = "written";
        if (Check)
        {
            // 📝 The stored volume is float16: a voxel matches while it is within half a unit in the last place of its distance
            //    (2⁻¹¹ of it, at least 6 × 10⁻⁵ m), whatever compiler baked it.
            double Drift    = -1.0;
            size_t Exceeded = 0;
            if (Archive.Distance && Archive.Distance->Distances.size() == Volume.Distances.size())
            {
                Drift = 0.0;
                for (size_t Index = 0; Index < Volume.Distances.size(); ++Index)
                {
                    const double Difference = std::abs(static_cast<double>(Archive.Distance->Distances[Index]) - Volume.Distances[Index]);
                    Drift = std::max(Drift, Difference);
                    Exceeded += Difference > std::max(6.0e-5, std::abs(static_cast<double>(Volume.Distances[Index])) * 4.9e-4) + 1.0e-7 ? 1u : 0u;
                }
            }
            const bool Matches = Drift >= 0.0 && Exceeded == 0;
            Failures += Matches ? 0 : 1;
            char Line[192];
            if (Drift < 0.0) std::snprintf(Line, sizeof(Line), "stale or missing (%s)", Archive.Diagnostic.c_str());
            else std::snprintf(Line, sizeof(Line), "stored volume drift %.2e m%s", Drift, Matches ? "" : " (bake again)");
            Outcome = Line;
        }
        else
        {
            std::string Diagnostic;
            if (!Frontier::WriteShapeDistance(Volume, Archive.DistancePath, &Diagnostic))
            {
                Outcome = "not written: " + Diagnostic;
                ++Failures;
            }
        }
        char Deviation[32] = "n/a";
        if (Archive.Primitive != Frontier::ShapePrimitive::Cone) std::snprintf(Deviation, sizeof(Deviation), "%.2f mm", Worst * 1000.0);
        std::printf("[ShapeArchiveBake] %-9s %4zu facets, %u x %u x %u texels of %.4f m, %5.1f %% inside, ideal-form deviation %s — %s\n",
                    Archive.Name.c_str(), Archive.Facets.size(), Volume.Resolution[0], Volume.Resolution[1], Volume.Resolution[2],
                    Volume.VoxelExtent, 100.0 * Inside / std::max<size_t>(1, Volume.Distances.size()), Deviation, Outcome.c_str());
    }
    return Failures == 0 ? 0 : 1;
}
