import {SetCloudDiagnosticField} from './CloudField.js';
import {WindSequence} from './WindSequence.js';
import {CloudShadow} from './CloudShadow.js';
import {CloudDefaults,CloudExtent,BuildCloudField,CloudSkySamples,CloudLocalSamples} from './CloudSettings.js';
// One bounded cloud pass + two low-resolution targets. Optional sun-projected 2D shadows;
// no 3D lighting cache, temporal reconstruction or extra full-screen composition.
export class CloudSequence{
 constructor(Exchange,Shaders){this.Exchange=Exchange;this.Shaders=Shaders;this.Time=0;this.Wind=new WindSequence();this.Revision=0;}
 async ConstructPrograms(Frame){
  const E=this.Exchange;this.Frame=Frame;
  this.Params=E.ConstructRecords(544,GPUBufferUsage.UNIFORM|GPUBufferUsage.COPY_DST,'Cloud renderer v2');
  this.LightningParams=E.ConstructRecords(48,GPUBufferUsage.UNIFORM|GPUBufferUsage.COPY_DST,'Lightning cloud source');
  this.Shape=E.ConstructTexture({label:'Cloud shape (baked once)',size:[64,64,64],dimension:'3d',format:'r8unorm',usage:GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_DST});
  const Field=BuildCloudField();SetCloudDiagnosticField(Field);
  E.Device.queue.writeTexture({texture:this.Shape},Field,{bytesPerRow:64,rowsPerImage:64},[64,64,64]);
  this.Sampler=E.ConstructSampler({addressModeU:'repeat',addressModeV:'repeat',addressModeW:'repeat',magFilter:'linear',minFilter:'linear'});
  this.Layout=E.ConstructDescriptorLayout(['Uniform','Uniform','Texture3D','Sampler','TextureDepth2D','StorageTexture2D:rgba16float','StorageTexture2D:rgba16float','Uniform'],GPUShaderStage.COMPUTE,'Cloud renderer v2');
  this.Program=await this.Shaders.ConstructComputeProgram('CloudTrace.wgsl','DrawClouds',[this.Layout]);
  this.Shadows=new CloudShadow(E,this.Shaders);await this.Shadows.ConstructPrograms(Frame,this);
  this.Allocate([1,1]);
 }
 Allocate(Extent){
  for(const T of this.Textures??[])T.destroy();this.Extent=Extent;
  const Make=Label=>this.Exchange.ConstructTexture({label:Label,size:Extent,format:'rgba16float',usage:GPUTextureUsage.STORAGE_BINDING|GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_SRC|GPUTextureUsage.COPY_DST});
  this.Colour=Make('Cloud optical integration');this.Guide=Make('Cloud depth guide');this.Textures=[this.Colour,this.Guide];this.ColourView=this.Colour.createView();this.GuideView=this.Guide.createView();
 }
 Prepare(Display,Surface,Settings,Camera,Light,Delta,Lamps=null,Lightning=null,BoltActive=false){
  const S={...CloudDefaults,...Settings};this.Settings=S;this.LastForward=Camera.Forward.slice();this.Time+=Math.max(0,Math.min(.1,Delta||0));
  const Global=S.CloudEnabled&&S.CloudDensity>0&&S.CloudCoverage>0&&S.CloudBase<S.CloudCeiling;
  const Local=S.LocalCloudEnabled&&S.LocalCloudDensity>0&&S.LocalCloudCoverage>0;const Fog=S.LocalFogEnabled&&S.LocalFogDensity>0;this.Active=Settings.Inspection===0&&(Global||Local||Fog);const Anchor=[S.LocalCloudX,S.LocalCloudY,S.LocalCloudZ].join(',');if(Anchor!==this.LocalAnchor||(S.LocalCloudEnabled&&!this.LocalWasEnabled))this.Wind.Offsets.LocalCloud.fill(0);this.LocalAnchor=Anchor;this.LocalWasEnabled=S.LocalCloudEnabled;const Motion=this.Wind.Advance(S,Delta);
  const Extent=this.Active?CloudExtent(...Display.ScreenExtent,S.CloudResolution):[1,1];
  const Changed=Extent.some((V,I)=>V!==this.Extent[I]);if(Changed)this.Allocate(Extent);
  if(Changed||this.Depth!==Surface.Views.Depth){this.Depth=Surface.Views.Depth;this.Group=this.Exchange.ConstructDescriptorGroup(this.Layout,[this.Frame,this.Params,this.Shape.createView(),this.Sampler,this.Depth,this.ColourView,this.GuideView,this.LightningParams]);Display.RefreshCloudBindings?.();}
  const R=this.UniformData??new Float32Array(136);R.fill(0);R.set([S.CloudBase,S.CloudThickness,S.CloudCoverage,S.CloudDensity]);R.set([S.CloudScale,S.CloudAnisotropy,S.CloudType,S.CloudAnvil],4);
  R.set([S.LocalCloudX,S.LocalCloudY,S.LocalCloudZ,+Local],8);R.set([S.LocalCloudHalfX,S.LocalCloudHalfY,S.LocalCloudHalfZ,S.LocalCloudScale],12);R.set([S.LocalCloudDensity,S.LocalCloudCoverage,S.LocalCloudAnisotropy,CloudLocalSamples(S)],16);
  const SkySamples=CloudSkySamples(S);
  R.set([+Global,SkySamples,this.Time,S.CloudCeiling],20);R.set([...Motion.Cloud,0],24);R.set([S.CloudResolution/100,+this.Active,40000,+BoltActive],28);
  R.set([S.CloudDetail,Math.round(S.CloudLightSteps),S.CloudAbsorption,S.CloudPowder],32);
  R.set([S.LocalCloudShape,S.LocalCloudForm,S.LocalCloudSoftness,S.LocalCloudDetail],36);R.set([...Motion.LocalCloud,0],40);
  R.set([S.LocalFogX,S.LocalFogY,S.LocalFogZ,+Fog],44);const FogSize=S.LocalFogShape===2?[S.LocalFogHalfX,S.LocalFogHalfX,S.LocalFogHalfX]:S.LocalFogShape===3?[S.LocalFogHalfX,S.LocalFogHalfX,S.LocalFogHalfZ*.5]:[S.LocalFogHalfX,S.LocalFogHalfY,S.LocalFogHalfZ];
  if(S.LocalFogShape===3)R[46]+=S.LocalFogHalfZ*.5;R.set([...FogSize,S.LocalFogShape],48);
  R.set([S.LocalFogDensity,S.LocalFogNoise,S.LocalFogScale,Math.round(S.LocalFogSteps)],52);R.set([...S.LocalFogAlbedo,S.LocalFogAbsorption],56);
  R.set([S.LocalFogAnisotropy,S.LocalFogSoftness,S.LocalFogLightGain,0],60);R.set([...Motion.LocalFog,0],64);
  // Four strongest relevant lights, selected once on CPU, not a 64-light loop at each march sample.
  const Indices=this.LampIndices??=new Int32Array(4),Scores=this.LampScores??=new Float32Array(4);Indices.fill(-1);Scores.fill(-1);let Count=0;
  if(Fog&&Lamps)for(let I=0;I<64;I++){
   const J=I*16;if(!Lamps[J+15]||Lamps[J+11]<=0)continue;
   const Distance=Math.hypot(Lamps[J]-S.LocalFogX,Lamps[J+1]-S.LocalFogY,Lamps[J+2]-S.LocalFogZ);
   if(Distance>Lamps[J+7]+Math.hypot(S.LocalFogHalfX,S.LocalFogHalfY,S.LocalFogHalfZ))continue;
   const Score=Lamps[J+11]/Math.max(1,Distance*Distance);let Slot=0;for(let K=1;K<4;K++)if(Scores[K]<Scores[Slot])Slot=K;
   if(Score>Scores[Slot]){if(Indices[Slot]<0)Count++;Indices[Slot]=J;Scores[Slot]=Score;}
  }
  R[68]=Count;R[69]=+S.LocalFogFalloff;R[70]=+S.LocalFogSelfShadow;let Slot=0;for(let I=0;I<4;I++)if(Indices[I]>=0){for(let J=0;J<16;J++)R[72+Slot*16+J]=Lamps[Indices[I]+J];Slot++;}
  const Flash=this.FlashData??=new Float32Array(12);Flash.fill(0);if(Lightning)Flash.set(Lightning.subarray(0,12));this.Exchange.Device.queue.writeBuffer(this.LightningParams,0,Flash);
  this.UniformData=R;this.Exchange.Device.queue.writeBuffer(this.Params,0,R);this.Shadows.Prepare(this,Camera,Light);
 }
 Record(Encoder,Metrics){if(!this.Active)return false;const P=Encoder.beginComputePass({label:'Clouds',timestampWrites:Metrics?.QueryWrites('Clouds')});P.setPipeline(this.Program);P.setBindGroup(0,this.Group);P.dispatchWorkgroups(Math.ceil(this.Extent[0]/8),Math.ceil(this.Extent[1]/8));P.end();this.Revision++;return true;}
}
