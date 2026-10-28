// Optional real-browser proof: npm install --no-save playwright @sparticuz/chromium
import {chromium} from 'playwright';
import Chromium from '@sparticuz/chromium';
import {inflate} from './node_modules/@sparticuz/chromium/build/lambdafs.js';
import path from 'node:path';
import fs from 'node:fs';
const ProofFrames=Number(process.env.PROOF_FRAMES??1);
const Output=process.env.OUT_DIR??'/home/user/native-proof';fs.mkdirSync(Output,{recursive:true});
const Executable=await Chromium.executablePath();await inflate(path.resolve('Experimental/VehicleHmiPrototype/Tools/node_modules/@sparticuz/chromium/bin/al2023.tar.br'));
const Browser=await chromium.launch({executablePath:Executable,env:{...process.env,LD_LIBRARY_PATH:'/tmp/al2023/lib',VK_ICD_FILENAMES:'/tmp/vk_swiftshader_icd.json'},args:[...Chromium.args.filter(A=>!['--single-process','--in-process-gpu'].includes(A)),'--enable-unsafe-webgpu','--use-webgpu-adapter=swiftshader','--enable-features=Vulkan','--use-vulkan=swiftshader','--disable-vulkan-surface'],headless:true});
const Page=await Browser.newPage({viewport:{width:1600,height:1100},deviceScaleFactor:1});let Closing=false;
const Errors=[];Page.on('pageerror',E=>{if(!Closing){Errors.push(String(E));console.log('PAGE ERROR',String(E));}});
await Page.addInitScript(Count=>{const RAF=window.requestAnimationFrame.bind(window);window.__ProofRAF=RAF;window.requestAnimationFrame=Callback=>RAF(Time=>{if(window.IlluminationExhibit?.FrameIndex>=Count)return;Callback(Time);});},ProofFrames);
if(process.env.CLOUD_COVERAGE)await Page.addInitScript(Coverage=>{try{localStorage.setItem('Frontier.Environment.v1',JSON.stringify({CloudCoverage:Coverage}));}catch{}},Number(process.env.CLOUD_COVERAGE));
async function Shot(Options){const Box=await Page.locator('#inspector').boundingBox();return Page.screenshot({...Options,clip:Box,timeout:90000});}
await Page.goto('http://127.0.0.1:8080/?quality=low&stresspool=0&inspector=atmosphere',{waitUntil:'domcontentloaded'});
await Page.waitForSelector('.native-environment h1',{timeout:90000});
await Page.waitForFunction(N=>window.IlluminationExhibit?.FrameIndex>=N,ProofFrames,{timeout:150000,polling:100});
await Page.evaluate(()=>window.IlluminationExhibit.Exchange.Device.queue.onSubmittedWorkDone());

console.log(await Page.evaluate(async()=>{
 const H=window.IlluminationExhibit,{PlaceLocalCloud,LocalCloudPresets}=await import('/Source/LocalCloudPlacement.js');
 Object.assign(H.Settings,LocalCloudPresets.Puff,{CloudEnabled:false,CloudResolution:35,LocalCloudFollowWind:false,CloudFollowWind:false});PlaceLocalCloud(H.Settings,H.CameraEye,H.CameraState.Forward);
 for(let I=0;I<3;I++){await H.Exchange.Device.queue.onSubmittedWorkDone();await new Promise(Resolve=>window.__ProofRAF(Time=>{H.AdvanceFrame(Time);Resolve();}));}await H.Exchange.Device.queue.onSubmittedWorkDone();
 const C=H.Clouds,D=H.Exchange.Device,[W,N]=C.Extent,Stride=Math.ceil(W*8/256)*256,B=D.createBuffer({size:Stride*N,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ}),En=D.createCommandEncoder();En.copyTextureToBuffer({texture:C.Colour},{buffer:B,bytesPerRow:Stride},[W,N]);D.queue.submit([En.finish()]);await B.mapAsync(GPUMapMode.READ);const A=new Uint16Array(B.getMappedRange());const Half=H=>{const E=(H>>10)&31,M=H&1023;return E?2**(E-15)*(1+M/1024):2**(-14)*M/1024;};let Sum=0,Max=0,MaxPixel=null;for(let Y=0;Y<N;Y++)for(let X=0;X<W;X++){const At=Y*Stride/2+X*4,V=1-Half(A[At+3]);Sum+=V;if(V>Max){Max=V;MaxPixel=[X,Y,Half(A[At]),Half(A[At+1]),Half(A[At+2]),Half(A[At+3])];}}B.destroy();H.Shell.Select('local-cloud');return {eye:H.CameraEye,forward:C.LastForward,uniform:Array.from(C.UniformData),extent:C.Extent,opacityMean:Sum/(W*N),opacityMax:Max,maxPixel:MaxPixel,frames:H.FrameIndex};
}));
await Page.waitForTimeout(1500);await Page.screenshot({path:path.join(Output,'local-visible.png'),timeout:90000});Closing=true;await Browser.close();if(Errors.length)process.exitCode=1;
