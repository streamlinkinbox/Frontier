import fs from 'node:fs';
import {launchBrowser} from './browser.mjs';
import {normalizeGLB} from './normalize-glb.mjs';
import {validateBytes} from 'gltf-validator';
const browser=await launchBrowser();
try{
  const page=await browser.newPage({viewport:{width:1440,height:1000},deviceScaleFactor:1,reducedMotion:'reduce'});
  page.on('pageerror',e=>console.error(e));page.on('console',m=>{if(['error','warn'].includes(m.type()))console.log(m.type(),m.text());});
  await page.goto('http://127.0.0.1:5174/frog.html?build=1',{waitUntil:'networkidle',timeout:120000});
  await page.waitForFunction(()=>window.frogReady||window.frogError,null,{timeout:120000});
  const err=await page.evaluate(()=>window.frogError);if(err)throw new Error(err);
  console.log('Geometry',await page.evaluate(()=>window.frog.stats));
  const base64=await page.evaluate(async()=>{const data=await window.frog.exportGLB();return await new Promise(resolve=>{const r=new FileReader();r.onload=()=>resolve(r.result.split(',')[1]);r.readAsDataURL(new Blob([data]));});});
  const bytes=normalizeGLB(Buffer.from(base64,'base64'));fs.writeFileSync('public/models/frog.glb',bytes);
  const report=await validateBytes(new Uint8Array(bytes),{uri:'frog.glb',maxIssues:25});console.log('GLB bytes',bytes.length,JSON.stringify(report.issues,null,2));
  if(report.issues.numErrors)process.exitCode=1;
  const metadata=await page.evaluate(()=>({name:'Neutral common frog',species:'Rana temporaria',...window.frog.stats,animations:window.frog.clips.map(c=>({name:c.name,duration:c.duration})),colour:'Muted olive-brown / warm ivory',authorship:'Original procedural surface study, not a scan or certified AAA asset.'}));
  fs.writeFileSync('public/models/frog-manifest.json',JSON.stringify({...metadata,bytes:bytes.length,validation:{errors:report.issues.numErrors,warnings:report.issues.numWarnings}},null,2)+'\n');
  await page.evaluate(()=>window.frog.poseAt('Idle',0));fs.mkdirSync('.screenshots/frog',{recursive:true});await page.screenshot({path:'.screenshots/frog/first.png',timeout:90000,animations:'disabled'});
}finally{await browser.close();}
