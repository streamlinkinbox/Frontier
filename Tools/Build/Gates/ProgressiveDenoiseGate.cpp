#include "AtrousDenoiseMirror.h"
#include "ReSTIRIntegrator.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include <cstdio>

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

    // Check actual CPU dispatch flags, not just source text: non-raytraced paths must not
    // retain jitter, ReSTIR reuse, stochastic sky selection or the ray denoiser.
    {
        using namespace Frontier;
        HostRuntime::FlyThroughSolver Camera;
        constexpr uint32_t RayFeatures = DispatchFeatureRaytracing | DispatchFeatureAntiAliasing
            | DispatchFeatureTemporalReuse | DispatchFeatureSpatialReuse | DispatchFeatureGiReuse
            | DispatchFeatureAliasPick | DispatchFeatureTemporalReprojection | DispatchFeatureDenoise
            | DispatchFeatureSkyReservoir;
        for (uint32_t Path = 0u; Path < 3u; ++Path)
            for (bool Gi : { false, true })
            {
                ReSTIRIntegratorConfiguration Configuration{};
                Configuration.RenderPath = Path;
                Configuration.GlobalIllumination = Gi;
                ReSTIRIntegrator Pipeline(Configuration);
                const auto Dispatch = Pipeline.BuildDispatch(Camera, 1280u, 720u, 0u, 0u);
                if (Path != 0u) assert((Dispatch.FeatureFlags & RayFeatures) == 0u);
                else assert((Dispatch.FeatureFlags & (RayFeatures & ~DispatchFeatureGiReuse))
                         == (RayFeatures & ~DispatchFeatureGiReuse));
                if (!Gi) assert((Dispatch.FeatureFlags & DispatchFeatureGiReuse) == 0u);
            }
        std::puts("PASS actual dispatch: raster/SDF have no jitter or ReSTIR flags; GI-off has no GI reuse");
    }

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

    // ⚠️ THE OWNER'S REPORT (2026-09-26): "it used to keep rendering after it converged and get crisper; now it
    //    stays blurry". A one-pixel stripe cannot show that, because only the step-1 level can blur it — the
    //    fixture above is blind to the four WIDE levels, which are exactly the ones that make a held frame look
    //    soft (level 4 mixes neighbours 32 px away). This fixture carries detail at 1, 2, 4, 8 and 16 px, one
    //    band per 13 rows, and reports what survives per band.
    {
        constexpr unsigned M=64, Px=M*M;
        std::vector<float> src(Px*4),surf(Px*4),age(Px),scratch(Px*4);
        for(unsigned y=0;y<M;++y)for(unsigned x=0;x<M;++x){
            const unsigned i=y*M+x, size=1u<<std::min(y/13u,4u);
            const float detail=((x/size)&1u)?.02f:0.f;
            for(unsigned c=0;c<3;++c)src[4*i+c]=.3f+detail;
            src[4*i+3]=.1f;surf[4*i+2]=1;surf[4*i+3]=3;
        }
        DenoiseMirror::RunConfiguration wide;wide.Extent=M;
        const auto Bias=[&](float samplesPerPixel,unsigned band){
            std::fill(age.begin(),age.end(),samplesPerPixel);
            auto current=src;
            for(unsigned level=0;level<5;++level){
                wide.StepSize=1u<<level;wide.FinalLevel=level==4;
                DenoiseMirror::Run(wide,current.data(),surf.data(),scratch.data(),nullptr,age.data());
                current.swap(scratch);
            }
            double error=0;unsigned count=0;
            for(unsigned y=0;y<M;++y)for(unsigned x=0;x<M;++x)
                if(std::min(y/13u,4u)==band){error+=std::abs(current[(y*M+x)*4]-src[(y*M+x)*4]);++count;}
            return count?error/count:0.0;
        };
        double youngest[5];
        for(unsigned band=0;band<5;++band)youngest[band]=Bias(33.f,band);
        double previous[5];
        for(unsigned band=0;band<5;++band)previous[band]=youngest[band];
        for(float samplesPerPixel:{64.f,128.f,256.f,1024.f,8192.f}){
            std::printf("Valid samples %6.0f: detail bias",static_cast<double>(samplesPerPixel));
            for(unsigned band=0;band<5;++band){
                const double bias=Bias(samplesPerPixel,band);
                // Every band must keep improving, and the WIDE bands must improve FASTER than the 1 px band —
                //    that ordering is the whole point of keying the fade to the level's tap spacing.
                assert(bias<previous[band]);
                previous[band]=bias;
                std::printf("  %2u px %.8f",1u<<band,bias);
            }
            std::printf("\n");
        }
        for(unsigned band=1;band<5;++band)assert(previous[band]<previous[0]);
        // A held frame at 256 spp must have recovered most of the detail the young frame lost, at every scale.
        for(unsigned band=0;band<5;++band)assert(Bias(256.f,band)<youngest[band]*.4);
        // Young history is untouched by the change: 33 samples still gets the full five-level chain.
        for(unsigned band=0;band<5;++band)assert(Bias(1.f,band)==youngest[band]);
    }
    std::puts("PASS actual shader CPU port: progressive detail at five spatial scales, per-pixel reset, weather-like input, disabled identity; actual integrator exceeds 256. Not GPU validation.");
}
