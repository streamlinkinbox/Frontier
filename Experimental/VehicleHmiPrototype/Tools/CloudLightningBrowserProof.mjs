import {chromium} from 'playwright';import Chromium from '@sparticuz/chromium';import {inflate} from './node_modules/@sparticuz/chromium/build/lambdafs.js';import path from 'node:path';import fs from 'node:fs';import assert from 'node:assert/strict';
const Out='/home/user/lightning-cloud-proof';fs.mkdirSync(Out,{recursive:true});const Executable=await Chromium.executablePath();await inflate(path.resolve('Tools/node_modules/@sparticuz/chromium/bin/al2023.tar.br'));
const Browser=await chromium.launch({executablePath:Executable,env:{...process.env,LD_LIBRARY_PATH:'/tmp/al2023/lib:/tmp',VK_ICD_FILENAMES:'/tmp/vk_swiftshader_icd.json'},args:[...Chromium.args.filter(A=>!['--single-process','--in-process-gpu','--disable-web-security'].includes(A)),'--enable-unsafe-webgpu','--use-webgpu-adapter=swiftshader','--enable-features=Vulkan','--use-vulkan=swiftshader','--disable-vulkan-surface','--enable-unsafe-swiftshader'],headless:true});
try{
 const Page=await Browser.newPage({viewport:{width:1280,height:800}}),Errors=[];Page.on('pageerror',E=>Errors.push(String(E)));Page.on('console',M=>{if(M.type()==='error')Errors.push(M.text());});
 await Page.addInitScript(()=>{const RAF=requestAnimationFrame.bind(window);window.requestAnimationFrame=Cb=>RAF(T=>{if(window.IlluminationExhibit?.FrameIndex>=1)return;Cb(T);});});
 await Page.goto('http://127.0.0.1:8080/?quality=low&scale=.25&stresspool=0&preset=Night&inspector=lightning',{waitUntil:'domcontentloaded'});
 await Page.waitForFunction(()=>window.IlluminationExhibit?.FrameIndex>=1,null,{timeout:180000,polling:100});
 const Result=await Page.evaluate(async()=>{
  const H=window.IlluminationExhibit,D=H.Exchange.Device,C=H.Clouds,Camera=H.CameraState;await D.queue.onSubmittedWorkDone();
  const Half=V=>{const E=(V>>10)&31,M=V&1023;return E?2**(E-15)*(1+M/1024):2**(-14)*M/1024;};
  async function Sample(S,Flash){C.Prepare(H.Display,H.Surface,S,Camera,{SunDirection:[0,0,1]},0,null,Flash,false);const E=D.createCommandEncoder();C.Record(E,null);D.queue.submit([E.finish()]);const [W,N]=C.Extent,Stride=Math.ceil(W*8/256)*256,B=D.createBuffer({size:Stride*N,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ}),Copy=D.createCommandEncoder();Copy.copyTextureToBuffer({texture:C.Colour},{buffer:B,bytesPerRow:Stride},[W,N]);D.queue.submit([Copy.finish()]);await B.mapAsync(GPUMapMode.READ);const V=new Uint16Array(B.getMappedRange()),A=[];for(let Y=0;Y<N;Y++)for(let X=0;X<W;X++){const I=Y*Stride/2+X*4;A.push([Half(V[I]),Half(V[I+1]),Half(V[I+2]),Half(V[I+3])]);}B.unmap();B.destroy();return A;}
  const Results=[];
  for(const Local of [false,true]){
   const S={...H.Settings,CloudEnabled:!Local,CloudCoverage:1,CloudDensity:1.2,CloudBase:300,CloudThickness:350,CloudFollowWind:false,WindEnabled:false,LocalCloudEnabled:Local,LocalCloudCoverage:1,LocalCloudDensity:1,LocalCloudDetail:0,LocalCloudX:Camera.Position[0]+45,LocalCloudY:Camera.Position[1],LocalCloudZ:22,LocalCloudHalfX:25,LocalCloudHalfY:30,LocalCloudHalfZ:22,LocalCloudFollowWind:false,LightningEnabled:true,LightningAuto:false,LightningDistance:800,LightningRadius:1800,LightningCloudTarget:Local?2:1};
   H.Lightning.Storm.Clear();H.Lightning.Storm.LastStrike=-Infinity;H.Lightning.Storm.Request();H.Lightning.Storm.Advance(.016,S,Camera,C.UniformData);H.Lightning.Storm.Advance(.02,S,Camera,C.UniformData);const Flash=H.Lightning.Storm.Light.slice(),Before=await Sample(S,new Float32Array(8)),After=await Sample(S,Flash);
   let Changed=0,BeforeSum=0,AfterSum=0,AlphaError=0,MaxGain=0;for(let I=0;I<Before.length;I++){const A=Before[I],B=After[I],Gain=B[0]+B[1]+B[2]-A[0]-A[1]-A[2];BeforeSum+=A[0]+A[1]+A[2];AfterSum+=B[0]+B[1]+B[2];if(Gain>1e-5)Changed++;MaxGain=Math.max(MaxGain,Gain);AlphaError=Math.max(AlphaError,Math.abs(A[3]-B[3]));}
   Results.push({medium:Local?'local':'global',anchor:H.Lightning.Storm.Bolts[0].Anchor,pixels:Before.length,changed:Changed,beforeRgbSum:BeforeSum,afterRgbSum:AfterSum,maxRgbGain:MaxGain,alphaError:AlphaError});
  }
  return Results;
 });
 console.log(JSON.stringify({results:Result,errors:Errors},null,2));fs.writeFileSync(Out+'/result.json',JSON.stringify({results:Result,errors:Errors},null,2));for(const R of Result){assert.ok(R.anchor.Embedded,R.medium+' source should be inside density');assert.ok(R.changed>0&&R.afterRgbSum>R.beforeRgbSum,R.medium+' cloud radiance should increase');assert.equal(R.alphaError,0,'flash must not change cloud density/transmittance');}assert.equal(Errors.length,0);
}finally{await Browser.close();}
