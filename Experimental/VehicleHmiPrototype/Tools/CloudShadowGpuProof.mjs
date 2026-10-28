// Production projected shadow generation + receiver lookup, executed on a real WebGPU device.
import assert from 'node:assert/strict';import fs from 'node:fs';import {create,globals} from 'webgpu';
import {CloudSequence} from '../Source/CloudSequence.js';import {CloudDefaults} from '../Source/CloudSettings.js';
import {DeviceExchange} from '../Source/DeviceExchange.js';import {ShaderInterchange} from '../Source/ShaderInterchange.js';
Object.assign(globalThis,globals);const GPU=create([`backend=${process.env.BACKEND??'vulkan'}`]),Adapter=await GPU.requestAdapter();const Timestamp=Adapter.features.has('timestamp-query');const D=await Adapter.requestDevice({requiredFeatures:Timestamp?['timestamp-query']:[]});let Errors=0;D.addEventListener('uncapturederror',E=>{Errors++;console.error(E.error.message);});
const E=new DeviceExchange();E.Device=D;const Shaders=new ShaderInterchange(E,'Shader');globalThis.fetch=async P=>({ok:true,text:async()=>fs.readFileSync(new URL('../'+P,import.meta.url),'utf8')});await Shaders.RetrieveSources(fs.readdirSync(new URL('../Shader/',import.meta.url)).filter(N=>N.endsWith('.wgsl')));
const Frame=E.ConstructRecords(1072,GPUBufferUsage.UNIFORM|GPUBufferUsage.COPY_DST),C=new CloudSequence(E,Shaders);await C.ConstructPrograms(Frame);
const Depth=D.createTexture({size:[64,32],format:'depth32float',usage:GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.RENDER_ATTACHMENT});const Display={ScreenExtent:[64,32]},Surface={Views:{Depth:Depth.createView()}},Camera={Position:[0,0,2],Forward:[1,0,0]};
Shaders.Sources.set('CloudShadowProbe.wgsl',`
@group(0) @binding(0) var<uniform> Shadow:CloudShadowParameters;
@group(0) @binding(1) var CloudShadowMap:texture_2d<f32>;
@group(0) @binding(2) var CloudShadowSampler:sampler;
@group(0) @binding(3) var<storage,read_write> Results:array<vec4f>;
@group(0) @binding(4) var<storage,read> Points:array<vec4f>;
//! include "CloudShadowSample.wgsl"
@compute @workgroup_size(1) fn Probe(@builtin(global_invocation_id) ID:vec3u){Results[ID.x]=vec4f(CloudGroundTransmission(Points[ID.x].xyz),0,0,1);}`);
const Layout=E.ConstructDescriptorLayout(['Uniform','Texture2D','Sampler','Storage','StorageRead'],GPUShaderStage.COMPUTE),Program=await Shaders.ConstructComputeProgram('CloudShadowProbe.wgsl','Probe',[Layout]);
const Points=E.ConstructRecords(16*16,GPUBufferUsage.STORAGE|GPUBufferUsage.COPY_DST),Results=E.ConstructRecords(16*16,GPUBufferUsage.STORAGE|GPUBufferUsage.COPY_SRC),Read=E.ConstructRecords(16*16,GPUBufferUsage.MAP_READ|GPUBufferUsage.COPY_DST);
const Group=E.ConstructDescriptorGroup(Layout,[C.Shadows.Params,C.Shadows.View,C.Shadows.Sampler,Results,Points]);
let Query,Resolve,QRead;const Timings=[];if(Timestamp){Query=D.createQuerySet({type:'timestamp',count:2});Resolve=E.ConstructRecords(16,GPUBufferUsage.QUERY_RESOLVE|GPUBufferUsage.COPY_SRC);QRead=E.ConstructRecords(16,GPUBufferUsage.MAP_READ|GPUBufferUsage.COPY_DST);}
const Defaults={...CloudDefaults,Inspection:0,CloudShadowStrength:1,CloudShadowSoftness:0,CloudShadowDistance:2000,CloudFollowWind:false,LocalCloudFollowWind:false};
async function Run(Options,Positions,Sun=[0,0,1]){
 C.Prepare(Display,Surface,{...Defaults,...Options},Camera,{SunDirection:Sun},.016);
 D.queue.writeBuffer(Points,0,new Float32Array(Positions.flatMap(P=>[...P,1])));
 const En=D.createCommandEncoder();const Updated=C.Shadows.Record(En,{QueryWrites:()=>Timestamp?{querySet:Query,beginningOfPassWriteIndex:0,endOfPassWriteIndex:1}:undefined});
 const P=En.beginComputePass();P.setPipeline(Program);P.setBindGroup(0,Group);P.dispatchWorkgroups(Positions.length);P.end();En.copyBufferToBuffer(Results,0,Read,0,16*Positions.length);
 if(Timestamp&&Updated){En.resolveQuerySet(Query,0,2,Resolve,0);En.copyBufferToBuffer(Resolve,0,QRead,0,16);}D.queue.submit([En.finish()]);await Read.mapAsync(GPUMapMode.READ);const Values=Array.from(new Float32Array(Read.getMappedRange().slice(0))).filter((_,I)=>I%4===0).slice(0,Positions.length);Read.unmap();
 assert.ok(Values.every(V=>Number.isFinite(V)&&V>=0&&V<=1));if(Timestamp&&Updated){await QRead.mapAsync(GPUMapMode.READ);const Q=new BigUint64Array(QRead.getMappedRange());Timings.push(Number(Q[1]-Q[0])/1e6);QRead.unmap();}return {Values,Updated};
}
const Global=await Run({CloudCoverage:1,CloudDensity:2},[[0,0,-.1],[0,0,0],[0,0,.1],[0,0,15000],[2000,0,0]]);assert.ok(Global.Values[0]<.1);assert.ok(Math.abs(Global.Values[0]-Global.Values[2])<1e-5,'no discontinuity at world Z zero');assert.equal(Global.Values[3],1,'above clouds stays lit');assert.equal(Global.Values[4],1,'outside map stays lit');
const Reuse=await Run({CloudCoverage:1,CloudDensity:2},[[0,0,0]]);assert.equal(Reuse.Updated,false,'static field reused');
const LocalSettings={CloudEnabled:false,LocalCloudEnabled:true,LocalCloudX:0,LocalCloudY:0,LocalCloudZ:200,LocalCloudHalfX:150,LocalCloudHalfY:150,LocalCloudHalfZ:80,LocalCloudCoverage:1,LocalCloudDensity:2};
const Local=await Run(LocalSettings,[[0,0,0],[0,0,400],[900,0,0]]);assert.ok(Local.Values[0]<.5,'local-only cloud casts shadow');assert.equal(Local.Values[1],1);assert.equal(Local.Values[2],1);
const Slant=await Run(LocalSettings,[[-150,0,0],[150,0,0]],[.6,0,.8]);assert.ok(Slant.Values[0]<.5&&Slant.Values[1]>.95,'shadow projects away from sun');
const Negative=await Run({...LocalSettings,LocalCloudZ:-400},[[0,0,-600],[0,0,0]]);assert.ok(Negative.Values[0]<.5&&Negative.Values[1]===1,'below-zero local clouds work without shadowing surfaces above them');
const Off=await Run({CloudShadows:false},[[0,0,0]]);assert.equal(Off.Updated,false);assert.equal(Off.Values[0],1);
const Night=await Run({},[[0,0,0]],[0,0,-1]);assert.equal(Night.Updated,false);assert.equal(Night.Values[0],1);
const Soft=await Run({...LocalSettings,CloudShadowSoftness:50},[[0,0,0]]);assert.ok(Soft.Values[0]<1);
console.log(JSON.stringify({global:Global.Values,local:Local.Values,slanted:Slant.Values,negativeZ:Negative.Values,soft:Soft.Values,staticReuse:true,offAndNightBypass:true,map:[128,128],updateMilliseconds:Timings,errors:Errors},null,2));assert.equal(Errors,0);D.destroy();process.exit(0);
