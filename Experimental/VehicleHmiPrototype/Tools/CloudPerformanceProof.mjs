// Isolated cloud-pass timings. Software Vulkan comparisons are NOT hardware FPS predictions.
import {CloudPresentProbe} from './CloudPresentProbe.mjs';
import fs from 'node:fs';import {performance} from 'node:perf_hooks';import {create,globals} from 'webgpu';
import {CloudSequence} from '../Source/CloudSequence.js';import {CloudDefaults} from '../Source/CloudSettings.js';import {DeviceExchange} from '../Source/DeviceExchange.js';import {ShaderInterchange} from '../Source/ShaderInterchange.js';
Object.assign(globalThis,globals);const GPU=create([`backend=${process.env.BACKEND??'vulkan'}`]),A=await GPU.requestAdapter();const Timestamp=A.features.has('timestamp-query');const D=await A.requestDevice({requiredFeatures:Timestamp?['timestamp-query']:[]});let Errors=0;D.addEventListener('uncapturederror',E=>{Errors++;console.error(E.error.message);});
const E=new DeviceExchange();E.Device=D;const Shaders=new ShaderInterchange(E,'Shader');globalThis.fetch=async P=>({ok:true,text:async()=>fs.readFileSync(new URL('../'+P,import.meta.url),'utf8')});await Shaders.RetrieveSources(fs.readdirSync(new URL('../Shader/',import.meta.url)).filter(N=>N.endsWith('.wgsl')));
const W=+(process.env.WIDTH??640),H=+(process.env.HEIGHT??360),Frame=D.createBuffer({size:1072,usage:GPUBufferUsage.UNIFORM|GPUBufferUsage.COPY_DST});const F=new Float32Array(268),U=new Uint32Array(F.buffer);
F.set([0,1,0,0,0,0,1,0,0,0,2,1,1,0,.0002,.0001],16);const VP=[0,0,0,1,1,0,0,0,0,1,0,0,0,-2,0,0];F.set(VP,32);F.set(VP,48);F.set([0,0,2],80);F[91]=1;F.set([W,H,1/W,1/H],92);F.set([.3,.2,.932,.004],128);F.set([4,4,4,0],164);
const C=new CloudSequence(E,Shaders);await C.ConstructPrograms(Frame);const Make=(Format='rgba16float')=>D.createTexture({size:[W,H],format:Format,usage:GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.COPY_DST});const B=[Make(),Make()],Depth=Make('depth32float'),Mask=Make('rgba8unorm');D.queue.writeTexture({texture:Mask},new Uint8Array(W*H*4).fill(255),{bytesPerRow:W*4},[W,H]);
const Display={ScreenExtent:[W,H],ResolvedTextures:B,Views:{Resolved:B.map(T=>T.createView())},FlareTransmissionView:Mask.createView(),AssignComposed(){}};const Surface={Views:{Depth:Depth.createView()}};
const Probe=await CloudPresentProbe(Shaders,E,Frame);const Out=Make();const Names=['Clouds','Presentation'];let Queries,Resolve,Read;
if(Timestamp){Queries=D.createQuerySet({type:'timestamp',count:4});Resolve=D.createBuffer({size:32,usage:GPUBufferUsage.QUERY_RESOLVE|GPUBufferUsage.COPY_SRC});Read=D.createBuffer({size:32,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});}
const Results={};
for(const Enabled of [false,true]){
 const Times=Object.fromEntries(Names.map(N=>[N,[]]));
 for(let I=0;I<10;I++){
  U[83]=I;D.queue.writeBuffer(Frame,0,F);C.Prepare(Display,Surface,{...CloudDefaults,CloudEnabled:Enabled,CloudCoverage:.7,CloudLightSteps:+(process.env.LIGHT_TAPS??CloudDefaults.CloudLightSteps),CloudResolution:+(process.env.RESOLUTION??50),Inspection:0},{Position:[0,0,2],Forward:[1,0,0]},{},1/60);
  const Written=new Set(),Metrics={QueryWrites:N=>{Written.add(N);const Q=Names.indexOf(N)*2;return Timestamp?{querySet:Queries,beginningOfPassWriteIndex:Q,endOfPassWriteIndex:Q+1}:undefined;}};
  const En=D.createCommandEncoder(),P=En.beginRenderPass({colorAttachments:[],depthStencilAttachment:{view:Surface.Views.Depth,depthClearValue:0,depthLoadOp:'clear',depthStoreOp:'store'}});P.end();C.Record(En,Metrics);Probe.Record(En,C,Surface.Views.Depth,Display.FlareTransmissionView,Out.createView(),Metrics.QueryWrites('Presentation'));
  if(Timestamp){En.resolveQuerySet(Queries,0,4,Resolve,0);En.copyBufferToBuffer(Resolve,0,Read,0,32);}const Start=performance.now();D.queue.submit([En.finish()]);await D.queue.onSubmittedWorkDone();
  if(Timestamp){await Read.mapAsync(GPUMapMode.READ);const V=new BigUint64Array(Read.getMappedRange().slice(0));Read.unmap();for(let J=0;J<Names.length;J++)if(I>2&&Written.has(Names[J]))Times[Names[J]].push(Number(V[J*2+1]-V[J*2])/1e6);}else if(I>2)(Times.total??=[]).push(performance.now()-Start);
 }
 Results[Enabled?'enabled':'disabled']=Object.fromEntries(Object.entries(Times).map(([K,V])=>[K,V.length?V.sort((A,B)=>A-B)[Math.floor(V.length/2)]:null]));
}
console.log(JSON.stringify({backend:'Vulkan (software adapter in this sandbox)',screen:[W,H],trace:C.Extent,timestampQueries:Timestamp,errors:Errors,medianMilliseconds:Results,note:'Presentation probe executes production AddClouds against white input; excludes scene, ACES, flare and cabin transmission generation. Not hardware FPS.'},null,2));D.destroy();process.exit(Errors?1:0);
