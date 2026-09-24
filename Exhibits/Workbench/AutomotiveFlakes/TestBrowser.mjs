import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {brotliDecompressSync} from 'node:zlib';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const cache=path.join(root,'.cache/native-icon-browser');
const {default:chromium}=await import(path.join(cache,'node_modules/@sparticuz/chromium/build/index.js'));
const {chromium:playwright}=await import(path.join(cache,'node_modules/playwright-core/index.mjs'));
await mkdir(path.join(cache,'libs'),{recursive:true});await writeFile(path.join(cache,'libs.tar'),brotliDecompressSync(await readFile(path.join(cache,'node_modules/@sparticuz/chromium/bin/al2023.tar.br'))));execFileSync('tar',['xf',path.join(cache,'libs.tar'),'-C',path.join(cache,'libs')]);process.env.LD_LIBRARY_PATH=path.join(cache,'libs/lib');
const browser=await playwright.launch({executablePath:await chromium.executablePath(),args:chromium.args,headless:true});
const page=await browser.newPage({viewport:{width:1440,height:1100}});page.setDefaultTimeout(120000);const errors=[];page.on('pageerror',e=>errors.push(e.message));
const out=path.join(root,'Exhibits/Gallery/AutomotiveFlakes');
try{
 await page.goto(process.argv[2]||'http://127.0.0.1:5188/',{waitUntil:'networkidle',timeout:120000});
 if(!await page.evaluate(()=>window.paintReady))throw Error(await page.evaluate(()=>window.paintFailure));
 const gpu=await page.evaluate(()=>window.paintProbes());const cpu=JSON.parse(await readFile(path.join(out,'CpuProbes.json'),'utf8'));
 let maxError=0;for(let i=0;i<64;i++)for(let c=0;c<3;c++){if(!Number.isFinite(gpu[i][c])||gpu[i][c]<0)throw Error('Invalid GLSL output');maxError=Math.max(maxError,Math.abs(cpu[i][c]-gpu[i][c])/Math.max(.01,Math.abs(cpu[i][c])))}
 if(maxError>.008)throw Error('CPU/GLSL mismatch '+maxError);
 const checksum=()=>page.evaluate(()=>{window.paintDraw();const canvas=document.getElementById('paint'),gl=canvas.getContext('webgl2'),pixels=new Uint8Array(canvas.width*canvas.height*4);gl.readPixels(0,0,canvas.width,canvas.height,gl.RGBA,gl.UNSIGNED_BYTE,pixels);let sum=2166136261;for(let i=0;i<pixels.length;i++)sum=Math.imul(sum^pixels[i],16777619);return sum>>>0});
 const metal=await checksum();await page.click('[data-preset="smooth"]');const smooth=await checksum();await page.click('[data-preset="pearl"]');const pearl=await checksum();if(new Set([metal,smooth,pearl]).size!==3)throw Error('Material controls did not change rendering');
 await page.click('[data-preset="metallic"]');await page.locator('#light').fill('0.35');await page.locator('#light').dispatchEvent('input');const moved=await checksum();if(moved===metal)throw Error('Light control did not change rendering');
 const parameterChecks={};
 for(const [id,value] of [['density','0.2'],['diameter','0.08'],['spread','0.5'],['roughness','0.15'],['coat','0.4'],['distance','0.35']]){
  await page.click('#reset');await page.locator('#'+id).fill(value);await page.locator('#'+id).dispatchEvent('input');parameterChecks[id]=await checksum();if(parameterChecks[id]===metal)throw Error(id+' did not change rendering');
 }
 await page.click('#reset');await page.click('[data-preset="pearl"]');await page.locator('#film').fill('650');await page.locator('#film').dispatchEvent('input');parameterChecks.film=await checksum();if(parameterChecks.film===pearl)throw Error('Film control did not change pearl');
 await page.click('#reset');const bounds=await page.locator('#paint').boundingBox();await page.mouse.move(bounds.x+bounds.width*.5,bounds.y+bounds.height*.5);await page.mouse.down();await page.mouse.move(bounds.x+bounds.width*.5+45,bounds.y+bounds.height*.5);await page.mouse.up();parameterChecks.orbit=await checksum();if(parameterChecks.orbit===metal)throw Error('Camera orbit did not change view');
 await page.click('#reset');
 for(const density of [1,4,12,16]){await page.locator('#density-number').fill(String(density));await page.locator('#density-number').dispatchEvent('input');if(Number(await page.locator('#density').inputValue())!==density)throw Error('Density numeric input not synchronized');parameterChecks['density'+density]=await checksum();}
 if(new Set([parameterChecks.density1,parameterChecks.density4,parameterChecks.density12,parameterChecks.density16]).size!==4)throw Error('Above-one density was clamped or did not change the shader');
 await page.click('#reset');await page.click('#add-flake');if(await page.locator('.palette-row').count()!==2)throw Error('Add colour failed');parameterChecks.addColour=await checksum();if(parameterChecks.addColour===metal)throw Error('Added colour not rendered');
 const setColour=async(selector,value)=>{await page.locator(selector).evaluate((input,value)=>{input.value=value;input.dispatchEvent(new Event('input',{bubbles:true}))},value)};
 await setColour('.palette-row:last-child .flake-min','#00cc33');await setColour('.palette-row:last-child .flake-max','#99ffbb');parameterChecks.colourRange=await checksum();if(parameterChecks.colourRange===parameterChecks.addColour)throw Error('Colour range not applied');
 await page.locator('.palette-row:last-child .flake-weight').fill('5');await page.locator('.palette-row:last-child .flake-weight').dispatchEvent('input');parameterChecks.colourShare=await checksum();if(parameterChecks.colourShare===parameterChecks.colourRange)throw Error('Colour share not applied');
 await page.locator('.palette-row:last-child .remove-flake').click();if(await page.locator('.palette-row').count()!==1||await checksum()!==metal)throw Error('Remove colour did not restore original palette');
 for(let i=0;i<7;i++)await page.click('#add-flake');if(await page.locator('.palette-row').count()!==8||!await page.locator('#add-flake').isDisabled())throw Error('Palette capacity guard failed');parameterChecks.eightColours=await checksum();
 await page.click('#reset');await setColour('#coat-colour','#2266ee');parameterChecks.coatTint=await checksum();if(parameterChecks.coatTint===metal)throw Error('Clearcoat colour not applied');
 await page.click('[data-preset="rgb"]');if(await page.locator('.palette-row').count()!==3||Number(await page.locator('#density').inputValue())!==4)throw Error('RGB / blue coat preset failed');parameterChecks.rgbBlueCoat=await checksum();await page.screenshot({path:path.join(out,'PaintLab-Colours.png'),fullPage:true});
 await page.click('#reset');const restored=await checksum();if(restored!==metal)throw Error('Reset not deterministic');
 await page.screenshot({path:path.join(out,'PaintLab.png'),fullPage:true});
 const renderer=await page.evaluate(()=>{const gl=document.getElementById('paint').getContext('webgl2'),ext=gl.getExtension('WEBGL_debug_renderer_info');return ext?gl.getParameter(ext.UNMASKED_RENDERER_WEBGL):gl.getParameter(gl.RENDERER)});
 const files=['shared.glsl','paint.js','index.html','CpuProbes.json','PaintLab.png','PaintLab-Colours.png'];const sha={};for(const f of files)sha[f]=createHash('sha256').update(await readFile(path.join(out,f))).digest('hex');
 const report={browser:browser.version(),renderer,glslCompilation:'PASS',probes:64,channels:192,maxRelativeError:maxError,controlChecks:{metal,smooth,pearl,moved,restored,...parameterChecks},pageErrors:errors,sha256:sha};if(errors.length)throw Error(errors.join('\n'));
 await writeFile(path.join(out,'BrowserProof.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));
}finally{await browser.close()}
