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


for(const Panel of ['clouds','local-cloud','local-fog','fog','sun','atmosphere','flare','stars','moon','render']){
 await Page.evaluate(P=>window.IlluminationExhibit.Shell.Select(P),Panel);
 const Sliders=await Page.locator('#inspector input[type=range]').evaluateAll(Inputs=>Inputs.map(E=>{const R=E.getBoundingClientRect(),Root=E.closest('.frontier-slider'),Head=Root?.querySelector('.frontier-slider-head')?.getBoundingClientRect();return {shared:!!Root,width:R.width,height:R.height,below:Head&&R.top>=Head.bottom};}));
 if(Sliders.some(S=>!S.shared||S.width<50||S.height<20||!S.below))throw new Error(Panel+' has non-shared or broken sliders: '+JSON.stringify(Sliders));
 if(['clouds','fog','sun','render'].includes(Panel))await Shot({path:path.join(Output,Panel+'-controls.png')});
 console.log(Panel,Sliders.length,'shared sliders');
}
await Page.evaluate(()=>window.IlluminationExhibit.Shell.Select('local-fog'));
const Density=Page.getByRole('slider',{name:'Extinction',exact:true});await Density.evaluate(E=>{E.value='.037';E.dispatchEvent(new Event('input',{bubbles:true}));});
if(await Page.evaluate(()=>window.IlluminationExhibit.Settings.LocalFogDensity)!==.037)throw new Error('Fog slider precision/sync failed');
const Numeric=Page.getByRole('spinbutton',{name:'Extinction value',exact:true});await Numeric.fill('.042');await Numeric.dispatchEvent('input');await Numeric.blur();
if(await Page.evaluate(()=>window.IlluminationExhibit.Settings.LocalFogDensity)!==.042)throw new Error('Fog numeric input precision/sync failed');
await Page.getByRole('button',{name:'Place fog in front of camera',exact:true}).click();
if(!await Page.evaluate(()=>window.IlluminationExhibit.Settings.LocalFogEnabled))throw new Error('Fog placement binding failed');
await Page.evaluate(()=>window.IlluminationExhibit.Shell.Select('stars'));await Page.getByRole('button',{name:'Preview clear night',exact:true}).click();
if(await Page.evaluate(()=>window.IlluminationExhibit.Settings.LocalFogEnabled))throw new Error('Clear night must disable fog');
console.log('Shared panel layout, fractional input, placement and clear-night bindings passed.');Closing=true;await Browser.close();if(Errors.length)process.exitCode=1;
