// Inspect the existing native algorithm, without changing it or claiming GPU execution.
#include "AtmosphericOptics.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
using Optics=Frontier::AtmosphericOptics;
static double Difference(Optics::LensFlareSettings A,Optics::LensFlareSettings B){
    const float Sun[]={.28f,.43f};double Sum=0;
    for(int Y=0;Y<72;++Y)for(int X=0;X<128;++X){
        const float P[]={(X+.5f)/128,(Y+.5f)/72};float L[3],R[3];
        Optics::LensFlare(A,P,Sun,1,128.f/72,L);Optics::LensFlare(B,P,Sun,1,128.f/72,R);
        for(int C=0;C<3;++C){if(!std::isfinite(L[C])||!std::isfinite(R[C]))throw std::runtime_error("nonfinite output");Sum+=std::abs(L[C]-R[C]);}
    }
    return Sum;
}
int main(){
    unsigned Checks=0;
    auto Check=[&](bool V,const char* Label){++Checks;if(!V)throw std::runtime_error(Label);std::printf("PASS %s\n",Label);};
    Optics::LensFlareSettings A,B;
    for(unsigned I=0;I<4;++I)for(unsigned J=I+1;J<4;++J){A.Category=static_cast<Optics::LensFlareCategory>(I);B=A;B.Category=static_cast<Optics::LensFlareCategory>(J);Check(Difference(A,B)>.01,"preset images differ");}
    A={};B=A;B.HaloRadius=.5f;Check(Difference(A,B)>.01,"existing halo radius changes actual CPU pixels");
    B=A;B.StreakGain=0;Check(Difference(A,B)>.01,"unexposed streak gain changes actual CPU pixels");
    B=A;B.Chromatic=0;Check(Difference(A,B)>.01,"chromatic slider changes actual CPU pixels");
    B=A;B.GhostCount=0;Check(Difference(A,B)>.01,"ghost count changes actual CPU pixels");
    B=A;B.ApertureBlades=7;Check(Difference(A,B)>.01,"aperture blades change actual CPU pixels; physical correctness not certified");
    A.Category=Optics::LensFlareCategory::Anamorphic;B=A;B.HaloRadius=.5f;Check(Difference(A,B)==0,"anamorphic preset suppresses halo, so radius has no effect there");
    std::printf("%u existing-algorithm checks passed; CPU only, not shader or bake validation.\n",Checks);
}
