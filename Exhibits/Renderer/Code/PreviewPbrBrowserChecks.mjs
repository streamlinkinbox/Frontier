// 🧩 Reproduce earlier-HTML Vulkan controls and layout checks with explicitly provisioned browser tools.
import {pathToFileURL} from 'node:url';
const [Executable,Playwright,RuntimeLibraries]=process.argv.slice(2);
const {chromium}=await import(pathToFileURL(Playwright).href);
import Fs from 'node:fs';
import Crypto from 'node:crypto';
import Assert from 'node:assert/strict';
const Browser=await chromium.launch({executablePath:Executable,headless:true,args:['--no-sandbox','--disable-dev-shm-usage','--use-angle=swiftshader','--enable-unsafe-swiftshader'],env:{...process.env,LD_LIBRARY_PATH:RuntimeLibraries}});
const Page=await Browser.newPage({viewport:{width:1440,height:1000}});
const Errors=[];
const FailedRequests=[];
Page.on('response',Response=>{if(Response.status()>=400)FailedRequests.push(Response.url());});
Page.on('pageerror',Failure=>Errors.push(Failure.message));
await Page.goto('http://127.0.0.1:8775/?renderer=1');
await Page.locator('#RevisionSelect').selectOption({label:'Preview PBR'});
await Page.waitForFunction(()=>document.querySelector('.vulkan-status')?.textContent.includes('live parameter'),{timeout:60000});
Assert.equal(await Page.locator('.vulkan-capture').evaluate(Image=>Image.naturalWidth),960);

const Initial=await Page.locator('.vulkan-capture').getAttribute('src');
const QueryPixels=async()=>await Page.locator('.vulkan-capture').evaluate(Image=>{const Canvas=document.createElement('canvas');Canvas.width=32;Canvas.height=32;Canvas.getContext('2d').drawImage(Image,0,0,32,32);return Canvas.toDataURL();});
await Page.waitForFunction(()=>document.querySelector('.vulkan-capture').complete);
const InitialPixels=await QueryPixels();
await Page.locator('input[name=Roughness]').evaluate(Input=>{Input.value='.8';Input.dispatchEvent(new Event('input'));Input.dispatchEvent(new Event('change'));});
await Page.waitForFunction(Previous=>document.querySelector('.vulkan-capture').src!==Previous && document.querySelector('.vulkan-status').textContent.includes('live parameter'),Initial,{timeout:60000});
await Page.waitForFunction(()=>document.querySelector('.vulkan-capture').complete);
Assert.notEqual(await QueryPixels(),InitialPixels,'Roughness did not change device pixels');
await Page.locator('#RevisionSelect').selectOption({label:'MatCap'});
await Page.waitForFunction(()=>document.querySelector('.vulkan-status')?.textContent.includes('live parameter'));
Assert.equal(await Page.locator('input[name=Metallic]').isDisabled(),true);
await Page.locator('#RevisionSelect').selectOption({label:'CAD'});
await Page.waitForFunction(()=>document.querySelector('.vulkan-status')?.textContent.includes('live parameter'));
await Page.locator('#RevisionSelect').selectOption({label:'Preview PBR'});
await Page.locator('[data-preview="1"]').click();
await Page.waitForFunction(()=>document.querySelector('.vulkan-capture')?.naturalWidth===720 && document.querySelector('.vulkan-status').textContent.includes('live parameter'),{timeout:60000});
Assert((await Page.locator('.vulkan-statistics').textContent()).startsWith('5 shared pipelines · bracket/sphere uploads 1/1'));
await Page.screenshot({path:'Exhibits/Renderer/Images/PreviewPbrBrowser.png',fullPage:true});
await Page.locator('[data-collapse="left"]').filter({visible:true}).click();
Assert(await Page.locator('body').evaluate(Body=>Body.classList.contains('left-collapsed')));
await Page.setViewportSize({width:760,height:900});
Assert(await Page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
await Page.locator('#Back').click();
await Page.waitForFunction(()=>document.querySelectorAll('.file-card').length===6);
Assert.equal(await Page.locator('.file-card').count(),6);
Assert.equal(await Page.locator('[data-open="renderer"]').count(),0);
for(const Name of ['Styles.css','PanelLayout.css','ReferenceRenderer.js','Browser.js'])
{
    const Digest=Crypto.createHash('sha256').update(Fs.readFileSync('Experimental/Project browser/'+Name)).digest('hex').slice(0,12);
    Assert.equal(await Page.locator(`[src="${Name}?v=${Digest}"],[href="${Name}?v=${Digest}"]`).count(),1);
}
for(const Token of ['valley','mount','stone','island','bracket','faceted'])
{
    await Page.locator(`[data-open="${Token}"]`).first().click();
    await Page.locator('#Back').click();
    Assert.equal(await Page.locator('.file-card').count(),6);
}
await Page.locator('[data-open="mount"]').first().click();
Assert.equal(await Page.locator('#OpenEditor').isEnabled(),true);
const ImGuiReview=await Page.request.get('http://127.0.0.1:8775/Exhibits/SpaceFormats/index.html');
Assert.equal(ImGuiReview.status(),200);
Assert((await ImGuiReview.text()).includes('Open renderer evidence review'));
await Page.goto('http://127.0.0.1:8775/Exhibits/SpaceFormats/index.html');
for(const Tab of await Page.locator('[data-tab]').all())
{
    await Tab.click();
    Assert.equal(await Page.locator('[role="tabpanel"]:not([hidden])').count(),1);
}
await Page.locator('[data-tab="evidence"]').click();
const Capture=await Page.locator('#MaterialLibraryCapture').getAttribute('src');
Assert.equal(Crypto.createHash('sha256').update(Buffer.from(Capture.split(',')[1],'base64')).digest('hex'),
    Crypto.createHash('sha256').update(Fs.readFileSync('Exhibits/TextureStudio/Images/MaterialLibrary.png')).digest('hex'));
const WorkspaceCapture=await Page.locator('#MaterialWorkspaceCapture').getAttribute('src');
Assert.equal(Crypto.createHash('sha256').update(Buffer.from(WorkspaceCapture.split(',')[1],'base64')).digest('hex'),
    Crypto.createHash('sha256').update(Fs.readFileSync('Exhibits/TextureStudio/Images/MaterialWorkspace.png')).digest('hex'));
Assert.equal(await Page.locator('#MaterialWorkspaceCapture').evaluate(Image=>Image.naturalWidth),1440);
const ReferenceCapture=await Page.locator('#MaterialReferenceCapture').getAttribute('src');
Assert.equal(Crypto.createHash('sha256').update(Buffer.from(ReferenceCapture.split(',')[1],'base64')).digest('hex'),
    Crypto.createHash('sha256').update(Fs.readFileSync('Exhibits/TextureStudio/Images/ReferenceMaterialWorkspace.png')).digest('hex'));

const Original=await Page.locator('[download="SpaceFormatsOriginalBrief.txt"]').getAttribute('href');
Assert.equal(Crypto.createHash('sha256').update(Buffer.from(Original.split(',')[1],'base64')).digest('hex'),
    Crypto.createHash('sha256').update(Fs.readFileSync('Documentation/Plans/References/SpaceFormatsOriginalBrief.txt')).digest('hex'));

Assert.deepEqual(Errors,[]);
Assert.deepEqual(FailedRequests,[]);
const RequestRefusals=[];
for(const Address of ['/.git/config','/_ClaudeScratch/','/CLAUDE.md'])
{
    const Response=await Page.request.get('http://127.0.0.1:8775'+Address);
    Assert.equal(Response.status(),403,'Evidence server exposed a private repository path');
    RequestRefusals.push({path:Address,expected_status:403,actual_status:Response.status()});
}
for(const Specification of [{Geometry:0,Mode:2,Roughness:0,Metallic:.8,Coating:.75,Exposure:1},{Geometry:.5,Mode:2,Roughness:.32,Metallic:.8,Coating:.75,Exposure:1}])
{
    const Response=await Page.request.post('http://127.0.0.1:8775/api/renderer',{data:Specification});
    Assert.equal(Response.status(),400);
    RequestRefusals.push({expected_status:400,actual_status:Response.status()});
}
const Sources=['Exhibits/SpaceFormats/index.html','Exhibits/SpaceFormats/Code/ReviewTemplate.html','Exhibits/SpaceFormats/Code/BuildReview.py','Exhibits/ProjectBrowser/index.html','Exhibits/Renderer/index.html','Exhibits/Renderer/Code/RendererReview.js','Exhibits/Renderer/Code/VulkanPreview.js','Exhibits/Renderer/Code/VulkanPreview.css','Exhibits/Renderer/Code/ServeRenderer.py','Experimental/Project browser/Browser.js','Experimental/Project browser/index.html','Exhibits/Renderer/Code/PreviewPbrBrowserChecks.mjs'];
const Fingerprints=Sources.map(File=>({file:File,sha256:Crypto.createHash('sha256').update(Fs.readFileSync(File)).digest('hex')}));
Fs.writeFileSync('Exhibits/Renderer/Results/PreviewPbrBrowserChecks.json',JSON.stringify({result:'passed',checks:['actual Vulkan image','roughness changes actual image pixels','CAD/MatCap/PBR mode changes','calibration sphere','five resident pipelines','one upload per geometry','evidence isolated from Project Browser catalogue','left collapse','760px no horizontal overflow','all six original samples reopen without renderer validation','content-hashed browser asset revisions','renderer review linked from existing ImGui evidence','all five evidence tabs switch','embedded current library/workspace captures and original brief bytes match','private repository paths refused'],page_errors:Errors,failed_requests:FailedRequests,request_refusals:RequestRefusals,sources:Fingerprints,windows_editor_execution:false},null,2)+'\n');
console.log('PASS: browser Vulkan controls, modes, resident counts, layout and original sample route');
await Browser.close();
