#include "AtrousDenoiseMirror.h"
#include "ReSTIRIntegrator.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    Frontier::ReSTIRIntegrator integrator({});
    for (unsigned i=0;i<8192;++i) {
        assert(integrator.QueryAccumulationIndex()==i);
        integrator.IncrementAccumulationIndex();
    }
    integrator.ResetAccumulation();
    integrator.IncrementAccumulationIndex();
    assert(integrator.QueryAccumulationIndex()==0);
    integrator.IncrementAccumulationIndex();
    assert(integrator.QueryAccumulationIndex()==1);

    constexpr unsigned N=32, Pixels=N*N;
    std::vector<float> source(Pixels*4),surface(Pixels*4),samples(Pixels),out(Pixels*4);
    // Small high-frequency lighting detail on a flat surface, plus a uniform
    // weather-like contribution already composited into the input. Deliberately
    // HOLD variance high to test identity convergence independent of early-outs.
    for(unsigned i=0;i<Pixels;++i) {
        for(unsigned c=0;c<3;++c) source[4*i+c]=.3f+((i%N)%2?.02f:0.f);
        source[4*i+3]=.1f;surface[4*i+2]=1;surface[4*i+3]=3;
    }
    DenoiseMirror::RunConfiguration cfg;cfg.Extent=N;
    double previous=1e30;
    for(float age:{1.f,33.f,256.f,512.f,2048.f,8192.f,1048576.f}) {
        std::fill(samples.begin(),samples.end(),age);
        auto current=source;
        for(unsigned level=0;level<5;++level) {
            cfg.StepSize=1u<<level;cfg.FinalLevel=level==4;
            DenoiseMirror::Run(cfg,current.data(),surface.data(),out.data(),nullptr,samples.data());
            current.swap(out);
        }
        double error=0;
        for(unsigned i=0;i<Pixels;++i){
            error+=std::abs(current[i*4]-source[i*4]);
            assert(std::isfinite(current[i*4+3])&&current[i*4+3]>=0);
            assert(current[i*4]>=.3f-1e-6f); // input composite retained
        }
        error/=Pixels;
        assert(error<=previous+1e-8);previous=error;
        std::printf("Valid samples %.0f: five-level detail bias %.9f\n",age,error);
    }
    assert(previous<.000003);
    // Old global frame age must not suppress filtering for a newly exposed pixel.
    cfg.StepSize=1;cfg.FinalLevel=true;
    std::vector<float> young(Pixels*4),mixed(Pixels*4),presentation(Pixels*4);
    DenoiseMirror::Run(cfg,source.data(),surface.data(),young.data(),nullptr);
    samples[Pixels/2]=1;
    DenoiseMirror::Run(cfg,source.data(),surface.data(),mixed.data(),presentation.data(),samples.data());
    for(unsigned c=0;c<4;++c)assert(mixed[Pixels/2*4+c]==young[Pixels/2*4+c]);
    cfg.Enabled=false;
    DenoiseMirror::Run(cfg,source.data(),surface.data(),out.data(),presentation.data(),samples.data());
    assert(out==source);
    for(float v:presentation)assert(std::isfinite(v));
    std::puts("PASS actual shader CPU port: progressive detail, per-pixel reset, weather-like input, disabled identity; actual integrator exceeds 256. Not GPU validation.");
}
