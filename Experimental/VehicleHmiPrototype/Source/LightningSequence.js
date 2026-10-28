import {SparkSequence} from './SparkSequence.js';
import {LightningStorm} from './LightningStorm.js';
import {ThunderAudio} from './ThunderAudio.js';
export class LightningSequence extends SparkSequence{
 constructor(E,Shaders,Host){super(E,Shaders);this.Audio=new ThunderAudio();this.Storm=new LightningStorm({Audio:this.Audio,Event:(N,D)=>Host.Telemetry?.Event(N,D)});this.Host=Host;}
 async ConstructPrograms(Frame){
  const Shaders=this.Shaders;this.Shaders={ConstructRasterProgram:(Name,...Args)=>Shaders.ConstructRasterProgram(Name==='SparkRaster.wgsl'?'LightningRaster.wgsl':Name,...Args.map((A,I)=>I===0?{...A,Targets:A.Targets.map(T=>({...T,blend:{...T.blend,alpha:{srcFactor:'one',dstFactor:'one',operation:'max'}}}))}:A))};
  try{await super.ConstructPrograms(Frame);}finally{this.Shaders=Shaders;}
  this.Resize([1,1]);
 }
 Trigger(){this.Host.Settings.LightningEnabled=true;this.Storm.Request();}
 Advance(Delta,Camera){if(globalThis.document?.hidden){this.Storm.Clear();this.Live=0;return;}this.Storm.Advance(Delta,this.Host.Settings,Camera,this.Host.Clouds?.UniformData);this.Live=0;
  for(const B of this.Storm.Bolts)for(const Q of B.Segments){if(this.Live>=2048)break;const O=this.Live++*12;this.Instances.set([...Q.a,Q.w,...Q.b,1,.72*B.Energy*24,.82*B.Energy*24,B.Energy*24,1],O);}
  if(this.Live)this.Exchange.Device.queue.writeBuffer(this.InstanceRecords,0,this.Instances,0,this.Live*12);
 }
 Resize(Extent){
  if(this.Extent?.every((V,I)=>V===Extent[I]))return;
  this.Texture?.destroy();this.Extent=Extent.slice();this.Texture=this.Exchange.ConstructTexture({label:'Lightning radiance + reverse depth',size:Extent,format:'rgba16float',usage:GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_SRC});this.View=this.Texture.createView();this.NeedsClear=true;this.Host.Display?.RefreshCloudBindings?.();
 }
 Record(Encoder,Depth,Metrics,OtherWeather=false){
  const Draw=this.Live>0&&this.Host.Settings.Inspection===0;
  if(!Draw&&!OtherWeather&&!this.WasDrawn&&!this.NeedsClear)return;
  const P=Encoder.beginRenderPass({label:'Lightning',timestampWrites:Draw?Metrics?.QueryWrites('Lightning'):undefined,colorAttachments:[{view:this.View,loadOp:'clear',clearValue:[0,0,0,0],storeOp:'store'}],depthStencilAttachment:Draw?{view:Depth,depthReadOnly:true}:undefined});
  if(Draw){P.setPipeline(this.Program);P.setBindGroup(0,this.Group);P.setVertexBuffer(0,this.InstanceRecords);P.draw(6,this.Live);}
  P.end();this.WasDrawn=Draw||OtherWeather;this.NeedsClear=false;
 }
}
