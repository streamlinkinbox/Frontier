// Does the GLSL sky agree with the C++ model it was transcribed from?
//
// The shader cannot be run here, but it CAN be re-transcribed back into C++ line by line and compared against
// AtmosphereModel. A transcription error - a swapped axis, a dropped 1.1 on the Mie extinction, a wrong phase
// denominator - shows as a numeric divergence rather than a compile error, which is why this is worth doing.
#include "Engine/DisplayPresentation/AtmosphereModel.h"
#include "Engine/DisplayPresentation/SkyConstantRecord.h"
#include <cmath>
#include <cstdio>
#include <cstdint>
using namespace Frontier;

namespace {
// ── Transcribed BACK from Engine/Shaders/SkyRecords.slang, not from AtmosphereModel ──────────────────────────
bool GlslIntersect(const float O[3], const float D[3], float R, float& Near, float& Far)
{
    float B = O[0]*D[0]+O[1]*D[1]+O[2]*D[2];
    float C = O[0]*O[0]+O[1]*O[1]+O[2]*O[2] - R*R;
    float Disc = B*B - C;
    if (Disc < 0.0f) { Near = Far = 0.0f; return false; }
    float S = std::sqrt(Disc);
    Near = -B - S; Far = -B + S; return true;
}

void GlslSkyRadiance(const SkyConstantRecord& K, const float Dir[3], float Out[3])
{
    Out[0]=Out[1]=Out[2]=0.0f;
    if (K.SunRadiance[3] <= 0.0f) return;
    const float PlanetRadius = K.Planet[0];
    const float TopRadius = PlanetRadius + K.Planet[1];
    const float Origin[3] = { 0.0f, 0.0f, PlanetRadius + std::fmax(K.Planet[2], 0.0f) };
    float Near, Far;
    if (!GlslIntersect(Origin, Dir, TopRadius, Near, Far) || Far < 0.0f) return;
    float Start = std::fmax(Near, 0.0f), End = Far;
    float GN, GF;
    if (GlslIntersect(Origin, Dir, PlanetRadius, GN, GF) && GN > 0.0f) End = GN;
    if (End <= Start) return;

    const float BetaR[3] = { K.Rayleigh[0], K.Rayleigh[1], K.Rayleigh[2] };
    const float BetaM = K.Mie[0];
    const float BetaO[3] = { K.Ozone[0], K.Ozone[1], K.Ozone[2] };
    const float Hr = K.Rayleigh[3], Hm = K.Mie[1], G = K.Mie[2];

    float Mu = Dir[0]*K.SunDirection[0]+Dir[1]*K.SunDirection[1]+Dir[2]*K.SunDirection[2];
    const float kPi = 3.14159265358979323846f;
    float PhaseR = 3.0f/(16.0f*kPi)*(1.0f+Mu*Mu);
    float Den = (2.0f+G*G)*std::pow(std::fmax(1.0f+G*G-2.0f*G*Mu,1e-6f),1.5f);
    float PhaseM = 3.0f/(8.0f*kPi)*((1.0f-G*G)*(1.0f+Mu*Mu))/std::fmax(Den,1e-9f);

    uint32_t N = K.Control[0] ? K.Control[0] : 1u, NL = K.Control[1] ? K.Control[1] : 1u;
    float Length = End - Start;
    float SumR[3]={0,0,0}, SumM[3]={0,0,0}, OpticalR=0, OpticalM=0;
    for (uint32_t I=0;I<N;++I){
        float S0=float(I)/float(N), S1=float(I+1u)/float(N); S0*=S0; S1*=S1;
        float Ta=Start+Length*S0, Tb=Start+Length*S1, Seg=Tb-Ta, Tm=0.5f*(Ta+Tb);
        float P[3]={Origin[0]+Dir[0]*Tm,Origin[1]+Dir[1]*Tm,Origin[2]+Dir[2]*Tm};
        float H=std::sqrt(P[0]*P[0]+P[1]*P[1]+P[2]*P[2])-PlanetRadius;
        float DR=std::exp(-H/Hr)*Seg, DM=std::exp(-H/Hm)*Seg;
        OpticalR+=DR; OpticalM+=DM;
        float LN,LF;
        if(!GlslIntersect(P,K.SunDirection,TopRadius,LN,LF)) continue;
        float LR=0,LM=0; bool Lit=true;
        for(uint32_t J=0;J<NL;++J){
            float Q0=float(J)/float(NL), Q1=float(J+1u)/float(NL); Q0*=Q0; Q1*=Q1;
            float SegL=LF*(Q1-Q0), Tq=LF*0.5f*(Q0+Q1);
            float Q[3]={P[0]+K.SunDirection[0]*Tq,P[1]+K.SunDirection[1]*Tq,P[2]+K.SunDirection[2]*Tq};
            float Hq=std::sqrt(Q[0]*Q[0]+Q[1]*Q[1]+Q[2]*Q[2])-PlanetRadius;
            if(Hq<0.0f){Lit=false;break;}
            LR+=std::exp(-Hq/Hr)*SegL; LM+=std::exp(-Hq/Hm)*SegL; }
        if(!Lit) continue;
        for(int C=0;C<3;++C){
            float Tau=BetaR[C]*(OpticalR+LR)+BetaM*1.1f*(OpticalM+LM)+BetaO[C]*(OpticalR+LR);
            float A=std::exp(-Tau); SumR[C]+=A*DR; SumM[C]+=A*DM; } }
    for(int C=0;C<3;++C)
        Out[C]=(SumR[C]*BetaR[C]*PhaseR+SumM[C]*BetaM*PhaseM)*K.SunRadiance[C];
}

int Failures=0;
void Expect(bool Ok,const char* What){ if(!Ok)++Failures;
    std::printf("  %-66s %s\n",What,Ok?"PASS":"FAIL"); }
}

int main(){
    std::printf("\nSkyRecords.slang against AtmosphereModel.h — the transcription is faithful\n");
    for(int I=0;I<108;++I) std::putchar('=');
    std::printf("\n\n");

    AtmosphereMedium Medium{};
    AtmosphereLight  Light{};
    TwilightSettings Twilight{};

    struct Case { const char* Name; float Sun[3]; float View[3]; };
    const Case Cases[] = {
        { "noon, looking up",      {0.0f,0.20f,0.98f},  {0.0f,0.0f,1.0f} },
        { "noon, at the horizon",  {0.0f,0.20f,0.98f},  {0.0f,0.9998f,0.02f} },
        { "low sun, toward it",    {0.0f,0.94f,0.34f},  {0.0f,0.94f,0.34f} },
        { "low sun, away",         {0.0f,0.94f,0.34f},  {0.0f,-0.94f,0.34f} },
        { "sun behind, up",        {0.30f,-0.60f,0.74f},{0.0f,0.0f,1.0f} },
        { "steep view, high sun",  {0.10f,0.10f,0.99f}, {0.4f,0.4f,0.82f} },
    };

    std::printf("  %-24s %-30s %-30s %s\n","case","C++ model","GLSL transcription","worst rel");
    double Worst = 0.0;
    for (const Case& T : Cases)
    {
        for (int C=0;C<3;++C) Light.Direction[C]=T.Sun[C];
        const AtmosphereSample Model = AtmosphereModel::Integrate(Medium, Light, 2.0f, T.View, 16u, 6u);
        const SkyConstantRecord K = PackSkyConstants(Medium, Light, Twilight, 30.0f, 2.0f, 16u, 6u, true);
        float Glsl[3]; GlslSkyRadiance(K, T.View, Glsl);

        double Rel = 0.0;
        for (int C=0;C<3;++C){
            const double M = Model.Radiance[C], S = Glsl[C];
            const double D = std::fabs(M-S)/std::fmax(std::fmax(std::fabs(M),std::fabs(S)),1e-9);
            if (std::fabs(M) > 1e-6 || std::fabs(S) > 1e-6) Rel = std::fmax(Rel, D); }
        Worst = std::fmax(Worst, Rel);
        std::printf("  %-24s %7.4f %7.4f %7.4f  %7.4f %7.4f %7.4f  %.2e\n", T.Name,
                    Model.Radiance[0],Model.Radiance[1],Model.Radiance[2],
                    Glsl[0],Glsl[1],Glsl[2], Rel);
    }
    std::printf("\n  worst relative difference across %zu directions: %.3e\n", sizeof(Cases)/sizeof(Cases[0]), Worst);
    Expect(Worst < 1e-5, "the shader computes the same sky as the model it was transcribed from");

    // The packer must not invent values: a disabled sky is black, and the counts survive.
    {
        const SkyConstantRecord Off = PackSkyConstants(Medium, Light, Twilight, 30.0f, 2.0f, 16u, 6u, false);
        float Out[3]; GlslSkyRadiance(Off, Cases[0].View, Out);
        Expect(Out[0]==0.0f && Out[1]==0.0f && Out[2]==0.0f, "a disabled sky is exactly black, not merely dim");
        const SkyConstantRecord Zero = PackSkyConstants(Medium, Light, Twilight, 30.0f, 2.0f, 0u, 0u, true);
        Expect(Zero.Control[0]==1u && Zero.Control[1]==1u, "zero sample counts clamp to one rather than dividing by zero");
    }

    // And it must carry the medium rather than a copy of it.
    {
        AtmosphereMedium Hazy = Medium;
        Hazy.MieStrength = 4.0f;
        const SkyConstantRecord A = PackSkyConstants(Medium, Light, Twilight, 30.0f, 2.0f, 16u, 6u, true);
        const SkyConstantRecord B = PackSkyConstants(Hazy,  Light, Twilight, 30.0f, 2.0f, 16u, 6u, true);
        Expect(B.Mie[0] > A.Mie[0] * 3.5f, "changing the medium changes the block, so there is no second copy");
    }

    std::printf("\n");
    for(int I=0;I<108;++I) std::putchar('=');
    std::printf("\n%s\n\n", Failures==0 ? "  the shader and the model agree" : "  THE SHADER AND THE MODEL DISAGREE");
    return Failures==0?0:1;
}
