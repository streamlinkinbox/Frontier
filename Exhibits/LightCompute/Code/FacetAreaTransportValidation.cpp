/*============================================================================================================================================
                                                   FACETAREATRANSPORTVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Render fresh native camera/primary/selected-path transport and audit emitted rays without GPU capture inputs.
#include "Engine/Rendering/FacetAreaTransport.h"
#include "Engine/Rendering/FacetAreaReservoir.h"
#include "FacetAreaProposalScenes.h"
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>

namespace
{
using Quartet=std::array<float,4>;
uint64_t Checks=0,PrimaryChecks=0,CandidateChecks=0,PathChecks=0,VisibilityChecks=0,PrimaryTies=0;
void Check(bool Accepted)
{
    ++Checks;
    if (!Accepted) throw std::runtime_error("Native transport verification failed at check="+std::to_string(Checks));
}
float Bits(uint32_t Ordinal) { float Encoded;std::memcpy(&Encoded,&Ordinal,4);return Encoded; }
SpatialVector Spatial(const std::array<float,3>& P) { return {P[0],P[1],P[2]}; }
std::vector<FacetAreaShadingTriangle> Shading(const LightComputeArchive& Scene)
{
    std::vector<FacetAreaShadingTriangle> Surfaces;
    for (const auto& F:Scene.Surfaces)
    {
        Check(F.Emissive[3]==0);
        FacetAreaShadingTriangle S;
        for (uint32_t A=0;A<3;++A)
        {
            S.Triangle.A[A]=F.A[A];S.Triangle.B[A]=F.B[A];S.Triangle.C[A]=F.C[A];
            S.Normals[0][A]=F.NormalA[A];S.Normals[1][A]=F.NormalB[A];S.Normals[2][A]=F.NormalC[A];
            S.Albedo[A]=F.AlbedoRoughness[A];
        }
        Surfaces.push_back(S);
    }
    return Surfaces;
}

struct DoubleStrike { uint32_t Index=UINT32_MAX;double Distance=0,U=0,V=0; };
DoubleStrike Brute(const LightComputeArchive& Scene,const std::array<float,3>& Origin,
                   const std::array<float,3>& Direction,float Minimum,float Maximum)
{
    DoubleStrike Result;Result.Distance=Maximum;
    for (uint32_t Index=0;Index<Scene.Surfaces.size();++Index)
    {
        const auto& T=Scene.Surfaces[Index];TraceVector E{},F{},O{},D{Direction[0],Direction[1],Direction[2]};
        for (uint32_t A=0;A<3;++A)
        { E[A]=double(T.B[A])-T.A[A];F[A]=double(T.C[A])-T.A[A];O[A]=double(Origin[A])-T.A[A]; }
        const auto P=TraceCross(D,F);const double Determinant=TraceDot(E,P);
        if (std::abs(Determinant)<1e-12) continue;
        const double U=TraceDot(O,P)/Determinant;const auto Q=TraceCross(O,E);
        const double V=TraceDot(D,Q)/Determinant,Distance=TraceDot(F,Q)/Determinant;
        if (U>=0 && V>=0 && U+V<=1 && Distance>Minimum && Distance<Result.Distance)
            Result={Index,Distance,U,V};
    }
    return Result;
}

void ValidateUnits()
{
    auto Scene=ConstructGeometricAreaScene(0);auto Surfaces=Shading(Scene);
    FacetAreaTransport Transport;
    Check(!Transport.Trace({0,0,2},{0,0,1},.00001f,100).Accepted);
    Check(!Transport.Prepare({},{}));Check(Transport.Prepare(Surfaces,Scene.Visibility));
    const auto Hit=Transport.Primary({0,0,2},{0,0,1});Check(Hit.Accepted);
    Check(Transport.Trace({0,0,2},{0,0,0},.00001f,100).Accepted==false);
    Check(!Transport.Trace({0,0,2},{0,0,1},1,1).Accepted);
    Check(!Transport.Trace({std::numeric_limits<float>::infinity(),0,2},{0,0,1},0,100).Accepted);
    const auto Miss=Transport.Trace({100,100,2},{0,0,1},.00001f,100);
    Check(Miss.Accepted && Miss.Index==UINT32_MAX);
    const auto Short=Transport.Trace({0,0,2},{0,0,1},.00001f,.1f);
    Check(Short.Accepted && Short.Index==UINT32_MAX);
    auto Broken=Scene.Visibility;Broken.Enclosures[0].Links={0,0,0,0};
    Check(!Transport.Prepare(Surfaces,Broken));Check(!Transport.Primary({0,0,2},{0,0,1}).Accepted);
    Broken=Scene.Visibility;Broken.Corners[0].A[0]+=1;Check(!Transport.Prepare(Surfaces,Broken));
    Broken=Scene.Visibility;Broken.Enclosures[0].Minimum[0]=std::numeric_limits<float>::quiet_NaN();
    Check(!Transport.Prepare(Surfaces,Broken));
    Check(Transport.Prepare(Surfaces,Scene.Visibility));
    for (uint32_t Pixel=0;Pixel<1024;++Pixel)
    {
        const auto Ray=ConstructGeometricAreaRay(Pixel);
        const std::array<float,3> O{Ray.ViewMetallic[0],Ray.ViewMetallic[1],2},D{0,0,1};
        const auto Primary=Transport.Primary(O,D);const auto Expected=Brute(Scene,O,D,.00001f,100);
        Check(Primary.Accepted && std::abs(Primary.Strike[0]-Expected.Distance)<2e-5);
    }
    // 📝 The exact shared diagonal must not crack;finite segment endpoints remain open.
    TriangleVisibilitySpace QuadSpace;
    QuadSpace.Build({{{{-1,-1,1},{1,-1,1},{1,1,1}}},{{{-1,-1,1},{1,1,1},{-1,1,1}}}});
    const auto QuadAtlas=QuadSpace.PackFloat32();
    std::vector<FacetAreaShadingTriangle> Quad;
    for (const auto& F:QuadAtlas.Corners)
    {
        FacetAreaShadingTriangle S;
        for (uint32_t A=0;A<3;++A)
        { S.Triangle.A[A]=F.A[A];S.Triangle.B[A]=F.B[A];S.Triangle.C[A]=F.C[A]; }
        S.Normals={{{0,0,-1},{0,0,-1},{0,0,-1}}};S.Albedo={1,1,1};Quad.push_back(S);
    }
    Check(Transport.Prepare(Quad,QuadAtlas));
    const auto Edge=Transport.Primary({0,0,0},{0,0,1});
    Check(Edge.Accepted && Edge.Position[2]==1);
    Check(Transport.Trace({0,0,0},{0,0,1},0,1).Index==UINT32_MAX);
    Check(Transport.Trace({0,0,0},{0,0,1},1,2).Index==UINT32_MAX);
    std::cout<<"FACET_AREA_TRANSPORT_UNIT checks="<<Checks<<" pass=1\n";
}

void Capture(const std::string& Directory)
{
    constexpr uint32_t Count=24576;
    std::filesystem::create_directories(Directory);
    for (uint32_t Corpus=0;Corpus<3;++Corpus)
        for (uint32_t Fixture=0;Fixture<3;++Fixture)
        {
            const auto Scene=Corpus==2?ConstructDeformedAreaScene(Fixture):ConstructGeometricAreaScene(Fixture);
            const auto Surfaces=Shading(Scene);std::vector<FacetAreaTriangle> Triangles;
            std::vector<std::array<SpatialVector,3>> DoubleTriangles;
            for (const auto& S:Surfaces)
            {
                Triangles.push_back(S.Triangle);
                DoubleTriangles.push_back({Spatial(S.Triangle.A),Spatial(S.Triangle.B),Spatial(S.Triangle.C)});
            }
            TriangleVisibilitySpace DoubleSpace;DoubleSpace.Build(DoubleTriangles);
            FacetAreaTransport Transport;FacetAreaProposals Proposals;
            Check(Transport.Prepare(Surfaces,Scene.Visibility));Check(Proposals.Prepare(Triangles));
            std::vector<Quartet> Output(41*Count);
            for (uint32_t Pixel=0;Pixel<Count;++Pixel)
            {
                const uint32_t Packet=Pixel%4096,Method=Pixel/4096;
                const auto Ray=Corpus==0?ConstructGeometricAreaRay(Pixel%1024):ConstructPerspectiveAreaRay(Pixel%1024,Fixture);
                std::array<float,3> O{},D{};
                for (uint32_t A=0;A<3;++A) { O[A]=Ray.ViewMetallic[A];D[A]=Corpus==0?(A==2?1.f:0.f):Ray.NormalRoughness[A]; }
                const auto Primary=Transport.Primary(O,D);Check(Primary.Accepted);++PrimaryChecks;
                const auto Expected=Brute(Scene,O,D,.00001f,100);
                const auto Double=DoubleSpace.Trace(Spatial(O),Spatial(D),.00001f,100);
                Check(Double.Hit && Expected.Index!=UINT32_MAX && std::abs(Primary.Strike[0]-Expected.Distance)<2e-5);
                Check(std::abs(Double.Travel-Expected.Distance)<1e-10);
                if (Expected.Index!=Primary.Index)
                {
                    Check(std::min({Expected.U,Expected.V,1-Expected.U-Expected.V})<2e-6);
                    Check(std::min({double(Primary.Strike[1]),double(Primary.Strike[2]),1.-Primary.Strike[1]-Primary.Strike[2]})<2e-6);
                    Check(Surfaces[Expected.Index].Albedo==Primary.Albedo);++PrimaryTies;
                }
                Output[19*Count+Pixel]={Bits(Primary.Index),Primary.Strike[0],Primary.Strike[1],Primary.Strike[2]};
                for (uint32_t A=0;A<3;++A)
                { Output[20*Count+Pixel][A]=Primary.Position[A];Output[21*Count+Pixel][A]=Primary.Normal[A];Output[22*Count+Pixel][A]=Primary.Albedo[A]; }
                Output[20*Count+Pixel][3]=1;
                const uint32_t Seed=HashFacetAreaBits((Packet+std::array<uint32_t,3>{0x739ab531u,0x4a615e23u,0x5e92b341u}[Corpus])^
                                                     (Fixture*0x6c8e9cf5u));
                uint32_t Calls=0;
                for (uint32_t Lane=0;Lane<4;++Lane)
                {
                    std::array<FacetAreaProposal,2> P{};std::array<FacetAreaScore,2> H{};
                    const uint32_t ProposalsCount=Method<2 || Method>=4?2:1;
                    for (uint32_t Ordinal=0;Ordinal<ProposalsCount;++Ordinal)
                    {
                        P[Ordinal]=Proposals.Draw(Seed,2*Lane+Ordinal,FacetAreaMeasureCategory(Method%2));
                        const auto Source=Transport.Source(P[Ordinal]);Check(Source.Accepted);
                        FacetAreaScoreInput Input;Input.Triangle=Surfaces[P[Ordinal].Index].Triangle;
                        Input.U=P[Ordinal].U;Input.V=P[Ordinal].V;Input.SourceNormal=Source.Normal;Input.SourceAlbedo=Source.Albedo;
                        Input.Receiver=Primary.Position;Input.ReceiverNormal=Primary.Normal;Input.ReceiverAlbedo=Primary.Albedo;
                        H[Ordinal]=EvaluateFacetAreaScore(Input);Check(H[Ordinal].Accepted);++CandidateChecks;
                    }
                    uint32_t Selected=0;float Normalizer=1/P[0].Density,Uniform=0;
                    if (Method<2)
                    {
                        Uniform=DrawFacetAreaSelectionUniform(Seed,Lane);
                        const auto Selection=SelectFacetAreaPair({{{H[0].Target,P[0].Density},{H[1].Target,P[1].Density}}},Uniform);
                        Check(Selection.Accepted);Selected=Selection.Selected;Normalizer=float(Selection.Normalizer);
                    }
                    std::array<uint32_t,2> Packed{P[0].Index,P[1].Index};
                    std::array<float,3> Contribution{},OmitConnection{},OmitSuffix{};
                    for (uint32_t Ordinal=0;Ordinal<ProposalsCount;++Ordinal)
                    {
                        if (Method<2 && Ordinal!=Selected) continue;
                        const auto Path=Transport.Connect(P[Ordinal],Primary);Check(Path.Accepted);++PathChecks;
                        const std::array<float,3> Light{.8f,0,.6f};
                        for (uint32_t Which=0;Which<2;++Which)
                        {
                            const auto& Direction=Which==0?Light:Path.Direction;const float Maximum=Which==0?100.f:Path.Maximum;
                            const bool ExpectedVisible=Brute(Scene,Path.Origin,Direction,.00001f,Maximum).Index==UINT32_MAX;
                            const bool DoubleVisible=!DoubleSpace.Occluded(Spatial(Path.Origin),Spatial(Direction),.00001f,Maximum);
                            const bool Actual=Which==0?Path.SuffixVisible:Path.ConnectionVisible;
                            Check(ExpectedVisible==Actual && DoubleVisible==Actual);++VisibilityChecks;
                        }
                        Packed[Ordinal]=P[Ordinal].Index|(1u<<18)|(Path.SuffixVisible?1u<<16:0)|(Path.ConnectionVisible?1u<<17:0);
                        for (uint32_t A=0;A<3;++A)
                        { Output[(23+2*Calls)*Count+Pixel][A]=Path.Origin[A];Output[(24+2*Calls)*Count+Pixel][A]=Path.Direction[A]; }
                        Output[(23+2*Calls)*Count+Pixel][3]=Path.Maximum;++Calls;
                        const float Z=Method<2?Normalizer:1/P[Ordinal].Density;
                        for (uint32_t A=0;A<3;++A)
                        {
                            const float Scale=Method>=4?.5f:1.f;
                            Contribution[A]+=Scale*(Path.Rgb[A]*Z);
                            OmitConnection[A]+=Scale*(Path.SuffixOnly[A]*Z);
                            OmitSuffix[A]+=Scale*(Path.ConnectionOnly[A]*Z);
                        }
                    }
                    for (uint32_t Ordinal=0;Ordinal<2;++Ordinal)
                        Output[(2+4*Lane+Ordinal)*Count+Pixel]={Bits(Packed[Ordinal]),P[Ordinal].U,P[Ordinal].V,P[Ordinal].Density};
                    if (Method>=4) Normalizer=0;
                    Output[(4+4*Lane)*Count+Pixel]={Contribution[0],Contribution[1],Contribution[2],Normalizer};
                    Output[(5+4*Lane)*Count+Pixel]={H[0].Target,H[1].Target,float(Selected),Uniform};
                    for (uint32_t A=0;A<3;++A)
                    {
                        Output[Pixel][A]+=Contribution[A]/4;
                        Output[39*Count+Pixel][A]+=OmitConnection[A]/4;
                        Output[40*Count+Pixel][A]+=OmitSuffix[A]/4;
                    }
                }
                Output[Pixel][3]=1;Output[18*Count+Pixel]={float(Calls),float(Calls),Method<2 || Method>=4?8.f:4.f,0};
            }
            const std::string Name=Directory+"/NativeAreaTransport"+std::to_string(Corpus)+std::to_string(Fixture)+".f32";
            std::ofstream File(Name,std::ios::binary);File.write(reinterpret_cast<const char*>(Output.data()),Output.size()*sizeof(Quartet));
            File.close();Check(bool(File));
            std::cout<<"FRESH_NATIVE_CASE corpus="<<Corpus<<" fixture="<<Fixture<<" pixels="<<Count<<" pass=1\n";
        }
    Check(PrimaryChecks==221184 && CandidateChecks==1474560 && PathChecks==1179648 && VisibilityChecks==2359296);
    std::cout<<"FRESH_NATIVE_TOTAL primary="<<PrimaryChecks<<" candidates="<<CandidateChecks<<" paths="<<PathChecks
             <<" visibility="<<VisibilityChecks<<" double_primary_ties="<<PrimaryTies<<" pass=1\n";
}
}
int main(int Count,char** Arguments)
{
    try
    {
        if (Count==2 && std::strcmp(Arguments[1],"--unit")==0) ValidateUnits();
        else if (Count==2) Capture(Arguments[1]);
        else throw std::runtime_error("Use --unit or output-directory;no GPU input is accepted");
        return 0;
    }
    catch(const std::exception& Error) { std::cerr<<Error.what()<<'\n';return 1; }
}
