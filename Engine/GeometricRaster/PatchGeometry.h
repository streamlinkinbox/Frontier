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
#include <limits>
#include <stdexcept>

namespace Frontier::PatchGeometry {
// v2: the coarse error is a measured surface deviation, not v1's summed collapse chain. A v1 entry decodes to a
//    ~30x larger error and would keep the old several-hundred-metre switch distance alive, so the version word
//    (and the directory) move together and every stale entry rebakes.
constexpr uint32_t kPatchCacheVersion = 2u;
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
// Closest point on a triangle to p (Ericson, Real-Time Collision Detection §5.1.5 — the Voronoi-region form).
// Exact for degenerate inputs because every region test is a sign comparison, and the final clamp keeps the
//    barycentric pair inside the face even when the triangle has near-zero area.
inline Vector3 ClosestOnTriangle(Vector3 p,Vector3 a,Vector3 b,Vector3 c){
 const Vector3 ab=b-a,ac=c-a,ap=p-a;
 const float d1=Dot(ab,ap),d2=Dot(ac,ap);
 if(d1<=0&&d2<=0)return a;
 const Vector3 bp=p-b;const float d3=Dot(ab,bp),d4=Dot(ac,bp);
 if(d3>=0&&d4<=d3)return b;
 const float vc=d1*d4-d3*d2;
 if(vc<=0&&d1>=0&&d3<=0){const float t=d1-d3!=0?d1/(d1-d3):0.f;return a+ab*t;}
 const Vector3 cp=p-c;const float d5=Dot(ab,cp),d6=Dot(ac,cp);
 if(d6>=0&&d5<=d6)return c;
 const float vb=d5*d2-d1*d6;
 if(vb<=0&&d2>=0&&d6<=0){const float t=d2-d6!=0?d2/(d2-d6):0.f;return a+ac*t;}
 const float va=d3*d6-d5*d4;
 if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0){const float den=(d4-d3)+(d5-d6);const float t=den!=0?(d4-d3)/den:0.f;return b+(c-b)*t;}
 const float den=va+vb+vc;if(den==0)return a;
 return a+ab*(vb/den)+ac*(vc/den);
}

// ⚠️ THE SELECTION DISTANCE IS THIS NUMBER. `CoarseError` is multiplied by the focal length and divided by the
//    view depth, so whatever it over-states, the camera has to travel that same factor further before the
//    alternative is allowed. v1 accumulated `radius[a] = max(radius[a], radius[b] + |ab|)` — the full edge length
//    of every collapse, summed along the collapse chain. On the native 960-triangle sphere that reported 0.19 to
//    0.47 object-space units for patches whose surface actually moves by a few millimetres: a ~30x over-estimate,
//    which is why nothing changed detail until the camera was several hundred metres away.
//
// This measures the deviation instead of bounding it by a chain sum: the one-sided distance from the ORIGINAL
//    patch surface to the simplified one, sampled at every fine vertex, every fine edge midpoint and every fine
//    triangle centroid (6 samples per fine triangle, ≤ 768 per patch), each closed against every simplified
//    triangle. It is a SAMPLED Hausdorff estimate, not a certified bound — the true maximum can sit between
//    samples — and for a 128-triangle patch the sampling is dense relative to the triangles that remain.
//    Measured once on the final index set: the collapse loop stops on the triangle target, never on the error,
//    so a running value would change nothing except the cost.
inline float MeasureDeviation(const std::vector<VertexRecord>& v,const std::vector<uint32_t>& fine,
                              const std::vector<uint32_t>& coarse){
 if(coarse.empty()||coarse.size()>=fine.size())return 0.f;
 // Two exact prunings, because this runs on the cold-load path: a SURVIVING fine triangle contributes distance
 //    zero at all seven of its samples (it IS part of the simplified surface), and a candidate triangle whose
 //    bounding sphere is already further than the best distance found so far cannot improve it.
 std::set<std::array<uint32_t,3>> kept;
 std::vector<Vector3> centre(coarse.size()/3);std::vector<float> reach(coarse.size()/3);
 for(size_t t=0,k=0;t+2<coarse.size();t+=3,++k){
  std::array<uint32_t,3> face{coarse[t],coarse[t+1],coarse[t+2]};std::sort(face.begin(),face.end());kept.insert(face);
  const Vector3 a=v[coarse[t]].SpatialLocation,b=v[coarse[t+1]].SpatialLocation,c=v[coarse[t+2]].SpatialLocation;
  centre[k]=(a+b+c)*(1.f/3.f);
  reach[k]=std::max({Length(a-centre[k]),Length(b-centre[k]),Length(c-centre[k])});
 }
 float worst=0.f;
 const auto Sample=[&](Vector3 p){
  float best=std::numeric_limits<float>::max();
  for(size_t t=0,k=0;t+2<coarse.size();t+=3,++k){
   if(Length(p-centre[k])-reach[k]>=best)continue;                       // cannot beat the incumbent
   best=std::min(best,Length(p-ClosestOnTriangle(p,v[coarse[t]].SpatialLocation,
                                                   v[coarse[t+1]].SpatialLocation,
                                                   v[coarse[t+2]].SpatialLocation)));
   if(best<=worst)return;                                                // cannot raise the maximum either
  }
  if(best<std::numeric_limits<float>::max())worst=std::max(worst,best);
 };
 for(size_t t=0;t+2<fine.size();t+=3){
  std::array<uint32_t,3> face{fine[t],fine[t+1],fine[t+2]};std::sort(face.begin(),face.end());
  if(kept.count(face))continue;                                          // unchanged triangle, deviation zero
  const Vector3 a=v[fine[t]].SpatialLocation,b=v[fine[t+1]].SpatialLocation,c=v[fine[t+2]].SpatialLocation;
  Sample(a);Sample(b);Sample(c);
  Sample((a+b)*.5f);Sample((b+c)*.5f);Sample((c+a)*.5f);
  Sample((a+b+c)*(1.f/3.f));
 }
 return std::isfinite(worst)&&worst>0.f?worst:0.f;
}

inline Baked Bake(const std::vector<VertexRecord>& v,const std::vector<uint32_t>& fine){
 Baked out{fine};if(fine.empty()||fine.size()>384||fine.size()%3)return out;
 for(auto i:fine)if(i>=v.size())return out;
 using Edge=std::pair<uint32_t,uint32_t>;
 auto edge=[](uint32_t a,uint32_t b){return Edge{std::min(a,b),std::max(a,b)};};
 std::map<Edge,unsigned> counts;
 for(size_t t=0;t<fine.size();t+=3)for(int j=0;j<3;++j)++counts[edge(fine[t+j],fine[t+(j+1)%3])];
 std::set<uint32_t> locked;
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
   (void)distance;   // v1 summed this; the deviation is measured against the final surface instead (see below)
   out.Indices=std::move(next);collapsed=true;break;
  }
  if(!collapsed)break;
 }
 out.Error=MeasureDeviation(v,fine,out.Indices);
 return out;
}
// Explicit little-endian words, magic/version/key/count/error/payload/checksum; no native structs on disk.
inline void Word(std::ostream& s,uint32_t x){for(int i=0;i<4;++i)s.put(char((x>>(i*8))&255));}
inline uint32_t Word(std::istream& s){uint32_t x=0;for(int i=0;i<4;++i){int c=s.get();if(c<0)throw std::runtime_error("truncated patch cache");x|=uint32_t(c)<<(8*i);}return x;}
inline Baked LoadOrBake(const std::vector<VertexRecord>& v,const std::vector<uint32_t>& fine){
 Baked out;uint64_t key=Key(v,fine);const char* env=std::getenv("FRONTIER_PATCH_CACHE");
 const auto root=std::filesystem::path(env?env:".frontier/cache/patch-geometry-v2");
 const auto path=root/(std::to_string(key)+".pgeom");
 try{std::ifstream f(path,std::ios::binary);if(f){
  if(Word(f)!=0x31475046u||Word(f)!=kPatchCacheVersion||Word(f)!=uint32_t(key)||Word(f)!=uint32_t(key>>32))throw std::runtime_error("patch cache version/key");
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
  Word(f,0x31475046u);Word(f,kPatchCacheVersion);Word(f,uint32_t(key));Word(f,uint32_t(key>>32));Word(f,uint32_t(out.Indices.size()));Word(f,bits);
  for(auto q:out.Indices){Word(f,q);check=Mix(check,q);}Word(f,uint32_t(check));Word(f,uint32_t(check>>32));f.close();
  if(f) {std::error_code ec;std::filesystem::rename(tmp,path,ec);if(ec)std::filesystem::remove(tmp,ec);}
 }catch(...){} // unwritable cache never prevents source geometry from loading
 return out;
}
}
