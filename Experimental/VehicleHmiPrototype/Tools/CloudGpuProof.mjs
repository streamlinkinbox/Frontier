// Isolated execution of production cloud shaders on Vulkan; no scene simulation.
import {CloudPresentProbe} from './CloudPresentProbe.mjs';
import assert from 'node:assert/strict';import fs from 'node:fs';
import {create,globals} from 'webgpu';import {CloudSequence} from '../Source/CloudSequence.js';import {CloudDefaults} from '../Source/CloudSettings.js';
import {DeviceExchange} from '../Source/DeviceExchange.js';import {ShaderInterchange} from '../Source/ShaderInterchange.js';import {DecodeHalf} from '../Source/SkyLutPreview.js';
Object.assign(globalThis,globals);const Gpu=create([`backend=${process.env.BACKEND??'vulkan'}`]);const A=await Gpu.requestAdapter();assert.ok(A);const D=await A.requestDevice();let Errors=0;D.addEventListener('uncapturederror',E=>{Errors++;console.error(E.error.message);});
const E=new DeviceExchange();E.Device=D;const Shaders=new ShaderInterchange(E,'Shader');globalThis.fetch=async P=>({ok:true,text:async()=>fs.readFileSync(new URL('../'+P,import.meta.url),'utf8')});
await Shaders.RetrieveSources(['SceneRecords.wgsl','FrameDescriptor.wgsl','CloudRecords.wgsl','CloudTrace.wgsl','CloudPresent.wgsl','CloudField.wgsl','CloudShadowRecords.wgsl','CloudShadowProject.wgsl','CloudShadowSample.wgsl']);
const Frame=D.createBuffer({size:1072,usage:GPUBufferUsage.UNIFORM|GPUBufferUsage.COPY_DST});const Scalars=new Float32Array(268),Ints=new Uint32Array(Scalars.buffer);
// Camera looking along +X, Z-up, reversed depth. Depth 1 is foreground 1 m; depth 0 is sky.
Scalars.set([0,1,0,0,0,0,1,0,0,0,2,1,1,0,.0002,.0001],16);
const VP=[0,0,0,1,1,0,0,0,0,1,0,0,0,-2,0,0];Scalars.set(VP,32);Scalars.set(VP,48);
Scalars.set([0,0,2],80);Scalars[91]=1;Scalars.set([64,32,1/64,1/32],92);Scalars.set([.3,.2,.932,.004],128);Scalars.set([4,4,4,0],164);
const C=new CloudSequence(E,Shaders);await C.ConstructPrograms(Frame);
const Texture=(Name,Format='rgba16float')=>D.createTexture({label:Name,size:[64,32],format:Format,usage:GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_DST|GPUTextureUsage.RENDER_ATTACHMENT});
const BG=[Texture('opaque A'),Texture('opaque B')],Mask=Texture('cabin transmission','rgba8unorm'),Depth=Texture('depth','depth32float');
const White=new Uint16Array(64*32*4).fill(0x3c00);for(const T of BG)D.queue.writeTexture({texture:T},White,{bytesPerRow:64*8},[64,32]);
function SetMask(V){D.queue.writeTexture({texture:Mask},new Uint8Array(64*32*4).fill(V),{bytesPerRow:256},[64,32]);}SetMask(255);
const Display={ScreenExtent:[64,32],ResolvedTextures:BG,Views:{Resolved:BG.map(T=>T.createView())},FlareTransmissionView:Mask.createView(),AssignComposed(V,T){this.Output=T;}};
const Surface={Views:{Depth:Depth.createView()}};const Camera={Position:[0,0,2],Forward:[1,0,0]},Light={SunDirection:[.3,.2,.932],SolarIlluminance:[4,4,4]};let Index=0;
async function Read(T,W,H){const Stride=Math.ceil(W*8/256)*256,B=D.createBuffer({size:Stride*H,usage:GPUBufferUsage.MAP_READ|GPUBufferUsage.COPY_DST}),En=D.createCommandEncoder();En.copyTextureToBuffer({texture:T},{buffer:B,bytesPerRow:Stride},[W,H]);D.queue.submit([En.finish()]);await B.mapAsync(GPUMapMode.READ);const V=new Uint16Array(B.getMappedRange()),Result=[];for(let Y=0;Y<H;Y++)for(let X=0;X<W;X++)for(let J=0;J<4;J++)Result.push(DecodeHalf(V[Y*Stride/2+X*4+J]));B.destroy();assert.ok(Result.every(Number.isFinite),'finite GPU output');return Result;}
async function Run(Options={},DepthValue=0){const S={...CloudDefaults,CloudEnabled:true,CloudBase:10,CloudThickness:60,CloudCoverage:1,CloudDensity:1,CloudTemporal:0,Inspection:0,...Options};const P=Index++%2;Ints[83]=Index;D.queue.writeBuffer(Frame,0,Scalars);
 C.Prepare(Display,Surface,S,Camera,Light,1/60,Options.TestLamps);if(Options.TestSkySamples){C.UniformData[21]=Options.TestSkySamples;D.queue.writeBuffer(C.Params,0,C.UniformData);}const En=D.createCommandEncoder();const Clear=En.beginRenderPass({colorAttachments:[],depthStencilAttachment:{view:Surface.Views.Depth,depthClearValue:DepthValue,depthLoadOp:'clear',depthStoreOp:'store'}});Clear.end();if(!C.Active){assert.equal(C.Record(En),false);return {Opacity:0};}assert.ok(C.Record(En));D.queue.submit([En.finish()]);const Pixels=await Read(C.Colour,...C.Extent);const Opacity=Pixels.filter((_,I)=>I%4===3).reduce((Sum,T)=>Sum+1-T,0)/(Pixels.length/4);return {Opacity,Pixels,Extent:C.Extent.slice()};}
const Empty=await Run({CloudDensity:0});assert.equal(Empty.Opacity,0);
const Global=await Run();assert.ok(Global.Opacity>.01);const Clipped=await Run({},1);assert.ok(Clipped.Opacity<1e-6);
const Local=await Run({CloudEnabled:false,LocalCloudEnabled:true,LocalCloudX:30,LocalCloudY:0,LocalCloudZ:2,LocalCloudHalfX:15,LocalCloudHalfY:15,LocalCloudHalfZ:15,LocalCloudCoverage:1,LocalCloudDensity:2});assert.ok(Local.Opacity>.002);
const FogOptions={CloudEnabled:false,LocalCloudEnabled:false,LocalFogEnabled:true,LocalFogX:20,LocalFogY:0,LocalFogZ:2,LocalFogHalfX:12,LocalFogHalfY:12,LocalFogHalfZ:8,LocalFogDensity:.12};
const Fog=await Run(FogOptions);assert.ok(Fog.Opacity>.005,'fog-only pass contributes');assert.equal(C.Shadows.Active,false,'fog alone must not enable cloud shadows');
const FogClipped=await Run(FogOptions,1);assert.equal(FogClipped.Opacity,0,'fog clipped by foreground depth');
const FogBox=await Run({...FogOptions,LocalFogShape:1});assert.ok(FogBox.Opacity>Fog.Opacity,'box and ellipsoid are distinct');
const FogSphere=await Run({...FogOptions,LocalFogShape:2});assert.ok(FogSphere.Opacity>0,'sphere fog contributes');
const FogDome=await Run({...FogOptions,LocalFogShape:3});assert.ok(FogDome.Opacity>0,'dome fog contributes');
const FogUnshadowed=await Run({...FogOptions,LocalFogSelfShadow:false});assert.ok(FogUnshadowed.Pixels.some((V,I)=>I%4<3&&V>Fog.Pixels[I]+.00001),'self-shadow toggle changes sun scattering');
const FogInside=await Run({...FogOptions,LocalFogX:0});assert.ok(FogInside.Opacity>.02,'camera inside fog');
const Overlap=await Run({...FogOptions,CloudEnabled:true,CloudBase:1,CloudThickness:30});assert.ok(Overlap.Opacity>Fog.Opacity,'overlap contributes in one integration');
const Lamps=new Float32Array(64*16);Lamps.set([10,0,2,0,1,0,0,60,1,.7,.3,5000,.9,.7,0,1]);
const LitFog=await Run({...FogOptions,TestLamps:Lamps});assert.equal(C.UniformData[68],1);assert.ok(LitFog.Pixels.some((V,I)=>I%4<3&&V>Fog.Pixels[I]+.001),'actual scene light contributes to fog');
// User regression: fixed 12 samples across 6 km produced a repeating diagonal hatch.
const ThickOptions={CloudBase:1163,CloudThickness:6000,CloudCoverage:.55,CloudDensity:1,CloudFollowWind:false,WindEnabled:false,CloudResolution:100};
const Thick=await Run(ThickOptions);assert.equal(C.UniformData[21],60);
const ThickReference=await Run({...ThickOptions,TestSkySamples:96});
const ThickOldBudget=await Run({...ThickOptions,TestSkySamples:12});
const ErrorToReference=R=>R.Pixels.reduce((Sum,V,I)=>Sum+(I%4===3?Math.abs(V-ThickReference.Pixels[I]):0),0)/(R.Pixels.length/4);
const ThickError=ErrorToReference(Thick),SparseError=ErrorToReference(ThickOldBudget);assert.ok(ThickError<SparseError,'thickness-aware sampling must approach dense reference better than 12 samples');
await Run({...ThickOptions,CloudThickness:595});assert.equal(C.UniformData[21],12,'thin layers retain base budget');
await Run({...ThickOptions,CloudThickness:6000,CloudSteps:64});assert.equal(C.UniformData[21],96,'adaptive work stays capped');
const Quarter=await Run({CloudResolution:25}),Full=await Run({CloudResolution:100});assert.deepEqual(Quarter.Extent,[16,8]);assert.deepEqual(Full.Extent,[64,32]);assert.ok(Math.abs(Full.Opacity-Global.Opacity)<.07);
const Probe=await CloudPresentProbe(Shaders,E,Frame);const Out=D.createTexture({size:[64,32],format:'rgba16float',usage:GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.COPY_SRC});
async function Present(MaskValue,Active=true,Inspection=0){SetMask(MaskValue);C.UniformData[29]=+Active;D.queue.writeBuffer(C.Params,0,C.UniformData);Ints[90]=Inspection;D.queue.writeBuffer(Frame,0,Scalars);const En=D.createCommandEncoder();Probe.Record(En,C,Surface.Views.Depth,Mask.createView(),Out.createView());D.queue.submit([En.finish()]);return Read(Out,64,32);}
const Visible=await Present(255);assert.ok(Visible.some((V,I)=>I%4<3&&Math.abs(V-1)>.01),'actual presentation adds clouds');
const Masked=await Present(0);assert.ok(Masked.every(V=>Math.abs(V-1)<1e-5),'cabin blocks clouds');
const Glass=await Present(128);assert.ok(Glass.every((V,I)=>Math.abs(V-(1+(Visible[I]-1)*128/255))<.002),'glass transmission weights clouds');
const Disabled=await Present(255,false);assert.ok(Disabled.every(V=>Math.abs(V-1)<1e-5),'disabled presentation bypass');
await Run(FogOptions);const FogPresented=await Present(255);assert.ok(FogPresented.some((V,I)=>I%4<3&&Math.abs(V-1)>.01));const FogMasked=await Present(0);assert.ok(FogMasked.every(V=>Math.abs(V-1)<1e-5),'cabin blocks fog-only presentation');
// Regression for the old hard guide rejection contour: two nearby guides straddle
// max(.5 m, 2%) at a foreground pixel. Presentation must vary continuously.
await Run({...FogOptions,LocalFogX:0,LocalFogHalfX:100,LocalFogHalfY:100,LocalFogHalfZ:100},1);
const Fixed=new Uint16Array(C.Extent[0]*C.Extent[1]*4);for(let I=0;I<Fixed.length;I+=4)Fixed.set([0x3000,0x3400,0x3800,0x3800],I);
D.queue.writeTexture({texture:C.Colour},Fixed,{bytesPerRow:C.Extent[0]*8},C.Extent);
async function GuideProbe(Half){const Guide=new Uint16Array(Fixed.length);for(let I=0;I<Guide.length;I+=4)Guide[I]=Half;D.queue.writeTexture({texture:C.Guide},Guide,{bytesPerRow:C.Extent[0]*8},C.Extent);return Present(255);}
const GuideBefore=await GuideProbe(0x3dfe),GuideAfter=await GuideProbe(0x3e02),Centre=(16*64+32)*4;
const BandJump=Math.abs(GuideBefore[Centre]-GuideAfter[Centre]);assert.ok(BandJump<.01,'fog must not disappear at the old guide cutoff');assert.ok(GuideAfter[Centre]<.9,'fog contribution persists across cutoff');
// Cloud-only coverage of the same presentation seam, with fog fully disabled.
await Run({CloudEnabled:true,CloudBase:0,CloudThickness:4,CloudCoverage:1,LocalCloudEnabled:false,LocalFogEnabled:false},1);
D.queue.writeTexture({texture:C.Colour},Fixed,{bytesPerRow:C.Extent[0]*8},C.Extent);
const GlobalGuideA=await GuideProbe(0x3dfe),GlobalGuideB=await GuideProbe(0x3e02);
const GlobalBandJump=Math.abs(GlobalGuideA[Centre]-GlobalGuideB[Centre]);assert.ok(GlobalBandJump<.01&&GlobalGuideB[Centre]<.9,'global cloud reconstruction stays continuous across guide cutoff');
await Run({CloudEnabled:false,LocalCloudEnabled:true,LocalCloudX:0,LocalCloudY:0,LocalCloudZ:2,LocalCloudHalfX:100,LocalCloudHalfY:100,LocalCloudHalfZ:100,LocalCloudCoverage:1,LocalFogEnabled:false},1);
D.queue.writeTexture({texture:C.Colour},Fixed,{bytesPerRow:C.Extent[0]*8},C.Extent);
const LocalGuideA=await GuideProbe(0x3dfe),LocalGuideB=await GuideProbe(0x3e02);
const LocalBandJump=Math.abs(LocalGuideA[Centre]-LocalGuideB[Centre]);assert.ok(LocalBandJump<.01&&LocalGuideB[Centre]<.9,'local cloud reconstruction stays continuous across guide cutoff');
// Deliberately supply cloudy neighbouring texels: full-resolution guards must
// still keep closer foreground geometry cloud-free, for each cloud type.
for(const Options of [{CloudEnabled:true,CloudBase:10,LocalCloudEnabled:false},{CloudEnabled:false,LocalCloudEnabled:true,LocalCloudX:30,LocalCloudY:0,LocalCloudZ:2,LocalCloudHalfX:10,LocalCloudHalfY:10,LocalCloudHalfZ:10}]){
 await Run({...Options,LocalFogEnabled:false},1);D.queue.writeTexture({texture:C.Colour},Fixed,{bytesPerRow:C.Extent[0]*8},C.Extent);const Guarded=await GuideProbe(0x3e02);assert.ok(Guarded.every(V=>Math.abs(V-1)<1e-5),'cloud neighbours cannot leak in front of the volume');
}
console.log(JSON.stringify({guideCutoffJump:BandJump,globalCloudGuideJump:GlobalBandJump,localCloudGuideJump:LocalBandJump,cloudForegroundGuards:'passed',thickLayerAlphaError:ThickError,sparseLayerAlphaError:SparseError,fog:Fog.Opacity,fogBox:FogBox.Opacity,fogSphere:FogSphere.Opacity,fogDome:FogDome.Opacity,fogInside:FogInside.Opacity,overlap:Overlap.Opacity,global:Global.Opacity,local:Local.Opacity,clipped:Clipped.Opacity,quarter:Quarter.Opacity,full:Full.Opacity,targets:C.Textures.length,cabinAndGlass:'passed',fogCabinAndLamp:'passed',errors:Errors},null,2));assert.equal(Errors,0);D.destroy();process.exit(0);
