// A small sun-projected 2D field, independent of screen resolution. Updated at most 8 Hz
// for wind, immediately for edits/repositioning. The receiver uses the matching frozen sun.
export class CloudShadow {
 constructor(Exchange,Shaders){this.Exchange=Exchange;this.Shaders=Shaders;this.Revision=0;this.LastUpdate=-Infinity;}
 async ConstructPrograms(Frame,Cloud){
  const E=this.Exchange;this.Params=E.ConstructRecords(48,GPUBufferUsage.UNIFORM|GPUBufferUsage.COPY_DST,'Projected cloud shadows');
  this.Texture=E.ConstructTexture({label:'Cloud shadow optical depth',size:[128,128],format:'rgba16float',usage:GPUTextureUsage.STORAGE_BINDING|GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_SRC});this.View=this.Texture.createView();
  this.Sampler=E.ConstructSampler({addressModeU:'clamp-to-edge',addressModeV:'clamp-to-edge',magFilter:'linear',minFilter:'linear'});
  this.Layout=E.ConstructDescriptorLayout(['Uniform','Uniform','Texture3D','Sampler','Uniform','StorageTexture2D:rgba16float'],GPUShaderStage.COMPUTE,'Projected cloud shadows');
  this.Group=E.ConstructDescriptorGroup(this.Layout,[Frame,Cloud.Params,Cloud.Shape.createView(),Cloud.Sampler,this.Params,this.View]);
  this.Program=await this.Shaders.ConstructComputeProgram('CloudShadowProject.wgsl','ProjectCloudShadow',[this.Layout]);
 }
 Prepare(Cloud,Camera,Light){
  const S=Cloud.Settings,Sun=Light.SunDirection??Light.Direction??[0,0,1],Eye=Camera.Position??[0,0,2];
  const Active=Cloud.Active&&(Cloud.UniformData[20]>.5||Cloud.UniformData[11]>.5)&&S.CloudShadows&&S.CloudShadowStrength>0&&Sun[2]>.03&&S.SunEnabled!==false;
  this.Active=!!Active;this.Dirty=false;
  if(!Active){this.LastKey=null;this.LastUpdate=-Infinity;this.Exchange.Device.queue.writeBuffer(this.Params,0,new Float32Array(12));return;}
  const Span=S.CloudShadowDistance,Texel=Span/128;
  const Centre=Eye.slice(0,2).map((V,I)=>Math.round((V-Eye[2]*Sun[I]/Sun[2])/Texel)*Texel);
  const Key=JSON.stringify([Centre,Span,Sun,S.LocalCloudX,S.LocalCloudY,S.LocalCloudZ,S.CloudShadowStrength,S.CloudShadowSoftness,S.CloudShadowSamples,...Array.from(Cloud.UniformData).filter((_,I)=>!(I>=8&&I<11)&&I!==22&&I!==31&&!(I>=24&&I<28)&&!(I>=40))]);
  const Moving=S.WindEnabled!==false&&S.CloudWindSpeed>0&&((Cloud.UniformData[20]>.5&&S.CloudFollowWind)||(Cloud.UniformData[11]>.5&&S.LocalCloudFollowWind));
  this.Dirty=Key!==this.LastKey||(Moving&&Cloud.Time-this.LastUpdate>=.125);
  if(!this.Dirty)return;
  this.LastKey=Key;this.LastUpdate=Cloud.Time;
  this.Data=new Float32Array([Centre[0]-Span/2,Centre[1]-Span/2,Span,1,...Sun,S.CloudShadowStrength,S.CloudShadowSoftness,Math.round(S.CloudShadowSamples),S.CloudBase,Math.min(S.CloudCeiling,S.CloudBase+S.CloudThickness)]);
  this.Exchange.Device.queue.writeBuffer(this.Params,0,this.Data);
 }
 Record(Encoder,Metrics){if(!this.Active||!this.Dirty)return false;const P=Encoder.beginComputePass({label:'Projected cloud shadows',timestampWrites:Metrics?.QueryWrites('Cloud shadows')});P.setPipeline(this.Program);P.setBindGroup(0,this.Group);P.dispatchWorkgroups(16,16);P.end();this.Dirty=false;this.Revision++;return true;}
}
