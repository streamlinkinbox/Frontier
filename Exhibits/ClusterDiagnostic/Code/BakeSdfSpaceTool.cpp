/*==========================================================================================================================================
                                                 HEADLESS .SDFSPACE BAKER TOOL
==========================================================================================================================================*/
// Standalone UI-free CLI baker converting binary FCT1 .facet triangle archives into compact IEEE-754 Float16 .sdfspace
// signed distance field volumes via Engine/ContentInterchange/VolumeDistanceField::BakeVolumeDistanceField.

#include "Engine/Geometry/GeometryProcessing/VolumeDistanceArchive.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

int main(int Argc, char** Argv)
{
    if (Argc < 6)
    {
        std::cerr << "Usage: FrontierBakeSdfSpace <input.facet> <output.sdfspace> <Nx> <Ny> <Nz> [skip_glass=1]\n";
        return 1;
    }

    const std::string InputPath  = Argv[1];
    const std::string OutputPath = Argv[2];
    const uint32_t    Nx         = static_cast<uint32_t>(std::strtoul(Argv[3], nullptr, 10));
    const uint32_t    Ny         = static_cast<uint32_t>(std::strtoul(Argv[4], nullptr, 10));
    const uint32_t    Nz         = static_cast<uint32_t>(std::strtoul(Argv[5], nullptr, 10));
    const bool        SkipGlass  = (Argc > 6) ? (std::strtoul(Argv[6], nullptr, 10) != 0u) : true;

    Frontier::FacetField Source{};
    if (!Frontier::LoadFacetStreamFromFile(InputPath, Source, {}, true, -1))
    {
        std::cerr << "[FrontierBakeSdfSpace] LoadFacetStreamFromFile failed for " << InputPath << "\n";
        return 1;
    }

    Frontier::HeadlessSdfBakeOptions Opts{};
    Opts.Dimensions   = {Nx, Ny, Nz};
    Opts.PaddingRatio = 0.10f;
    Opts.SignedField  = true;
    Opts.SkipGlass    = SkipGlass;

    const Frontier::VolumeDistanceField Baked = Frontier::BakeFacetFieldDistanceVolume(Source, Opts);
    if (Baked.Distances.empty())
    {
        std::cerr << "[FrontierBakeSdfSpace] BakeFacetFieldDistanceVolume returned empty volume.\n";
        return 1;
    }

    std::string Diag;
    if (!Frontier::SaveVolumeDistanceSpace(Baked, OutputPath, &Diag))
    {
        std::cerr << "[FrontierBakeSdfSpace] SaveVolumeDistanceSpace failed: " << Diag << "\n";
        return 1;
    }

    std::cout << "[FrontierBakeSdfSpace] Wrote " << OutputPath << " (" << Nx << "x" << Ny << "x" << Nz
              << ", ClosedManifold=" << (Baked.ClosedManifold ? 1 : 0)
              << ", MinDist=" << Baked.MinimumDistance << " m, MaxDist=" << Baked.MaximumDistance << " m)\n";
    return 0;
}
