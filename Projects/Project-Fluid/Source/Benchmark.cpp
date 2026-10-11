#include "PbfFluid.h"
#include "SurfaceReconstruction.h"
#include "AnisotropicSurfaceMesh.h"
#include "PondWave.h"
#include <chrono>
#include <iostream>
#ifdef _OPENMP
#include <omp.h>
#endif
using namespace Frontier::ProjectFluid;
template<class F> double Ms(F&& Work){auto Start=std::chrono::steady_clock::now();Work();return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-Start).count();}
int main(){
#ifdef _OPENMP
 std::cout<<"openmp_max_threads="<<omp_get_max_threads()<<'\n';
#else
 std::cout<<"openmp_max_threads=1\n";
#endif
 PbfFluid Fluid;Fluid.Step(1.f/60);
 std::cout<<"particles="<<Fluid.Positions().size()<<"\nsolver_mean_ms="<<Ms([&]{for(int I=0;I<60;++I)Fluid.Step(1.f/60);})/60<<'\n';
 const auto& S=Fluid.LastTimings();
 std::cout<<"last_step_neighbours_density_ms="<<S.NeighboursMs<<"\nlast_step_surface_tension_ms="<<S.SurfaceTensionMs<<"\nlast_step_pressure_ms="<<S.PressureMs<<"\nlast_step_viscosity_ms="<<S.ViscosityMs<<"\nlast_step_total_ms="<<S.TotalMs<<'\n';
 SurfaceReconstruction Reconstruction;
 std::cout<<"reconstruction_mean_ms="<<Ms([&]{for(int I=0;I<10;++I)Reconstruction.Update(Fluid.Positions());})/10<<'\n';
 AnisotropicSurfaceMesh Surface;
 std::cout<<"extraction_first_ms="<<Ms([&]{Surface.Update(Reconstruction.Kernels());})<<'\n';
 std::cout<<"extraction_unchanged_mean_ms="<<Ms([&]{for(int I=0;I<100;++I)Surface.Update(Reconstruction.Kernels());})/100<<'\n';
 Fluid.Step(1.f/60);Reconstruction.Update(Fluid.Positions());
 std::cout<<"extraction_changed_ms="<<Ms([&]{Surface.Update(Reconstruction.Kernels());})<<"\ndirty_bricks="<<Surface.DirtyBrickCount()<<'\n';
 const auto& T=Surface.LastTimings();
 std::cout<<"index_ms="<<T.IndexMs<<"\nfield_ms="<<T.FieldMs<<"\ntriangles_ms="<<T.TrianglesMs<<"\nassembly_smoothing_ms="<<T.AssemblyMs<<"\nsmoothing_subset_ms="<<T.SmoothingMs<<'\n';
 PbfFluid PipelineFluid;SurfaceReconstruction PipelinePca;AnisotropicSurfaceMesh PipelineMesh;
 auto frame=[&]{PipelineFluid.Step(1.f/60);PipelinePca.Update(PipelineFluid.Positions());PipelineMesh.Update(PipelinePca.Kernels());};
 for(int i=0;i<5;++i)frame();
 std::cout<<"pipeline_20_frame_mean_ms="<<Ms([&]{for(int i=0;i<20;++i)frame();})/20<<'\n';
 PondWave Pond;Pond.Disturb(0,0,.8f,.4f);
 std::cout<<"pond_cells="<<Pond.Rows()*Pond.Columns()<<"\npond_step_mean_ms="<<Ms([&]{for(int I=0;I<360;++I)Pond.Step(Pond.StableDt());})/360<<'\n';
}
