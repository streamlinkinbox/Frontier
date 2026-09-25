#include "PbfFluid.h"
#include "SurfaceReconstruction.h"
#include "AnisotropicSurfaceMesh.h"
#include "PondWave.h"
#include <chrono>
#include <iostream>
using namespace Frontier::ProjectFluid;
template<class F> double Ms(F&& Work){auto Start=std::chrono::steady_clock::now();Work();return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-Start).count();}
int main(){
 PbfFluid Fluid;Fluid.Step(1.f/60);
 std::cout<<"particles="<<Fluid.Positions().size()<<"\nsolver_mean_ms="<<Ms([&]{for(int I=0;I<60;++I)Fluid.Step(1.f/60);})/60<<'\n';
 SurfaceReconstruction Reconstruction;
 std::cout<<"reconstruction_mean_ms="<<Ms([&]{for(int I=0;I<10;++I)Reconstruction.Update(Fluid.Positions());})/10<<'\n';
 AnisotropicSurfaceMesh Surface;
 std::cout<<"extraction_first_ms="<<Ms([&]{Surface.Update(Reconstruction.Kernels());})<<'\n';
 std::cout<<"extraction_unchanged_mean_ms="<<Ms([&]{for(int I=0;I<100;++I)Surface.Update(Reconstruction.Kernels());})/100<<'\n';
 Fluid.Step(1.f/60);Reconstruction.Update(Fluid.Positions());
 std::cout<<"extraction_changed_ms="<<Ms([&]{Surface.Update(Reconstruction.Kernels());})<<"\ndirty_bricks="<<Surface.DirtyBrickCount()<<'\n';
 const auto& T=Surface.LastTimings();
 std::cout<<"index_ms="<<T.IndexMs<<"\nfield_ms="<<T.FieldMs<<"\ntriangles_ms="<<T.TrianglesMs<<"\nassembly_smoothing_ms="<<T.AssemblyMs<<"\nsmoothing_subset_ms="<<T.SmoothingMs<<'\n';
 PondWave Pond;Pond.Disturb(0,0,.8f,.4f);
 std::cout<<"pond_cells="<<Pond.Rows()*Pond.Columns()<<"\npond_step_mean_ms="<<Ms([&]{for(int I=0;I<360;++I)Pond.Step(Pond.StableDt());})/360<<'\n';
}
