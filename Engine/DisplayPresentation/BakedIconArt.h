#pragma once
#include "IconArt.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
namespace Frontier {
// Build-time browser rasterization of approved local SVGs. Exact source bytes are
// embedded to reject stale bakes. Never silently strips unsupported SVG filters.
inline bool LoadApprovedIconBake(const std::filesystem::path& Path,const std::string& Source,IconRaster& Out){
 std::ifstream File(Path,std::ios::binary|std::ios::ate);
 if(!File||File.tellg()<16||File.tellg()>1024*1024)return false;
 std::vector<uint8_t> Bytes(static_cast<size_t>(File.tellg()));File.seekg(0);
 File.read(reinterpret_cast<char*>(Bytes.data()),static_cast<std::streamsize>(Bytes.size()));
 if(!File||std::memcmp(Bytes.data(),"FIB1",4))return false;
 auto U32=[&](size_t At){return uint32_t(Bytes[At])|(uint32_t(Bytes[At+1])<<8)|(uint32_t(Bytes[At+2])<<16)|(uint32_t(Bytes[At+3])<<24);};
 const uint32_t W=U32(4),H=U32(8),Length=U32(12);
 if(W!=256||H!=256||Length!=Source.size()||Bytes.size()!=16ull+Length+uint64_t(W)*H*4||std::memcmp(Bytes.data()+16,Source.data(),Length))return false;
 if(!Out.Width||!Out.Height||Out.Width>512||Out.Height>512)return false;
 Out.Rgba.assign(size_t(Out.Width)*Out.Height*4,0);
 const auto* Pixels=Bytes.data()+16+Length;
 const uint32_t Size=std::min(Out.Width,Out.Height),OX=(Out.Width-Size)/2,OY=(Out.Height-Size)/2;
 // Area filtering in premultiplied alpha, returning the adapter's straight RGBA.
 for(uint32_t Y=0;Y<Size;++Y)for(uint32_t X=0;X<Size;++X){
  const double X0=double(X)*W/Size,X1=double(X+1)*W/Size,Y0=double(Y)*H/Size,Y1=double(Y+1)*H/Size;
  double A=0,RGB[3]={},Area=(X1-X0)*(Y1-Y0);
  for(uint32_t SY=uint32_t(Y0);SY<std::min(H,uint32_t(std::ceil(Y1)));++SY)
   for(uint32_t SX=uint32_t(X0);SX<std::min(W,uint32_t(std::ceil(X1)));++SX){
    const double Weight=(std::min(X1,double(SX+1))-std::max(X0,double(SX)))*(std::min(Y1,double(SY+1))-std::max(Y0,double(SY)));
    const auto* P=Pixels+(size_t(SY)*W+SX)*4;const double Alpha=Weight*P[3];A+=Alpha;
    for(int C=0;C<3;++C)RGB[C]+=Alpha*P[C];
   }
  auto* P=Out.Rgba.data()+(size_t(Y+OY)*Out.Width+X+OX)*4;
  P[3]=uint8_t(std::clamp(std::lround(A/Area),0l,255l));
  if(A>0)for(int C=0;C<3;++C)P[C]=uint8_t(std::clamp(std::lround(RGB[C]/A),0l,255l));
 }
 Out.Result=IconResult::Ready;Out.Substitute=false;
 Out.Diagnostic="Approved SVG, browser-baked with filters intact; source bytes verified.";return true;
}
}
