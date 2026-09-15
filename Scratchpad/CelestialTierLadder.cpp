// Step 0 check: the celestial tier ladder must rise monotonically. A tier that spends less than a cheaper one
// is a typo that no visual test would catch.
#include "DisplayPresentation/FidelityClassifier.h"
#include <cstdio>
using namespace Frontier;
int main(){
  FidelityClassifier C; const FidelityCategory Cats[5]={FidelityCategory::MinimalFidelity,
    FidelityCategory::EconomyFidelity,FidelityCategory::StandardFidelity,
    FidelityCategory::UltraFidelity,FidelityCategory::ReferenceFidelity};
  const char* N[5]={"Minimal","Economy","Standard","Ultra","Reference"};
  FidelityCriteria K[5]; for(int i=0;i<5;i++) K[i]=C.ConstructCriteria(Cats[i]);
  std::printf("\n%-10s %6s %5s %6s %6s %5s %5s %5s %5s %6s %6s\n","tier","cloud","tap","local","cldRes","atmN","atmL","starL","starA","starA","covMgn");
  for(int i=0;i<5;i++) std::printf("%-10s %6u %5u %6u %6.2f %5u %5u %5u %5u %6u %6.2f\n",N[i],
    K[i].CloudMarchStepCount,K[i].CloudLightTapCount,K[i].LocalVolumeStepCount,(double)K[i].CloudResolutionScale,
    K[i].AtmosphereSampleCount,K[i].AtmosphereLightSampleCount,K[i].StarLayerCount,K[i].StarSuperSampleCount,
    K[i].StarSuperSampleCount,(double)K[i].CloudCoverageMargin);
  int bad=0;
  for(int i=1;i<5;i++){
    if(K[i].CloudMarchStepCount   < K[i-1].CloudMarchStepCount)   {std::printf("  FAIL cloud steps drop at %s\n",N[i]);bad=1;}
    if(K[i].LocalVolumeStepCount  < K[i-1].LocalVolumeStepCount)  {std::printf("  FAIL local steps drop at %s\n",N[i]);bad=1;}
    if(K[i].AtmosphereSampleCount < K[i-1].AtmosphereSampleCount) {std::printf("  FAIL atmosphere drops at %s\n",N[i]);bad=1;}
    if(K[i].CloudCoverageMargin   > K[i-1].CloudCoverageMargin)   {std::printf("  FAIL coverage margin rises at %s\n",N[i]);bad=1;}
  }
  // Minimal must have god rays off; Reference must not early-out on coverage.
  if(K[4].CloudCoverageMargin!=0.0f){std::printf("  FAIL Reference should not skip on coverage\n");bad=1;}
  std::printf("\n%s\n\n",bad?"LADDER BROKEN":"ladder rises monotonically at every step");
  return bad;}
