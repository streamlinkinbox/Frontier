// Spherical quadrature of second-order radiance L2 and scattering fraction F.
// Isotropic higher orders form the convergent geometric series Psi = L2 / (1-F).
//! include "AtmosphereMedium.wgsl"
@group(0) @binding(1) var Target : texture_storage_2d<rgba16float,write>;
@group(0) @binding(2) var TransmittanceLut : texture_2d<f32>;
@group(0) @binding(3) var LutSampler : sampler;
@compute @workgroup_size(8,8)
fn IntegrateMultiple(@builtin(global_invocation_id) ID: vec3u) {
 let Size=textureDimensions(Target);if(any(ID.xy>=Size)){return;}
 let UV=(vec2f(ID.xy)+0.5)/vec2f(Size);
 let Mu=UV.x*2.0-1.0;let Sun=vec3f(sqrt(max(0.0,1.0-Mu*Mu)),0.0,Mu);
 let Origin=vec3f(0.0,0.0,GroundRadius+max(1.0,UV.y*UV.y*(AtmosphereRadius-GroundRadius)));
 var L2=vec3f(0.0);var F=vec3f(0.0);
 for(var I=0u;I<32u;I++) {
  let Z=1.0-2.0*(f32(I)+0.5)/32.0;let Phi=f32(I)*2.39996323;
  let R=sqrt(1.0-Z*Z);let D=vec3f(R*cos(Phi),R*sin(Phi),Z);
  let Ground=IntersectSphere(Origin,D,GroundRadius).x;
  var Distance=IntersectSphere(Origin,D,AtmosphereRadius).y;
  if(Ground>0.0){Distance=Ground;}
  var T=vec3f(1.0);var L=vec3f(0.0);var Fraction=vec3f(0.0);
  let Cosine=dot(D,Sun);let g=Atmosphere.Medium.w;let g2=g*g;
  let PhaseR=3.0/(16.0*3.14159265)*(1.0+Cosine*Cosine);
  let PhaseM=3.0/(8.0*3.14159265)*(1.0-g2)*(1.0+Cosine*Cosine)/((2.0+g2)*pow(max(0.001,1.0+g2-2.0*g*Cosine),1.5));
  for(var J=0u;J<20u;J++) {
   let A=f32(J)/20.0;let B=f32(J+1u)/20.0;let Step=Distance*(B*B-A*A);
   let P=Origin+D*(Distance*(A*A+B*B)*0.5);
   let Density=ResolveDensities(max(0.0,length(P)-GroundRadius));
   let E=ResolveExtinction(Density);let Segment=exp(-E*Step);let Integral=(1.0-Segment)/max(E,vec3f(1e-12));
   let SR=RayleighScatter*Atmosphere.Medium.x*Density.x;let SM=vec3f(MieScatter*Atmosphere.Medium.y*Density.y);
   L+=T*(SR*PhaseR+SM*PhaseM)*LookupTransmittance(TransmittanceLut,LutSampler,P,Sun)*Integral;
   Fraction+=T*(SR+SM)*Integral;T*=Segment;
  }
  if(Ground>0.0){let P=Origin+D*Distance;L+=T*Atmosphere.Ground.rgb*(max(0.0,dot(normalize(P),Sun))/3.14159265)*LookupTransmittance(TransmittanceLut,LutSampler,P+normalize(P),Sun);}
  L2+=L/32.0;F+=Fraction/32.0;
 }
 textureStore(Target,vec2i(ID.xy),vec4f(L2/max(vec3f(0.02),1.0-min(F,vec3f(0.98))),1.0));
}
