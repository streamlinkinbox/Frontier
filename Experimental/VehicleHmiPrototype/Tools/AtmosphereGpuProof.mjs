// Execute the atmosphere LUT chain on Dawn Vulkan (not the null validator).
// VK_ICD_FILENAMES=/path/to/icd.json BACKEND=vulkan node Tools/AtmosphereGpuProof.mjs
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {create,globals} from 'webgpu';
import {AtmosphereIntegrator} from '../Source/AtmosphereIntegrator.js';
import {ShaderInterchange} from '../Source/ShaderInterchange.js';
import {DeviceExchange} from '../Source/DeviceExchange.js';
import {EnvironmentDefaults} from '../Source/EnvironmentSettings.js';
import {DecodeHalf} from '../Source/SkyLutPreview.js';
Object.assign(globalThis,globals);
const Gpu=create([`backend=${process.env.BACKEND??'vulkan'}`]),Adapter=await Gpu.requestAdapter();assert.ok(Adapter);
const Device=await Adapter.requestDevice();let Errors=0;Device.addEventListener('uncapturederror',E=>{Errors++;console.error(E.error.message);});
const Exchange=new DeviceExchange();Exchange.Device=Device;
const Shaders=new ShaderInterchange(Exchange,'Shader');
globalThis.fetch=async Location=>{const Bytes=fs.readFileSync(new URL('../'+Location,import.meta.url));return {ok:true,text:async()=>Bytes.toString()};};
await Shaders.RetrieveSources(['SkyProjection.wgsl','NativeTwilight.wgsl','AtmosphereMedium.wgsl','AtmosphereTransmittance.wgsl','AtmosphereMultiple.wgsl','AtmosphereScattering.wgsl']);
const A=new AtmosphereIntegrator(Exchange,Shaders);await A.ConstructResources();
async function Read(Texture,W,H){const Stride=Math.ceil(W*8/256)*256,B=Device.createBuffer({size:Stride*H,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ}),E=Device.createCommandEncoder();E.copyTextureToBuffer({texture:Texture},{buffer:B,bytesPerRow:Stride},[W,H]);Device.queue.submit([E.finish()]);await B.mapAsync(GPUMapMode.READ);const Half=new Uint16Array(B.getMappedRange()),Out=new Float32Array(W*H*3);for(let Y=0;Y<H;Y++)for(let X=0;X<W;X++)for(let C=0;C<3;C++)Out[(Y*W+X)*3+C]=DecodeHalf(Half[Y*Stride/2+X*4+C]);B.destroy();assert.ok([...Out].every(Number.isFinite),'all computed HDR texels must be finite');assert.ok([...Out].every(V=>V>=0));return Out;}
async function Bake(Elevation,Options={}){const S={...structuredClone(EnvironmentDefaults),...Options};A.ResolveLight(Elevation,20,12,S);const Encoder=Device.createCommandEncoder();const Changed=A.RecordSkyView(Encoder,12);Device.queue.submit([Encoder.finish()]);await Device.queue.onSubmittedWorkDone();return {Changed,Pixels:await Read(A.SkyTexture,192,108)};}
const Mean=P=>P.reduce((A,B)=>A+B,0)/P.length;
const Single=await Bake(35,{MultipleScattering:false}),MediumRevision=A.MediumRevision;
const Multi=await Bake(35,{MultipleScattering:true});assert.equal(A.MediumRevision,MediumRevision,'toggle must not rebuild physical medium LUTs');assert.ok(Mean(Multi.Pixels)>Mean(Single.Pixels),'multiple scattering must add energy');
assert.equal((await Bake(35,{MultipleScattering:true})).Changed,false,'unchanged atmosphere does not dispatch');
const Transmission=await Read(A.TransmittanceTexture,256,64),Multiple=await Read(A.MultipleTexture,32,32);assert.ok(Transmission.every(V=>V<=1.001));assert.ok(Mean(Multiple)>0);
const Dusk=await Bake(-4),Night=await Bake(-18);assert.ok(Mean(Dusk.Pixels)>Mean(Night.Pixels),'twilight must exceed deep night');
const Off=await Bake(35,{AtmosphereEnabled:false});assert.ok(Off.Pixels.every(V=>V===0));
const Hazy=await Bake(35,{Mie:4});assert.ok(Mean(Hazy.Pixels)>0);assert.ok(A.MediumRevision>MediumRevision);
const Warm=await Bake(35,{SunTemperature:2500});assert.ok(Mean(Warm.Pixels)>0);
console.log(JSON.stringify({backend:'executed Vulkan',errors:Errors,singleMean:Mean(Single.Pixels),multipleMean:Mean(Multi.Pixels),duskMean:Mean(Dusk.Pixels),nightMean:Mean(Night.Pixels),transmissionMean:Mean(Transmission),multipleLutMean:Mean(Multiple)},null,2));
assert.equal(Errors,0);Device.destroy();process.exit(0);
