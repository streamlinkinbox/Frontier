// 🧩 Verify authored TOML material apply/import/export, native compilation diagnostics and unchanged resident resources.
import Fs from 'node:fs';
import Assert from 'node:assert/strict';
import Crypto from 'node:crypto';
import {pathToFileURL} from 'node:url';
const [Executable,Playwright,RuntimeLibraries]=process.argv.slice(2);
const {chromium}=await import(pathToFileURL(Playwright).href);
const Browser=await chromium.launch({executablePath:Executable,headless:true,args:['--no-sandbox','--disable-dev-shm-usage','--use-angle=swiftshader','--enable-unsafe-swiftshader'],env:{...process.env,LD_LIBRARY_PATH:RuntimeLibraries}});
const Page=await Browser.newPage({viewport:{width:1440,height:1000},acceptDownloads:true});
const Errors=[];
Page.on('pageerror',Failure=>Errors.push(Failure.message));
await Page.goto('http://127.0.0.1:8775/?renderer=1');
const WaitCapture=async()=>await Page.waitForFunction(()=>document.querySelector('.material-diagnostic')?.textContent==='Material compiled and rendered successfully.' && document.querySelector('.vulkan-capture')?.complete,{timeout:60000});
await WaitCapture();
Assert.equal(await Page.locator('#RevisionSelect').inputValue(),'0');
Assert.equal(await Page.locator('[aria-label="Material specimen"] option').count(),4);
await Page.locator('[data-preview="1"]').click();
await WaitCapture();
for(const Name of ['CoatedCeramic.material','TexturedAlloy.material','LuminousPattern.material'])
{
    await Page.locator('[aria-label="Material specimen"]').selectOption(Name);
    await WaitCapture();
}
await Page.locator('[aria-label="Material specimen"]').selectOption('TexturedAlloy.material');
await WaitCapture();
const QueryPixels=async()=>await Page.locator('.vulkan-capture').evaluate(Image=>{const Canvas=document.createElement('canvas');Canvas.width=32;Canvas.height=32;Canvas.getContext('2d').drawImage(Image,0,0,32,32);return Canvas.toDataURL();});
const OriginalPixels=await QueryPixels();
const Original=await Page.locator('[aria-label="Material TOML"]').inputValue();
const Modified=Original.replace('Roughness = 0.8','Roughness = 0.15');
Assert.notEqual(Modified,Original);
await Page.locator('[aria-label="Material TOML"]').fill(Modified);
await Page.locator('[data-material-apply]').click();
await WaitCapture();
Assert.notEqual(await QueryPixels(),OriginalPixels,'Authored roughness failed to change native pixels');
Assert((await Page.locator('.vulkan-statistics').textContent()).includes('texture uploads 4'));
const LastCapture=await Page.locator('.vulkan-capture').getAttribute('src');
await Page.locator('[aria-label="Material TOML"]').fill(Modified.replace('Category = "Opaque"','Category = "Transmission"'));
await Page.locator('[data-material-apply]').click();
await Page.waitForFunction(()=>document.querySelector('.material-diagnostic')?.textContent.includes('Unsupported scattering Category'));
Assert.equal(await Page.locator('.vulkan-capture').getAttribute('src'),LastCapture,'Invalid material replaced last valid capture');
await Page.locator('.material-authoring input[type=file]').setInputFiles({name:'Imported.material',mimeType:'text/plain',buffer:Buffer.from(Original)});
await Page.locator('[data-material-apply]').click();
await WaitCapture();
const DownloadReady=Page.waitForEvent('download');
await Page.locator('[data-material-export]').click();
const Download=await DownloadReady;
Assert.equal(Download.suggestedFilename(),'Surface.material');
const DownloadPath=await Download.path();
Assert.equal(Fs.readFileSync(DownloadPath,'utf8'),Original);
await Page.locator('[aria-label="Material specimen"]').selectOption('BrushedCopper.material');
await WaitCapture();
const AnisotropicPixels=await QueryPixels();
const Copper=await Page.locator('[aria-label="Material TOML"]').inputValue();
await Page.locator('[aria-label="Material TOML"]').fill(Copper.replace('Anisotropy = 0.8','Anisotropy = 0.0'));
await Page.locator('[data-material-apply]').click();
await WaitCapture();
Assert.notEqual(await QueryPixels(),AnisotropicPixels,'Anisotropy did not change rendered lobe');
await Page.locator('[aria-label="Material TOML"]').fill(Copper.replace('Rotation = 0.35','Rotation = 1.92'));
await Page.locator('[data-material-apply]').click();
await WaitCapture();
Assert.notEqual(await QueryPixels(),AnisotropicPixels,'Tangent rotation did not change rendered lobe');
await Page.locator('[aria-label="Material specimen"]').selectOption('CoatedCeramic.material');
await WaitCapture();
const CoatedPixels=await QueryPixels();
const Ceramic=await Page.locator('[aria-label="Material TOML"]').inputValue();
for(const Modified of [Ceramic.replace('NormalStrength = 0.5','NormalStrength = 0.0'),
    Ceramic.replace('UvScaleOffset = [0.5, 2.0, 0.13, 0.0]','UvScaleOffset = [2.0, 0.5, 0.0, 0.13]'),
    Ceramic.replace('Textures/CoatNormal.png','Textures/Normal.png'),
    Ceramic.replace('Fresnel = "Dielectric"','Fresnel = "Schlick"')])
{
    Assert.notEqual(Modified,Ceramic);
    await Page.locator('[aria-label="Material TOML"]').fill(Modified);
    await Page.locator('[data-material-apply]').click();
    await WaitCapture();
    Assert.notEqual(await QueryPixels(),CoatedPixels,'Independent layer optics/input did not change rendered pixels');
}
await Page.locator('[aria-label="Material TOML"]').fill(Ceramic);
await Page.locator('[data-material-apply]').click();
await WaitCapture();
await Page.screenshot({path:'Exhibits/Renderer/Images/MaterialBrowser.png',fullPage:true});
await Page.setViewportSize({width:760,height:900});
Assert(await Page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
Assert.deepEqual(Errors,[]);
const Sources=['Exhibits/Renderer/index.html','Exhibits/Renderer/Code/RendererReview.js','Exhibits/Renderer/Code/VulkanPreview.js','Exhibits/Renderer/Code/VulkanPreview.css','Exhibits/Renderer/Code/ServeRenderer.py','Exhibits/Renderer/Code/MaterialBrowserChecks.mjs'];
Fs.writeFileSync('Exhibits/Renderer/Results/MaterialBrowserChecks.json',JSON.stringify({result:'passed',checks:['four authored material specimens','actual Vulkan pixels change after TOML apply','texture uploads remain four','unsupported transmission diagnostic','last valid capture retained after error','import source and native recompile','export exact TOML source','760px layout','anisotropic GGX and tangent rotation change Vulkan pixels','independent coating UVs, normal source and strength change Vulkan pixels','exact dielectric Fresnel changes Vulkan pixels'],page_errors:Errors,sources:Sources.map(File=>({file:File,sha256:Crypto.createHash('sha256').update(Fs.readFileSync(File)).digest('hex')})),windows_editor_execution:false},null,2)+'\n');
console.log('PASS: authored material browser apply/import/export, native diagnostics, live pixels and texture reuse');
await Browser.close();
