
#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"
// Spherical quadrature of second-order radiance L2 and scattering fraction F.
// Isotropic higher orders form the convergent geometric series Psi = L2 / (1-F).
#include "AtmosphereMedium.slang"
  [[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Target;
  [[vk::binding(2, 0)]] Texture2D<float4> TransmittanceLut;
  [[vk::binding(3, 0)]] SamplerState LutSampler;
 
[shader("compute")]
[numthreads(8, 8, 1)]
void IntegrateMultiple(uint3 ID : SV_DispatchThreadID) {
 let Size = TextureExtent(Target);if (any(ID.xy>=Size)){return;}
 let UV = (float2(ID.xy)+0.5)/float2(Size);
 let Mu = UV.x*2.0-1.0;let Sun = float3(sqrt(max(0.0,1.0-Mu*Mu)),0.0,Mu);
 let Origin = float3(0.0,0.0,GroundRadius+max(1.0,UV.y*UV.y*(AtmosphereRadius-GroundRadius)));
 var L2 = float3(0.0);var F = float3(0.0);
 for (var I = 0u; I<32u; I++) {
  let Z = 1.0-2.0*(float(I)+0.5)/32.0;let Phi = float(I)*2.39996323;
  let R = sqrt(1.0-Z*Z);let D = float3(R*cos(Phi),R*sin(Phi),Z);
  let Ground = IntersectSphere(Origin,D,GroundRadius).x;
  var Distance = IntersectSphere(Origin,D,AtmosphereRadius).y;
  if (Ground>0.0){Distance=Ground;}
  var T = float3(1.0);var L = float3(0.0);var Fraction = float3(0.0);
  let Cosine = dot(D,Sun);let g = Atmosphere.Medium.w;let g2 = g*g;
  let PhaseR = 3.0/(16.0*3.14159265)*(1.0+Cosine*Cosine);
  let PhaseM = 3.0/(8.0*3.14159265)*(1.0-g2)*(1.0+Cosine*Cosine)/((2.0+g2)*pow(max(0.001,1.0+g2-2.0*g*Cosine),1.5));
  for (var J = 0u; J<20u; J++) {
   let A = float(J)/20.0;let B = float(J+1u)/20.0;let Step = Distance*(B*B-A*A);
   let P = Origin+D*(Distance*(A*A+B*B)*0.5);
   let Density = ResolveDensities(max(0.0,length(P)-GroundRadius));
   let E = ResolveExtinction(Density);let Segment = exp(-E*Step);let Integral = (1.0-Segment)/max(E,float3(1e-12));
   let SR = RayleighScatter*Atmosphere.Medium.x*Density.x;let SM = float3(MieScatter*Atmosphere.Medium.y*Density.y);
   L+=T*(SR*PhaseR+SM*PhaseM)*LookupTransmittance(TransmittanceLut,LutSampler,P,Sun)*Integral;
   Fraction+=T*(SR+SM)*Integral;T*=Segment;
  }
  if (Ground>0.0){let P = Origin+D*Distance;L+=T*Atmosphere.Ground.rgb*(max(0.0,dot(normalize(P),Sun))/3.14159265)*LookupTransmittance(TransmittanceLut,LutSampler,P+normalize(P),Sun);}
  L2+=L/32.0;F+=Fraction/32.0;
 }
 Target[int2(ID.xy)] = float4(L2/max(float3(0.02),1.0-min(F,float3(0.98))),1.0);
}
