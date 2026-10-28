//! include "CloudRecords.wgsl"
//! include "PrecipitationRecords.wgsl"
@group(0) @binding(0) var<uniform> C:CloudParameters;
@group(0) @binding(1) var ShapeField:texture_3d<f32>;
@group(0) @binding(2) var FieldSampler:sampler;
@group(0) @binding(3) var<uniform> P:PrecipitationParameters;
@group(0) @binding(4) var<storage,read_write> Flakes:array<SnowParticle>;
@group(0) @binding(5) var<storage,read_write> Deposits:array<atomic<u32>>;
@group(0) @binding(6) var<storage,read> Roof:array<f32>;
@group(0) @binding(7) var<storage,read_write> Stats:array<atomic<u32>>;
@group(0) @binding(8) var<storage,read_write> Grains:array<SettledGrain>;
@group(0) @binding(9) var<storage,read_write> MeshDraw:array<atomic<u32>>;
//! include "CloudField.wgsl"
fn Hash(V:u32)->u32{var X=V;X=(X^(X>>16u))*2246822519u;X=(X^(X>>13u))*3266489917u;return X^(X>>16u);}
fn Random(V:u32)->f32{return f32(Hash(V)&16777215u)/16777216.0;}
fn WindAt(Z:f32)->vec3f{let H=max(0.0,Z)/1000.0;let A=P.Wind.w*H*.01745329252;let S=1.0+P.Wind.z*H;let V=vec2f(cos(A)*P.Wind.x+sin(A)*P.Wind.y,-sin(A)*P.Wind.x+cos(A)*P.Wind.y)*S;let T=P.Weather.y*length(P.Wind.xy);return (P.Extra.y*P.Extra.z)*vec3f(V+vec2f(sin(P.Weather.x*.31+H),sin(P.Weather.x*.27+H*3.0))*T,sin(P.Weather.x*.22+H)*T*.12);}
// Reference cloudCoverAt uses a wider footprint than the visible cloud core.
// Use that same threshold margin on THIS renderer's baked/advection field (do not replace accepted clouds).
fn CelestialCloudCover(Pos:vec3f)->f32{
 let Q=(Pos-C.Wind.xyz)/max(7200.0*C.Shape.x,1.0);let N=Field(Q)-C.Formation.x*(1.0-Field(Q*4.1+vec3f(.21,.47,.13)))*.22;
 return clamp((N-(1.0-C.Layer.z-.12))/max(.001,C.Layer.z+.12),0.0,1.0);
}
fn Depth(I:i32,Edge:f32)->f32{if(I<0){return 0.0;}return f32(atomicLoad(&Deposits[u32(I)]))*1e-6/(Edge*Edge);}
fn ReserveGrain()->u32{var Old=atomicLoad(&MeshDraw[1]);for(var K=0u;K<16u;K++){if(Old>=arrayLength(&Grains)){return 4294967295u;}let R=atomicCompareExchangeWeak(&MeshDraw[1],Old,Old+1u);if(R.exchanged){return Old;}Old=R.old_value;}return 4294967295u;}
// Contact -> rest -> mesh. Never append geometry for a flying or bouncing particle.
fn Convert(F:ptr<function,SnowParticle>,Category:u32,GroundCount:u32){
 let Receiver=u32((*F).Shape.w);let Car=Receiver>=GroundCount;let Units=u32((*F).VelocityVolume.w);
 if(Category<2u||P.Look.w<=0.0){atomicAdd(&Stats[9],Units);(*F).VelocityVolume.w=0;(*F).Shape.y=0;atomicAdd(&Stats[select(1u,2u,Car)],1u);return;}
 let Slot=ReserveGrain();if(Slot==4294967295u){(*F).Shape.y=2;return;}
 Grains[Slot].PositionRadius=vec4f((*F).OriginDensity.xyz,(*F).Position.w);
 Grains[Slot].Meta=vec4f(f32(Category),select(0.0,1.0,Car),f32(Units),0);
 atomicAdd(&Deposits[Receiver],Units);atomicMax(&Stats[12],Slot+1u);atomicAdd(&Stats[select(1u,2u,Car)],1u);
 (*F).VelocityVolume.w=0;(*F).Shape.y=0;
}
@compute @workgroup_size(64)
fn AdvancePrecipitation(@builtin(global_invocation_id) ID:vec3u){
 let I=ID.x;if(I>=arrayLength(&Flakes)){return;}var F=Flakes[I];let Seed=Hash(I+u32(P.Snow.w)*32771u+713u);let GroundCount=u32(P.Region.w*P.Region.w);
 if(F.Shape.y<.5){
  if(I>=u32(P.Step.z)||P.Step.y<.5||Random(Seed)>P.Step.w){return;}
  let Radius=sqrt(Random(Seed+1u))*P.Emitter.w;let Angle=Random(Seed+2u)*6.283185;
  let XY=P.Emitter.xy+P.Direction.xy*P.Emitter.w*.45+vec2f(cos(Angle),sin(Angle))*Radius;
  let Floor=max(TerrainHeight(XY),P.Emitter.z-40.0);if(P.Direction.w<=Floor){return;}
  let Z=Floor+pow(Random(Seed+3u),.8)*(P.Direction.w-Floor);
  let Local=C.Centre.w>.5&&(P.Weather.w>1.5||(C.Control.x<.5&&P.Weather.w<.5));
  var CloudZ=C.Layer.x+C.Layer.y*.25;if(Local){CloudZ=C.Centre.z-C.Size.z*.5;}
  let CloudSample=vec3f(XY,CloudZ);var Cover=1.0;
  if(P.Direction.z>.5){
   if(Local){Cover=PuffDensity(CloudSample);}else{if(C.Control.x<.5||P.Weather.w>1.5){return;}Cover=CelestialCloudCover(CloudSample);}
   if(Cover<.04||Random(Seed+4u)>min(1.0,.25+Cover*1.2)){atomicAdd(&Stats[7],1u);return;}
  }
  let Category=u32(P.Physics.w);let T=PrecipType(Category);let Size=(.7+Random(Seed+5u)*.6)*P.Extra.x;
  let PhysicalRadius=Size*T.w*.0006*select(1.0,1.6,Category==3u);
  let W=WindAt(P.Emitter.z+20.0);let Volume=max(1u,u32(round(4.1887902*PhysicalRadius*PhysicalRadius*PhysicalRadius*1e6*max(.01,P.Look.w))));
  F.Position=vec4f(XY,Z,PhysicalRadius);F.Shape=vec4f(Random(Seed+6u)*6.28,1,f32(Category*4u),0);
  F.VelocityVolume=vec4f(W.x*(1.2+Random(Seed+7u)*1.2),W.y*(1.2+Random(Seed+8u)*1.2),-T.x*(.85+Random(Seed+9u)*.3),f32(Volume));F.OriginDensity=vec4f(CloudSample,Cover);
  atomicAdd(&Stats[0],1u);let Prior=atomicAdd(&Stats[10],Volume);if(Prior>4294967295u-Volume){atomicAdd(&Stats[11],1u);}
 }
 let Category=u32(F.Shape.z)/4u;let T=PrecipType(Category);let Car=u32(F.Shape.w)>=GroundCount;
 if(F.Shape.y>2.5&&F.Shape.y<3.5){atomicAdd(&Stats[6],1u);atomicAdd(&Stats[4],1u);return;}
 if(F.Shape.y==2.0||F.Shape.y==4.0){
  // Rest position follows its actual receiver, including a moving vehicle.
  if(Car){F.Position=vec4f((P.Body*vec4f(F.OriginDensity.xyz,1)).xyz,F.Position.w);}else{F.Position=vec4f(F.OriginDensity.xyz,F.Position.w);}
  F.Shape.x+=P.Step.x;if(F.Shape.y==2.0||F.Shape.x>=P.Look.z){Convert(&F,Category,GroundCount);}else{atomicAdd(&Stats[14],1u);}
 }else{
  var V=F.VelocityVolume.xyz;var Position=F.Position.xyz;let Steps=u32(clamp(ceil(length(V)*P.Step.x/.08),1.0,32.0));let DT=P.Step.x/f32(Steps);
  for(var K=0u;K<Steps;K++){
   let W=WindAt(Position.z);V.z+=(-T.x+W.z-V.z)*min(1.0,DT*2.5);V=vec3f(V.xy+(W.xy*2.0-V.xy)*min(1.0,DT*T.y),V.z);
   if(Category==3u){F.Shape.x+=DT*2.2;V.x+=sin(F.Shape.x)*DT*1.4;V.y+=cos(F.Shape.x*.7)*DT*1.2;}
   let Before=Position;Position+=V*DT;var Receiver=-1;var LocalContact=Position;
   let T0=f32(K)/f32(Steps);let T1=f32(K+1u)/f32(Steps);let Body=mix((P.PreviousInverseBody*vec4f(Position,1)).xyz,(P.InverseBody*vec4f(Position,1)).xyz,T1);let PriorBody=mix((P.PreviousInverseBody*vec4f(Before,1)).xyz,(P.InverseBody*vec4f(Before,1)).xyz,T0);let CI=ReceiverIndex(Body.xy,P.Car);
   if(CI>=0&&Roof[u32(CI)]> -9999.0){let Top=Roof[u32(CI)]+Depth(i32(GroundCount)+CI,P.Car.z);if(Body.z-F.Position.w<=Top&&PriorBody.z>=Top-.05&&PriorBody.z>=Body.z){Receiver=i32(GroundCount)+CI;LocalContact=vec3f(Body.xy,Top+F.Position.w);Position=(P.Body*vec4f(LocalContact,1)).xyz;}}
   if(Receiver<0){let GI=ReceiverIndex(Position.xy,P.Region);let Top=TerrainHeight(Position.xy)+Depth(GI,P.Region.z);if(Position.z-F.Position.w<=Top){if(GI<0){F.Shape.y=3;break;}Receiver=GI;Position.z=Top+F.Position.w;LocalContact=Position;}}
   if(Receiver>=0){atomicAdd(&Stats[3],1u);let Bounces=u32(F.Shape.z)%4u;let Rest=P.Physics.z*T.z;
    if(Rest>.05&&abs(V.z)>1.5&&Bounces<3u){V.z=abs(V.z)*Rest;V=vec3f(V.xy*.8,V.z);F.Shape.z+=1;Position.z+=.01;atomicAdd(&Stats[13],1u);break;}
    // Stop all velocity first; mesh conversion occurs only after the reference rest interval.
    V=vec3f(0);F.Shape.y=4;F.Shape.w=f32(Receiver);F.Shape.x=0;F.OriginDensity=vec4f(LocalContact,1);break;
   }
  }
  F.Position=vec4f(Position,F.Position.w);F.VelocityVolume=vec4f(V,F.VelocityVolume.w);
 }
 if(F.Shape.y==2.0){atomicAdd(&Stats[6],1u);}if(F.Shape.y==3.0){atomicAdd(&Stats[6],1u);}
 if(F.Shape.y==1.0&&F.VelocityVolume.z>0.0){atomicAdd(&Stats[5],1u);}if(F.Shape.y>.5){atomicAdd(&Stats[4],1u);}Flakes[I]=F;
}
