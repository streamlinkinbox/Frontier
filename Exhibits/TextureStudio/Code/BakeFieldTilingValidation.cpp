/*============================================================================================================================================
                                                    BAKEFIELDTILINGVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Check tiled field evaluation and encoding against a full-frame bake, including partial edge tiles.
#include "Engine/ContentInterchange/BakeField.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <iterator>
#include <stb_image.h>
#include <stdexcept>
int main()
{
    using namespace Frontier;
    try
    {
        auto Assets=BuildDemoBakeAssets("Cube");
        BakeSettings Settings; Settings.Resolution=65; Settings.Samples=4; Settings.BevelSamples=4;
        Settings.Channels=ImplementedMeshMapKeys();
        std::vector<const BakePart *> Targets{&Assets.Low.Parts[0]};
        auto Reference=RunBakeFieldEvaluation(Targets,Assets.High,Settings);
        uint64_t Hits=0, Misses=0;
        for (uint32_t Y=0; Y<Settings.Resolution; Y+=16) for (uint32_t X=0; X<Settings.Resolution; X+=16)
        {
            auto Request=ConstructBakeFieldTileRequest(Targets,Assets.High,Settings,16,X,Y);
            std::vector<std::vector<BakeTexelField>> Fields(1);
            EvaluateBakeField(Request,0,Fields[0]);
            auto Tile=EncodeBakeResults(Request,Fields,Settings);
            Hits+=Tile[0].Stats.Hits; Misses+=Tile[0].Stats.Misses;
            for (size_t Map=0; Map<Reference[0].Maps.size(); ++Map)
                for (uint32_t Row=0; Row<std::min(16u,Settings.Resolution-Y); ++Row)
                    for (uint32_t Col=0; Col<std::min(16u,Settings.Resolution-X); ++Col)
                        for (uint32_t Channel=0; Channel<4; ++Channel)
                        {
                            auto A=Reference[0].Maps[Map].Rgba[(size_t(Y+Row)*Settings.Resolution+X+Col)*4+Channel];
                            auto B=Tile[0].Maps[Map].Rgba[(size_t(Row)*16+Col)*4+Channel];
                            if (A!=B) throw std::runtime_error("Tile mismatch in " + Reference[0].Maps[Map].Key);
                        }
        }
        if (Hits!=Reference[0].Stats.Hits || Misses!=Reference[0].Stats.Misses) throw std::runtime_error("Tile projection statistics differ.");
        Settings.Resolution=259; Settings.Padding=0; Settings.Channels={"normal","coverage","wireframe"};
        const auto Defaults=RunBakeFieldEvaluation(Targets,Assets.High,Settings);
        Settings.MapOutput["normal"]={.5f,true,true,false};
        Settings.MapOutput["wireframe"]={.75f,false,false,false};
        const auto Full=RunBakeFieldEvaluation(Targets,Assets.High,Settings);
        const auto Cpu=RunMeshBake(Targets,Assets.High,Settings);
        if (Full[0].Maps[1].Rgba!=Defaults[0].Maps[1].Rgba || Full[0].Maps[0].Rgba==Defaults[0].Maps[0].Rgba) throw std::runtime_error("Map output settings are not independent.");
        for(size_t M=0;M<Full[0].Maps.size();++M) if(Full[0].Maps[M].Rgba!=Cpu[0].Maps[M].Rgba) throw std::runtime_error("Map settings differ between CPU and field paths.");
        std::filesystem::path TemporaryPath;
        {
            auto Tiled=RunTiledMeshBake(Targets,Assets.High,Settings);
            if (Tiled.size()!=1 || Tiled[0].Maps.size()!=3 || Tiled[0].Resolution!=259) throw std::runtime_error("Tiled output metadata mismatch.");
            for (size_t M=0;M<Tiled[0].Maps.size();++M)
            {
                const auto &Map=Tiled[0].Maps[M];
                std::ifstream In(Map.Storage->Path,std::ios::binary);
                std::vector<uint8_t> Raw((std::istreambuf_iterator<char>(In)),{});
                if (Raw!=Full[0].Maps[M].Rgba) throw std::runtime_error("Disk-backed tiled pixels differ: "+Map.Key);
                if (Map.PreviewResolution!=128 || Map.Rgba.size()!=128u*128u*4u) throw std::runtime_error("Invalid preview size.");
                for(uint32_t Y=0;Y<128;++Y) for(uint32_t X=0;X<128;++X)
                {
                    uint32_t SX=(uint64_t(X)*2+1)*259/256, SY=(uint64_t(Y)*2+1)*259/256;
                    for(int C=0;C<4;++C) if(Map.Rgba[(size_t(Y)*128+X)*4+C]!=Raw[(size_t(SY)*259+SX)*4+C]) throw std::runtime_error("Tiled preview mismatch.");
                }
            }
            TemporaryPath=Tiled[0].Maps[0].Storage->Path;
            auto Png=std::filesystem::temp_directory_path()/"frontier-tiled-validation.png";
            WriteBakeMapPng(Tiled[0].Maps[0],259,Png);
            std::ifstream Image(Png,std::ios::binary); std::string Header(24,'\0'); Image.read(Header.data(),24);
            if (!Image || Header.substr(1,3)!="PNG" || uint8_t(Header[18])!=1 || uint8_t(Header[19])!=3) throw std::runtime_error("PNG dimensions/header mismatch.");
            Image.close(); std::ifstream PngInput(Png,std::ios::binary); std::vector<unsigned char> Encoded((std::istreambuf_iterator<char>(PngInput)),{});
            int Width=0, Height=0, Components=0;
            unsigned char *Decoded=stbi_load_from_memory(Encoded.data(),int(Encoded.size()),&Width,&Height,&Components,4);
            if (!Decoded || Width!=259 || Height!=259 || !std::equal(Decoded,Decoded+size_t(259)*259*4,Full[0].Maps[0].Rgba.begin()))
            { stbi_image_free(Decoded); throw std::runtime_error("PNG decoding or pixel data mismatch."); }
            stbi_image_free(Decoded);
            PngInput.close();
            std::filesystem::remove(Png);
        }
        if (std::filesystem::exists(TemporaryPath)) throw std::runtime_error("Temporary image was not removed.");
        BakeMapOverrides Wire=CaptureBakeMapOverrides(Settings); Wire.WireWidth=4.f;
        Settings.MapOverrides["wireframe"]=Wire;
        auto OverrideOutput=RunTiledMeshBake(Targets,Assets.High,Settings);
        BakeSettings WireSettings=Settings; ApplyBakeMapOverrides(WireSettings,Wire); WireSettings.Channels={"wireframe"};
        auto ExpectedWire=RunBakeFieldEvaluation(Targets,Assets.High,WireSettings);
        for(size_t M=0;M<OverrideOutput[0].Maps.size();++M)
        {
            std::ifstream F(OverrideOutput[0].Maps[M].Storage->Path,std::ios::binary);
            std::vector<uint8_t> Pixels((std::istreambuf_iterator<char>(F)),{});
            const auto &Wanted=M==2 ? ExpectedWire[0].Maps[0].Rgba : Full[0].Maps[M].Rgba;
            if(Pixels!=Wanted) throw std::runtime_error("Per-map bake override was not isolated.");
        }
        if(ExpectedWire[0].Maps[0].Rgba==Full[0].Maps[2].Rgba) throw std::runtime_error("Wire width override did not affect output.");
        Settings.MapOverrides.clear();
        Settings.Padding=2;
        auto Padded=RunTiledMeshBake(Targets,Assets.High,Settings);
        std::vector<uint32_t> Mask(259u*259u);
        for(size_t I=0;I<Mask.size();++I) Mask[I]=Full[0].Maps[1].Rgba[I*4+3] ? 1 : 0;
        auto Expected=Full[0].Maps;
        for(int Pass=0;Pass<2;++Pass)
        {
            std::vector<int32_t> Sources(Mask.size(),-1);
            for(int Y=0;Y<259;++Y) for(int X=0;X<259;++X)
            {
                size_t P=size_t(Y)*259+X; if(Mask[P]) continue;
                for(int DY=-1;DY<=1 && Sources[P]<0;++DY) for(int DX=-1;DX<=1;++DX)
                {
                    int NX=X+DX,NY=Y+DY; if(NX<0 || NY<0 || NX>=259 || NY>=259) continue;
                    size_t Q=size_t(NY)*259+NX; if(Mask[Q]) { Sources[P]=int32_t(Q); break; }
                }
            }
            for(size_t P=0;P<Sources.size();++P) if(Sources[P]>=0)
                for(auto &Map:Expected) std::copy_n(Map.Rgba.data()+size_t(Sources[P])*4,4,Map.Rgba.data()+P*4);
            for(size_t P=0;P<Sources.size();++P) if(Sources[P]>=0) Mask[P]=1;
        }
        for(size_t M=0;M<Expected.size();++M)
        {
            std::ifstream File(Padded[0].Maps[M].Storage->Path,std::ios::binary);
            std::vector<uint8_t> Raw((std::istreambuf_iterator<char>(File)),{});
            if(Raw!=Expected[M].Rgba) throw std::runtime_error("Padded output differs across tile seams: "+Expected[M].Key);
        }
        std::cout << "PASS: all 27 tiled maps match full-frame; disk-backed pixels, preview, independent per-map parameters, PNG decode, seam-safe padding and cleanup verified\n";
    }
    catch (const std::exception &Failure) { std::cerr << Failure.what() << '\n'; return 1; }
}
