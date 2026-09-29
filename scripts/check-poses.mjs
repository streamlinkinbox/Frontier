import fs from 'node:fs';
import { launchBrowser } from './browser.mjs';
const browser = await launchBrowser();
try {
  const page = await browser.newPage({ viewport:{ width:1100,height:800 },deviceScaleFactor:1 });
  page.on('pageerror',e=>console.log('ERROR',e));
  await page.goto('http://127.0.0.1:5173/',{waitUntil:'networkidle'});
  await page.waitForFunction(()=>window.mantisReady,null,{timeout:90000});
  fs.mkdirSync('.screenshots',{recursive:true});
  for (const [name,time] of [['Idle',0],['Walk',.35],['Attack',.77],['Attack',.93]]) {
    const result=await page.evaluate(({name,time})=>{
      window.mantis.poseAt(name,time);
      const m=window.mantis;
      const points={};
      for(const n of ['Head','Middle_L_Tarsus','Middle_R_Tarsus','Hind_L_Tarsus','Hind_R_Tarsus','Fore_L_Tibia','Fore_R_Tibia']){
        const b=m.model.getObjectByName(n);points[n]=b?Array.from(b.matrixWorld.elements).slice(12,15):null;
      }
      return {image:m.renderer.domElement.toDataURL(),points};
    },{name,time});
    console.log(name,time,JSON.stringify(result.points));
    fs.writeFileSync(`.screenshots/${name.toLowerCase()}-${time}.png`,Buffer.from(result.image.split(',')[1],'base64'));
  }
  const macro=await page.evaluate(()=>{
    const m=window.mantis;m.poseAt('Idle',0);m.camera.position.set(.019,.036,.059);m.controls.target.set(0,.033,.025);m.controls.update();m.renderer.render(m.scene,m.camera);
    return m.renderer.domElement.toDataURL();
  });
  fs.writeFileSync('.screenshots/head.png',Buffer.from(macro.split(',')[1],'base64'));
} finally {await browser.close();}
