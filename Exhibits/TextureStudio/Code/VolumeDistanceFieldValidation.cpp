/*============================================================================================================================================
                                                VOLUMEDISTANCEFIELDVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validate signed, unsigned and anisotropic volume baking against a closed cube and an open triangle.
#include "Engine/ContentInterchange/VolumeDistanceField.h"
#include <cmath>
#include <fstream>
#include <iterator>
#include <cstring>
#include <iostream>
#include <stdexcept>

int main()
{
    using namespace Frontier;
    auto Assets = BuildDemoBakeAssets("Cube");
    auto Field = BakeVolumeDistanceField(Assets.High, {24, 18, 20}, .14f, true);
    if (!Field.ClosedManifold || Field.Distances.size() != 24u*18u*20u) throw std::runtime_error("closed cube or dimensions");
    auto Index = [](int X,int Y,int Z){ return (Z*18+Y)*24+X; };
    if (!(Field.Distances[Index(12,9,10)] < 0 && Field.Distances[Index(0,0,0)] > 0)) throw std::runtime_error("signed interior/exterior");
    const auto Path = std::filesystem::temp_directory_path() / "frontier-volume-validation.nrrd";
    ExportVolumeDistanceFieldNrrd(Field, Path);
    std::ifstream File(Path, std::ios::binary);
    std::string Data((std::istreambuf_iterator<char>(File)), {});
    auto Payload = Data.find("\n\n");
    if (Data.find("sizes: 24 18 20") == std::string::npos || Payload == std::string::npos || Data.size()-Payload-2 != Field.Distances.size()*4)
        throw std::runtime_error("NRRD header or payload size");
    if (std::memcmp(Data.data()+Payload+2, Field.Distances.data(), 4) != 0) throw std::runtime_error("NRRD float payload");
    File.close();
    std::filesystem::remove(Path);
    auto Unsigned = BakeVolumeDistanceField(Assets.High, {24, 18, 20}, .14f, false);
    for (float Distance : Unsigned.Distances) if (Distance < 0 || !std::isfinite(Distance)) throw std::runtime_error("unsigned distance");
    Assets.High.Parts[0].Indices.resize(3);
    auto Open = BakeVolumeDistanceField(Assets.High, {8, 8, 8}, .14f, true, .1f);
    if (Open.ClosedManifold) throw std::runtime_error("open triangle marked closed");
    for (float Distance : Open.Distances) if (std::abs(Distance) > .100001f) throw std::runtime_error("narrow band");
    std::cout << "PASS: anisotropic Float32 volume, cube signs, unsigned mode, open surface and narrow band\n";
}
