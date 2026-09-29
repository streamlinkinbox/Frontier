import fs from 'node:fs';
import {launchBrowser} from './browser.mjs';
const browser=await launchBrowser();
try{
  const page=await browser.newPage({viewport:{width:1365,height:950},deviceScaleFactor:1,reducedMotion:'reduce'});
  page.on('pageerror',err=>console.error(err));
  await page.goto('http://127.0.0.1:5173/',{waitUntil:'networkidle'});
  await page.waitForFunction(()=>window.mantisReady||window.mantisError,null,{timeout:90000});
  const error=await page.evaluate(()=>window.mantisError);if(error)throw new Error(error);
  fs.mkdirSync('.screenshots/stance',{recursive:true});
  for(const [name,p,t] of [['front',[0,.041,.156],[0,.027,-.002]],['perspective',[.055,.060,.138],[0,.026,-.001]],['side',[.151,.052,.012],[0,.027,-.002]]]){
    const image=await page.evaluate(({p,t})=>{
      const m=window.mantis;m.poseAt('Stance',1.2);m.camera.position.set(...p);m.controls.target.set(...t);m.controls.update();m.renderer.render(m.scene,m.camera);
      return m.renderer.domElement.toDataURL();
    },{p,t});
    fs.writeFileSync(`.screenshots/stance/${name}.png`,Buffer.from(image.split(',')[1],'base64'));
  }
  await page.evaluate(()=>{const m=window.mantis;m.camera.position.set(.055,.060,.138);m.controls.target.set(0,.026,-.001);m.controls.update();m.renderer.render(m.scene,m.camera);});
  await page.screenshot({path:'.screenshots/stance/studio.png',timeout:90000,animations:'disabled'});
  await page.setViewportSize({width:390,height:844});
  await page.screenshot({path:'.screenshots/stance/mobile.png',timeout:90000,animations:'disabled'});
  console.log('Stance captured in front, three-quarter, side and mobile views.');
}finally{await browser.close();}
