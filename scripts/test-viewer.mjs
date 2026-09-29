import assert from 'node:assert/strict';
import fs from 'node:fs';
import { launchBrowser } from './browser.mjs';
import { STRIKE } from '../src/strike-motion.js';
const browser=await launchBrowser();
const errors=[];
try {
 const page=await browser.newPage({viewport:{width:1365,height:900},deviceScaleFactor:1, reducedMotion:'reduce'});
 page.on('pageerror',e=>errors.push(e.message));
 await page.goto('http://127.0.0.1:5173/',{waitUntil:'networkidle'});
 await page.waitForFunction(()=>window.mantisReady,null,{timeout:90000});
 await page.evaluate(()=>window.mantis.poseAt('Idle',0));
 assert.equal(await page.locator('.motion-card.active').getAttribute('data-clip'),'Idle');
 assert.match(await page.locator('#bone-count').innerText(),/43/);
 assert.equal(await page.locator('.motion-card').count(),4);
 // Imported GLB only: verify movement after a portable-asset round trip.
 const poses=await page.evaluate((strike)=>{
  const m=window.mantis;
  function pose(name,time){m.mixer.stopAllAction();const clip=m.clips.find(c=>c.name===name);const a=m.mixer.clipAction(clip);a.reset().play();a.time=time;m.mixer.update(0);m.model.updateMatrixWorld(true);const p={};for(const n of ['Head','Fore_L_Tibia','Fore_L_Tarsus','Middle_L_Tarsus','Middle_R_Tarsus','Hind_L_Tarsus','Hind_R_Tarsus'])p[n]=Array.from(m.model.getObjectByName(n).matrixWorld.elements).slice(12,15);return p;}
  return {idle:pose('Idle',0),walk:pose('Walk',.35),open:pose('Attack',strike.closeStart),closed:pose('Attack',strike.closeEnd),retracted:pose('Attack',strike.retractEnd)};
 },STRIKE);
 assert(poses.open.Fore_L_Tibia[1]>poses.open.Head[1],'attack loads the femur above the head instead of punching horizontally');
 assert(poses.open.Fore_L_Tarsus[1]-poses.closed.Fore_L_Tarsus[1]>.01,'tibia sweeps down to clamp');
 assert(poses.closed.Fore_L_Tibia[2]-poses.retracted.Fore_L_Tibia[2]>.01,'closed grippers pull back promptly');
 const feet=['Middle_L_Tarsus','Middle_R_Tarsus','Hind_L_Tarsus','Hind_R_Tarsus'];
 assert.equal(feet.filter(n=>poses.walk[n][1]>.002).length,1,'one foot swings while three support');
 for(const n of feet)assert(Math.abs(poses.closed[n][1]-.0017)<.00002,'feet stay planted through the attack');
 await page.evaluate(()=>window.mantis.poseAt('Idle',0));
 await page.locator('[data-clip="Attack"]').click();
 assert.equal(await page.locator('#current-clip').innerText(),'Attack');
 assert.equal(await page.locator('#loop-toggle').isChecked(),false,'Attack defaults to a single strike');
 await page.evaluate(()=>window.mantis.poseAt('Attack',.520));
 await page.locator('[data-speed="0.25"]').click();
 assert.equal(await page.locator('#speed-label').innerText(),'0.25×');
 await page.waitForFunction(()=>document.querySelector('#clip-type').textContent==='SWEEP + CLAMP');
 await page.locator('#loop-toggle').check();
 assert.equal(await page.locator('#loop-toggle').isChecked(),true);
 await page.locator('#loop-toggle').uncheck();
 assert.equal(await page.locator('#loop-toggle').isChecked(),false);
 for(const id of ['wireframe-toggle','skeleton-toggle','labels-toggle']){
  await page.locator('#'+id).click();assert.equal(await page.locator('#'+id).getAttribute('aria-pressed'),'true');
  await page.locator('#'+id).click();assert.equal(await page.locator('#'+id).getAttribute('aria-pressed'),'false');
 }
 await page.locator('#references-button').click();
 assert(await page.locator('#references-dialog').isVisible());
 assert(await page.locator('#references-dialog img').evaluate(img=>img.complete&&img.naturalWidth>0));
 await page.locator('#close-references').click();
 await page.locator('#fullscreen-button').click();
 assert(await page.locator('body').evaluate(el=>el.classList.contains('focus-mode')));
 await page.keyboard.press('Escape');
 assert(!(await page.locator('body').evaluate(el=>el.classList.contains('focus-mode'))));
 const response=await page.request.get(new URL('/models/mantis.glb', page.url()).href); assert(response.ok());assert((await response.body()).length>1000000);
 await page.locator('[data-clip="Idle"]').click();
 assert.equal(await page.locator('#loop-toggle').isChecked(),true,'Idle retains its own loop setting');
 await page.evaluate(()=>window.mantis.poseAt('Idle',0));
 fs.mkdirSync('.screenshots',{recursive:true});
 await page.screenshot({path:'.screenshots/final-desktop.png',timeout:90000,animations:'disabled'});
 // The newly added held defensive pose and portable wing morph.
 await page.locator('[data-clip="Stance"]').click();
 assert.equal(await page.locator('#current-clip').innerText(),'Stance');
 assert.equal(await page.locator('#loop-toggle').isChecked(),true);
 await page.evaluate(()=>window.mantis.poseAt('Stance',1.2));
 const fan=await page.evaluate(()=>window.mantis.model.getObjectByName('Hindwing_membrane').morphTargetInfluences[0]);
 assert(fan>.97,'stance unfolds the actual skinned membrane');
 await page.locator('[data-clip="Walk"]').click();
 assert.match(await page.locator('#viewport-mode').innerText(),/TRACKING/);
 await page.evaluate(()=>window.mantis.poseAt('Walk',.3));
 const groundA=await page.evaluate(()=>window.mantis.walkDistance);
 await page.evaluate(()=>window.mantis.poseAt('Walk',.7));
 const groundB=await page.evaluate(()=>window.mantis.walkDistance);
 assert(Math.abs((groundB-groundA)-.001)<1e-8,'tracking surface must match 2.5 mm/s');
 assert((await page.evaluate(()=>window.mantis.model.getObjectByName('Hindwing_membrane').morphTargetInfluences[0]))===0,'walking refolds the membranes');
 await page.keyboard.press('4');
 assert.equal(await page.locator('#current-clip').innerText(),'Stance');
 await page.evaluate(()=>window.mantis.poseAt('Stance',1.2));
 // Mobile layout and touch-sized viewing surface.
 await page.setViewportSize({width:390,height:844});
 await page.waitForFunction(()=>document.querySelector('#viewport').clientWidth===390);
 assert(await page.locator('[data-clip="Walk"]').isVisible());
 assert(await page.locator('[data-clip="Stance"]').isVisible());
 assert(await page.locator('.download').isVisible());
 const size=await page.evaluate(()=>({scroll:document.documentElement.scrollWidth,width:innerWidth,stage:document.querySelector('#viewport').clientHeight}));
 assert(size.scroll<=size.width+1,'mobile layout must not overflow horizontally');assert(size.stage>300);
 await page.screenshot({path:'.screenshots/final-mobile.png',timeout:90000,animations:'disabled'});
 assert.deepEqual(errors,[]);
 console.log('PASS: portable GLB animation, foot contact, cocked strike / overlapping clamp / retraction, one-shot and per-clip looping, held Stance / wing morph, tracking walk, playback, inspection tools, references, focus mode, download and responsive layout.');
}finally{await browser.close();}
