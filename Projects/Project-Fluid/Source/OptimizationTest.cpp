#include "SpatialIndex.h"
#include "AnisotropicSurfaceMesh.h"
#include <random>
#include <iostream>
#include <stdexcept>
using namespace Frontier::ProjectFluid;
void Check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
void Near(Vec3 a,Vec3 b,float tolerance=1e-5f){Check(Length(a-b)<tolerance,"reference vector mismatch");}
int main(){try{
    std::mt19937 random(42);std::uniform_real_distribution<float> value(-2,2);
    std::vector<Vec3> points;for(int i=0;i<2000;++i)points.push_back({value(random),value(random),value(random)});
    points.push_back({-.31f,0,0});points.push_back({0,0,0});points.push_back({.31f,0,0});
    SpatialIndex index(points,.31f);std::vector<uint32_t> found,wanted;
    for(const auto p:points){index.Query(p,found);wanted.clear();for(uint32_t j=0;j<points.size();++j){const auto d=points[j]-p;if(Dot(d,d)<.31f*.31f)wanted.push_back(j);}Check(found==wanted,"spatial neighbours/order mismatch");}
    SpatialIndex empty({},.31f);empty.Query({},found);Check(found.empty(),"empty index");
    const std::vector<Vec3> sparse{{-10000,-10000,-10000},{10000,10000,10000}};
    SpatialIndex sparseIndex(sparse,.31f);sparseIndex.Query(sparse[0],found);Check(found==std::vector<uint32_t>{0},"sparse fallback");
    PbfFluid toggled,reset;const auto before=toggled.BoundarySampleCount();toggled.SetObstacle(false);reset.SetObstacle(false);reset.SetReferenceNeighbourSearch(true);
    toggled.Step(1.f/60);reset.Step(1.f/60);Check(toggled.BoundarySampleCount()<before,"obstacle cache invalidation");
    for(size_t i=0;i<toggled.Positions().size();++i)Near(toggled.Positions()[i],reset.Positions()[i]);
    // A copied solver must not retain references into another solver's buffers.
    PbfFluid copy=reset;reset.Reset();copy.Step(1.f/60);toggled.Step(1.f/60);
    for(size_t i=0;i<copy.Positions().size();++i)Near(copy.Positions()[i],toggled.Positions()[i]);
    PbfFluid fast,reference;reference.SetReferenceNeighbourSearch(true);
    for(int frame=0;frame<12;++frame){fast.Step(1.f/60);reference.Step(1.f/60);
        Check(fast.Positions().size()==reference.Positions().size(),"particle count");
        for(size_t i=0;i<fast.Positions().size();++i){Near(fast.Positions()[i],reference.Positions()[i]);Near(fast.Velocities()[i],reference.Velocities()[i]);}}
    for(auto material:{Material::Milk,Material::Honey,Material::Chocolate}){
        PbfFluid a,b;a.SetMaterial(material);b.SetMaterial(material);b.SetReferenceNeighbourSearch(true);
        for(int frame=0;frame<4;++frame){a.Step(1.f/60);b.Step(1.f/60);}
        for(size_t i=0;i<a.Positions().size();++i){Near(a.Positions()[i],b.Positions()[i]);Near(a.Velocities()[i],b.Velocities()[i]);}
    }
    SurfaceReconstruction pca,oracle;oracle.SetReferenceNeighbourSearch(true);pca.Update(fast.Positions());oracle.Update(fast.Positions());
    for(size_t i=0;i<pca.Kernels().size();++i){const auto&a=pca.Kernels()[i];const auto&b=oracle.Kernels()[i];Near(a.Centre,b.Centre);Near(a.AxisA,b.AxisA);Near(a.AxisB,b.AxisB);Near(a.AxisC,b.AxisC);Check(std::abs(a.VolumeWeight-b.VolumeWeight)<1e-5f,"weight mismatch");}
    // Real solver-generated kernels: full build, incremental motion and removal.
    AnisotropicSurfaceMesh mesh,full;full.SetReferenceEvaluation(true);
    auto kernels=pca.Kernels();
    for(int state=0;state<3;++state){
        if(state==1)for(auto& k:kernels)k.Centre.x+=.001f;
        if(state==2)kernels.resize(20);
        mesh.Update(kernels);full.Update(kernels);
        Check(mesh.Mesh().Indices==full.Mesh().Indices,"mesh topology differs from exhaustive field");
        Check(mesh.Mesh().Vertices.size()==full.Mesh().Vertices.size(),"vertex count mismatch");
        for(size_t i=0;i<mesh.Mesh().Vertices.size();++i){Near(mesh.Mesh().Vertices[i].Position,full.Mesh().Vertices[i].Position,2e-4f);Near(mesh.Mesh().Vertices[i].Normal,full.Mesh().Vertices[i].Normal,2e-3f);}
        Check(mesh.OpenEdgeCount()==full.OpenEdgeCount()&&mesh.NonManifoldEdgeCount()==full.NonManifoldEdgeCount(),"topology diagnostics mismatch");
    }
    std::cout<<"PASS spatial query/order, 12 solver steps, PCA, full/incremental/removal meshes versus exhaustive references\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
