/*==========================================================================================================================================
                                                        REFERENCEBROWSERCHECKS.MJS
==========================================================================================================================================*/
// 🧩 Confirm the updated React/Three reference actually renders; do not describe this WebGL page as the native application.

import Fs from 'node:fs';
import Assert from 'node:assert/strict';
import Crypto from 'node:crypto';
import {pathToFileURL} from 'node:url';
const [Executable,Playwright,Libraries] = process.argv.slice(2);
const {chromium} = await import(pathToFileURL(Playwright).href);
const Browser = await chromium.launch({executablePath:Executable,headless:true,
    args:['--no-sandbox','--disable-dev-shm-usage','--use-angle=swiftshader','--enable-unsafe-swiftshader'],
    env:{...process.env,LD_LIBRARY_PATH:Libraries}});
const Page = await Browser.newPage({viewport:{width:1440,height:1000}});
const Errors = [];
Page.on('pageerror',Failure=>Errors.push(Failure.message));
await Page.goto('http://127.0.0.1:8774/?studio=texture');
await Page.waitForFunction(()=>document.querySelector('.tp-studio canvas')?.dataset.materialReady==='true',null,{timeout:180000});
const Canvas = Page.locator('.tp-studio canvas');
Assert(Number(await Canvas.getAttribute('data-triangle-count'))>10000);
Assert((await Canvas.evaluate(Surface=>Surface.toDataURL().length))>10000);
Assert.equal(await Page.locator('.studio-workspace-error').count(),0);
Assert.deepEqual(Errors,[]);
await Page.screenshot({path:'Exhibits/TextureStudio/Images/ReferenceStudio.png',fullPage:true});
await Page.goto('http://127.0.0.1:8774/?studio=material');
await Page.waitForFunction(()=>document.querySelector('.studio-material .viewport-panel canvas')?.dataset.materialReady==='true',null,{timeout:180000});
Assert(await Page.locator('.studio-material .material-grid .material-select').count()>0);
Assert(await Page.locator('.studio-material .inspector-panel').isVisible());
await Page.evaluate(()=>document.fonts.ready);
await Page.locator('.compile-notice').waitFor({state:'hidden',timeout:600000});
await Page.screenshot({path:'Exhibits/TextureStudio/Images/ReferenceMaterialWorkspace.png',fullPage:false});
Assert.deepEqual(Errors,[]);
Fs.writeFileSync('Exhibits/TextureStudio/Results/ReferenceBrowserChecks.json',JSON.stringify({result:'passed',
    execution:'React/Three WebGL reference on Chromium + SwiftShader; not the native Material Library',
    checks:['updated texture workspace becomes material-ready','teapot has more than 10000 triangles','canvas contains rendered image','no workspace recovery error','no page errors','Material workspace renders at 1440 by 1000 with library and inspector','all reference material thumbnails complete before capture'],
    page_errors:Errors,sources:['Exhibits/TextureStudio/Code/ReferenceBrowserChecks.mjs',
    'Exhibits/TextureStudio/Results/ReferenceImport.json'].map(File=>({file:File,sha256:Crypto.createHash('sha256').update(Fs.readFileSync(File)).digest('hex')}))},null,2)+'\n');
console.log('PASS: updated TextureStudio WebGL reference renders; not native application execution');
await Browser.close();
