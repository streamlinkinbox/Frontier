#include "PbfFluid.h"
#include "SurfaceReconstruction.h"
#include "AnisotropicSurfaceMesh.h"
#include <iostream>

namespace PF=Frontier::ProjectFluid;
int main(){
    PF::PbfFluid fluid;
    for(int i=0;i<12;++i)fluid.Step(1.0f/60.0f);
    PF::SurfaceReconstruction kernels;kernels.Update(fluid.Positions());
    PF::AnisotropicSurfaceMesh surface;surface.Update(kernels.Kernels());
    const auto firstDirty=surface.DirtyBrickCount();surface.Update(kernels.Kernels());
    const auto& mesh=surface.Mesh();
    if(mesh.Vertices.empty()||mesh.Indices.empty()||mesh.Indices.size()%3!=0)return 1;
    if(surface.OpenEdgeCount()!=0||surface.NonManifoldEdgeCount()!=0)return 2;
    if(firstDirty==0||surface.DirtyBrickCount()!=0)return 3;
    for(auto index:mesh.Indices)if(index>=mesh.Vertices.size())return 4;
    std::cout<<mesh.Vertices.size()<<" vertices, "<<mesh.Indices.size()/3
             <<" triangles, watertight manifold, "<<firstDirty
             <<" initial and 0 unchanged dirty bricks\n";
    return 0;
}
