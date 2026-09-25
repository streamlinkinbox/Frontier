#include "Engine/GeometricRaster/PatchReference.h"
#include "Engine/Editor/ConstructWorld.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <array>
using namespace Frontier;
using namespace Frontier::PatchGeometry;
int Checks=0;
void Check(bool b,const char* message){++Checks;if(!b)throw std::runtime_error(message);}
using Edge=std::pair<uint32_t,uint32_t>;
std::map<Edge,int> Edges(const std::vector<uint32_t>& ix){std::map<Edge,int> e;for(size_t t=0;t<ix.size();t+=3)for(int j=0;j<3;++j){auto a=ix[t+j],b=ix[t+(j+1)%3];++e[{std::min(a,b),std::max(a,b)}];}return e;}
CameraClipConfiguration Camera(float distance){return {{0,-distance,0},{0,1,0},{1,0,0},{0,0,1},.57735f,1,.01f};}
uint32_t Selected(const SceneStructure& scene,float distance,bool preview=true){uint32_t n=0;auto cam=Camera(distance);for(auto& c:scene.QueryClusters()){
 auto& i=scene.QueryInstances()[c.InstanceIndex];auto& m=scene.QueryMaterials().QueryRecords()[i.MaterialIndex];auto& s=scene.QueryMaterials().QuerySlabRecords()[m.SlabOffset];
 n+=Select(c,i,m,&s,cam,720,preview)?c.CoarseTriangleCount:c.TriangleCount;
}return n;}
void Validate(const SceneStructure& s){uint32_t fineTotal=0;for(auto&i:s.QueryInstances()){
 fineTotal+=i.TriangleCount;Check(i.TriangleCount<=8192,"instance budget");
 for(uint32_t j=0;j<i.ClusterCount;++j){auto& c=s.QueryClusters()[i.ClusterOffset+j];Check(c.FirstIndex==i.FirstIndex+c.FirstPrimitive*3,"fine primitive address");
 if(!c.CoarseTriangleCount)continue;
 Check(c.CoarseFirstPrimitive+c.CoarseTriangleCount<=16384,"14-bit alternate token");Check(c.CoarseFirstIndex==i.FirstIndex+c.CoarseFirstPrimitive*3,"alternate primitive address");Check(c.CoarseFirstPrimitive>=i.TriangleCount,"no overwrite of ray range");
 Check(c.CoarseTriangleCount<c.TriangleCount,"coarse actually reduces triangles");Check(std::isfinite(c.CoarseError)&&c.CoarseError>=0,"finite error");
 std::vector<uint32_t> fine(s.QueryIndices().begin()+c.FirstIndex,s.QueryIndices().begin()+c.FirstIndex+c.TriangleCount*3),coarse(s.QueryIndices().begin()+c.CoarseFirstIndex,s.QueryIndices().begin()+c.CoarseFirstIndex+c.CoarseTriangleCount*3);
 auto fe=Edges(fine),ce=Edges(coarse);for(auto [e,n]:fe)if(n==1)Check(ce[e]==1,"boundary edges remain exact, no inter-patch cracks");
 std::set<uint32_t> allowed(fine.begin(),fine.end());for(auto index:coarse)Check(allowed.count(index),"coarse indices retain existing attributes");
 for(auto [e,n]:ce)Check(n<=2,"manifold coarse edges");
 std::set<std::array<uint32_t,3>> unique;
 for(size_t t=0;t<coarse.size();t+=3){std::array<uint32_t,3> tri{coarse[t],coarse[t+1],coarse[t+2]};std::sort(tri.begin(),tri.end());Check(unique.insert(tri).second,"no duplicate faces");}
 }
 }Check(fineTotal==s.QueryTriangleCount(),"public scene count excludes alternatives");Check(fineTotal==s.QueryFlatTriangles().size(),"secondary rays retain ALL fine triangles, NO alternatives");}
// Perspective painter's reference, not a Vulkan screenshot. Uses actual native mesh indices
// and the shared policy. Far image is optically magnified for topology inspection only.
void Svg(const SceneStructure& s,const std::string& path,float distance,bool preview){
 struct Face{float depth;uint32_t patch;float p[6];};std::vector<Face> faces;auto cam=Camera(distance);
 for(uint32_t id=0;id<s.QueryClusters().size();++id){auto& c=s.QueryClusters()[id];auto& i=s.QueryInstances()[c.InstanceIndex];auto& m=s.QueryMaterials().QueryRecords()[i.MaterialIndex];auto& slab=s.QueryMaterials().QuerySlabRecords()[m.SlabOffset];bool coarse=Select(c,i,m,&slab,cam,720,preview);
 uint32_t start=coarse?c.CoarseFirstIndex:c.FirstIndex,count=coarse?c.CoarseTriangleCount:c.TriangleCount;
 for(uint32_t t=0;t<count;++t){Face f{};f.patch=id;for(int k=0;k<3;++k){auto p=TransformPoint(ProjectionFromColumns(i.World),s.QueryVertices()[s.QueryIndices()[start+t*3+k]+i.VertexOffset].SpatialLocation)-cam.Origin;f.depth+=p.y;float magnify=distance/1.7f;f.p[k*2]=360+360*p.x/(p.y*cam.TanHalfFieldOfView)*magnify;f.p[k*2+1]=390-360*p.z/(p.y*cam.TanHalfFieldOfView)*magnify;}faces.push_back(f);}}
 std::sort(faces.begin(),faces.end(),[](auto a,auto b){return a.depth>b.depth;});std::ofstream out(path);out<<"<svg xmlns='http://www.w3.org/2000/svg' width='720' height='760' viewBox='0 0 720 760'><rect width='720' height='760' fill='#16191f'/><g font-family='sans-serif' fill='#eee'><text x='28' y='38' font-size='23'>Patch Geometry — CPU reference</text><text x='28' y='65' font-size='14'>"<<(preview?"Native coarse selection":"Original native sphere")<<" · "<<faces.size()<<" triangles · not a GPU capture</text></g>";
 for(auto& f:faces){out<<"<polygon points='";for(int k=0;k<3;++k)out<<f.p[k*2]<<","<<f.p[k*2+1]<<" ";uint32_t hash=f.patch*2654435761u;hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;hash*=0x846ca68bu;hash^=hash>>16;
 out<<"' fill='rgb("<<int((hash&255)*.8f+51)<<","<<int(((hash>>8)&255)*.8f+51)<<","<<int(((hash>>16)&255)*.8f+51)<<")' stroke='#18202a' stroke-width='.75'/>";}
 out<<"<text x='28' y='732' fill='#b4bbc7' font-size='13' font-family='sans-serif'>Camera distance "<<distance<<" m; display magnified; original ray mesh unchanged.</text></svg>";
}
int main(int argc,char**argv){try{
 Check(sizeof(ClusterRecord)==64&&offsetof(ClusterRecord,CoarseFirstIndex)==48,"runtime/persistent ABI");
 SceneStructure sphere;
 for(auto kind:{ConstructKind::Cube,ConstructKind::Sphere,ConstructKind::Cylinder,ConstructKind::Cone,ConstructKind::Plane,ConstructKind::Torus,ConstructKind::Area}){
  SceneStructure scene;ConstructRequest r;r.Kind=kind;Check(bool(ConstructEntity(scene,r)),"actual Construct entity build");Validate(scene);
  uint32_t fine=uint32_t(scene.QueryFlatTriangles().size()),far=Selected(scene,1000);
  Check(Selected(scene,1000,false)==fine,"production always fine");
  if(kind==ConstructKind::Sphere||kind==ConstructKind::Torus)Check(far<fine,"native parametric primitive has usable coarse geometry");
  if(kind==ConstructKind::Area)Check(far==fine,"emitter protected");
  std::cout<<ConstructName(kind)<<": fine="<<fine<<", preview far="<<far<<", patches="<<scene.QueryClusters().size()<<"\n";
 }
 ConstructRequest request;request.Kind=ConstructKind::Sphere;Check(bool(ConstructEntity(sphere,request)),"sphere reference");
 auto fine=uint32_t(sphere.QueryFlatTriangles().size());auto far=Selected(sphere,1000);Check(far<fine,"far selection reduces");Check(Selected(sphere,1.7f)>=far,"distance policy");
 if(argc>1){std::filesystem::create_directories(argv[1]);Svg(sphere,std::string(argv[1])+"/native-sphere-fine.svg",1.7f,false);Svg(sphere,std::string(argv[1])+"/native-sphere-coarse.svg",1000,true);}
 auto& desc=*sphere.AccessMaterials().AccessDescriptor(0);desc.Slabs[0].TransmissionWeight=1;sphere.Finalise();Check(Selected(sphere,1000)==fine,"LIVE opaque-to-glass edit protects complete shell");Validate(sphere);
 desc.Slabs[0].TransmissionWeight=0;desc.Slabs[0].GeometryOpacity=.5f;sphere.Finalise();Check(Selected(sphere,1000)==fine,"opacity edit protected");
 for(int flags:{2,4,16,32})Check(!PatchOpaque(1,flags,0,0,1,false,false,false),"material flags protected");
 Check(!PatchOpaque(2,0,0,0,1,false,false,false),"layers protected");Check(!PatchOpaque(1,0,0,1,1,false,false,false),"SSS protected");Check(!PatchOpaque(1,0,0,0,1,true,false,false),"uncertain textures protected");Check(!PatchOpaque(1,0,0,0,1,false,false,true),"thin slab protected");
 for(int distance=2;distance<1002;++distance){bool chosen=PatchChooseCoarse(true,true,true,false,.01f,1,float(distance),.5f,0,600,.01f);
  if(chosen)Check(PatchChooseCoarse(true,true,true,false,.01f,1,float(distance+1),.5f,0,600,.01f),"distance monotonic");
  Check(!PatchChooseCoarse(true,false,true,true,.01f,1,float(distance),.5f,0,600,.01f),"glass never collapses even backfacing");}
 Check(PatchChooseCoarse(true,true,true,true,.1f,1,5,.5f,0,600,.01f),"opaque backfacing uses coarse");Check(!PatchChooseCoarse(true,true,true,true,.1f,1,.2f,.5f,0,600,.01f),"near-plane guard");Check(!PatchChooseCoarse(true,true,true,true,std::numeric_limits<float>::quiet_NaN(),1,5,.5f,0,600,.01f),"NaN fail closed");
 auto& c=sphere.QueryClusters()[0];std::vector<uint32_t> ix(sphere.QueryIndices().begin()+c.FirstIndex,sphere.QueryIndices().begin()+c.FirstIndex+c.TriangleCount*3);
 auto key=Key(sphere.QueryVertices(),ix);auto path=std::filesystem::path(std::getenv("FRONTIER_PATCH_CACHE"))/(std::to_string(key)+".pgeom");std::filesystem::remove(path);
 auto cold=LoadOrBake(sphere.QueryVertices(),ix),warm=LoadOrBake(sphere.QueryVertices(),ix);Check(!cold.CacheHit&&warm.CacheHit&&cold.Indices==warm.Indices&&cold.Error==warm.Error,"cold/warm cache roundtrip");
 {std::fstream f(path,std::ios::binary|std::ios::in|std::ios::out);f.seekp(24);f.put(char(255));}
 auto repair=LoadOrBake(sphere.QueryVertices(),ix);Check(!repair.CacheHit&&repair.Indices==cold.Indices,"corrupt cache fallback");Check(LoadOrBake(sphere.QueryVertices(),ix).CacheHit,"corrupt cache repaired");
 {std::ofstream f(path,std::ios::binary|std::ios::trunc);f<<"FPG";}
 Check(!LoadOrBake(sphere.QueryVertices(),ix).CacheHit,"truncated cache fallback");
 // Header corruption is bounded and never trusted as an allocation/index count.
 for(auto field:std::vector<std::pair<int,uint32_t>>{{4,99},{8,0},{16,0xffffffffu},{20,0x7f800000u}}){
  {std::fstream f(path,std::ios::binary|std::ios::in|std::ios::out);f.seekp(field.first);Word(f,field.second);}
  Check(!LoadOrBake(sphere.QueryVertices(),ix).CacheHit,"invalid version/key/count/error fallback");
 }
 // An indexed disk enters the common path, but its silhouette is never decimated.
 {GeometryStructure disk;std::vector<VertexRecord> dv(65);dv[0].NormalDirection={0,0,1};
  std::vector<uint32_t> di;for(unsigned j=0;j<64;++j){float a=6.28318530718f*j/64;dv[j+1].SpatialLocation={std::cos(a),std::sin(a),0};dv[j+1].NormalDirection={0,0,1};for(auto q:{0u,j+1,(j+1)%64+1})di.push_back(q);}
  disk.AppendVertices(dv.data(),dv.size());disk.AppendIndices(di.data(),di.size());SceneStructure ds;MaterialDescriptor md;md.Slabs.emplace_back();auto mi=ds.RegisterMaterial(md);ds.RegisterInstance(disk,Matrix4x4{},mi,0);ds.Finalise();Validate(ds);Check(Selected(ds,1000)==64,"disk silhouette boundary retained");
  std::cout<<"Indexed disk: fine=64, preview far="<<Selected(ds,1000)<<", boundary protected\n";
 }
 // More than 16k fine triangles: real registration split + alternate token addressing.
 GeometryStructure mesh;std::vector<VertexRecord> vertices;std::vector<uint32_t> indices;
 for(uint32_t y=0;y<=92;++y)for(uint32_t x=0;x<=92;++x){VertexRecord v{};v.SpatialLocation={float(x),float(y),0};v.NormalDirection={0,0,1};v.TangentDirection={1,0,0,1};v.TextureCoordinateU=float(x)/92;v.TextureCoordinateV=float(y)/92;vertices.push_back(v);}
 for(uint32_t y=0;y<92;++y)for(uint32_t x=0;x<92;++x){uint32_t a=y*93+x;for(auto q:{a,a+1,a+94,a,a+94,a+93})indices.push_back(q);}
 mesh.AppendVertices(vertices.data(),vertices.size());mesh.AppendIndices(indices.data(),indices.size());SceneStructure large;MaterialDescriptor mat;mat.Slabs.emplace_back();auto slot=large.RegisterMaterial(mat);large.RegisterInstance(mesh,Matrix4x4{},slot,0u);large.Finalise();Validate(large);Check(large.QueryInstances().size()==3,"large mesh split");
 std::cout<<"PASS "<<Checks<<" CPU checks. No Vulkan runtime/image/timing validation.\n";
}catch(const std::exception&e){std::cerr<<"FAIL after "<<Checks<<": "<<e.what()<<"\n";return 1;}}
