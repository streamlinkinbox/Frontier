/*============================================================================================================================================
                                                      BAKETILEDOUTPUT.CPP
============================================================================================================================================*/
// 🧩 Bounded-memory tiled bakes with disk-backed full-size pixels and streaming PNG export.
#include "Engine/ContentInterchange/BakeField.h"
#include "Engine/ContentInterchange/BakeSurfaceImport.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace Frontier
{
BakeImageStorage::~BakeImageStorage() noexcept
{
    std::error_code Error;
    if (!Path.empty()) std::filesystem::remove(Path, Error);
}
namespace
{
std::filesystem::path NewStoragePath()
{
    static std::atomic<uint64_t> Serial{0};
    const auto Time=std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("frontier-bake-" + std::to_string(Time) + "-" + std::to_string(Serial.fetch_add(1)) + ".rgba");
}
void BigEndian(std::ostream &Stream, uint32_t Value)
{
    const char B[]{char(Value>>24),char(Value>>16),char(Value>>8),char(Value)}; Stream.write(B,4);
}
uint32_t CrcUpdate(uint32_t Crc, const uint8_t *Data, size_t Size)
{
    static const auto Table=[] { std::array<uint32_t,256> T{}; for(uint32_t I=0;I<256;++I) { uint32_t C=I; for(int J=0;J<8;++J) C=(C>>1) ^ (0xedb88320u & (0u-(C&1u))); T[I]=C; } return T; }();
    for(size_t I=0;I<Size;++I) Crc=Table[(Crc^Data[I])&255]^(Crc>>8);
    return Crc;
}
void Chunk(std::ostream &Out, const char Name[4], const std::vector<uint8_t> &Bytes)
{
    BigEndian(Out,uint32_t(Bytes.size())); Out.write(Name,4);
    if (!Bytes.empty()) Out.write(reinterpret_cast<const char *>(Bytes.data()),std::streamsize(Bytes.size()));
    uint32_t Crc=CrcUpdate(0xffffffffu,reinterpret_cast<const uint8_t *>(Name),4);
    Crc=CrcUpdate(Crc,Bytes.data(),Bytes.size()); BigEndian(Out,Crc^0xffffffffu);
}
}

void WriteBakeMapPng(const BakeMapImage &Map, uint32_t Resolution, const std::filesystem::path &Path)
{
    if (!Resolution) throw std::runtime_error("Cannot export an empty map.");
    const size_t RowBytes=size_t(Resolution)*4;
    std::ifstream Input;
    if (Map.Storage) { Input.open(Map.Storage->Path,std::ios::binary); if (!Input) throw std::runtime_error("Temporary bake image is unavailable."); }
    else if (Map.Rgba.size()!=RowBytes*Resolution) throw std::runtime_error("Bake image data has an invalid size.");
    std::ofstream Output(Path,std::ios::binary|std::ios::trunc);
    if (!Output) throw std::runtime_error("Could not create the PNG file.");
    const char Signature[]{char(137),'P','N','G',char(13),char(10),char(26),char(10)}; Output.write(Signature,8);
    std::vector<uint8_t> Header(13); for(int A=0;A<4;++A) { Header[A]=uint8_t(Resolution>>(24-8*A)); Header[4+A]=Header[A]; }
    Header[8]=8; Header[9]=6; Chunk(Output,"IHDR",Header);
    uint32_t AdlerA=1, AdlerB=0; std::vector<uint8_t> Row(RowBytes+1), Encoded; Row[0]=0;
    for (uint32_t Y=0; Y<Resolution; ++Y)
    {
        if (Map.Storage) { Input.read(reinterpret_cast<char *>(Row.data()+1),std::streamsize(RowBytes)); if (!Input) throw std::runtime_error("Could not read the complete temporary bake image."); }
        else std::copy_n(Map.Rgba.data()+size_t(Y)*RowBytes,RowBytes,Row.data()+1);
        for (uint8_t B : Row) { AdlerA=(AdlerA+B)%65521u; AdlerB=(AdlerB+AdlerA)%65521u; }
        Encoded.clear(); if (Y==0) { Encoded.push_back(0x78); Encoded.push_back(0x01); }
        for (size_t Offset=0; Offset<Row.size();)
        {
            uint16_t Count=uint16_t(std::min<size_t>(65535,Row.size()-Offset)); uint16_t Inverse=uint16_t(~Count);
            Encoded.push_back(0); Encoded.push_back(uint8_t(Count)); Encoded.push_back(uint8_t(Count>>8)); Encoded.push_back(uint8_t(Inverse)); Encoded.push_back(uint8_t(Inverse>>8));
            Encoded.insert(Encoded.end(),Row.begin()+Offset,Row.begin()+Offset+Count); Offset+=Count;
        }
        Chunk(Output,"IDAT",Encoded);
    }
    std::vector<uint8_t> Last{1,0,0,255,255,uint8_t(AdlerB>>8),uint8_t(AdlerB),uint8_t(AdlerA>>8),uint8_t(AdlerA)};
    Chunk(Output,"IDAT",Last); Chunk(Output,"IEND",{}); Output.flush();
    if (!Output) throw std::runtime_error("Could not write the complete PNG file.");
}

std::vector<BakePartResult> RunTiledMeshBake(const std::vector<const BakePart *> &Targets, const BakeMeshSet &High,
    const BakeSettings &Settings, const std::function<void(const BakeProgress &)> &OnProgress)
{
    if (!Settings.ProjectionRay) throw std::runtime_error("Tiled baking supports normal-ray projection only.");
    if (Settings.Resolution<8 || Settings.Resolution>16384) throw std::runtime_error("Tiled resolution must be between 8 and 16384.");
    const uint32_t Res=Settings.Resolution, TileSize=128, Preview=std::min(128u,Res);
    uint64_t ChannelCount=0; for (const auto &Key:Settings.Channels) if (IsMeshMapImplemented(Key)) ++ChannelCount;
    if (Targets.empty() || !ChannelCount) return {};
    const uint64_t Required=uint64_t(Res)*Res*4*ChannelCount*Targets.size();
    std::error_code SpaceError; auto Space=std::filesystem::space(std::filesystem::temp_directory_path(),SpaceError);
    if (!SpaceError && (Required > Space.available || Space.available-Required < 64u*1024u*1024u)) throw std::runtime_error("Not enough temporary disk space for full-resolution bake maps.");
    std::vector<BakePartResult> Results;
    const uint64_t TilesAxis=(Res+TileSize-1)/TileSize, Total=TilesAxis*TilesAxis*Targets.size(); uint64_t Done=0;
    for (const BakePart *Target:Targets)
    {
        BakeMeshSet Sources=High;
        if (Settings.MatchByName)
        {
            Sources.Parts.clear(); for(const auto &Part:High.Parts) if(BakeCorrespondenceName(Part.Name)==BakeCorrespondenceName(Target->Name)) Sources.Parts.push_back(Part);
            if(Sources.Parts.empty()) throw std::runtime_error("No high source matches target: "+Target->Name);
        }
        BakePartResult Result; Result.Name=Target->Name; Result.Resolution=Res;
        std::vector<std::fstream> Files;
        for(const auto &Key:Settings.Channels) if(IsMeshMapImplemented(Key))
        {
            BakeMapImage Image; Image.Key=Key; Image.PreviewResolution=Preview; Image.Rgba.resize(size_t(Preview)*Preview*4);
            Image.Storage=std::make_shared<BakeImageStorage>(); Image.Storage->Path=NewStoragePath();
            Files.emplace_back(Image.Storage->Path,std::ios::binary|std::ios::in|std::ios::out|std::ios::trunc);
            if(!Files.back()) throw std::runtime_error("Could not create a temporary bake image.");
            Result.Maps.push_back(std::move(Image));
        }
        for(uint32_t Y=0;Y<Res;Y+=TileSize) for(uint32_t X=0;X<Res;X+=TileSize)
        {
            const uint32_t Padding=std::min(Settings.Padding,32u);
            const uint32_t OriginX=X>Padding?X-Padding:0, OriginY=Y>Padding?Y-Padding:0;
            const uint32_t WorkSize=TileSize+Padding*2;
            const uint32_t InnerX=X-OriginX, InnerY=Y-OriginY;
            auto Request=ConstructBakeFieldTileRequest({Target},Sources,Settings,WorkSize,OriginX,OriginY);
            std::vector<std::vector<BakeTexelField>> Fields(1); EvaluateBakeField(Request,0,Fields[0]);
            auto Tile=EncodeBakeResults(Request,Fields,Settings);
            for (auto &Map : Tile[0].Maps)
            {
                auto Override = Settings.MapOverrides.find(Map.Key);
                if (Override == Settings.MapOverrides.end()) continue;
                BakeSettings Specific = Settings;
                ApplyBakeMapOverrides(Specific, Override->second);
                Specific.Channels = {Map.Key};
                auto PerMapRequest=ConstructBakeFieldTileRequest({Target},Sources,Specific,WorkSize,OriginX,OriginY);
                std::vector<std::vector<BakeTexelField>> PerMapFields(1);
                EvaluateBakeField(PerMapRequest,0,PerMapFields[0]);
                auto PerMapResult=EncodeBakeResults(PerMapRequest,PerMapFields,Specific);
                Map=std::move(PerMapResult[0].Maps[0]);
            }
            Result.Stats.Charts=Tile[0].Stats.Charts;
            const uint32_t W=std::min(TileSize,Res-X), H=std::min(TileSize,Res-Y);
            auto Coverage=Request.Targets[0].Encode.ChartIds;
            for(uint32_t Row=0;Row<H;++Row) for(uint32_t Col=0;Col<W;++Col)
            {
                const size_t P=size_t(InnerY+Row)*WorkSize+InnerX+Col;
                if(Coverage[P]) { if(Fields[0][P].HitPosition[3]>.5f) ++Result.Stats.Hits; else ++Result.Stats.Misses; }
            }
            // 🧩 Dilate only from chart-covered pixels; a halo makes every tile agree at its edges.
            std::vector<int32_t> Source(Coverage.size(),-1);
            for(uint32_t Pass=0;Pass<Padding;++Pass)
            {
                std::fill(Source.begin(),Source.end(),-1);
                for(uint32_t Row=0;Row<WorkSize;++Row) for(uint32_t Col=0;Col<WorkSize;++Col)
                {
                    const size_t P=size_t(Row)*WorkSize+Col;
                    if(Coverage[P] || OriginX+Col>=Res || OriginY+Row>=Res) continue;
                    for(int DY=-1;DY<=1 && Source[P]<0;++DY) for(int DX=-1;DX<=1;++DX)
                    {
                        int NX=int(Col)+DX, NY=int(Row)+DY;
                        if(NX<0 || NY<0 || NX>=int(WorkSize) || NY>=int(WorkSize)) continue;
                        size_t Q=size_t(NY)*WorkSize+size_t(NX);
                        if(Coverage[Q]) { Source[P]=int32_t(Q); break; }
                    }
                }
                for(size_t P=0;P<Source.size();++P) if(Source[P]>=0)
                {
                    const size_t Q=size_t(Source[P]);
                    for(auto &Map:Tile[0].Maps) std::copy_n(Map.Rgba.data()+Q*4,4,Map.Rgba.data()+P*4);
                }
                for(size_t P=0;P<Source.size();++P) if(Source[P]>=0) Coverage[P]=Coverage[size_t(Source[P])];
            }
            for(size_t M=0;M<Result.Maps.size();++M)
            {
                auto &File=Files[M]; auto &Image=Result.Maps[M]; const auto &Pixels=Tile[0].Maps[M].Rgba;
                for(uint32_t Row=0;Row<H;++Row)
                {
                    File.seekp(std::streamoff((uint64_t(Y+Row)*Res+X)*4));
                    File.write(reinterpret_cast<const char *>(Pixels.data()+(size_t(InnerY+Row)*WorkSize+InnerX)*4),std::streamsize(W*4));
                }
                if(!File) throw std::runtime_error("Temporary bake image write failed (disk full?).");
                for(uint32_t PY=0;PY<Preview;++PY)
                {
                    uint32_t SY=uint32_t((uint64_t(PY)*2+1)*Res/(2*Preview)); if(SY<Y || SY>=Y+H) continue;
                    for(uint32_t PX=0;PX<Preview;++PX)
                    {
                        uint32_t SX=uint32_t((uint64_t(PX)*2+1)*Res/(2*Preview)); if(SX<X || SX>=X+W) continue;
                        std::copy_n(Pixels.data()+(size_t(InnerY+SY-Y)*WorkSize+InnerX+SX-X)*4,4,Image.Rgba.data()+(size_t(PY)*Preview+PX)*4);
                    }
                }
            }
            ++Done; if(OnProgress) OnProgress(BakeProgress{Target->Name,uint32_t(Results.size()),uint32_t(Targets.size()),Done,Total});
        }
        for(auto &File:Files) { File.flush(); if(!File) throw std::runtime_error("Temporary bake image flush failed."); }
        Results.push_back(std::move(Result));
    }
    return Results;
}
}
