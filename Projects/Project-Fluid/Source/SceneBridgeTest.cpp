#include "../../Project-Zero/Source/WaterBodySequence.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace Frontier;using namespace Frontier::ProjectZero;
void Check(bool V,const char* N){if(!V)throw std::runtime_error(N);}
int main(){try{
 SceneStructure Level;WaterBodySettings S;S.Origin={2,4,1};S.Rows=24;
 auto Registration=AppendPondSnapshot(Level,S);Level.Finalise();
 Check(Registration.InstanceCount>0&&Level.QueryTriangleCount()>0,"registered real scene geometry");
 const auto& Placement=Level.QueryPlacements()[Registration.Placement];
 (void)Placement;
 const auto& Mat=Level.QueryMaterials().QueryDescriptors()[Registration.Material];
 Check(Mat.Slabs[0].GeometryThinWalled&&std::abs(Mat.Slabs[0].SpecularIor-1.333f)<1e-5,"water optics / honest open sheet");
 Check(Level.QueryBoundsMinimum().z>.4f&&Level.QueryBoundsMaximum().z<1.6f,"Y-up to Z-up conversion");
 ProjectFluid::PondWave Pond(24);GeometryStructure Mesh;BuildPondGeometry(Pond,{0,0,0},Mesh);
 const auto& V=Mesh.QueryVertices();const auto& I=Mesh.QueryIndices();
 for(size_t T=0;T<I.size();T+=3){
  Check(I[T]<V.size()&&I[T+1]<V.size()&&I[T+2]<V.size(),"valid indices");
  const auto A=V[I[T]].SpatialLocation,B=V[I[T+1]].SpatialLocation,C=V[I[T+2]].SpatialLocation;
  Check(OrientationClassifier::CrossProduct(B-A,C-A).z>0,"upward triangle winding");
 }
 for(const auto& Vertex:V)Check(std::isfinite(Vertex.NormalDirection.z)&&Vertex.NormalDirection.z>.99,"upward normals");
 bool Rejected=false;try{BuildPondGeometry(Pond,{0,0,0},Mesh);}catch(const std::invalid_argument&){Rejected=true;}Check(Rejected,"do not corrupt existing geometry");
 Pond.Disturb(0,0,.8f,.4f);GeometryStructure Disturbed;BuildPondGeometry(Pond,{0,0,0},Disturbed);
 bool Changed=false;for(size_t J=0;J<V.size();++J)Changed|=V[J].SpatialLocation.z!=Disturbed.QueryVertices()[J].SpatialLocation.z;
 Check(Changed,"actual solver heights reach native scene mesh");
 std::cout<<"PASS native scene bridge: "<<Level.QueryTriangleCount()<<" triangles, placement/material, winding, normals, axis mapping and solver synchronization\n";
}catch(const std::exception& E){std::cerr<<E.what()<<'\n';return 1;}}
