import fs from 'node:fs';
import { launchBrowser } from './browser.mjs';
import { STRIKE } from '../src/strike-motion.js';
const browser = await launchBrowser();
try {
  const page = await browser.newPage({viewport:{width:1180,height:850},deviceScaleFactor:1,reducedMotion:'reduce'});
  page.on('pageerror',e=>console.error(e));
  await page.goto('http://127.0.0.1:5173/',{waitUntil:'networkidle'});
  await page.waitForFunction(()=>window.mantisReady,null,{timeout:90000});
  await page.evaluate(()=>{window.mantis.poseAt('Attack',0);window.mantis.camera.position.set(.145,.042,.001);window.mantis.controls.target.set(0,.023,.002);window.mantis.controls.update();});
  fs.mkdirSync('.screenshots/strike-v2',{recursive:true});
  for(const [label,time] of [['set',STRIKE.setEnd],['approach',STRIKE.approachEnd],['sweep',.518],['clamp',STRIKE.closeEnd],['retract',STRIKE.retractEnd]]){
    const image=await page.evaluate(({time})=>{window.mantis.poseAt('Attack',time);return window.mantis.renderer.domElement.toDataURL();},{time});
    fs.writeFileSync(`.screenshots/strike-v2/${label}.png`,Buffer.from(image.split(',')[1],'base64'));
  }
  await page.evaluate(()=>{window.mantis.poseAt('Attack',.544);window.mantis.camera.position.set(.10,.063,.105);window.mantis.controls.target.set(0,.023,.005);window.mantis.controls.update();window.mantis.renderer.render(window.mantis.scene,window.mantis.camera);});
  await page.screenshot({path:'.screenshots/strike-v2/studio.png',timeout:90000,animations:'disabled'});
  console.log('Side-on phase poses and studio capture saved in .screenshots/strike-v2.');
}finally{await browser.close();}
