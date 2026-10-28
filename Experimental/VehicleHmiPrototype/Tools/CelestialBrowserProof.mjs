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

await Page.evaluate(()=>window.IlluminationExhibit.Shell.Select('stars'));
await Page.getByRole('button',{name:'Preview clear night',exact:true}).evaluate(E=>E.click());
const Night=await Page.evaluate(()=>{const S=window.IlluminationExhibit.Settings;return {elevation:S.SunElevation,cloud:S.CloudEnabled,local:S.LocalCloudEnabled};});
if(Night.elevation!==-18||Night.cloud||Night.local)throw new Error('Clear-night preview binding failed');
await Shot({path:path.join(Output,'stars-controls.png')});
await Page.evaluate(()=>window.IlluminationExhibit.Shell.Select('moon'));
if(await Page.getByRole('spinbutton',{name:'Size value',exact:true}).inputValue()!=='25.00')throw new Error('Default Moon diameter is not 25');
const Power=Page.getByRole('slider',{name:'Moonlight power',exact:true});await Power.evaluate(E=>{E.value='1.5';E.dispatchEvent(new Event('input',{bubbles:true}));});
if(await Page.evaluate(()=>window.IlluminationExhibit.Settings.MoonLightIntensity)!==1.5)throw new Error('Moonlight power binding failed');
await Page.locator('[data-native-card="Moonlight"]').evaluate(E=>E.scrollIntoView({block:'start'}));
await Shot({path:path.join(Output,'moonlight-controls.png')});
console.log('Clear-night preset, 25-degree Earth Moon and moonlight power binding passed.');Closing=true;await Browser.close();if(Errors.length)process.exitCode=1;
