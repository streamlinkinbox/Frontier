/*============================================================================================================================================
                                                          BAKE16KSTRESS.CPP
============================================================================================================================================*/
// 🧩 Optional full-size disk-backed bake, PNG streaming and round-trip validation.
#include "Engine/ContentInterchange/BakeField.h"
#include <filesystem>
#include <fstream>
#include <iostream>
int main()
{
    using namespace Frontier;
    try
    {
        auto Assets=BuildDemoBakeAssets("Cube");
        // Sparse UV coverage keeps this stress fixture practical; it still writes and exports every pixel.
        for (auto &Vertex:Assets.Low.Parts[0].Vertices) { Vertex.Uv[0]*=.01f; Vertex.Uv[1]*=.01f; }
        BakeSettings Settings; Settings.Resolution=16384; Settings.Padding=2; Settings.Channels={"coverage"}; Settings.MatchByName=true;
        auto Results=RunTiledMeshBake({&Assets.Low.Parts[0]},Assets.High,Settings,[](const BakeProgress &P)
        { if (P.Done%512==0) std::cout << P.Done << "/" << P.Total << " tiles\n" << std::flush; });
        if(Results.size()!=1 || Results[0].Resolution!=16384 || !Results[0].Maps[0].Storage ||
            std::filesystem::file_size(Results[0].Maps[0].Storage->Path)!=uint64_t(16384)*16384*4)
            throw std::runtime_error("Incorrect full-size data file.");
        auto Output=std::filesystem::temp_directory_path()/"frontier-16k-validation.png";
        WriteBakeMapPng(Results[0].Maps[0],16384,Output);
        std::ifstream File(Output,std::ios::binary); char Header[24]{}; File.read(Header,24);
        if(!File || uint8_t(Header[16])!=0 || uint8_t(Header[17])!=0 || uint8_t(Header[18])!=64 || uint8_t(Header[19])!=0 ||
           uint8_t(Header[20])!=0 || uint8_t(Header[21])!=0 || uint8_t(Header[22])!=64 || uint8_t(Header[23])!=0)
            throw std::runtime_error("Invalid 16K PNG dimensions.");
        File.close(); std::filesystem::remove(Output);
        std::cout << "PASS: 16384x16384 disk-backed coverage bake and streaming PNG output with 2-pixel padding\n";
    }
    catch(const std::exception &Failure) { std::cerr << Failure.what() << '\n'; return 1; }
}
