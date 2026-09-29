import fs from 'node:fs';
import {launchBrowser} from './browser.mjs';
import {normalizeGLB} from './normalize-glb.mjs';
import {validateBytes} from 'gltf-validator';
const browser=await launchBrowser();
try{
  const page=await browser.newPage({viewport:{width:1440,height:1000},deviceScaleFactor:1,reducedMotion:'reduce'});
  page.on('pageerror',e=>console.error(e));page.on('console',m=>{if(['error','warn'].includes(m.type()))console.log(m.type(),m.text());});
  await page.goto('http://127.0.0.1:5175/crab.html?build=1',{waitUntil:'networkidle',timeout:120000});
  await page.waitForFunction(()=>window.crabReady||window.crabError,null,{timeout:120000});
  const error=await page.evaluate(()=>window.crabError);if(error)throw new Error(error);
  const stats=await page.evaluate(()=>window.crab.stats);console.log('Geometry',stats);
  const base64=await page.evaluate(async()=>{const b=await window.crab.exportGLB();return new Promise(resolve=>{const r=new FileReader();r.onload=()=>resolve(r.result.split(',')[1]);r.readAsDataURL(new Blob([b]));});});
  const bytes=normalizeGLB(Buffer.from(base64,'base64'));fs.writeFileSync('public/models/crab.glb',bytes);
  const report=await validateBytes(new Uint8Array(bytes),{uri:'crab.glb',maxIssues:25});console.log('GLB',bytes.length,'bytes',JSON.stringify(report.issues,null,2));
  if(report.issues.numErrors||report.issues.numWarnings)process.exitCode=1;
  const clips=await page.evaluate(()=>window.crab.clips.map(c=>({name:c.name,duration:c.duration})));
  fs.writeFileSync('public/models/crab-manifest.json',JSON.stringify({species:'Carcinus maenas',...stats,bytes:bytes.length,clips,lateralSpeedMetresPerSecond:.01,walkingLegPairs:4,claws:2,carapaceWidthMm:72,provenance:'Original procedural anatomical study, not a scan or verified AAA production asset.',validation:{errors:report.issues.numErrors,warnings:report.issues.numWarnings}},null,2)+'\n');
  fs.mkdirSync('.screenshots/crab',{recursive:true});await page.evaluate(()=>window.crab.poseAt('Idle',0));if(process.argv.includes('--screenshot'))await page.screenshot({path:'.screenshots/crab/generator.png',timeout:90000,animations:'disabled'});
}finally{await browser.close();}
