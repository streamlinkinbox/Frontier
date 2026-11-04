/*============================================================================================================================================
                                                   VOLUMECOMPARISON.CPP
============================================================================================================================================*/
// 🧩 Encode a native cube and volume for comparison with the standalone browser baker.
#include "Engine/ContentInterchange/VolumeDistanceField.h"
#include <iostream>
#include <iomanip>
int main(){auto A=Frontier::BuildDemoBakeAssets("Cube");auto V=Frontier::BakeVolumeDistanceField(A.High,{24,18,20},.14f,true);std::cout<<std::setprecision(9)<<"{\"positions\":[";bool first=true;for(auto &v:A.High.Parts[0].Vertices)for(float p:v.Position){if(!first)std::cout<<',';first=false;std::cout<<p;}std::cout<<"],\"indices\":[";first=true;for(auto i:A.High.Parts[0].Indices){if(!first)std::cout<<',';first=false;std::cout<<i;}std::cout<<"],\"distances\":[";first=true;for(float d:V.Distances){if(!first)std::cout<<',';first=false;std::cout<<d;}std::cout<<"]}";}
