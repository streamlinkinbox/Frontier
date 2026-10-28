import { test } from 'node:test';
import assert from 'node:assert/strict';
import { EnvironmentDefaults as Defaults, EncodeEnvironment } from '../Source/EnvironmentSettings.js';
import { ResolveSunTransmittance, AtmosphereIntegrator } from '../Source/AtmosphereIntegrator.js';
test('environment frame extension has 35 aligned vec4 records and explicit switches',()=>{
 const Light={SunDirection:[1,0,0]};
 const R=EncodeEnvironment({...Defaults,SunElevation:-15},Light);
 assert.equal(R.length,140);assert.ok([...R].every(Number.isFinite));
 const Disabled=EncodeEnvironment({...Defaults,SunElevation:0,StarsEnabled:false,MoonEnabled:false,FogEnabled:false,AerialEnabled:false},Light);
 for(const I of [4,17,18,21]) assert.equal(Disabled[I],0);
});
test('CPU extinction responds to the same physical medium controls as the sky',()=>{
 const Clear=ResolveSunTransmittance([0,0,1],{Rayleigh:0,Mie:0,Ozone:0});
 assert.deepEqual(Clear,[1,1,1]);
 const Normal=ResolveSunTransmittance([0,0,1],Defaults);
 const Hazy=ResolveSunTransmittance([0,0,1],{...Defaults,Mie:5});
 Normal.forEach((V,I)=>{assert.ok(V>0&&V<1);assert.ok(Hazy[I]<V);});
 assert.deepEqual(ResolveSunTransmittance([0,0,-1],Defaults),[0,0,0]);
});
test('moon key light respects position, phase and disabled state',()=>{
 const A=new AtmosphereIntegrator({},{});
 const S={...Defaults,MoonAzimuth:90,MoonElevation:45};
 const Full=A.ResolveLight(-20,0,12,S).Illuminance.slice();
 assert.ok(Math.abs(A.Light.Direction[0])<1e-6);
 const New=A.ResolveLight(-20,0,12,{...S,MoonPhase:0}).Illuminance;
 assert.ok(New.every((V,I)=>V<Full[I]));
 assert.ok(A.ResolveLight(-20,0,12,{...S,MoonEnabled:false}).Illuminance.every(V=>V===0));
});

import { DecodeHalf } from '../Source/SkyLutPreview.js';
test('GPU rgba16float preview decoding handles normal, subnormal and special values',()=>{
 assert.equal(DecodeHalf(0x3c00),1); assert.equal(DecodeHalf(0xc000),-2);
 assert.equal(DecodeHalf(1),2**-24); assert.equal(DecodeHalf(0),0);
 assert.equal(DecodeHalf(0x7c00),Infinity); assert.ok(Number.isNaN(DecodeHalf(0x7c01)));
});
test('moon below the horizon cannot illuminate the scene',()=>{
 const A=new AtmosphereIntegrator({},{});
 assert.ok(A.ResolveLight(-20,0,12,{...Defaults,MoonElevation:-20}).Illuminance.every(V=>V===0));
});
test('flare switch and optics are packed in aligned frame records',()=>{
 const R=EncodeEnvironment({...Defaults,SunElevation:10,FlareEnabled:false},{SunDirection:[1,0,0],SolarIlluminance:[1,2,3]});
 assert.equal(R[24],0);assert.equal(R[26],Defaults.FlareGhosts);assert.deepEqual([...R.slice(36,39)],[1,2,3]);
});
import { SkyLutPreview } from '../Source/SkyLutPreview.js';
test('sky LUT readback is dirty-gated and releases staging buffers',async()=>{
 const OldUsage=globalThis.GPUBufferUsage, OldMap=globalThis.GPUMapMode;
 globalThis.GPUBufferUsage={COPY_DST:1,MAP_READ:2};globalThis.GPUMapMode={READ:1};
 let Copies=0,Destroyed=0;
 const Half=new Uint16Array(192*108*4);Half[0]=0x3c00;
 const A={LastKey:'first',SkyTexture:{},Exchange:{Device:{
 createBuffer:()=>({mapAsync:async()=>{},getMappedRange:()=>Half.buffer,destroy:()=>Destroyed++}),
 createCommandEncoder:()=>({copyTextureToBuffer:()=>Copies++,finish:()=>({})}),
 queue:{submit:()=>{}}
 }}};
 try {
  const Preview=new SkyLutPreview(A);
  await Preview.Refresh();assert.equal(Preview.Pixels[0],1);
  await Preview.Refresh();assert.equal(Copies,1);
  A.LastKey='changed';await Preview.Refresh();assert.equal(Copies,2);assert.equal(Destroyed,2);
 } finally {globalThis.GPUBufferUsage=OldUsage;globalThis.GPUMapMode=OldMap;}
});
test('sun temperature invalidates the sky LUT and unchanged frames skip integration',()=>{
 const Device={queue:{writeBuffer:()=>{}}};
 const A=new AtmosphereIntegrator({Device},{});
 let Passes=0;
 const Encoder={beginComputePass:()=>{Passes++;return {setPipeline(){},setBindGroup(){},dispatchWorkgroups(){},end(){}};}};
 A.ResolveLight(30,0,12,Defaults);assert.equal(A.RecordSkyView(Encoder,12),true);
 assert.equal(A.RecordSkyView(Encoder,12),false);
 A.ResolveLight(30,0,12,{...Defaults,SunTemperature:3000});assert.equal(A.RecordSkyView(Encoder,12),true);
 assert.equal(Passes,4);
});

test('spectral flare retains native source/far samples and bounded optical response',async()=>{
 const {LFRender}=await import('../Source/NativeLensFlareKernel.js');
 const S={...Defaults,FlareIntensity:1,FlareSpread:1,FlareAnamorphic:true,FlareWarmStreak:false,FlareBurst:true,FlareRotation:0,FlareAperture:8,FlareGhosts:6,FlareGhostBrightness:.35,FlareGhostSpacing:1,FlareGhostShape:6,FlareHaloBrightness:.5,FlareHaloRadius:.28,FlareHaloWidth:.045,FlareChromatic:.6,FlareStreak:1};
 for(const [U,V,Expected]of [[.28,.43,[1.20350003,1.18599999,1.17624998]],[.15,.7,[.000943595136,.000849235628,.000707696367]]]){
  LFRender(S,U,V,.28,.43,640/280).forEach((X,I)=>assert.ok(Math.abs(X-Expected[I])<2e-6,`${U},${V} channel ${I}: ${X}`));
 }
 const Spectral=LFRender(S,.5,.5,.28,.43,640/280);assert.ok(Spectral.every(V=>Number.isFinite(V)&&V>=0));assert.notDeepEqual(Spectral,LFRender({...S,FlareChromatic:0},.5,.5,.28,.43,640/280));
 assert.deepEqual(LFRender(S,.5,.5,.28,.43,640/280,0),[0,0,0]);
});
test('persisted environment rejects invalid values and bounds body settings',async()=>{
 const {SanitiseEnvironment}=await import('../Source/EnvironmentPersistence.js');
 const S=SanitiseEnvironment({MoonBody:800,MoonPhase:-1,FogDensity:Infinity,StarsEnabled:'false',SunTemperature:50000,Unknown:2,GroundAlbedo:[-1,.3,7],MoonInstances:[{MoonBody:99,MoonEnabled:false}]});
 assert.equal(S.MoonBody,5);assert.equal(S.MoonPhase,0);assert.equal(S.SunTemperature,10000);assert.equal(S.FogDensity,undefined);assert.equal(S.StarsEnabled,undefined);assert.equal(S.Unknown,undefined);assert.deepEqual(S.GroundAlbedo,[0,.3,1]);assert.equal(S.MoonInstances[0].MoonBody,5);assert.equal(S.MoonInstances[0].MoonEnabled,false);
});
test('environment storage roundtrip and unavailable storage are safe',async()=>{
 const {SaveEnvironment,LoadEnvironment}=await import('../Source/EnvironmentPersistence.js');
 const Old=globalThis.localStorage;const Map0=new Map();
 try{globalThis.localStorage={getItem:K=>Map0.get(K),setItem:(K,V)=>Map0.set(K,V)};SaveEnvironment({...structuredClone(Defaults),MoonPhase:7.38,MultipleScattering:false});const S={};LoadEnvironment(S);assert.equal(S.MoonPhase,7.38);assert.equal(S.MultipleScattering,false);globalThis.localStorage={getItem:()=>{throw Error('blocked')},setItem:()=>{throw Error('blocked')}};assert.doesNotThrow(()=>SaveEnvironment(Defaults));assert.doesNotThrow(()=>LoadEnvironment({}));}finally{if(Old===undefined)delete globalThis.localStorage;else globalThis.localStorage=Old;}
});
test('HDR export never reuses a preview baked from stale flare settings',async()=>{
 const {NativeFlarePreview}=await import('../Source/NativeFlarePanel.js');const P=new NativeFlarePreview(),S={...Defaults,FlareIntensity:1};
 P.Key=JSON.stringify(Object.fromEntries(Object.entries(S).filter(([K])=>K.startsWith('Flare'))));P.Pixels=new Float32Array([42,42,42]);assert.equal(P.ResolvePixels(S),P.Pixels);
 const Fresh=P.ResolvePixels({...S,FlareIntensity:0});assert.equal(Fresh.length,640*280*3);assert.ok(Fresh.every(X=>X===0));assert.notEqual(Fresh,P.Pixels);
});
test('native sliders preserve density precision and snap relative to minimum',async()=>{
 const {SnapNativeSlider,NativeSliderDigits}=await import('../Source/NativeEnvironmentControls.js');
 assert.equal(SnapNativeSlider(.000043,0,.001,.00001),.00004);
 assert.equal(SnapNativeSlider(.376,0,1,.25),.5);
 assert.equal(SnapNativeSlider(.151,.05,1,.1),.15);
 assert.equal(SnapNativeSlider(-4,0,1,.01),0);assert.equal(SnapNativeSlider(7,0,1,.01),1);
 assert.equal(NativeSliderDigits(0,1e-7),7);
});
test('unchanged static inspector diagrams do not repaint, live signals still do',async()=>{
 const {EditorControls}=await import('../Source/EditorControls.js');const C=new EditorControls(()=>{});let Static=0,Live=0,Key='a';const Draw=()=>Static++;Draw.Static=true;C.NativeReadKey=()=>Key;C.Live=[Draw,()=>Live++];
 C.RefreshLive();C.RefreshLive();C.RefreshLive();assert.equal(Static,1);assert.equal(Live,3);Key='b';C.RefreshLive();assert.equal(Static,2);assert.equal(Live,4);
});
test('flare optical image is cached until shape/camera/light key changes',async()=>{
 const {DisplayProjection}=await import('../Source/DisplayProjection.js');let Dispatches=0;
 const E={beginComputePass:()=>({setPipeline(){},setBindGroup(){},dispatchWorkgroups(){Dispatches++;},end(){}})};
 const P=Object.create(DisplayProjection.prototype);P.FlareImageExtent=[960,540];
 assert.equal(P.RecordFlareImage(E,'camera1'),true);assert.equal(P.RecordFlareImage(E,'camera1'),false);assert.equal(P.RecordFlareImage(E,'camera2'),true);assert.equal(Dispatches,2);
});
