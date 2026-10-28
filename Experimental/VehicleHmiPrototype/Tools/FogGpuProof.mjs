// Execute the SAME fog function used by DisplayRecombination, isolated from scene/UI.
// BACKEND=vulkan node Tools/FogGpuProof.mjs
import assert from 'node:assert/strict';
import {FogPathTransmission} from '../Source/FogInspectorDashboard.js';
import fs from 'node:fs';
import {create,globals} from 'webgpu';
Object.assign(globalThis,globals);
const Gpu=create([`backend=${process.env.BACKEND??'vulkan'}`]);
const Adapter=await Gpu.requestAdapter();assert.ok(Adapter);const D=await Adapter.requestDevice();let Errors=0;
D.addEventListener('uncapturederror',E=>{Errors++;console.error(E.error.message);});
const Cases=[
 ['disabled',50,2,0,30,0,0,.5],
 ['height_default_50m',50,2,.002,30,0,0,.5],
 ['height_dense_50m',50,2,.02,30,0,0,.5],
 ['height_max_50m',50,2,.05,30,0,0,.5],
 ['height_dense_above_layer',50,500,.02,30,0,0,.5],
 ['height_dense_tall_layer',50,500,.02,500,0,0,.5],
 ['aerial_before_start',50,2,0,30,.001,100,.5],
 ['aerial_default_50m',50,2,0,30,.00004,100,.5],
 ['aerial_dense_50m_start0',50,2,0,30,.001,0,.5],
 ['aerial_dense_1000m',1000,2,0,30,.001,100,.5],
 ['aerial_grey_1000m',1000,2,0,30,.001,100,1],
 ['aerial_spectral_1000m',1000,2,0,30,.001,100,0],
 ['combined',1000,2,.002,30,.001,0,.5],
];
const Records=new Float32Array(Cases.length*16);Cases.forEach(([Name,Distance,Z,Density,Height,Aerial,Start,Mie],I)=>Records.set([0,0,Z,0,Distance,0,Z,0,Density,Height,Aerial,Start,Mie,0,0,0],I*16));
const Input=D.createBuffer({size:Records.byteLength,usage:GPUBufferUsage.STORAGE|GPUBufferUsage.COPY_DST});D.queue.writeBuffer(Input,0,Records);
const Out=D.createBuffer({size:Cases.length*16,usage:GPUBufferUsage.STORAGE|GPUBufferUsage.COPY_SRC}),Read=D.createBuffer({size:Cases.length*16,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});
const Code=fs.readFileSync(new URL('../Shader/FogMedium.wgsl',import.meta.url),'utf8')+`
struct Probe{Camera:vec4f,Position:vec4f,Medium:vec4f,Optics:vec4f}
@group(0) @binding(0) var<storage,read> Probes:array<Probe>;
@group(0) @binding(1) var<storage,read_write> Results:array<vec4f>;
@compute @workgroup_size(1) fn Main(@builtin(global_invocation_id) ID:vec3u){
 let P=Probes[ID.x];Results[ID.x]=vec4f(FogTransmission(P.Camera.xyz,P.Position.xyz,P.Medium.x,P.Medium.y,P.Medium.z,P.Medium.w,P.Optics.x),1.0);
}`;
const Pipeline=await D.createComputePipelineAsync({layout:'auto',compute:{module:D.createShaderModule({code:Code}),entryPoint:'Main'}});
const Group=D.createBindGroup({layout:Pipeline.getBindGroupLayout(0),entries:[{binding:0,resource:{buffer:Input}},{binding:1,resource:{buffer:Out}}]});
const E=D.createCommandEncoder(),Pass=E.beginComputePass();Pass.setPipeline(Pipeline);Pass.setBindGroup(0,Group);Pass.dispatchWorkgroups(Cases.length);Pass.end();E.copyBufferToBuffer(Out,0,Read,0,Cases.length*16);D.queue.submit([E.finish()]);await Read.mapAsync(GPUMapMode.READ);
const Values=new Float32Array(Read.getMappedRange()),Results={};Cases.forEach(([Name,Distance,Z,Density,Height,Aerial,Start,Mie],I)=>{
 const RGB=Array.from(Values.subarray(I*4,I*4+3));Results[Name]=RGB;
 const Inspector=FogPathTransmission({FogDensity:Density,FogHeight:Height,AerialDensity:Aerial,AerialStart:Start,AerialMie:Mie},[0,0,Z],[Distance,0,Z],'combined');
 RGB.forEach((V,C)=>assert.ok(Math.abs(V-Inspector[C])<2e-5,Name+' GPU/inspector mismatch'));
 const OD=Density*Distance*Math.exp(-Z/Height),AD=Aerial*Math.max(0,Distance-Start);
 RGB.forEach((V,C)=>{const Expected=Math.exp(-Math.min(80,OD*OD+([.45,.7,1][C]*(1-Mie)+Mie)*AD));assert.ok(Number.isFinite(V)&&Math.abs(V-Expected)<2e-5,`${Name}: GPU=${V} expected=${Expected}`);});
});
assert.ok(Results.height_max_50m[0]<.01);assert.ok(Results.height_dense_above_layer[0]>.99);assert.ok(Results.height_dense_tall_layer[0]<.9);
assert.deepEqual(Results.aerial_before_start,[1,1,1]);assert.ok(Results.aerial_dense_1000m[2]<.5);assert.ok(Results.aerial_spectral_1000m[0]>Results.aerial_spectral_1000m[2]);assert.equal(Errors,0);
console.log(JSON.stringify({backend:'executed Vulkan',errors:Errors,transmissions:Results},null,2));D.destroy();process.exit(0);
