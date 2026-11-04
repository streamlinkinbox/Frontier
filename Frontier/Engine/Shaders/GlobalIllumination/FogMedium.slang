
#pragma once
#include "IntrinsicParity.slang"
// Geometry fog common-domain by final-image recombination and the isolated GPU proof.
// Density [/m], distance/height [m]; squared optical depth matches native height fog.
float3 FogTransmission(float3 Camera,float3 Position,float Density,float Height,float AerialDensity,float AerialStart,float MieBlend) {
 let Distance = length(Position-Camera);let H = max(Height,0.1);
 let Delta = (Position.z-Camera.z)/H;var Column = 1.0;
 if (abs(Delta)>0.001){Column=(1.0-exp(clamp(-Delta,-50.0,50.0)))/Delta;}
 let HeightOD = Density*Distance*exp(clamp(-Camera.z/H,-50.0,20.0))*Column;
 let AerialOD = AerialDensity*max(0.0,Distance-AerialStart);
 return exp(-min(float3(80.0),float3(HeightOD*HeightOD)+lerp(float3(0.45,0.7,1.0),float3(1.0),MieBlend)*AerialOD));
}
