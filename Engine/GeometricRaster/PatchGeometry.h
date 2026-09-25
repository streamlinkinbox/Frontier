#pragma once
// Patch Geometry v1: conservative endpoint collapses with locked patch boundaries.
// One coarse alternative. Original vertices/UVs/normals and ray geometry remain intact.
#include "GeometryStructure.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <chrono>
#include <array>
#include <iterator>
#include <stdexcept>

namespace Frontier::PatchGeometry {
struct Baked { std::vector<uint32_t> Indices; float Error=0; bool CacheHit=false; };
inline float Length(Vector3 v){return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}
inline float Dot(Vector3 a,Vector3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vector3 Cross(Vector3 a,Vector3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline uint64_t Mix(uint64_t h,uint32_t x){for(int i=0;i<4;++i){h^=(x>>(8*i))&255;h*=1099511628211ull;}return h;}
inline uint64_t Key(const std::vector<VertexRecord>& v,const std::vector<uint32_t>& ix){
 uint64_t h=Mix(1469598103934665603ull,1);h=Mix(h,uint32_t(ix.size()));
 for(auto i:ix){h=Mix(h,i);const auto& p=v.at(i);
  for(float x:{p.SpatialLocation.x,p.SpatialLocation.y,p.SpatialLocation.z,p.NormalDirection.x,p.NormalDirection.y,p.NormalDirection.z,p.TextureCoordinateU,p.TextureCoordinateV,p.TangentDirection.x,p.TangentDirection.y,p.TangentDirection.z,p.TangentDirection.w})h=Mix(h,std::bit_cast<uint32_t>(x));
 }return h;
}
inline Baked Bake(const std::vector<VertexRecord>& v,const std::vector<uint32_t>& fine){
 Baked out{fine};if(fine.empty()||fine.size()>384||fine.size()%3)return out;
 for(auto i:fine)if(i>=v.size())return out;
 using Edge=std::pair<uint32_t,uint32_t>;
 auto edge=[](uint32_t a,uint32_t b){return Edge{std::min(a,b),std::max(a,b)};};
 std::map<Edge,unsigned> counts;
 for(size_t t=0;t<fine.size();t+=3)for(int j=0;j<3;++j)++counts[edge(fine[t+j],fine[t+(j+1)%3])];
 std::set<uint32_t> locked;std::map<uint32_t,float> radius;
 for(auto [e,n]:counts){if(n!=2){locked.insert(e.first);locked.insert(e.second);}}
 const size_t target=std::max<size_t>(6,fine.size()/2/3*3);
 while(out.Indices.size()>target){
  struct Candidate{float length;uint32_t a,b;};std::vector<Candidate> candidates;std::set<Edge> edges;
  for(size_t t=0;t<out.Indices.size();t+=3)for(int j=0;j<3;++j)edges.insert(edge(out.Indices[t+j],out.Indices[t+(j+1)%3]));
  for(auto [a,b]:edges){
   if(locked.count(a)||locked.count(b))continue;
   const auto&A=v[a];const auto&B=v[b];
   if(Dot(A.NormalDirection,B.NormalDirection)<.97f)continue;
   if(std::abs(A.TextureCoordinateU-B.TextureCoordinateU)>.08f||std::abs(A.TextureCoordinateV-B.TextureCoordinateV)>.08f)continue;
   if(A.TangentDirection.w!=B.TangentDirection.w)continue;
   candidates.push_back({Length(A.SpatialLocation-B.SpatialLocation),a,b});
  }
  std::sort(candidates.begin(),candidates.end(),[](auto a,auto b){return a.length<b.length||(a.length==b.length&&std::pair(a.a,a.b)<std::pair(b.a,b.b));});
  bool collapsed=false;
  for(auto [distance,a,b]:candidates){
   std::set<uint32_t> na,nb,opposite;unsigned adjacent=0;
   for(size_t t=0;t<out.Indices.size();t+=3){bool ha=false,hb=false;for(int j=0;j<3;++j){ha|=out.Indices[t+j]==a;hb|=out.Indices[t+j]==b;}
    for(int j=0;j<3;++j){auto q=out.Indices[t+j];if(ha&&q!=a)na.insert(q);if(hb&&q!=b)nb.insert(q);if(ha&&hb&&q!=a&&q!=b)opposite.insert(q);}if(ha&&hb)++adjacent;
   }
   std::vector<uint32_t> common;std::set_intersection(na.begin(),na.end(),nb.begin(),nb.end(),std::back_inserter(common));
   if(adjacent!=2||common.size()!=2||std::set<uint32_t>(common.begin(),common.end())!=opposite)continue;
   std::vector<uint32_t> next;bool valid=true;std::set<std::array<uint32_t,3>> unique;
   for(size_t t=0;t<out.Indices.size();t+=3){uint32_t old[3],n[3];for(int j=0;j<3;++j){old[j]=out.Indices[t+j];n[j]=old[j]==b?a:old[j];}
    if(n[0]==n[1]||n[1]==n[2]||n[0]==n[2])continue;
    auto before=Cross(v[old[1]].SpatialLocation-v[old[0]].SpatialLocation,v[old[2]].SpatialLocation-v[old[0]].SpatialLocation);
    auto after=Cross(v[n[1]].SpatialLocation-v[n[0]].SpatialLocation,v[n[2]].SpatialLocation-v[n[0]].SpatialLocation);
    if(Length(after)<1e-10f||Dot(before,after)<=.2f*Length(before)*Length(after)){valid=false;break;}
    std::array<uint32_t,3> canonical{n[0],n[1],n[2]};std::sort(canonical.begin(),canonical.end());
    if(!unique.insert(canonical).second){valid=false;break;}
    next.insert(next.end(),n,n+3);
   }
   if(!valid||next.size()<target)continue;
   radius[a]=std::max(radius[a],radius[b]+distance);out.Error=std::max(out.Error,radius[a]);
   out.Indices=std::move(next);collapsed=true;break;
  }
  if(!collapsed)break;
 }
 return out;
}
// Explicit little-endian words, magic/version/key/count/error/payload/checksum; no native structs on disk.
inline void Word(std::ostream& s,uint32_t x){for(int i=0;i<4;++i)s.put(char((x>>(i*8))&255));}
inline uint32_t Word(std::istream& s){uint32_t x=0;for(int i=0;i<4;++i){int c=s.get();if(c<0)throw std::runtime_error("truncated patch cache");x|=uint32_t(c)<<(8*i);}return x;}
inline Baked LoadOrBake(const std::vector<VertexRecord>& v,const std::vector<uint32_t>& fine){
 Baked out;uint64_t key=Key(v,fine);const char* env=std::getenv("FRONTIER_PATCH_CACHE");
 const auto root=std::filesystem::path(env?env:".frontier/cache/patch-geometry-v1");
 const auto path=root/(std::to_string(key)+".pgeom");
 try{std::ifstream f(path,std::ios::binary);if(f){
  if(Word(f)!=0x31475046u||Word(f)!=1||Word(f)!=uint32_t(key)||Word(f)!=uint32_t(key>>32))throw std::runtime_error("patch cache version/key");
  uint32_t n=Word(f),bits=Word(f);if(n>fine.size()||n%3||n<3)throw std::runtime_error("patch cache size");
  out.Error=std::bit_cast<float>(bits);if(!std::isfinite(out.Error)||out.Error<0)throw std::runtime_error("patch cache error");
  uint64_t check=Mix(key,bits);std::set<uint32_t> allowed(fine.begin(),fine.end());
  for(uint32_t i=0;i<n;++i){uint32_t q=Word(f);if(!allowed.count(q))throw std::runtime_error("patch cache index");out.Indices.push_back(q);check=Mix(check,q);}
  if(Word(f)!=uint32_t(check)||Word(f)!=uint32_t(check>>32)||f.peek()!=EOF)throw std::runtime_error("patch cache checksum");
  out.CacheHit=true;return out;
 }}catch(...){out={};std::error_code ec;std::filesystem::remove(path,ec);}
 out=Bake(v,fine);
 try{std::filesystem::create_directories(root);auto tmp=path;tmp+=std::string(".")+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".tmp";
  std::ofstream f(tmp,std::ios::binary|std::ios::trunc);auto bits=std::bit_cast<uint32_t>(out.Error);uint64_t check=Mix(key,bits);
  Word(f,0x31475046u);Word(f,1u);Word(f,uint32_t(key));Word(f,uint32_t(key>>32));Word(f,uint32_t(out.Indices.size()));Word(f,bits);
  for(auto q:out.Indices){Word(f,q);check=Mix(check,q);}Word(f,uint32_t(check));Word(f,uint32_t(check>>32));f.close();
  if(f) {std::error_code ec;std::filesystem::rename(tmp,path,ec);if(ec)std::filesystem::remove(tmp,ec);}
 }catch(...){} // unwritable cache never prevents source geometry from loading
 return out;
}
}
