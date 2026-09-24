#include "IconArt.h"
#include <thorvg.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>
#include <stb_easy_font.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using namespace Frontier;
namespace fs=std::filesystem;
namespace {
int Gates=0;
void Require(bool Good,const char* Caption){if(!Good)throw std::runtime_error(Caption);++Gates;std::cout<<"PASS "<<Caption<<'\n';}
void Write(const fs::path& Path,const std::string& Text){fs::create_directories(Path.parent_path());std::ofstream(Path)<<Text;}
void Png(const fs::path& Path,const IconRaster& Raster){fs::create_directories(Path.parent_path());if(!stbi_write_png(Path.string().c_str(),Raster.Width,Raster.Height,4,Raster.Rgba.data(),Raster.Width*4))throw std::runtime_error("PNG write failed");}
std::string Svg(const std::string& Body,const std::string& View="0 0 40 40"){return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\""+View+"\">"+Body+"</svg>";}
std::shared_ptr<const IconRaster> Fixture(const fs::path& Root,const char* Caption,const std::string& Body,uint32_t W=40,uint32_t H=40,const std::string& View="0 0 40 40"){
    const fs::path Folder=Root/Caption;Write(Folder/"sun.svg",Svg(Body,View));IconArt Art(Folder);auto R=Art.Rasterize(IconSymbol::Sun,float(W),float(H));Require(R->Result==IconResult::Ready,Caption);Png(Root/(std::string(Caption)+".png"),*R);return R;
}
int Byte(const IconRaster& R,int X,int Y,int Channel){return R.Rgba[(size_t(Y)*R.Width+X)*4+Channel];}
struct Sheet {
    int W,H;std::vector<uint8_t> Pixels;
    Sheet(int Width,int Height):W(Width),H(Height),Pixels(size_t(W)*H*4,255){for(int Y=0;Y<H;++Y)for(int X=0;X<W;++X)for(int C=0;C<3;++C)Pixels[(size_t(Y)*W+X)*4+C]=28;}
    void Rect(int X,int Y,int Width,int Height,uint8_t R,uint8_t G,uint8_t B){for(int J=std::max(0,Y);J<std::min(H,Y+Height);++J)for(int I=std::max(0,X);I<std::min(W,X+Width);++I){const size_t P=(size_t(J)*W+I)*4;Pixels[P]=R;Pixels[P+1]=G;Pixels[P+2]=B;}}
    void Text(int X,int Y,const std::string& Text){struct Vertex {float X,Y,Z;uint8_t C[4];};std::vector<Vertex> Vertices(Text.size()*64);int Quads=stb_easy_font_print(float(X),float(Y),const_cast<char*>(Text.c_str()),nullptr,Vertices.data(),int(Vertices.size()*sizeof(Vertex)));for(int I=0;I<Quads;++I){const auto* V=Vertices.data()+I*4;Rect(int(V[0].X),int(V[0].Y),int(V[1].X-V[0].X),int(V[2].Y-V[0].Y),210,210,215);}}
    void Draw(int X,int Y,const IconRaster& R){for(uint32_t J=0;J<R.Height;++J)for(uint32_t I=0;I<R.Width;++I){if(X+int(I)<0||Y+int(J)<0||X+int(I)>=W||Y+int(J)>=H)continue;const size_t P=(size_t(J)*R.Width+I)*4,Q=(size_t(Y+J)*W+X+I)*4;const unsigned A=R.Rgba[P+3];for(int C=0;C<3;++C)Pixels[Q+C]=uint8_t((R.Rgba[P+C]*A+Pixels[Q+C]*(255-A)+127)/255);}}
    void Save(const fs::path& Path){if(!stbi_write_png(Path.string().c_str(),W,H,4,Pixels.data(),W*4))throw std::runtime_error("sheet write failed");}
};
void Verify(const fs::path& Root,const fs::path& Scratch){
    IconArt Art(Root);
    auto A=Art.Rasterize(IconSymbol::CollectionBracketedObjects,30,30);
    Require(A->Result==IconResult::Ready&&!A->Substitute,"approved bracketed collection renders");
    auto B=Art.Rasterize(IconSymbol::CollectionBracketedObjects,30,30);auto C=Art.Rasterize(IconSymbol::CollectionBracketedObjects,15,15,2);
    Require(A==B&&A==C&&Art.Statistics().Decodes==1&&Art.Statistics().Rasterizations==1,"same physical extent reuses identical immutable raster without decode");
    auto D=Art.Rasterize(IconSymbol::CollectionBracketedObjects,30,30,2);
    Require(D->Width==60&&D!=A,"display scale produces correct physical resolution");
    Art.Clear();Require(Art.Statistics().ResidentBytes==0&&A->Rgba.size()==3600,"clear releases resident ownership without dangling returned pixels");
    for(float Size:{0.0f,-1.0f,513.0f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})Require(Art.Rasterize(IconSymbol::Sun,Size,30)->Result==IconResult::InvalidRequest,"invalid dimensions refused with bounded substitute");
    Require(Art.Rasterize(IconSymbol::Count,30,30)->Result==IconResult::InvalidRequest,"invalid typed identifier refused");
    Require(Art.Rasterize(IconSymbol::Sun,30,30,-1)->Result==IconResult::InvalidRequest,"invalid display scale refused");
    IconArt Tiny(Root,4096);auto Kept=Tiny.Rasterize(IconSymbol::CollectionBracketedObjects,30,30);auto Evicting=Tiny.Rasterize(IconSymbol::FolderEnvironment,30,30);
    Require(Tiny.Statistics().Evictions==1&&Tiny.Statistics().ResidentBytes<=4096&&Kept->Rgba==A->Rgba,"bounded least-recently-used eviction retains borrowed pixels");
    IconArt Missing(Scratch/"absent");auto Bad=Missing.Rasterize(IconSymbol::Sun,30,30);Require(Bad->Result==IconResult::Missing&&Bad->Substitute,"missing asset has explicit reason and visible substitute");Png(Scratch/"Missing.png",*Bad);
    Write(Scratch/"broken/sun.svg","not an svg");IconArt Broken(Scratch/"broken");Require(Broken.Rasterize(IconSymbol::Sun,30,30)->Result==IconResult::DecodeFailure,"malformed SVG refused");
    const auto Channels=Fixture(Scratch,"Channels","<rect width=\"20\" height=\"40\" fill=\"#ff0000\" fill-opacity=\".5\"/><rect x=\"20\" width=\"20\" height=\"40\" fill=\"#0000ff\"/>");
    Require(Byte(*Channels,10,20,0)>250&&Byte(*Channels,10,20,2)==0&&std::abs(Byte(*Channels,10,20,3)-128)<=1,"RGBA red and straight alpha are correct (not premultiplied)");
    Require(Byte(*Channels,30,20,0)==0&&Byte(*Channels,30,20,2)==255&&Byte(*Channels,30,20,3)==255,"RGBA blue channel is not swapped");
    const auto Aspect=Fixture(Scratch,"Aspect","<rect width=\"80\" height=\"40\" fill=\"#00ff00\"/>",80,80,"0 0 80 40");
    Require(Byte(*Aspect,40,10,3)==0&&Byte(*Aspect,40,70,3)==0&&Byte(*Aspect,40,40,1)==255,"non-square SVG is centered without stretch or opaque margins");
    const auto Clip=Fixture(Scratch,"Clipping","<defs><clipPath id=\"c\"><circle cx=\"20\" cy=\"20\" r=\"12\"/></clipPath></defs><rect width=\"40\" height=\"40\" fill=\"#00ff00\" clip-path=\"url(#c)\"/>");
    Require(Byte(*Clip,1,1,3)==0&&Byte(*Clip,20,20,1)==255,"clipping excludes corners and preserves interior");
    const auto Gradient=Fixture(Scratch,"Gradient","<defs><linearGradient id=\"g\"><stop stop-color=\"#ff0000\"/><stop offset=\"1\" stop-color=\"#0000ff\"/></linearGradient></defs><rect width=\"40\" height=\"40\" fill=\"url(#g)\"/>");
    Require(Byte(*Gradient,2,20,0)>Byte(*Gradient,2,20,2)&&Byte(*Gradient,37,20,2)>Byte(*Gradient,37,20,0),"gradient endpoints preserve colour direction");
    const auto Blur=Fixture(Scratch,"GaussianBlur","<defs><filter id=\"b\" x=\"-100%\" y=\"-100%\" width=\"300%\" height=\"300%\"><feGaussianBlur stdDeviation=\"2\"/></filter></defs><rect x=\"12\" y=\"12\" width=\"16\" height=\"16\" fill=\"white\" filter=\"url(#b)\"/>");
    Require(Byte(*Blur,10,20,3)>0&&Byte(*Blur,10,20,3)<Byte(*Blur,20,20,3),"Gaussian blur creates a translucent exterior rather than a hard edge");
    Write(Scratch/"unsupported/sun.svg",Svg("<defs><filter id=\"n\"><feTurbulence baseFrequency=\".4\"/></filter></defs><rect width=\"40\" height=\"40\" fill=\"red\" filter=\"url(#n)\"/>"));
    IconArt UnsupportedArt(Scratch/"unsupported");auto U=UnsupportedArt.Rasterize(IconSymbol::Sun,40,40);
    Require(U->Result==IconResult::Unsupported&&U->Substitute&&U->Diagnostic.find("feTurbulence")!=std::string::npos,"unsupported effects are diagnosed, never silently accepted");
    auto Diagnostic=UnsupportedArt.Rasterize(IconSymbol::Sun,40,40,1,IconPolicy::Diagnostic);
    Require(Diagnostic->Result==IconResult::Unsupported&&!Diagnostic->Substitute,"opt-in diagnostic rendering retains unsupported designation");
    Png(Scratch/"Unsupported.png",*U);
    Require(tvg::Initializer::init(0)==tvg::Result::Success,"existing host can retain ThorVG lifetime");
    std::shared_ptr<const IconRaster> Survives;
    for(int I=0;I<8;++I){IconArt Scoped(Root);Survives=Scoped.Rasterize(IconSymbol::CollectionBracketedObjects,24,24);}
    Require(Survives->Rgba.size()==24*24*4,"pixels survive IconArt destruction");
    Require(tvg::Initializer::term()==tvg::Result::Success,"IconArt releases only its own runtime references");
}
}
int main(int Argc,char** Argv){try{
    if(Argc!=4){std::cerr<<"usage: IconArtProof <SVG root> <output> <temporary folder>\n";return 2;}
    const fs::path Root=Argv[1],Out=Argv[2],Scratch=Argv[3];fs::create_directories(Out);fs::create_directories(Scratch);
    Verify(Root,Scratch);
    IconArt Art(Root);
    const int Count=int(IconSymbol::Count),Columns=3,CellW=360,CellH=170,Height=60+((Count+Columns-1)/Columns)*CellH;
    Sheet Strict(Columns*CellW,Height),Diagnostic(Columns*CellW,Height);
    Strict.Text(18,20,"THORVG CPU / STRICT / 24, 30, 48, 96 PX / PINK X = UNSUPPORTED OR FAILED");
    Diagnostic.Text(18,20,"THORVG CPU / DIAGNOSTIC ONLY / UNSUPPORTED EFFECTS MAY BE MISSING / NOT APPROVED");
    std::ofstream Report(Out/"IconResults.tsv");Report<<"symbol\tfile\tresult\tsubstitute\tdiagnostic\n";
    int Ready=0,UnsupportedCount=0;
    for(int I=0;I<Count;++I){const auto Symbol=static_cast<IconSymbol>(I);int X=(I%Columns)*CellW+16,Y=60+(I/Columns)*CellH;
        auto R=Art.Rasterize(Symbol,96,96);Report<<IconArt::Name(Symbol)<<'\t'<<IconArt::Filename(Symbol)<<'\t'<<IconArt::ResultName(R->Result)<<'\t'<<R->Substitute<<'\t'<<(R->Diagnostic.empty()?"-":R->Diagnostic)<<'\n';
        if(R->Result==IconResult::Ready)++Ready;else if(R->Result==IconResult::Unsupported)++UnsupportedCount;
        Require(R->Result==IconResult::Ready||R->Result==IconResult::Unsupported,"shipped SVG decoded or explicitly unsupported");
        Strict.Text(X,Y,IconArt::Name(Symbol));Diagnostic.Text(X,Y,IconArt::Name(Symbol));
        Strict.Text(X,Y+145,IconArt::ResultName(R->Result));Diagnostic.Text(X,Y+145,IconArt::ResultName(R->Result));
        int Offset=0;for(int Size:{24,30,48,96}){auto S=Art.Rasterize(Symbol,float(Size),float(Size));auto D=Art.Rasterize(Symbol,float(Size),float(Size),1,IconPolicy::Diagnostic);Strict.Draw(X+Offset,Y+30+96-Size,*S);Diagnostic.Draw(X+Offset,Y+30+96-Size,*D);Offset+=Size+18;}
        auto Large=Art.Rasterize(Symbol,128,128,1,IconPolicy::Diagnostic);if(Large->Substitute)throw std::runtime_error(std::string("Diagnostic decode failed: ")+IconArt::Name(Symbol));Png(Out/"Raster"/(std::string(IconArt::Name(Symbol))+".png"),*Large);
    }
    Strict.Save(Out/"IconArtStrict.png");Diagnostic.Save(Out/"IconArtDiagnostic.png");
    std::cout<<"SUMMARY "<<Ready<<" ready, "<<UnsupportedCount<<" unsupported; "<<Gates<<" gates passed.\n";
    std::cout<<"Diagnostic sheets are not approval of omitted SVG features. No GPU execution.\n";
    return 0;
}catch(const std::exception& E){std::cerr<<"FAIL "<<E.what()<<'\n';return 1;}}
