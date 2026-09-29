import assert from 'node:assert/strict';import fs from 'node:fs';import {launchBrowser} from './browser.mjs';
const browser=await launchBrowser(),errors=[];
try{
 const page=await browser.newPage({viewport:{width:1365,height:950},deviceScaleFactor:1,reducedMotion:'reduce'});page.on('pageerror',e=>errors.push(e.message));
 await page.goto('http://127.0.0.1:5175/crab.html',{waitUntil:'networkidle'});await page.waitForFunction(()=>window.crabReady||window.crabError,null,{timeout:90000});assert.equal(await page.evaluate(()=>window.crabError),undefined);assert.equal(await page.evaluate(()=>window.crab.stats.bones),59);
 const before=await page.evaluate(()=>({screen:window.crab.targetScreen(),camera:window.crab.camera.position.toArray(),target:window.crab.controller.target.toArray()}));
 await page.mouse.move(before.screen.x,before.screen.y);await page.mouse.down();assert.equal(await page.evaluate(()=>window.crab.orbit.enabled),false);await page.mouse.move(before.screen.x+32,before.screen.y-12,{steps:4});await page.mouse.up();
 const after=await page.evaluate(()=>({camera:window.crab.camera.position.toArray(),target:window.crab.controller.target.toArray(),orbit:window.crab.orbit.enabled}));assert(after.orbit);assert(Math.hypot(...after.target.map((v,i)=>v-before.target[i]))>.001);assert(Math.hypot(...after.camera.map((v,i)=>v-before.camera[i]))<1e-7);
 const second=await page.evaluate(()=>window.crab.targetScreen());
 await page.mouse.move(second.x,second.y);await page.mouse.down();assert.equal(await page.evaluate(()=>window.crab.orbit.enabled),false);
 await page.keyboard.press('Escape');assert.equal(await page.evaluate(()=>window.crab.orbit.enabled),true);assert.equal(await page.evaluate(()=>window.crab.controller.dragging),false);await page.mouse.up();

 await page.locator('#target-z').focus();await page.keyboard.press('End');await page.waitForFunction(()=>!window.crab.controller.plan.valid&&document.querySelector('#grab-button').disabled);assert(await page.locator('#grab-button').isDisabled());
 await page.locator('[data-preset="25"]').click();await page.locator('#grab-button').click();await page.waitForFunction(()=>window.crab.controller.state==='hold',null,{timeout:30000});assert.equal(await page.locator('#grab-button-text').innerText(),'Release target');assert(await page.locator('#target-x').isDisabled());assert(await page.evaluate(()=>window.crab.controller.lastContact.hit));
 await page.evaluate(()=>window.crab.setPaused(true));fs.mkdirSync('.screenshots/crab',{recursive:true});await page.screenshot({path:'.screenshots/crab/held.png',timeout:90000,animations:'disabled'});
 await page.locator('#grab-button').click();await page.waitForFunction(()=>window.crab.controller.state==='released',null,{timeout:30000});await page.locator('#reset-target').click();
 await page.locator('[data-preset="-25"]').click();await page.locator('#grab-button').click();await page.waitForFunction(()=>window.crab.controller.state==='hold',null,{timeout:30000});assert.equal(await page.evaluate(()=>window.crab.controller.activePlan.claw.side),'L');
 await page.locator('#reset-target').click();await page.locator('[data-mode="motion"]').click();assert.equal(await page.locator('[data-clip]').count(),5);
 await page.locator('[data-clip="Walk_Left"]').click();assert.equal(await page.locator('#current-clip').innerText(),'Walk left');await page.evaluate(()=>window.crab.poseAt('Walk_Left',.2));
 for(const id of ['wireframe-toggle','skeleton-toggle']){await page.locator('#'+id).click();assert.equal(await page.locator('#'+id).getAttribute('aria-pressed'),'true');await page.locator('#'+id).click();}
 await page.locator('#references-button').click();assert(await page.locator('#references-dialog').isVisible());await page.locator('#close-references').click();
 await page.locator('[data-mode="grab"]').click();await page.evaluate(()=>window.crab.setPaused(true));await page.screenshot({path:'.screenshots/crab/desktop.png',timeout:90000,animations:'disabled'});
 await page.setViewportSize({width:390,height:844});assert(await page.locator('#grab-button').isVisible());assert(await page.locator('#target-z').isVisible());assert((await page.evaluate(()=>document.documentElement.scrollWidth))<=391);assert(await page.locator('.specimen-picker-select').isVisible());assert.equal(await page.locator('.specimen-picker-select').inputValue(),'/crab.html');await page.screenshot({path:'.screenshots/crab/mobile.png',fullPage:true,timeout:90000,animations:'disabled'});
 const response=await page.request.get('http://127.0.0.1:5175/models/crab.glb');assert(response.ok());assert((await response.body()).length>1e6);
 await Promise.all([page.waitForURL('**/frog.html',{waitUntil:'domcontentloaded'}),page.locator('.specimen-picker-select').selectOption('/frog.html')]);
 await page.waitForFunction(()=>window.frogReady||window.frogError,null,{timeout:90000});assert.equal(await page.evaluate(()=>window.frogError),undefined);assert.equal(await page.locator('.specimen-picker-select').inputValue(),'/frog.html');
 await Promise.all([page.waitForURL('**/index.html',{waitUntil:'domcontentloaded'}),page.locator('.specimen-picker-select').selectOption('/index.html')]);
 await page.waitForFunction(()=>window.mantisReady||window.mantisError,null,{timeout:90000});assert.equal(await page.evaluate(()=>window.mantisError),undefined);assert.equal(await page.locator('.specimen-picker-select').inputValue(),'/index.html');
 assert.deepEqual(errors,[]);
 console.log('PASS: crab load, real drag/orbit isolation and Escape cancellation, range limits, both pincer captures, lift/release/reset, lateral clips, inspection, references, download, mobile layout and navigation through all three studies.');
}finally{await browser.close();}
