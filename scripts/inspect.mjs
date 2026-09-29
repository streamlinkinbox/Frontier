import fs from 'node:fs';
import {launchBrowser} from './browser.mjs';
const browser=await launchBrowser();
try {
 const page=await browser.newPage({viewport:{width:1280,height:900},deviceScaleFactor:1});
 page.on('pageerror',e=>console.log('PAGE ERROR:',e));
 page.on('console',m=>{if(m.type()==='error')console.log('ERROR:',m.text());});
 await page.goto('http://127.0.0.1:5173/',{waitUntil:'networkidle',timeout:60000});
 await page.waitForFunction(()=>window.mantisReady||window.mantisError,null,{timeout:90000});
 console.log(await page.evaluate(()=>({error:window.mantisError,stats:window.mantis?.stats,clips:window.mantis?.clips.map(c=>({name:c.name,duration:c.duration})),status:document.querySelector('#render-status').textContent})));
 await page.evaluate(()=>{window.mantis.poseAt('Idle',0);});
 const data=await page.evaluate(()=>window.mantis.renderer.domElement.toDataURL('image/png'));
 fs.mkdirSync('.screenshots',{recursive:true});fs.writeFileSync('.screenshots/model.png',Buffer.from(data.split(',')[1],'base64'));
 console.log('canvas saved');
 await page.screenshot({path:'.screenshots/studio.png',timeout:90000,animations:'disabled'});
 console.log('screen saved');
} finally {await browser.close()}
