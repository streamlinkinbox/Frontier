import assert from 'node:assert/strict';
import fs from 'node:fs';
import {launchBrowser} from './browser.mjs';
const browser=await launchBrowser(),errors=[];
try{
  const page=await browser.newPage({viewport:{width:1365,height:950},deviceScaleFactor:1,reducedMotion:'reduce'});
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto('http://127.0.0.1:5174/frog.html',{waitUntil:'networkidle'});
  await page.waitForFunction(()=>window.frogReady||window.frogError,null,{timeout:90000});
  assert.equal(await page.evaluate(()=>window.frogError),undefined);assert.equal(await page.evaluate(()=>window.frog.stats.bones),39);
  await page.evaluate(()=>window.frog.poseAt('Idle',0));
  await page.locator('[data-palette="Stone"]').click();assert.equal(await page.locator('#palette-name').innerText(),'STONE');
  assert.equal(await page.evaluate(()=>{let c;window.frog.model.traverse(o=>{if(o.isMesh&&o.material.name==='Skin')c=o.material.color.getHexString();});return c;}),'c8cbd0');
  await page.locator('[data-palette="Olive"]').click();assert.equal(await page.locator('#palette-name').innerText(),'OLIVE');
  await page.locator('[data-palette="Earth"]').click();
  await page.locator('[data-clip="Blink"]').click();
  await page.evaluate(()=>window.frog.poseAt('Blink',.60));
  const blink=await page.evaluate(()=>window.frog.model.getObjectByName('Frog_eyelids').morphTargetInfluences[0]);assert(blink>.99);
  fs.mkdirSync('.screenshots/frog',{recursive:true});
  const image=await page.evaluate(()=>window.frog.renderer.domElement.toDataURL());fs.writeFileSync('.screenshots/frog/blink.png',Buffer.from(image.split(',')[1],'base64'));
  for(const id of ['wireframe-toggle','skeleton-toggle','rotate-toggle']){await page.locator('#'+id).click();assert.equal(await page.locator('#'+id).getAttribute('aria-pressed'),'true');await page.locator('#'+id).click();}
  await page.locator('#references-button').click();assert(await page.locator('#references-dialog').isVisible());assert(await page.locator('#references-dialog img').evaluate(i=>i.complete&&i.naturalWidth>0));await page.locator('#close-references').click();
  await page.locator('[data-view="face"]').click();
  await page.waitForFunction(()=>Math.abs(window.frog.camera.position.z-.076)<.0001);
  await page.evaluate(()=>window.frog.poseAt('Idle',0));
  const face=await page.evaluate(()=>window.frog.renderer.domElement.toDataURL());fs.writeFileSync('.screenshots/frog/face.png',Buffer.from(face.split(',')[1],'base64'));
  await page.locator('#reset-camera').click();await page.waitForFunction(()=>Math.abs(window.frog.camera.position.z-.106)<.0001);
  await page.screenshot({path:'.screenshots/frog/desktop.png',timeout:90000,animations:'disabled'});
  await page.setViewportSize({width:390,height:844});await page.screenshot({path:'.screenshots/frog/mobile.png',timeout:90000,animations:'disabled',fullPage:true});
  assert((await page.evaluate(()=>document.documentElement.scrollWidth))<=391);
  assert(await page.locator('[data-palette="Earth"]').isVisible());assert(await page.locator('[data-clip="Blink"]').isVisible());
  const download=await page.request.get('http://127.0.0.1:5174/models/frog.glb');assert(download.ok());assert((await download.body()).length>1000000);
  assert.deepEqual(errors,[]);console.log('PASS: frog GLB load, neutral palettes, embedded blink, camera presets, inspection, references, download and mobile layout.');
}finally{await browser.close();}
