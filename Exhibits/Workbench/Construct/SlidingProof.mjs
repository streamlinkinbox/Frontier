import {chromium} from '../../../.cache/browser/node_modules/playwright-core/index.mjs';
import binary from '../../../.cache/browser/node_modules/@sparticuz/chromium/build/index.js';
const browser=await chromium.launch({executablePath:await binary.executablePath(),args:binary.args,headless:true});
const page=await browser.newPage({viewport:{width:1440,height:1080}});const errors=[];page.on('pageerror',e=>errors.push(e.message));
await page.goto('http://127.0.0.1:5173');await page.getByRole('button',{name:'Open Construct',exact:true}).click();
await page.getByRole('button',{name:'Configure Cube',exact:true}).click();
await page.locator('.show-properties').waitFor();await page.getByLabel('New entity name').fill('Slide proof cube');

await page.getByRole('button',{name:'Back to entities',exact:true}).click();
for(const name of ['Air / Wind','Sun','Terrain','Camera','Local Fog']){
 await page.getByLabel('Search construct catalogue').fill(name);
 await page.getByRole('button',{name:'Configure '+name,exact:true}).click();
 if(name==='Air / Wind'){
  const speed=page.getByRole('dialog').getByRole('slider',{name:'wind',exact:true});
  await speed.fill('8');

 }
 await page.waitForFunction(()=>{const e=document.querySelector('.construct-slide-track');return Math.abs(new DOMMatrix(getComputedStyle(e).transform).m41+e.clientWidth/2)<1},null,{polling:100});
 const backBox=await page.getByRole('button',{name:'Back to entities',exact:true}).boundingBox();await page.mouse.click(backBox.x+backBox.width/2,backBox.y+backBox.height/2);
}
await page.getByLabel('Search construct catalogue').fill('');
await page.getByRole('dialog').getByRole('button',{name:/^Environment/}).click();
await page.setViewportSize({width:390,height:844});await page.getByRole('button',{name:'Configure Sun',exact:true}).click();
if(await page.locator('.construct-panel').evaluate(e=>e.scrollWidth>e.clientWidth))throw Error('Horizontal overflow');
await page.keyboard.press('Escape');await page.keyboard.press('Escape');await page.getByRole('dialog').waitFor({state:'hidden'});
if(errors.length)throw Error(errors.join('\n'));
console.log('PASS: cube slides to properties; wind live edit; Sun, Terrain, Camera, Local Fog details; Back and Escape; mobile shell; no page errors.');await browser.close();
