// Low-resolution, depth-bounded integration. Interval boundaries keep overlapping media in front-to-back order.
//! include "FrameDescriptor.wgsl"
//! include "CloudRecords.wgsl"
@group(0) @binding(1) var<uniform> C:CloudParameters;
@group(0) @binding(2) var ShapeField:texture_3d<f32>;
@group(0) @binding(3) var FieldSampler:sampler;
@group(0) @binding(4) var DepthMap:texture_depth_2d;
@group(0) @binding(5) var CloudTarget:texture_storage_2d<rgba16float,write>;
@group(0) @binding(6) var DepthTarget:texture_storage_2d<rgba16float,write>;
//! include "CloudField.wgsl"
struct WeatherFlash {PositionPower:vec4f,ColourRadius:vec4f,ChannelEnd:vec4f}
@group(0) @binding(7) var<uniform> Flash:WeatherFlash;
fn LightningScatter(P:vec3f)->vec3f{
 if(Flash.PositionPower.w<=0.0001){return vec3f(0);}
 let A=Flash.PositionPower.xyz;let Axis=Flash.ChannelEnd.xyz-A;
 let Along=select(0.0,clamp(dot(P-A,Axis)/max(1.0,dot(Axis,Axis)),0.0,1.0),Flash.ChannelEnd.w>.5);
 let Source=A+Axis*Along;let Distance=length(P-Source);let Radius=max(1.0,Flash.ColourRadius.w);
 if(Distance>=Radius){return vec3f(0);}
 // Localized source, attenuated by intervening cloud density (bounded 3-tap approximation).
 var OD=0.0;for(var I=0u;I<3u;I++){let Q=mix(P,Source,(f32(I)+.5)/3.0);OD+=(select(0.0,LayerDensity(Q)*.001,C.Control.x>.5)+select(0.0,PuffLightingDensity(Q)*.005,C.Centre.w>.5))*Distance/3.0;}
 let Edge=clamp(1.0-Distance/Radius,0.0,1.0);
 let Direct=Edge*Edge/(1.0+pow(Distance/(Radius*.2),2.0))*exp(-min(12.0,OD));
 // A bounded diffusion lobe approximates light scattered through an optically thick cloud.
 // Direct-only exponential attenuation made an embedded discharge disappear at the visible cloud face.
 // Extended along the discharge, still multiplied by actual participating density in the march; never a sky/exposure lift.
 let Diffuse=.45*Edge*Edge/(1.0+pow(Distance/(Radius*.65),2.0))/(1.0+OD*.2);
 return Flash.ColourRadius.rgb*Flash.PositionPower.w*(Direct+Diffuse);
}
fn PhaseHG(Cos:f32,G:f32)->f32{return (1.0-G*G)/max(.04,pow(1.0+G*G-2.0*G*Cos,1.5));}
fn MediumLight(P:vec3f,D:vec3f,G:f32,Local:bool)->vec3f{
 let Day=smoothstep(-.12,.12,Frame.SolarDisc.z);let Ambient=vec3f(.18,.23,.32)*Day*Frame.SkyScale+Frame.NightSkyRadiance;
 var OD=0.0;let Count=u32(C.Formation.y);let Step=select(max(20.0,C.Layer.y*.24),max(2.0,C.Size.z*.65),Local);
 for(var I=0u;I<Count;I++){let Q=P+Frame.SolarDisc.xyz*Step*(f32(I)+.5);if(Local){OD+=PuffLightingDensity(Q)*Step*.06;}else{OD+=LayerDensity(Q)*Step*.012;}}
 let Shadow=exp(-OD*(.35+C.Formation.z*1.8));let Powder=1.0+C.Formation.w*(1.0-exp(-OD*2.0));
 return LightningScatter(P)+Ambient+Frame.SolarRadiance.rgb*.12*Day*(.35+.2*PhaseHG(dot(D,Frame.SolarDisc.xyz),G))*Shadow*Powder;
}
fn FogLight(P:vec3f,D:vec3f)->vec3f{
 let Day=smoothstep(-.12,.12,Frame.SolarDisc.z);
 var Shadow=1.0;if(C.FogControl.z>.5){var OD=0.0;let Step=max(.25,min(C.FogSize.x,min(C.FogSize.y,C.FogSize.z))*.5);for(var I=1u;I<=4u;I++){OD+=FogDensity(P+Frame.SolarDisc.xyz*Step*f32(I))*(1.0+C.FogMaterial.w)*Step;}Shadow=exp(-OD);}
 var Light=vec3f(.12,.17,.24)*Day*Frame.SkyScale+Frame.NightSkyRadiance+Frame.SolarRadiance.rgb*.045*Day*PhaseHG(dot(D,Frame.SolarDisc.xyz),C.FogLighting.x)*Shadow;
 for(var I=0u;I<u32(C.FogControl.x);I++){
  let L=C.Lamps[I];let To=P-L.PositionType.xyz;let Distance=max(.5,length(To));let V=To/Distance;let Cos=dot(V,L.DirectionReach.xyz);
  var Cone=1.0;if(L.PositionType.w>.5){Cone=smoothstep(L.Params.y,max(L.Params.y+.001,L.Params.x),Cos);}
  let Falloff=pow(clamp(1.0-Distance/max(1.0,L.DirectionReach.w),0.0,1.0),2.0)/max(1.0,Distance*Distance);
  Light+=L.ColourIntensity.rgb*L.ColourIntensity.w*Cone*Falloff*PhaseHG(dot(D,V),C.FogLighting.x)*.08*C.FogLighting.z;
 }return (Light+LightningScatter(P))*C.FogMaterial.rgb*(1.0-C.FogMaterial.w);
}
// Avalanche hash: no tiled 8-position diagonal phase. Stable in screen space; no frame shimmer.
fn CloudSampleOffset(P:vec2u,I:u32)->f32{
 var H=(P.x*0x9e3779b9u)^(P.y*0x85ebca6bu)^(I*0xc2b2ae35u);
 H=(H^(H>>16u))*0x7feb352du;H=(H^(H>>15u))*0x846ca68bu;H=H^(H>>16u);
 return .35+.3*f32(H&65535u)/65535.0;
}
@compute @workgroup_size(8,8)
fn DrawClouds(@builtin(global_invocation_id) ID:vec3u){
 let N=textureDimensions(CloudTarget);if(any(ID.xy>=N)){return;}
 let Pixel=(vec2f(ID.xy)+.5)/vec2f(N)*Frame.ScreenExtent;let P=clamp(vec2i(Pixel),vec2i(0),vec2i(Frame.ScreenExtent)-1);
 let Depth=textureLoad(DepthMap,P,0);let World=ReconstructWorldPosition(vec2f(P)+.5,max(Depth,1e-6));let O=Frame.CameraPosition;let D=normalize(World-O);
 let Limit=select(C.Render.z,min(C.Render.z,length(World-O)),Depth>0);
 let GH=LayerHit(O,D,Limit);let LH=PuffHit(O,D,Limit);let FH=FogHit(O,D,Limit);
 var Edges=array<f32,6>(GH.x,GH.y,LH.x,LH.y,FH.x,FH.y);
 var EndCount=6u;
 if(C.Centre.w<.5&&C.FogCentre.w<.5){Edges[0]=GH.x;Edges[1]=GH.y;EndCount=2u;}else if(C.Control.x<.5&&C.FogCentre.w<.5){Edges[0]=LH.x;Edges[1]=LH.y;EndCount=2u;}else if(C.Control.x<.5&&C.Centre.w<.5){Edges[0]=FH.x;Edges[1]=FH.y;EndCount=2u;}
 for(var I=1u;I<EndCount;I++){var J=I;loop{if(J==0u){break;}if(Edges[J]>=Edges[J-1u]){break;}let V=Edges[J];Edges[J]=Edges[J-1u];Edges[J-1u]=V;J--;}}
 var RGB=vec3f(0);var Transmission=1.0;
 for(var K=0u;K+1u<EndCount;K++){
  let A=Edges[K];let B=Edges[K+1u];if(B<=A||Transmission<.0005){continue;}let Mid=(A+B)*.5;
  let Global=Mid>GH.x&&Mid<GH.y;let Local=Mid>LH.x&&Mid<LH.y;let Fog=Mid>FH.x&&Mid<FH.y;
  var Rate=0.0;
  if(Global){
   // World-space spacing must not depend on the surface-clipped ray length.
   // A long/slanted layer gets more samples, bounded by the same hard cap.
   let LayerDepth=max(1.0,min(C.Control.w,C.Layer.x+C.Layer.y)-C.Layer.x);
   let Step=min(LayerDepth/max(1.0,C.Control.y),max(20.0,C.Shape.x*180.0));
   Rate=max(Rate,1.0/max(1.0,Step));
  }
  if(Local){
   let Span=2.0*max(C.Size.x,max(C.Size.y,C.Size.z));
   let BaseStep=Span/max(1.0,C.Local.w);
   let FeatureStep=max(.5,C.Size.w*.25);
   Rate=max(Rate,1.0/max(.5,min(BaseStep,FeatureStep)));
  }
  if(Fog){
   // Geometry depth must not stretch the whole fog quadrature lattice. Keep
   // a feature/optical-depth-limited spacing as the view terminates on surfaces.
   let Diameter=2.0*length(C.FogSize.xyz);
   let BaseStep=Diameter/max(8.0,C.FogMedium.w);
   let FeatureStep=max(.04,C.FogMedium.z*.125);
   let OpticalStep=.25/max(.001,C.FogMedium.x*(1.0+C.FogMaterial.w));
   Rate=max(Rate,1.0/max(.04,min(BaseStep,min(FeatureStep,OpticalStep))));
  }if(Rate<=0.0){continue;}
  let Count=min(select(96u,192u,Fog),max(1u,u32(ceil((B-A)*Rate))));let DS=max(1.0/Rate,(B-A)/f32(Count));
  for(var I=0u;I<Count;I++){
   let Span=min(DS,max(0.0,B-A-f32(I)*DS));if(Span<=0.0){break;}
   let Distance=A+f32(I)*DS+CloudSampleOffset(ID.xy,I)*Span;let Q=O+D*Distance;var G=0.0;var L=0.0;var F=0.0;
   if(Global){G=LayerDensity(Q)*.012;}if(Local){L=PuffDensity(Q)*.06;}if(Fog){F=FogDensity(Q)*(1.0+C.FogMaterial.w);}
   let Density=G+L+F;if(Density<1e-6){continue;}var Lighting=vec3f(0);
   if(G>0){Lighting+=G*MediumLight(Q,D,C.Shape.y,false);}if(L>0){Lighting+=L*MediumLight(Q,D,C.Local.z,true);}if(F>0){Lighting+=F*FogLight(Q,D);}
   let Alpha=(1.0-exp(-Density*Span))*exp(-Distance/45000.0);RGB+=Transmission*Alpha*Lighting/Density;Transmission*=1.0-Alpha;if(Transmission<.0005){break;}
  }
 }
 textureStore(CloudTarget,vec2i(ID.xy),vec4f(RGB,Transmission));
 textureStore(DepthTarget,vec2i(ID.xy),vec4f(select(0.0,min(60000.0,length(World-O)),Depth>0),0,0,1));
}
