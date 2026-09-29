import fs from 'node:fs';
import {launchBrowser} from './browser.mjs';
const browser=await launchBrowser();
try{
  const page=await browser.newPage({viewport:{width:1440,height:1000},deviceScaleFactor:1,reducedMotion:'reduce'});
  page.on('pageerror',e=>console.error('PAGE ERROR',e));
  page.on('console',m=>{if(m.type()==='error')console.error(m.text());});
  await page.goto('http://127.0.0.1:5173/',{waitUntil:'networkidle'});
  await page.waitForFunction(()=>window.mantisReady||window.mantisError,null,{timeout:90000});
  console.log(await page.evaluate(()=>({error:window.mantisError,mode:window.mantis?.mode,status:window.mantis?.hunt.simulation.status})));
  await page.evaluate(()=>{const m=window.mantis;m.setPaused(true);m.camera.position.set(.102,.068,.126);m.controls.target.set(0,.024,.008);m.controls.update();m.hunt.update(0);m.renderer.render(m.scene,m.camera);});
  fs.mkdirSync('.screenshots/hunt',{recursive:true});
  await page.screenshot({path:'.screenshots/hunt/target-test.png',timeout:90000,animations:'disabled'});
  for(const [name,dt] of [['locked',.6],['lean',.55],['capture',.66],['held',.45]]){
    const data=await page.evaluate(({name,dt})=>{
      const m=window.mantis;if(name==='lean')m.hunt.simulation.requestStrike();m.hunt.update(dt);m.renderer.render(m.scene,m.camera);
      return {image:m.renderer.domElement.toDataURL(),status:m.hunt.simulation.status};
    },{name,dt});
    fs.writeFileSync(`.screenshots/hunt/${name}.png`,Buffer.from(data.image.split(',')[1],'base64'));console.log(name,data.status.phase,data.status.target);
  }
  const close=await page.evaluate(()=>{const m=window.mantis;m.camera.position.set(.047,.040,.076);m.controls.target.set(0,.026,.030);m.controls.update();m.hunt.update(0);m.renderer.render(m.scene,m.camera);return m.renderer.domElement.toDataURL();});
  fs.writeFileSync('.screenshots/hunt/grip-closeup.png',Buffer.from(close.split(',')[1],'base64'));
  await page.evaluate(()=>{const m=window.mantis;m.hunt.reset();m.camera.position.set(.102,.068,.126);m.controls.target.set(0,.024,.008);m.controls.update();});
  await page.setViewportSize({width:390,height:844});
  await page.screenshot({path:'.screenshots/hunt/mobile.png',timeout:90000,animations:'disabled',fullPage:true});
}finally{await browser.close();}
