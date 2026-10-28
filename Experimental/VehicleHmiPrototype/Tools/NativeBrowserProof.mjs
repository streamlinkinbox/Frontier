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
for(const [Id,Name,Heading]of [['clouds','Clouds','Clouds'],['local-cloud','Local Cloud','Clouds'],['atmosphere','Atmosphere','Atmosphere'],['sun','Sun','Sun'],['moon','Moons','Moon'],['stars','Stars','Stars'],['flare','Lens Flare','Lens Flare'],['fog','Height Fog','Fog'],['aerial','Atmospheric Fog','Fog']].filter(([Id])=>!process.env.CLOUD_ONLY||['clouds','local-cloud'].includes(Id))){
 await Page.locator('.object-button').filter({has:Page.locator('.row-name',{hasText:new RegExp('^'+Name+'$')})}).evaluate(E=>E.click());
 await Page.locator('.native-environment h1').filter({hasText:new RegExp('^'+Heading+'$')}).waitFor();
 await Page.evaluate(()=>window.IlluminationExhibit?.Shell.Controls.RefreshLive());
 await Shot({path:path.join(Output,Id+'.png')});
 console.log('Panel',Id,'cards',await Page.locator('.ne-card').count(),'sliders',await Page.locator('.ne-range').count());
 const Bad=await Page.locator('.ne-range').evaluateAll(Inputs=>Inputs.filter(I=>!Number.isFinite(+I.value)).length);if(Bad)throw new Error('Invalid slider value: '+Id);
 if(Id==='clouds'||Id==='local-cloud'){
  const Input=Page.getByRole('slider',{name:'Cloud resolution',exact:true});
  await Input.evaluate(E=>{E.value='75';E.dispatchEvent(new Event('input',{bubbles:true}));});
  if(await Page.evaluate(()=>window.IlluminationExhibit.Settings.CloudResolution)!==75)throw new Error('Cloud resolution binding failed');
  await Input.evaluate(E=>{E.value='50';E.dispatchEvent(new Event('input',{bubbles:true}));});
  await Page.waitForTimeout(800);
  await Page.locator('#inspector-content').evaluate(E=>E.scrollTop=0);
  await Shot({path:path.join(Output,Id+'.png')});
  await Page.locator('[data-native-card="Cloud rendering · resolution & budget"]').evaluate(E=>E.scrollIntoView({block:'start'}));
  await Shot({path:path.join(Output,Id+'-resolution.png')});
 }
 if(Id==='clouds'){
  const Stack=Page.locator('[data-native-card="Layer thickness"]');
  await Stack.evaluate(E=>E.scrollIntoView({block:'start'}));
  const Before=await Page.evaluate(()=>window.IlluminationExhibit.Settings.CloudThickness);
  await Stack.locator('canvas').focus();await Page.keyboard.press('ArrowUp');
  if(await Page.evaluate(()=>window.IlluminationExhibit.Settings.CloudThickness)!==Before+100)throw new Error('Thickness stack keyboard binding failed');
  await Shot({path:path.join(Output,'cloud-layer-stack.png')});
  await Stack.getByRole('spinbutton',{name:'Thickness value',exact:true}).fill(String(Before));
  await Stack.getByRole('spinbutton',{name:'Thickness value',exact:true}).dispatchEvent('change');
  await Page.locator('[data-native-card="Cloud shadows"]').evaluate(E=>E.scrollIntoView({block:'start'}));
  await Page.getByRole('slider',{name:'Shadow strength',exact:true}).evaluate(E=>{E.value='.4';E.dispatchEvent(new Event('input',{bubbles:true}));});
  if(await Page.evaluate(()=>window.IlluminationExhibit.Settings.CloudShadowStrength)!==.4)throw new Error('Shadow strength binding failed');
  await Page.getByRole('slider',{name:'Shadow strength',exact:true}).evaluate(E=>{E.value='.65';E.dispatchEvent(new Event('input',{bubbles:true}));});
  await Shot({path:path.join(Output,'cloud-shadows.png')});console.log('Thickness stack and shadow bindings passed');
 }
 if(Id==='local-cloud'){
  await Page.getByRole('button',{name:'Bank',exact:true}).evaluate(E=>E.click());
  if(await Page.evaluate(()=>window.IlluminationExhibit.Settings.LocalCloudHalfX)!==300)throw new Error('Local bank preset failed');
  await Page.getByRole('button',{name:'Wisp',exact:true}).evaluate(E=>E.click());
  if(await Page.evaluate(()=>window.IlluminationExhibit.Settings.LocalCloudHalfZ)!==25)throw new Error('Local wisp preset failed');
  await Page.getByRole('button',{name:'Puff',exact:true}).evaluate(E=>E.click());
  await Page.getByRole('button',{name:'Place in front of camera',exact:true}).evaluate(E=>E.click());
  if(!await Page.evaluate(()=>window.IlluminationExhibit.Settings.LocalCloudEnabled))throw new Error('Local placement did not enable volume');
  await Page.locator('[data-native-card="Local volume · quick start"]').evaluate(E=>E.scrollIntoView({block:'start'}));
  await Shot({path:path.join(Output,'local-cloud-placement.png')});
  console.log('Local puff/bank/wisp presets and placement passed');
 }
 if(Id==='fog'||Id==='aerial'){
  const Key=Id==='fog'?'FogDensity':'AerialDensity',Value=Id==='fog'?.02:.0006;
  const Original=await Page.evaluate(K=>window.IlluminationExhibit.Settings[K],Key);
  await Page.getByRole('slider',{name:'Density',exact:true}).evaluate((E,V)=>{E.value=String(V);E.dispatchEvent(new Event('input',{bubbles:true}));},Value);
  const Actual=await Page.evaluate(K=>window.IlluminationExhibit.Settings[K],Key);
  if(Math.abs(Actual-Value)>1e-8)throw new Error('Fog range binding failed: '+Id);
  const NumberInput=Page.getByRole('spinbutton',{name:'Density value',exact:true});
  await NumberInput.fill('');
  if(await Page.evaluate(K=>window.IlluminationExhibit.Settings[K],Key)!==Actual)throw new Error('Empty numeric input changed fog density');
  await NumberInput.fill(String(Original));await NumberInput.dispatchEvent('change');
  if(Math.abs(await Page.evaluate(K=>window.IlluminationExhibit.Settings[K],Key)-Original)>1e-8)throw new Error('Fog number binding failed: '+Id);
  console.log('Fog slider and numeric field bindings passed:',Id);
 }
 if(Id==='moon'){
  await Page.locator('.ne-body').filter({hasText:'Ember'}).evaluate(E=>E.click());
  await Page.locator('.ne-body').filter({hasText:'Luna'}).evaluate(E=>E.click());
  await Page.getByRole('spinbutton',{name:'Phase value',exact:true}).fill('7.38');
  await Page.getByRole('spinbutton',{name:'Phase value',exact:true}).dispatchEvent('change');
  await Page.locator('.ne-card').filter({has:Page.locator('h3',{hasText:'Lunar phase'})}).evaluate(E=>E.scrollIntoView({block:'start'}));
  await Page.evaluate(()=>window.IlluminationExhibit?.Shell.Controls.RefreshLive());
  await Shot({path:path.join(Output,'moon-phase.png')});
 }
}
await Page.screenshot({path:path.join(Output,'cloud-scene.png')});
// Render the local volume alone, not just its inspector. Paused RAF makes the proof deterministic.
await Page.evaluate(async()=>{const H=window.IlluminationExhibit;H.Settings.CloudEnabled=false;H.Settings.CloudResolution=35;for(let I=0;I<3;I++){await H.Exchange.Device.queue.onSubmittedWorkDone();await new Promise(Resolve=>window.__ProofRAF(Time=>{H.AdvanceFrame(Time);Resolve();}));}await H.Exchange.Device.queue.onSubmittedWorkDone();H.Shell.Select('local-cloud');});
await Page.waitForTimeout(1200);
await Page.screenshot({path:path.join(Output,'local-cloud-scene.png'),timeout:90000});
console.log('FAILURE',await Page.locator('#failure').innerText());console.log('Frames',await Page.evaluate(()=>window.IlluminationExhibit?.FrameIndex));
Closing=true;await Browser.close();if(Errors.length)process.exitCode=1;
