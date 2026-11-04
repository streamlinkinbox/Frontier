
#pragma once
#include "IntrinsicParity.slang"
#include "CloudLayouts.slang"
  [[vk::binding(6, 0)]] Texture2D<float4> CloudColour;
  [[vk::binding(7, 0)]] Texture2D<float4> CloudDepth;
  [[vk::binding(8, 0)]] Texture2D<float> CloudSceneDepth;
  [[vk::binding(9, 0)]] Texture2D<float4> CloudCabin;
  [[vk::binding(10, 0)]] ConstantBuffer<CloudParameters> CloudControl;
// Guard foregrounds at full pixel depth when reconstructing fog-only targets.
bool FogBeforeSurface(float2 Pixel,float Depth) {
 let C = CloudControl;let O = Refresh.CameraPosition;let World = ReconstructWorldPosition(Pixel,max(Depth,1e-6));let D = normalize(World-O);let Limit = SelectBranch(C.Render.z,length(World-O),Depth>0);
 let A = (O-C.FogCentre.xyz)/C.FogSize.xyz;let B = D/C.FogSize.xyz;
 if (C.FogSize.w==1.0||C.FogSize.w==3.0){let Safe = SelectBranch(float3(1e-7),B,abs(B)>float3(1e-7));let T0 = (-float3(1)-A)/Safe;let T1 = (float3(1)-A)/Safe;let Near = min(T0,T1);let Far = max(T0,T1);return min(Limit,min(Far.x,min(Far.y,Far.z)))>max(0.0,max(Near.x,max(Near.y,Near.z)));}
 let BB = dot(B,B);let AB = dot(A,B);let H = AB*AB-BB*(dot(A,A)-1);if (H<=0){return false;}return min(Limit,(-AB+sqrt(H))/BB)>max(0.0,(-AB-sqrt(H))/BB);
}
// Cloud guards use the full-resolution ray too: low-resolution neighbours
// must not paint cloud colour over geometry in front of every cloud volume.
bool CloudsBeforeSurface(float2 Pixel,float Depth) {
 let C = CloudControl;let O = Refresh.CameraPosition;let World = ReconstructWorldPosition(Pixel,max(Depth,1e-6));let D = normalize(World-O);let Limit = SelectBranch(C.Render.z,length(World-O),Depth>0);
 if (C.Control.x>.5){let Top = min(C.Control.w,C.Layer.x+C.Layer.y);if (Top>C.Layer.x){
  if (abs(D.z)<1e-7){if (O.z>C.Layer.x&&O.z<Top){return true;}}
  else{let A = (C.Layer.x-O.z)/D.z;let B = (Top-O.z)/D.z;if (min(Limit,max(A,B))>max(0.0,min(A,B))){return true;}}
 }}
 if (C.Centre.w>.5){let A = (O-C.Centre.xyz)/C.Size.xyz;let B = D/C.Size.xyz;
  if (C.LocalShape.x>.5){let Safe = SelectBranch(float3(1e-7),B,abs(B)>float3(1e-7));let T0 = (-float3(1)-A)/Safe;let T1 = (float3(1)-A)/Safe;let Near = min(T0,T1);let Far = max(T0,T1);if (min(Limit,min(Far.x,min(Far.y,Far.z)))>max(0.0,max(Near.x,max(Near.y,Near.z)))){return true;}}
  else{let BB = dot(B,B);let AB = dot(A,B);let H = AB*AB-BB*(dot(A,A)-1);if (H>0){if (min(Limit,(-AB+sqrt(H))/BB)>max(0.0,(-AB-sqrt(H))/BB)){return true;}}}
 }
 return false;
}
float3 AddClouds(float2 Pixel,float3 Original) {
 if (CloudControl.Render.y<.5||Refresh.InspectionMode!=0u){return Original;}
 let P = int2(Pixel);let Glass = LoadTexel(CloudCabin, P, 0).r;if (Glass<=0){return Original;}
 let D = LoadTexel(CloudSceneDepth, P, 0);
 if (!CloudsBeforeSurface(Pixel,D)&&(CloudControl.FogCentre.w<.5||!FogBeforeSurface(Pixel,D))){return Original;}let N = int2(TextureExtent(CloudColour));let UV = Pixel*Refresh.InverseScreenExtent;
 if (D<=0){let Q = clamp(int2(UV*float2(N)),int2(0),N-1);if (LoadTexel(CloudDepth, Q, 0).x==0){let S = CloudColour.SampleLevel(FlareSampler, UV, 0);return lerp(Original,Original*S.a+S.rgb,Glass);}}
 let World = ReconstructWorldPosition(Pixel,max(D,1e-6));let Distance = SelectBranch(0.0,min(60000.0,length(World-Refresh.CameraPosition)),D>0);
 let F = UV*float2(N)-.5;let B = int2(floor(F));let Frac = frac(F);var Sum = 0.0;var S = float4(0);
 for (var Y = 0; Y<2; Y++){for (var X = 0; X<2; X++){
  let Q = clamp(B+int2(X,Y),int2(0),N-1);let Guide = LoadTexel(CloudDepth, Q, 0).x;
  // Continuous bilateral weighting: the previous hard 2% depth rejection
  // returned Original at entire contour bands on sloping road surfaces.
  if ((Guide==0&&Distance==0)||(Guide>0&&Distance>0)){
   let Spatial = SelectBranch(1.0-Frac.x,Frac.x,X==1)*SelectBranch(1.0-Frac.y,Frac.y,Y==1);
   let Error = abs(Guide-Distance)/max(.5,Distance*.03);
   let W = Spatial/max(1.0,1.0+Error*Error*Error*Error);
   S+=LoadTexel(CloudColour, Q, 0)*W;Sum+=W;
  }
 }}
 if (Sum<=1e-12){return Original;}S/=Sum;return lerp(Original,Original*S.a+S.rgb,Glass);
}

// Bolt incandescence is NOT behind the entire cloud column. Blend separately after clouds,
// estimating only the portion of measured optical depth in front of the channel.
  [[vk::binding(11, 0)]] Texture2D<float4> LightningImage;
float2 BoltMediumInterval(float3 O,float3 D,float3 Centre,float3 Half) {
 let Safe = SelectBranch(float3(1e-7),D,abs(D)>float3(1e-7));let A = (Centre-Half-O)/Safe;let B = (Centre+Half-O)/Safe;
 let Lo = min(A,B);let Hi = max(A,B);return float2(max(0.0,max(Lo.x,max(Lo.y,Lo.z))),min(Hi.x,min(Hi.y,Hi.z)));
}
float2 BoltOpticalPortion(float2 Hit,float Distance,float Limit,float Weight) {
 let Far = min(Limit,Hit.y);return float2(max(0.0,min(Distance,Far)-Hit.x),max(0.0,Far-Hit.x))*Weight;
}
float3 AddLightning(float2 Pixel,float3 Original) {
 if (CloudControl.Render.w<.5||Refresh.InspectionMode!=0u){return Original;}
 let Bolt = LoadTexel(LightningImage, int2(Pixel), 0);if (Bolt.a<=0.0){return Original;}
 let Glass = LoadTexel(CloudCabin, int2(Pixel), 0).r;if (Glass<=0.0){return Original;}
 var Transmission = 1.0;
 if (CloudControl.Render.y>.5){
  let O = Refresh.CameraPosition;let W = ReconstructWorldPosition(Pixel,Bolt.a);let Distance = length(W-O);let D = normalize(W-O);
  let SurfaceDepth = LoadTexel(CloudSceneDepth, int2(Pixel), 0);let Surface = ReconstructWorldPosition(Pixel,max(1e-6,SurfaceDepth));let Limit = SelectBranch(CloudControl.Render.z,length(Surface-O),SurfaceDepth>0);
  var Optical = float2(0);let C = CloudControl;
  if (C.Control.x>.5&&abs(D.z)>1e-7){let A = (C.Layer.x-O.z)/D.z;let B = (min(C.Control.w,C.Layer.x+C.Layer.y)-O.z)/D.z;Optical+=BoltOpticalPortion(float2(max(0.0,min(A,B)),max(A,B)),Distance,Limit,C.Layer.w*.012);}
  if (C.Centre.w>.5){Optical+=BoltOpticalPortion(BoltMediumInterval(O,D,C.Centre.xyz,C.Size.xyz),Distance,Limit,C.Local.x*.06);}
  if (C.FogCentre.w>.5){Optical+=BoltOpticalPortion(BoltMediumInterval(O,D,C.FogCentre.xyz,C.FogSize.xyz),Distance,Limit,C.FogMedium.x);}
  if (Optical.y>1e-5){let T = CloudColour.SampleLevel(FlareSampler, Pixel*Refresh.InverseScreenExtent, 0).a;Transmission=pow(max(.00005,T),clamp(Optical.x/Optical.y,0.0,1.0));}
 }
 return Original+Bolt.rgb*Transmission*Glass;
}
