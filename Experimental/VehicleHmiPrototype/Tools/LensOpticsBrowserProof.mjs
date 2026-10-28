// Requires prototype server on 8080 and optional Tools dependencies playwright + @sparticuz/chromium@138.0.2.
import {chromium} from 'playwright';import Chromium from '@sparticuz/chromium';import {inflate} from './node_modules/@sparticuz/chromium/build/esm/lambdafs.js';
import path from 'node:path';import fs from 'node:fs';import assert from 'node:assert/strict';
const Out=process.env.OUT_DIR??'/home/user/lens-optics-proof';fs.mkdirSync(Out,{recursive:true});const Executable=await Chromium.executablePath();await inflate(path.resolve('Tools/node_modules/@sparticuz/chromium/bin/al2023.tar.br'));
const Browser=await chromium.launch({executablePath:Executable,env:{...process.env,LD_LIBRARY_PATH:'/tmp/al2023/lib:/tmp',VK_ICD_FILENAMES:'/tmp/vk_swiftshader_icd.json'},args:[...Chromium.args.filter(A=>!['--single-process','--in-process-gpu','--disable-web-security'].includes(A)),'--enable-unsafe-webgpu','--use-webgpu-adapter=swiftshader','--enable-features=Vulkan','--use-vulkan=swiftshader','--disable-vulkan-surface','--enable-unsafe-swiftshader'],headless:true});
try{
 const P=await Browser.newPage({viewport:{width:1440,height:1000}}),Errors=[];P.on('pageerror',E=>Errors.push(String(E)));
 await P.addInitScript(()=>{const RAF=requestAnimationFrame.bind(window);window.requestAnimationFrame=Cb=>RAF(T=>{if(window.IlluminationExhibit?.FrameIndex>=1)return;Cb(T);});});
 await P.goto('http://127.0.0.1:8080/?quality=low&scale=.25&stresspool=0&inspector=flare',{waitUntil:'domcontentloaded'});await P.waitForFunction(()=>window.IlluminationExhibit?.FrameIndex>=1,null,{timeout:150000,polling:100});
 const Get=K=>P.evaluate(Key=>window.IlluminationExhibit.Settings[Key],K);
 const Field=async(Name,V)=>{const E=P.getByRole('spinbutton',{name:Name,exact:true});await E.fill(String(V));await E.press('Tab');};
 assert.equal(await P.getByRole('tab').count(),4);
 await P.waitForFunction(()=>document.querySelector('.lo-worker-status')?.textContent==='',null,{timeout:30000,polling:100});
 const Image=P.getByRole('group',{name:'Drag the flare source; arrow keys move the preview light',exact:true});await Image.focus();await P.keyboard.press('ArrowRight');assert.ok(Math.abs(await Get('FlarePreviewX')-.30)<1e-12);
 await Field('Intensity',.8);assert.equal(await Get('FlareIntensity'),.8);
 await P.getByRole('combobox',{name:'Optical preset',exact:true}).selectOption('Anamorphic');assert.equal(await Get('FlareBurst'),false);assert.equal(await Get('FlareStreak'),1.5);
 await P.waitForFunction(()=>document.querySelector('.lo-worker-status')?.textContent==='',null,{timeout:30000,polling:100});await P.screenshot({path:Out+'/compose.png'});
 await P.getByRole('tab',{name:'Ghosts',exact:true}).click();await Field('Ghost count',9);assert.equal(await Get('FlareGhosts'),9);assert.equal(await Get('FlarePreset'),'Custom layers');await P.getByRole('combobox',{name:'Ghost shape',exact:true}).selectOption('8');assert.equal(await Get('FlareGhostShape'),8);
 const Axis=P.getByRole('group',{name:'Ghost optical axis · drag or use arrow keys to edit spacing',exact:true});await Axis.focus();const Spacing=await Get('FlareGhostSpacing');await P.keyboard.press('ArrowRight');assert.ok(Math.abs(await Get('FlareGhostSpacing')-Spacing-.05)<1e-12);await P.screenshot({path:Out+'/ghosts.png'});
 await P.getByRole('tab',{name:'Halo',exact:true}).click();await Field('Halo radius',.35);assert.equal(await Get('FlareHaloRadius'),.35);const Ring=P.getByRole('group',{name:'Halo radius guide · drag or use arrow keys',exact:true});await Ring.focus();await P.keyboard.press('ArrowLeft');assert.ok(Math.abs(await Get('FlareHaloRadius')-.34)<1e-12);await P.screenshot({path:Out+'/halo.png'});
 await P.getByRole('tab',{name:'Rays & streaks',exact:true}).click();await Field('Ray pairs',10);assert.equal(await Get('FlareRays'),10);await P.getByRole('button',{name:'Warm streak',exact:false}).click();assert.equal(await Get('FlareWarmStreak'),true);await P.screenshot({path:Out+'/rays.png'});
 const Enabled=await Get('FlareEnabled');await P.getByRole('button',{name:'Toggle lens flare',exact:true}).click();assert.equal(await Get('FlareEnabled'),!Enabled);
 const Download=P.waitForEvent('download');await P.getByRole('button',{name:'Export HDR',exact:false}).click();const D=await Download;assert.equal(D.suggestedFilename(),'LensFlare.pfm');await D.saveAs(Out+'/LensFlare.pfm');const Bytes=fs.readFileSync(Out+'/LensFlare.pfm');assert.equal(Bytes.subarray(0,16).toString(),'PF\n640 280\n-1.0\n');assert.equal(Bytes.length,16+640*280*3*4);
 assert.deepEqual(Errors,[]);console.log(JSON.stringify({categories:4,sourceKeyboard:true,preset:true,ghosts:true,halo:true,rays:true,enabled:true,hdrBytes:Bytes.length,Errors},null,2));
}finally{await Browser.close();}
