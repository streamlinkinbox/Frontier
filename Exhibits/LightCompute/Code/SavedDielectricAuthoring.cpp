/*==========================================================================================================================================
                                                     SAVEDDIELECTRICAUTHORING.CPP
==========================================================================================================================================*/
// 🧩 Exercise strict native volume authoring, disk round trips and GPU-fixture coefficient export.

#include "Engine/Render/SurfaceMaterial.h"
#include <fstream>
#include <iostream>
#include <limits>

using namespace Frontier;

std::string ReadText(const std::filesystem::path &Path)
{
    std::ifstream Input(Path);
    if (!Input) throw std::runtime_error("Material input missing.");
    return {std::istreambuf_iterator<char>(Input),std::istreambuf_iterator<char>()};
}

int main(int Count,char **Arguments)
{
    try
    {
        if (Count != 3 && Count != 4) throw std::runtime_error("Expected repository root and scratch destination.");
        const std::filesystem::path Root(Arguments[1]), Output(Arguments[2]);
        std::filesystem::create_directories(Output);
        uint32_t Checks = 0;
        auto Check = [&](bool Condition)
        {
            if (!Condition) throw std::runtime_error("Saved volume assertion failed.");
            ++Checks;
        };
        const auto Material = DecodeSurfaceMaterial(ReadText(Count == 4 ? std::filesystem::path(Arguments[3]) :
            Root/"Exhibits/LightCompute/Materials/ClosedGlass.material"));
        Check(Material.ClosedVolume.has_value());
        const std::string Prefix = "Format=1\nName='Invalid volume'\n[[Strata]]\nCategory='Opaque'\n";
        for (const std::string Bad : {"Ior=0.9\nAbsorption=[0,0,0]", "Ior=3.1\nAbsorption=[0,0,0]",
             "Ior=nan\nAbsorption=[0,0,0]", "Ior=1.5\nAbsorption=[-1,0,0]", "Ior=1.5\nAbsorption=[0,101,0]",
             "Ior=1.5\nAbsorption=[0,inf,0]", "Ior=1.5\nAbsorption=[0,0]", "Ior=1.5",
             "Absorption=[0,0,0]", "Ior='glass'\nAbsorption=[0,0,0]", "Ior=1.5\nAbsorption=[0,0,0]\nThickness=1"})
        {
            bool Rejected = false;
            try { (void)DecodeSurfaceMaterial(Prefix+"[DielectricVolume]\n"+Bad); }
            catch (const std::exception &) { Rejected = true; }
            Check(Rejected);
        }
        for (const char *Name : {"racing-green","performance-rubber","crystal-glass","pp-polypropylene","indigo-denim"})
        {
            const auto Old = DecodeSurfaceMaterial(ReadText(Root/"EngineContent/MaterialArchives/TextureStudio"/(std::string(Name)+".material")));
            Check(!Old.ClosedVolume);
            Check(!DecodeSurfaceMaterial(EncodeSurfaceMaterial(Old)).ClosedVolume);
        }
        auto Invalid = Material;
        Invalid.ClosedVolume->Ior = std::numeric_limits<float>::infinity();
        bool Rejected = false;
        try { (void)EncodeSurfaceMaterial(Invalid); } catch (const std::exception &) { Rejected = true; }
        Check(Rejected);
        Rejected = false;
        try { (void)CompileSurfaceMaterial(Invalid,Root); } catch (const std::exception &) { Rejected = true; }
        Check(Rejected);
        for (float Ior : {1.f,3.f}) Check(ValidateDielectricVolume({Ior,{0,100,0}}));
        std::ofstream Binary(Output/"SavedDielectric.bin",std::ios::binary);
        for (uint32_t Case = 0; Case < 4; ++Case)
        {
            auto Edited = Material;
            Edited.ClosedVolume->Ior = Case < 2 ? 1.f : Material.ClosedVolume->Ior;
            if (!(Case%2)) Edited.ClosedVolume->Absorption = {};
            const auto Path = Output/("ClosedGlass"+std::to_string(Case)+".material");
            { std::ofstream File(Path); File << EncodeSurfaceMaterial(Edited); Check(bool(File)); }
            auto Reloaded = DecodeSurfaceMaterial(ReadText(Path));
            Check(Reloaded.ClosedVolume == Edited.ClosedVolume);
            const auto Compiled = CompileSurfaceMaterial(Reloaded,Root);
            Check(Compiled.ClosedVolume == Reloaded.ClosedVolume);
            const auto &Volume = *Compiled.ClosedVolume;
            const std::array<float,4> Coefficients{Volume.Ior,Volume.Absorption[0],Volume.Absorption[1],Volume.Absorption[2]};
            Binary.write(reinterpret_cast<const char*>(Coefficients.data()),sizeof(Coefficients));
        }
        Check(bool(Binary));
        std::cout << "SAVED_DIELECTRIC_AUTHORING checks=" << Checks
                  << " native_toml=1 disk_roundtrip_cases=4 legacy_presets=5 rejected_inputs=11\n";
    }
    catch (const std::exception &Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
