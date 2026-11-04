
#pragma once
#include "IntrinsicParity.slang"
// Rebuilt star rendering: catalogue positions, pixel-filtered cores, brighter bounded
// radiance, compact halos and smooth twilight extinction. No new textures or dispatches.
struct CatalogueStar { float4 DirectionLuminance; float4 Colour; };
  [[vk::binding(7, 2)]] Texture2D<float4> StarCatalogueTexture;
float4 StarTexel(uint Index) {return LoadTexel(StarCatalogueTexture, int2(ToSigned(Index%1024u),ToSigned(Index/1024u)), 0);}
CatalogueStar ReadCatalogueStar(uint Index) {var S : CatalogueStar = {};S.DirectionLuminance=StarTexel(1024u+Index*2u);S.Colour=StarTexel(1025u+Index*2u);return S;}
uint StarCell(float3 D) {
 var P = D.xy/(abs(D.x)+abs(D.y)+abs(D.z));
 if (D.z<0.0){P=(1.0-abs(P.yx))*float2(SelectBranch(-1.0,1.0,P.x>=0.0),SelectBranch(-1.0,1.0,P.y>=0.0));}
 let Cell = uint2(clamp(floor((P*.5+.5)*32.0),float2(0),float2(31)));
 return Cell.y*32u+Cell.x;
}
float3 ResolveNightStars(float3 D,float PixelAngle) {
 let Night = (1.0-smoothstep(-14.0,-3.0,Refresh.Night.w))*smoothstep(0.0,.06,D.z);
 if (Night<=0.0||Refresh.Stars.x<=0.0){return float3(0);}
 let Rotation = Refresh.StarControl.y+Refresh.Stars.w;let Latitude = Refresh.StarControl.z;
 let PoleX = D.z*cos(Latitude)-D.y*sin(Latitude);let PoleY = -D.x;let PoleZ = D.z*sin(Latitude)+D.y*cos(Latitude);
 let V = normalize(float3(PoleX*cos(Rotation)+PoleY*sin(Rotation),PoleX*sin(Rotation)-PoleY*cos(Rotation),PoleZ));
 let Cell = uint2(StarTexel(StarCell(V)).xy);let Threshold = pow(10.0,-.4*Refresh.Stars.y);
 // A subpixel star is represented by a finite, resolution-aware point spread.
 let Width = max(.00008,PixelAngle*.70*Refresh.Stars.z);let W2 = Width*Width;var Light = float3(0);
 for (var I = 0u; I<Cell.y; I++){
  let S = ReadCatalogueStar(Cell.x+I);let Lum = S.DirectionLuminance.w;if (Lum<Threshold){continue;}
  // Difference vector avoids catastrophic 1-dot cancellation at high resolution.
  let Delta = V-S.DirectionLuminance.xyz;let Radius2 = dot(Delta,Delta)/W2;if (Radius2>12.0){continue;}
  let Core = exp(-Radius2*1.8);let Halo = exp(-Radius2*.4)*min(.12,sqrt(Lum)*.025)*Refresh.StarControl.w;
  let Phase = dot(S.DirectionLuminance.xyz,float3(91.7,213.1,57.3));
  let Time = Refresh.ElapsedSeconds*Refresh.Night.y;let Signal = .6*sin(Time*6.283185+Phase)+.28*sin(Time*11.71+Phase*1.7)+.12*sin(Time*19.37+Phase*2.3);
  let Flicker = 1.0+Refresh.Night.x*.35*Signal;
  // Compress magnitudes for an authored, readable night sky instead of vanishing faint points.
  let Gain = min(12.0,.04+3.0*pow(max(0.0,Lum),.65));
  Light+=S.Colour.rgb*(Core+Halo)*Gain*Flicker;
 }
 let Plane = dot(V,normalize(float3(.3,.83,.47)));
 // Bounded multiscale texture: structured galactic glow and a dark central dust lane.
 let Structure = .55+.25*sin(V.x*27.0+V.z*19.0)*sin(V.y*21.0-V.z*11.0)+.15*sin(V.x*73.0+V.y*49.0)*sin(V.z*57.0);
 let Band = exp(-Plane*Plane/.018)*max(.1,Structure);
 let Dust = 1.0-.75*exp(-pow((Plane+.018*sin(V.x*19.0))/.025,2.0))*(.65+.35*sin(V.y*17.0)*sin(V.z*23.0));
 let Airmass = 1.0/max(.12,D.z+.08);let Extinction = exp(-.06*(Airmass-1.0));
 return Night*Refresh.Stars.x*Extinction*(Light+float3(.002,.0024,.0034)*Band*Dust*Refresh.Night.z);
}
