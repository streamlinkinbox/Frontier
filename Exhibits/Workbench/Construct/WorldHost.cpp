#include "ConstructWorld.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <limits>
#include <stdexcept>
using namespace Frontier;
void Check(bool V,const char* M){if(!V)throw std::runtime_error(M);}
void Test(){
 SceneStructure W;
 unsigned Expected[]={12,960,128,64,2,768,2,0,0};
 unsigned Total=0;
 for(unsigned K=0;K<unsigned(ConstructKind::Count);++K){
  ConstructRequest R;R.Kind=ConstructKind(K);R.Position[0]=float(K)*2;R.Name="Object";
  auto Result=ConstructEntity(W,R);Check(bool(Result),"create refused");Check(Result.Placement==K,"stable placement ID");
  Total+=Expected[K];Check(W.QueryTriangleCount()==Total,"primitive triangle count");
  Check(W.QueryFlatTriangles().size()==Total,"renderer triangle table not rebuilt");
  const auto& P=W.QueryPlacements()[K];Check(P.WorldTransform[12]==R.Position[0],"world transform");
  Check(P.Name==(K?"Object "+std::to_string(K+1):"Object"),"unique naming");
  if(K<7)Check(P.InstanceCount==1&&W.QueryInstances()[P.FirstInstance].TriangleCount==Expected[K],"outliner placement instance attachment");
 }
 Check(W.QueryCameras().size()==1&&W.QueryPlacements()[7].Camera==0,"camera attachment");
 Check(W.QueryLuminaires().size()==2&&W.QueryLuminairePower()>0,"emission sampler rebuilt");
 for(auto& I:W.QueryInstances())for(unsigned T=0;T<I.TriangleCount;++T){
  auto V=[&](unsigned J)->const VertexRecord&{return W.QueryVertices()[I.VertexOffset+W.QueryIndices()[I.FirstIndex+T*3+J]];};
  auto N=OrientationClassifier::CrossProduct(V(1).SpatialLocation-V(0).SpatialLocation,V(2).SpatialLocation-V(0).SpatialLocation);
  Check(N.LengthSquared()>1e-14f,"degenerate triangle");Check(OrientationClassifier::DotProduct(N,V(0).NormalDirection+V(1).NormalDirection+V(2).NormalDirection)>0,"winding");
 }
 auto Before=W.QueryPlacements().size();ConstructRequest Bad;Bad.Size=std::numeric_limits<float>::quiet_NaN();
 Check(!ConstructEntity(W,Bad)&&W.QueryPlacements().size()==Before,"invalid request mutated world");
 Bad.Size=1;Bad.Kind=ConstructKind(99);Check(!ConstructEntity(W,Bad),"invalid kind");
 Bad.Kind=ConstructKind::Cube;Bad.Name="bad\nname";Check(!ConstructEntity(W,Bad),"invalid name");
 std::cout<<"PASS: nine catalogue entities, "<<Total<<" real scene triangles; placements, cameras, emission, normals, stable IDs, duplicate names and rejection invariants. CPU only.\n";
}
int main(int Argc,char** Argv){
 if(Argc>1&&std::string(Argv[1])=="--test"){Test();return 0;}
 SceneStructure World;std::vector<ConstructRequest> Requests;
 auto Snapshot=[&](){
  std::cout<<"{\"ok\":true,\"triangles\":"<<World.QueryTriangleCount()<<",\"instances\":"<<World.QueryInstances().size()<<",\"emitters\":"<<World.QueryLuminaires().size()<<",\"entities\":[";
  for(unsigned I=0;I<World.QueryPlacements().size();++I){
   const auto& P=World.QueryPlacements()[I];if(I)std::cout<<',';
   unsigned Triangles=0;for(unsigned J=0;J<P.InstanceCount;++J)Triangles+=World.QueryInstances()[P.FirstInstance+J].TriangleCount;
   std::cout<<"{\"id\":\"native-"<<I<<"\",\"placement\":"<<I<<",\"kind\":"<<unsigned(Requests[I].Kind)<<",\"name\":"<<std::quoted(P.Name)<<",\"position\":["<<P.WorldTransform[12]<<','<<P.WorldTransform[13]<<','<<P.WorldTransform[14]<<"],\"size\":"<<Requests[I].Size<<",\"triangles\":"<<Triangles<<",\"camera\":"<<(P.Camera!=kPlacementNone?"true":"false")<<'}';
  }std::cout<<"]}"<<std::endl;
 };
 std::string Line;while(std::getline(std::cin,Line)){
  if(Line=="list"){Snapshot();continue;}
  ConstructRequest R;unsigned Kind;std::istringstream Input(Line);
  if(!(Input>>Kind>>R.Position[0]>>R.Position[1]>>R.Position[2]>>R.Size>>std::quoted(R.Name))){std::cout<<"{\"ok\":false,\"error\":\"Malformed request\"}"<<std::endl;continue;}
  R.Kind=ConstructKind(Kind);auto Result=ConstructEntity(World,R);
  if(!Result){std::cout<<"{\"ok\":false,\"error\":"<<std::quoted(Result.Error)<<'}'<<std::endl;continue;}
  Requests.push_back(R);Snapshot();
 }
}
